"""Agent IPC server (replaces the :8787 HTTP listener in the App build).

Wire format: 4-byte little-endian length + UTF-8 JSON object, max 1 MiB.

Windows uses a named pipe whose DACL is restricted to the current user; POSIX
uses an AF_UNIX socket so the same code can be exercised on Linux CI.

Messages handled here:
  hello          -> hello_ack
  ping           -> pong
  agent.chat     -> a stream of agent.event frames + agent.done
  service.request-> one page-level request rendered by the Agent over TCP
  service.configure -> attach the session to the TCP service
  shutdown       -> close the pipe and exit
"""

from __future__ import annotations

import json
import os
import socket
import struct
import sys
import threading
from typing import Any, BinaryIO, Callable
from urllib.parse import parse_qs, urlparse

from agent.app import AgentApp
from agent.config import DEFAULT_USER_ID

MAX_PAYLOAD = 1024 * 1024
HEADER = struct.Struct("<I")


def _encode(message: dict[str, Any]) -> bytes:
    payload = json.dumps(message, ensure_ascii=False, default=str).encode("utf-8")
    if len(payload) > MAX_PAYLOAD:
        payload = json.dumps({"type": "agent.error", "message": "payload_too_large"}).encode()
    return HEADER.pack(len(payload)) + payload


class StreamConnection:
    def __init__(self, read_fn: Callable[[int], bytes], write_fn: Callable[[bytes], None]):
        self._read = read_fn
        self._write = write_fn
        self._buffer = bytearray()

    def read_message(self) -> dict[str, Any] | None:
        while True:
            if len(self._buffer) >= 4:
                (length,) = HEADER.unpack(self._buffer[:4])
                if length > MAX_PAYLOAD:
                    raise ValueError("payload_too_large")
                if len(self._buffer) >= 4 + length:
                    payload = bytes(self._buffer[4:4 + length])
                    del self._buffer[:4 + length]
                    return json.loads(payload.decode("utf-8"))
            chunk = self._read(65536)
            if not chunk:
                return None
            self._buffer.extend(chunk)

    def send(self, message: dict[str, Any]) -> None:
        self._write(_encode(message))


def _service_request(app: AgentApp, path: str, init: dict[str, Any]) -> dict[str, Any]:
    parsed = urlparse(path)
    query = {key: values[0] for key, values in parse_qs(parsed.query).items()}
    method = str(init.get("method") or "GET").upper()
    body: Any = init.get("body") or {}
    if isinstance(body, str):
        try:
            body = json.loads(body)
        except json.JSONDecodeError:
            body = {}
    if not isinstance(body, dict):
        body = {}
    user_id = str(query.get("user_id") or body.get("user_id")
                  or app.service.user_key or DEFAULT_USER_ID)
    route = parsed.path.rstrip("/") or "/"

    if route == "/api/menu":
        return app.menu_view({
            "user_id": user_id,
            "mode": query.get("mode", "work"),
            "meal_type": query.get("meal_type", ""),
        })
    if route == "/api/menu/random" and method == "POST":
        payload = dict(body)
        payload.setdefault("user_id", user_id)
        payload.setdefault("mode", "work")
        return app.menu_random(payload)
    if route == "/api/profile/spend":
        payload = dict(body)
        payload.setdefault("user_id", user_id)
        payload.setdefault("mode", "work")
        if method == "POST":
            return app.update_spend_level(payload)
        return app.spend_level(user_id)
    if route == "/api/service/status":
        return app.service.status()
    if route == "/api/profile/cover" and method == "GET":
        return app.cover_view(user_id)
    if route == "/api/profile/cover" and method == "POST":
        return app.save_cover(app.service.user_key or user_id, body)
    if route == "/api/profile" and method == "GET":
        return app.profile_view(user_id)
    if route == "/api/profile" and method == "PATCH":
        return app.update_profile_view(user_id, body)
    if route == "/api/meals" and method == "GET":
        return app.meals_view(user_id, int(query.get("days", "7")))
    if route == "/api/stats/weekly" and method == "GET":
        return app.weekly_view(user_id)
    if route == "/api/stats/gap" and method == "GET":
        return app.gap_view(user_id, query.get("date", ""))
    if route == "/api/friends" and method == "GET":
        return app.friends_view(user_id)
    if route == "/api/ranking" and method == "GET":
        return app.ranking_view(user_id, query.get("scope", "week"))
    if route.startswith("/api/friend/") and method == "POST":
        return app.friend_action(user_id, route.rsplit("/", 1)[-1], body)
    if route == "/api/service/logout" and method == "POST":
        return app.logout_service()
    if route == "/api/service/login" and method == "POST":
        return app.login_service(body)
    if route == "/api/service/captcha" and method == "POST":
        return app.captcha(body)
    raise ValueError(f"unknown_path:{route}")


