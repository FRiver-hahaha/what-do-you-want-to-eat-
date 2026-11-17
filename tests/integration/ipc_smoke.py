"""POSIX IPC smoke test for agent/ipc_server.py (no network, no API key)."""

from __future__ import annotations

import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADER = struct.Struct("<I")


def read_exact(sock: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise RuntimeError("pipe_closed")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def send(sock: socket.socket, message: dict) -> None:
    payload = json.dumps(message, ensure_ascii=False).encode("utf-8")
    sock.sendall(HEADER.pack(len(payload)) + payload)


def recv(sock: socket.socket) -> dict:
    (length,) = HEADER.unpack(read_exact(sock, 4))
    return json.loads(read_exact(sock, length).decode("utf-8"))


def main() -> int:
    socket_path = os.path.join(tempfile.mkdtemp(prefix="eat-ipc-"), "agent.sock")
    env = dict(os.environ)
    env["EAT_AGENT_SOCKET"] = socket_path
    env["EAT_MCP_DISABLE"] = "1"
    env["EAT_DISABLE_DOTENV"] = "1"
    env.pop("DEEPSEEK_API_KEY", None)
    server = subprocess.Popen(
        [sys.executable, "-m", "agent.ipc_server"], cwd=ROOT, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    try:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        for _ in range(60):
            if os.path.exists(socket_path):
                try:
                    client.connect(socket_path)
                    break
                except OSError:
                    pass
            if server.poll() is not None:
                print("FAIL server exited", server.stdout.read() if server.stdout else "")
                return 1
            time.sleep(0.2)
        else:
            print("FAIL socket not ready")
            return 1

        send(client, {"type": "hello"})
        hello = recv(client)
        assert hello.get("type") == "hello_ack", hello

        send(client, {"type": "ping"})
        assert recv(client).get("type") == "pong"

        send(client, {
            "type": "agent.chat",
            "id": "t1",
            "payload": {
                "user_id": "u_demo",
                "session_id": "s-ipc",
                "message": "今天吃什么比较好",
                "mode": "qa",
                "tone": "normal",
                "access_level": "manual",
            },
        })
        events = []
        for _ in range(200):
            message = recv(client)
            if message.get("type") == "agent.event":
                events.append(message.get("event"))
            elif message.get("type") == "agent.done":
                break
            elif message.get("type") == "agent.error":
                print("FAIL agent error", message)
                return 1
        assert "meta" in events, events
        assert "token" in events, events
        assert "done" in events, events
        print("IPC_SMOKE_OK events=%d" % len(events))
        return 0
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    sys.exit(main())