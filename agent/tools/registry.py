from __future__ import annotations

import json
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FutureTimeout
from typing import Any, Callable

from agent.config import MAX_TOOL_RESULT_CHARS, MODES_CONFIG
from agent.data.db import execute, now_iso
from agent.tools.base import ToolContext, ToolResult, ToolSpec


class ToolPermissionError(RuntimeError):
    pass


class ToolRegistry:
    def __init__(self) -> None:
        self._tools: dict[str, ToolSpec] = {}
        self._executor = ThreadPoolExecutor(max_workers=8, thread_name_prefix="tool")

    def register(self, spec: ToolSpec) -> None:
        self._tools[spec.name] = spec

    def register_many(self, specs: list[ToolSpec]) -> None:
        for spec in specs:
            self.register(spec)

    def unregister_source(self, prefix: str) -> None:
        for name in [name for name, spec in self._tools.items() if spec.source.startswith(prefix)]:
            self._tools.pop(name, None)

    def get(self, name: str) -> ToolSpec | None:
        return self._tools.get(name)

    def all_specs(self) -> list[ToolSpec]:
        return list(self._tools.values())

    def specs_for_mode(self, mode: str) -> list[ToolSpec]:
        mode_cfg = MODES_CONFIG.get(mode, MODES_CONFIG.get("qa", {}))
        categories = set(mode_cfg.get("tool_categories", ["read"]))
        allow_local = bool(mode_cfg.get("allow_local_tools", False))
        result = []
        for spec in self._tools.values():
            if mode not in spec.modes:
                continue
            if spec.category == "local" and not allow_local:
                continue
            if spec.category not in categories:
                continue
            result.append(spec)
        return result

    def openai_tools(self, mode: str) -> list[dict[str, Any]]:
        return [spec.openai_schema() for spec in self.specs_for_mode(mode)]

    def public_list(self, mode: str | None = None) -> list[dict[str, Any]]:
        specs = self.specs_for_mode(mode) if mode else self.all_specs()
        return [spec.public_dict() for spec in specs]

    def call(self, name: str, arguments: dict[str, Any], ctx: ToolContext) -> ToolResult:
        spec = self._tools.get(name)
        if spec is None:
            return ToolResult(ok=False, summary=f"工具 {name} 不存在", error="tool_not_found")

        mode_cfg = MODES_CONFIG.get(ctx.mode, MODES_CONFIG.get("qa", {}))
        if ctx.mode not in spec.modes or spec.category not in set(mode_cfg.get("tool_categories", [])):
            return ToolResult(ok=False, summary=f"当前{ctx.mode}模式无权调用 {name}", error="permission_denied")
        if spec.category == "local" and not mode_cfg.get("allow_local_tools", False):
            return ToolResult(ok=False, summary=f"当前模式未开启本地工具权限", error="permission_denied")

        if spec.require_confirm and not ctx.confirmed:
            return ToolResult(
                ok=False,
                need_confirm=True,
                summary=f"{spec.description}需要用户确认后才能执行",
                error="confirm_required",
            )

        safe_args = dict(arguments or {})
        safe_args.pop("user_id", None)
        safe_args.pop("session_id", None)

        try:
            future = self._executor.submit(spec.handler, safe_args, ctx)
            result = future.result(timeout=15.0)
        except FutureTimeout:
            result = ToolResult(ok=False, summary=f"工具 {name} 执行超时", error="timeout")
        except Exception as exc:
            result = ToolResult(ok=False, summary=f"工具 {name} 执行失败", error=str(exc))

        self._audit(ctx, name, safe_args, result)
        return result

    @staticmethod
    def _audit(ctx: ToolContext, name: str, arguments: dict[str, Any], result: ToolResult) -> None:
        try:
            execute(
                "INSERT INTO tool_audit (user_id, session_id, tool_name, args, ok, detail, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
                (
                    ctx.user_id,
                    ctx.session_id,
                    name,
                    json.dumps(arguments, ensure_ascii=False)[:2000],
                    1 if result.ok else 0,
                    (result.summary or result.error)[:500],
                    now_iso(),
                ),
            )
        except Exception:
            pass

    def wrap_mcp_tool(
        self,
        server: str,
        tool: dict[str, Any],
        caller: Callable[[str, dict[str, Any]], dict[str, Any]],
        exposed_name: str | None = None,
    ) -> ToolSpec:
        remote_name = str(tool.get("name"))
        name = exposed_name or remote_name
        schema = tool.get("inputSchema") or {"type": "object", "properties": {}}
        properties = dict(schema.get("properties") or {})
        properties.pop("user_id", None)
        properties.pop("session_id", None)

        def handler(arguments: dict[str, Any], ctx: ToolContext) -> ToolResult:
            payload = dict(arguments)
            payload["user_id"] = ctx.user_id
            response = caller(remote_name, payload)
            if not response.get("ok"):
                return ToolResult(ok=False, summary=f"MCP 工具 {remote_name} 返回错误", error=str(response.get("error", "")))
            result = response.get("result") or {}
            content = result.get("content") or []
            text = ""
            if content and isinstance(content[0], dict):
                text = str(content[0].get("text", ""))
            return ToolResult(ok=True, summary=text or f"MCP 工具 {remote_name} 执行完成", data=result.get("structuredContent"))

        return ToolSpec(
            name=name,
            description=str(tool.get("description") or f"MCP 工具 {name}"),
            parameters={"type": "object", "properties": properties, "required": schema.get("required", [])},
            handler=handler,
            category="read",
            modes=("qa", "work"),
            require_confirm=False,
            source=f"mcp:{server}",
            tags=("mcp",),
        )

    def shutdown(self) -> None:
        self._executor.shutdown(wait=False, cancel_futures=True)