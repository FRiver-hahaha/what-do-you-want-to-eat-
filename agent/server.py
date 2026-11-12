from __future__ import annotations

import json
import re
import sys
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Iterator
from urllib.parse import parse_qs, unquote, urlparse

from agent import __version__
from agent.app import AgentApp, AgentError
from agent.core.runtime import AgentEvent

MAX_BODY_BYTES = 1_000_000

SESSION_MESSAGES = re.compile(r"^/api/sessions/([^/]+)/messages$")
SESSION_DELETE = re.compile(r"^/api/sessions/([^/]+)$")
MEMORY_DELETE = re.compile(r"^/api/memory/(\d+)$")


class AgentHTTPRequestHandler(BaseHTTPRequestHandler):
    app: AgentApp
    server_version = "EatAgent/1.0"

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self._cors()
        self.send_header("Access-Control-Max-Age", "86400")
        self.end_headers()

    def do_GET(self) -> None:
        self._dispatch("GET")

    def do_POST(self) -> None:
        self._dispatch("POST")

    def do_DELETE(self) -> None:
        self._dispatch("DELETE")

    def log_message(self, fmt: str, *args: Any) -> None:
        if self.path.startswith("/health"):
            return
        sys.stderr.write(f"[agent-http] {self.address_string()} {fmt % args}\n")

    def _cors(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.send_header("Access-Control-Allow-Private-Network", "true")

    def _send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=False, default=str).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self._cors()
        self.end_headers()
        self.wfile.write(body)

    def _send_sse(self, events: Iterator[AgentEvent]) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache, no-transform")
        self.send_header("Connection", "close")
        self.send_header("X-Accel-Buffering", "no")
        self._cors()
        self.end_headers()
        try:
            for event in events:
                self.wfile.write(event.to_sse().encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            return

    def _read_body(self) -> dict[str, Any]:
        raw_length = self.headers.get("Content-Length") or "0"
        try:
            length = int(raw_length)
        except ValueError:
            raise AgentError(400, "bad_length", "Content-Length 非法")
        if length > MAX_BODY_BYTES:
            raise AgentError(413, "body_too_large", "请求体超过 1MB")
        if length <= 0:
            return {}
        raw = self.rfile.read(length)
        try:
            parsed = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise AgentError(400, "bad_json", "请求体必须是合法 JSON")
        if not isinstance(parsed, dict):
            raise AgentError(400, "bad_json", "请求体必须是 JSON 对象")
        return parsed

    def _dispatch(self, method: str) -> None:
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/") or "/"
        query = {key: values[0] for key, values in parse_qs(parsed.query).items()}
        try:
            body = self._read_body() if method in ("POST", "DELETE") else {}
            result = self._route(method, path, query, body)
            if isinstance(result, tuple) and result[0] == "sse":
                self._send_sse(result[1])
                return
            status, payload = result
            self._send_json(status, payload)
        except AgentError as exc:
            self._send_json(exc.status, {"ok": False, "error": exc.code, "message": exc.message})
        except Exception as exc:
            traceback.print_exc()
            self._send_json(500, {"ok": False, "error": "internal_error", "message": str(exc)})

    def _route(
        self,
        method: str,
        path: str,
        query: dict[str, str],
        body: dict[str, Any],
    ) -> tuple[int, dict[str, Any]] | tuple[str, Iterator[AgentEvent]]:
        app = self.app

        if path == "/" and method == "GET":
            return 200, {
                "name": "今天你吃了吗 Agent 服务",
                "version": __version__,
                "docs": "见 AGENT.md",
                "health": "/health",
                "meta": "/api/agent/meta",
            }

        if path == "/health" and method == "GET":
            return 200, {"ok": True, "version": __version__, "service": app.service.status()}

        if path == "/api/agent/meta" and method == "GET":
            return 200, app.meta()

        if path == "/api/models" and method == "GET":
            return 200, {"models": app.models(), "default": app.meta()["default_model"]}

        if path in ("/api/chat", "/api/chat/stream") and method == "POST":
            request = dict(body)
            should_stream = path.endswith("/stream") or bool(request.get("stream"))
            if should_stream:
                return "sse", app.chat_events(request)
            return 200, app.chat(request)

        match = SESSION_MESSAGES.match(path)
        if match and method == "GET":
            return 200, app.session_messages(unquote(match.group(1)))

        match = SESSION_DELETE.match(path)
        if match and method == "DELETE":
            return 200, app.delete_session(unquote(match.group(1)))

        if path == "/api/skills" and method == "GET":
            mode = query.get("mode", "qa")
            user_id = query.get("user_id", "u_demo")
            return 200, {"skills": app.list_skills(mode, user_id), "security": app.skills.security_summary()}

        if path == "/api/skills/enable" and method == "POST":
            return 200, app.enable_skill(body)

        if path == "/api/skills/install" and method == "POST":
            return 200, app.install_skill(body)

        if path == "/api/skills/market" and method == "GET":
            return 200, {"skills": app.market_list(query.get("search", ""), query.get("category", ""))}

        if path == "/api/skills/market/publish" and method == "POST":
            return 200, app.market_publish(body)

        if path == "/api/skills/market/install" and method == "POST":
            return 200, app.market_install(body)

        if path == "/api/skills/market/share" and method == "POST":
            return 200, app.market_share(body)

        if path == "/api/skills/market/shared" and method == "GET":
            return 200, {"shares": app.shared_with_me(query.get("user_id", "u_demo"))}

        if path == "/api/memory" and method == "GET":
            return 200, {"memories": app.memory_list(query.get("user_id", "u_demo"))}

        match = MEMORY_DELETE.match(path)
        if match and method == "DELETE":
            user_id = query.get("user_id") or str(body.get("user_id") or "u_demo")
            return 200, app.memory_delete(int(match.group(1)), user_id)

        if path == "/api/rag/search" and method == "POST":
            return 200, app.rag_search(body)

        if path == "/api/rag/ingest" and method == "POST":
            return 200, app.rag_ingest(body)

        if path == "/api/rag/stats" and method == "GET":
            return 200, app.rag_stats()

        if path == "/api/mcp/status" and method == "GET":
            return 200, app.mcp_status()

        if path == "/api/tools" and method == "GET":
            return 200, {"tools": app.tools(query.get("mode", "qa"))}

        if path == "/api/tools/call" and method == "POST":
            return 200, app.call_tool(body)

        if path == "/api/feedback" and method == "POST":
            return 200, app.feedback(body)

        if path == "/api/service/status" and method == "GET":
            return 200, app.service.status()

        raise AgentError(404, "not_found", f"接口不存在：{method} {path}")


def create_server(host: str, port: int) -> ThreadingHTTPServer:
    app = AgentApp()
    handler = type("BoundAgentHandler", (AgentHTTPRequestHandler,), {"app": app})
    server = ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True
    server.app_instance = app
    return server


def run_server(host: str, port: int) -> None:
    sys.stderr.write(f"[agent] 初始化中，MCP 服务与知识库正在加载...\n")
    server = create_server(host, port)
    sys.stderr.write(f"[agent] 已启动：http://{host}:{port}\n")
    sys.stderr.write(f"[agent] 元信息：http://{host}:{port}/api/agent/meta\n")
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        sys.stderr.write("\n[agent] 正在停止...\n")
    finally:
        server.shutdown()
        server.server_close()
        server.app_instance.close()