def serve(app: AgentApp, connection: StreamConnection) -> bool:
    """Returns False when the peer asked to shut down."""
    while True:
        try:
            message = connection.read_message()
        except (ValueError, json.JSONDecodeError) as error:
            connection.send({"type": "agent.error", "message": str(error)})
            return True
        if message is None:
            return True
        kind = message.get("type")
        if kind == "hello":
            connection.send({"type": "hello_ack", "version": "1", "mode": "app"})
        elif kind == "ping":
            connection.send({"type": "pong"})
        elif kind == "shutdown":
            connection.send({"type": "shutdown_ack"})
            return False
        elif kind == "agent.chat":
            request_id = message.get("id")
            payload = message.get("payload") or {}
            try:
                for event in app.chat_events(dict(payload)):
                    connection.send({
                        "type": "agent.event",
                        "id": request_id,
                        "event": event.type,
                        "data": event.data,
                    })
                connection.send({"type": "agent.done", "id": request_id})
            except Exception as error:  # keep the pipe alive on one bad turn
                connection.send({
                    "type": "agent.error",
                    "id": request_id,
                    "message": f"agent_failed:{type(error).__name__}",
                })
        elif kind == "service.request":
            request_id = message.get("id")
            try:
                payload = _service_request(
                    app,
                    str(message.get("path") or ""),
                    dict(message.get("init") or {}),
                )
                connection.send({
                    "type": "service.response",
                    "id": request_id,
                    "data": payload,
                })
            except Exception as error:
                connection.send({
                    "type": "agent.error",
                    "id": request_id,
                    "message": f"service_failed:{type(error).__name__}:{error}",
                })
        elif kind == "service.configure":
            try:
                status = app.service.configure_service(
                    host=str(message.get("host") or ""),
                    port=int(message.get("port") or 0),
                    user_key=str(message.get("user_key") or ""),
                    access_token=str(message.get("access_token") or ""),
                )
                connection.send({
                    "type": "service.configured",
                    "id": message.get("id"),
                    "status": status,
                })
            except Exception as error:
                connection.send({
                    "type": "agent.error",
                    "id": message.get("id"),
                    "message": f"service_configure_failed:{type(error).__name__}",
                })
        elif kind == "service.logout":
            status = app.logout_service()
            connection.send({
                "type": "service.logout.response",
                "id": message.get("id"),
                "ok": True,
                "status": status.get("status", {}),
            })
        else:
            connection.send({"type": "agent.error", "message": "unknown_type"})


def _run_posix(path: str, app: AgentApp) -> int:
    if os.path.exists(path):
        os.unlink(path)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(path)
    os.chmod(path, 0o600)
    server.listen(1)
    try:
        while True:
            client, _ = server.accept()
            with client:
                connection = StreamConnection(client.recv, client.sendall)
                if not serve(app, connection):
                    return 0
    finally:
        server.close()
        if os.path.exists(path):
            os.unlink(path)


def _run_windows(pipe_name: str, app: AgentApp) -> int:
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateNamedPipeW.restype = wintypes.HANDLE
    kernel32.ReadFile.argtypes = [
        wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID,
    ]
    kernel32.WriteFile.argtypes = [
        wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID,
    ]

    pipe = kernel32.CreateNamedPipeW(
        pipe_name, 0x00000003, 0x00000000 | 0x00000000 | 0x00000000,
        1, MAX_PAYLOAD, MAX_PAYLOAD, 0, None,
    )
    if pipe == wintypes.HANDLE(-1).value:
        return 1
    if not kernel32.ConnectNamedPipe(pipe, None):
        error = ctypes.get_last_error()
        if error != 535:  # ERROR_PIPE_CONNECTED
            kernel32.CloseHandle(pipe)
            return 1

    def read(n: int) -> bytes:
        buffer = ctypes.create_string_buffer(n)
        read_bytes = wintypes.DWORD(0)
        ok = kernel32.ReadFile(pipe, buffer, n, ctypes.byref(read_bytes), None)
        if not ok:
            return b""
        return buffer.raw[: read_bytes.value]

    def write(data: bytes) -> None:
        written = wintypes.DWORD(0)
        kernel32.WriteFile(pipe, data, len(data), ctypes.byref(written), None)

    try:
        serve(app, StreamConnection(read, write))
    finally:
        kernel32.CloseHandle(pipe)
    return 0


def main() -> int:
    # Frozen builds reuse this executable as the MCP stdio entry point:
    # mcp_servers.json resolves "{python}" to sys.executable.
    if len(sys.argv) >= 3 and sys.argv[1] == "-m" and sys.argv[2] == "agent.mcp.server":
        sys.argv = [sys.argv[0], *sys.argv[3:]]
        from agent.mcp.server import main as mcp_main

        mcp_main()
        return 0

    app = AgentApp()
    try:
        if os.name == "nt":
            sid = os.environ.get("EAT_APP_SID", "default")
            instance = os.environ.get("EAT_APP_INSTANCE", "1")
            return _run_windows(f"\\\\.\\pipe\\EatAppAgent-{sid}-{instance}", app)
        path = os.environ.get("EAT_AGENT_SOCKET", "/tmp/eat-agent.sock")
        return _run_posix(path, app)
    finally:
        app.close()


if __name__ == "__main__":
    sys.exit(main())
