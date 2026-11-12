from __future__ import annotations

import threading
from typing import Any

from agent.config import MCP_DISABLED, resolve_placeholders
from agent.mcp.client import MCPClientError, StdioMCPClient


class MCPManager:
    def __init__(self, config: dict[str, Any], registry: Any) -> None:
        self.config = config
        self.registry = registry
        self.clients: dict[str, StdioMCPClient] = {}
        self.errors: dict[str, str] = {}
        self.discovered: dict[str, list[str]] = {}
        self._lock = threading.Lock()
        self.simulated = False

    def start(self) -> None:
        if MCP_DISABLED:
            self.errors["_global"] = "MCP 已被 EAT_MCP_DISABLE=1 禁用"
            return
        servers = self.config.get("servers") or {}
        for name, cfg in servers.items():
            if not cfg.get("enabled", True):
                self.errors[name] = "配置中未启用"
                continue
            try:
                self._start_server(name, cfg)
            except Exception as exc:
                self.errors[name] = str(exc)

    def _start_server(self, name: str, cfg: dict[str, Any]) -> None:
        transport = str(cfg.get("transport", "stdio"))
        if transport != "stdio":
            self.errors[name] = f"暂不支持的传输方式：{transport}"
            return
        command = resolve_placeholders(str(cfg.get("command", "")))
        args = [resolve_placeholders(str(item)) for item in (cfg.get("args") or [])]
        cwd = resolve_placeholders(str(cfg.get("cwd", ""))) or None
        if not command:
            self.errors[name] = "缺少启动命令"
            return

        client = StdioMCPClient(
            name=name,
            command=command,
            args=args,
            cwd=cwd,
            timeout=float(cfg.get("timeout", 8)),
            env={str(key): str(value) for key, value in (cfg.get("env") or {}).items()},
        )
        client.start()
        tools = client.list_tools()
        allow = set((cfg.get("tools") or {}).get("allow") or [])
        deny = set((cfg.get("tools") or {}).get("deny") or [])
        registered: list[str] = []

        for tool in tools:
            remote_name = str(tool.get("name"))
            if allow and remote_name not in allow:
                continue
            if remote_name in deny:
                continue
            exposed = remote_name
            if self.registry.get(exposed) is not None:
                exposed = f"mcp_{name.replace('-', '_')}_{remote_name}"
            spec = self.registry.wrap_mcp_tool(
                server=name,
                tool=tool,
                caller=lambda tool_name, arguments, server_name=name: self.call(server_name, tool_name, arguments),
                exposed_name=exposed,
            )
            self.registry.register(spec)
            registered.append(exposed)

        with self._lock:
            self.clients[name] = client
            self.discovered[name] = registered
        self.errors.pop(name, None)

    def call(self, server: str, tool: str, arguments: dict[str, Any]) -> dict[str, Any]:
        self._ensure_simulated()
        client = self.clients.get(server)
        if client is None:
            return {"ok": False, "error": self.errors.get(server, f"MCP 服务 {server} 未启动")}
        return client.call_tool(tool, arguments)

    def _ensure_simulated(self) -> None:
        if self.simulated:
            return
        self.simulated = True

    def status(self) -> dict[str, Any]:
        servers = {}
        for name, cfg in (self.config.get("servers") or {}).items():
            client = self.clients.get(name)
            if client is not None:
                item = client.status()
                item["exposed_tools"] = self.discovered.get(name, [])
            else:
                item = {
                    "name": name,
                    "running": False,
                    "enabled": bool(cfg.get("enabled", True)),
                    "tools": [],
                    "exposed_tools": [],
                    "error": self.errors.get(name, ""),
                }
            servers[name] = item
        return {
            "disabled": MCP_DISABLED,
            "servers": servers,
            "errors": dict(self.errors),
        }

    def all_tools(self) -> list[str]:
        names: list[str] = []
        for tools in self.discovered.values():
            names.extend(tools)
        return names

    def close(self) -> None:
        for client in self.clients.values():
            client.close()
        self.clients.clear()