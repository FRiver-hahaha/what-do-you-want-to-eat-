from __future__ import annotations

import os
import queue
import subprocess
import threading
import time
from typing import Any

from agent.mcp.protocol import (
    MCP_PROTOCOL_VERSION,
    MCPProtocolError,
    PARSE_ERROR,
    decode_message,
    encode_message,
    notification_message,
    request_message,
)


class MCPTimeoutError(RuntimeError):
    pass


class MCPClientError(RuntimeError):
    pass


class StdioMCPClient:
    def __init__(
        self,
        name: str,
        command: str,
        args: list[str] | None = None,
        cwd: str | None = None,
        timeout: float = 8.0,
        env: dict[str, str] | None = None,
    ) -> None:
        self.name = name
        self.command = command
        self.args = list(args or [])
        self.cwd = cwd
        self.timeout = timeout
        self.env = env or {}
        self.process: subprocess.Popen[str] | None = None
        self._responses: queue.Queue[str] = queue.Queue()
        self._stderr_lines: list[str] = []
        self._lock = threading.Lock()
        self._request_id = 0
        self._closed = False
        self._reader: threading.Thread | None = None
        self._stderr_reader: threading.Thread | None = None
        self.server_info: dict[str, Any] = {}
        self.tools: list[dict[str, Any]] = []
        self.last_error = ""

    def start(self) -> dict[str, Any]:
        if self.process is not None:
            return self.server_info
        environment = os.environ.copy()
        environment.update({"PYTHONUNBUFFERED": "1", "PYTHONIOENCODING": "utf-8"})
        environment.update(self.env)
        try:
            self.process = subprocess.Popen(
                [self.command, *self.args],
                cwd=self.cwd or None,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                env=environment,
            )
        except OSError as exc:
            self.last_error = str(exc)
            raise MCPClientError(f"MCP 服务 {self.name} 启动失败：{exc}") from exc

        self._reader = threading.Thread(target=self._read_stdout, name=f"mcp-out-{self.name}", daemon=True)
        self._reader.start()
        self._stderr_reader = threading.Thread(target=self._read_stderr, name=f"mcp-err-{self.name}", daemon=True)
        self._stderr_reader.start()
        self.server_info = self.initialize()
        return self.server_info

    def _read_stdout(self) -> None:
        assert self.process is not None and self.process.stdout is not None
        while True:
            line = self.process.stdout.readline()
            if not line:
                break
            self._responses.put(line)
        self._responses.put('')

    def _read_stderr(self) -> None:
        assert self.process is not None and self.process.stderr is not None
        while True:
            line = self.process.stderr.readline()
            if not line:
                break
            text = line.rstrip()
            if text:
                self._stderr_lines.append(text[:500])
                if len(self._stderr_lines) > 50:
                    self._stderr_lines.pop(0)

    def stderr_tail(self, limit: int = 10) -> list[str]:
        return self._stderr_lines[-limit:]

    def _next_id(self) -> int:
        self._request_id += 1
        return self._request_id

    def _write(self, message: dict[str, Any]) -> None:
        if self.process is None or self.process.stdin is None:
            raise MCPClientError("MCP 进程未启动")
        self.process.stdin.write(encode_message(message))
        self.process.stdin.flush()

    def _read_until_response(self, request_id: int, timeout: float) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise MCPTimeoutError(f"MCP 服务 {self.name} 响应超时")
            try:
                line = self._responses.get(timeout=remaining)
            except queue.Empty as exc:
                raise MCPTimeoutError(f"MCP 服务 {self.name} 响应超时") from exc
            if line == "":
                raise MCPClientError(f"MCP 服务 {self.name} 已退出：{' | '.join(self.stderr_tail(3))}")
            try:
                message = decode_message(line)
            except MCPProtocolError:
                continue
            if message.get("id") == request_id:
                return message

    def request(self, method: str, params: dict[str, Any] | None = None, timeout: float | None = None) -> dict[str, Any]:
        with self._lock:
            request_id = self._next_id()
            self._write(request_message(request_id, method, params))
            response = self._read_until_response(request_id, timeout or self.timeout)
        if "error" in response:
            error = response["error"]
            raise MCPClientError(f"{error.get('code', PARSE_ERROR)}: {error.get('message', '未知错误')}")
        return dict(response.get("result") or {})

    def notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        with self._lock:
            self._write(notification_message(method, params))

    def initialize(self) -> dict[str, Any]:
        result = self.request(
            "initialize",
            {
                "protocolVersion": MCP_PROTOCOL_VERSION,
                "capabilities": {"roots": {"listChanged": False}, "sampling": {}},
                "clientInfo": {"name": "eat-agent", "version": "1.0.0"},
            },
        )
        self.notify("notifications/initialized")
        return result

    def list_tools(self) -> list[dict[str, Any]]:
        result = self.request("tools/list", {})
        self.tools = list(result.get("tools") or [])
        return self.tools

    def call_tool(self, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        try:
            result = self.request("tools/call", {"name": name, "arguments": arguments})
            return {"ok": True, "result": result}
        except (MCPClientError, MCPTimeoutError) as exc:
            return {"ok": False, "error": str(exc)}

    def ping(self) -> bool:
        try:
            self.request("ping", {}, timeout=3)
            return True
        except (MCPClientError, MCPTimeoutError):
            return False

    def status(self) -> dict[str, Any]:
        running = self.process is not None and self.process.poll() is None
        return {
            "name": self.name,
            "running": running,
            "command": self.command,
            "args": self.args,
            "pid": self.process.pid if self.process else None,
            "tools": [tool.get("name") for tool in self.tools],
            "server": self.server_info.get("serverInfo", {}),
            "protocol": self.server_info.get("protocolVersion", ""),
            "stderr_tail": self.stderr_tail(3),
        }

    def close(self) -> None:
        if self.process is None or self._closed:
            return
        self._closed = True
        try:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=3)
        except OSError:
            pass
        finally:
            for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                try:
                    if stream is not None:
                        stream.close()
                except OSError:
                    pass