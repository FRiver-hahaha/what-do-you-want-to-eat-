from __future__ import annotations

from typing import Any

from agent.tools.base import ToolContext, ToolResult, ToolSpec


def _schema(properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    return {"type": "object", "properties": properties, "required": required or []}


def _list_skills(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    skills = ctx.skills.list_for_mode(ctx.mode)
    summary = f"当前{ctx.mode}模式可用技能 {len(skills)} 个"
    return ToolResult(ok=True, summary=summary, data={"skills": skills})


def _enable_skill(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    skill_id = str(args.get("id") or "")
    enabled = bool(args.get("enabled", True))
    ok, message = ctx.skills.set_enabled(skill_id, ctx.user_id, enabled)
    return ToolResult(ok=ok, summary=message, data={"id": skill_id, "enabled": enabled})


def _install_skill(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    skill_id = str(args.get("id") or "")
    ok, message, meta = ctx.skills.install_from_market(skill_id, ctx.user_id)
    return ToolResult(ok=ok, summary=message, data=meta)


def _import_skill(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    content = str(args.get("content") or "")
    ok, message, meta = ctx.skills.import_text(content, ctx.user_id)
    return ToolResult(ok=ok, summary=message, data=meta)


def _agent_status(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    data = {
        "mode": ctx.mode,
        "service": ctx.service.status() if ctx.service else {},
        "tools": len(ctx.registry.all_specs()) if ctx.registry else 0,
        "skills": len(ctx.skills.all()) if ctx.skills else 0,
    }
    return ToolResult(ok=True, summary=f"当前运行在{ctx.mode}模式", data=data)


def build_system_tools() -> list[ToolSpec]:
    return [
        ToolSpec(
            name="list_skills",
            description="列出当前模式可用的 Agent 技能",
            parameters=_schema({}),
            handler=_list_skills,
            category="read",
            tags=("skill",),
        ),
        ToolSpec(
            name="enable_skill",
            description="启用或停用已安装的本地技能，仅工作模式可用",
            parameters=_schema({"id": {"type": "string"}, "enabled": {"type": "boolean"}}, required=["id", "enabled"]),
            handler=_enable_skill,
            category="local",
            modes=("work",),
            tags=("skill",),
        ),
        ToolSpec(
            name="install_skill",
            description="从 Skill 市场安装技能到当前用户，仅工作模式可用",
            parameters=_schema({"id": {"type": "string"}}, required=["id"]),
            handler=_install_skill,
            category="local",
            modes=("work",),
            require_confirm=True,
            tags=("skill", "market"),
        ),
        ToolSpec(
            name="import_skill",
            description="导入一份 SKILL.md 文本作为本地技能，仅工作模式可用",
            parameters=_schema({"content": {"type": "string", "description": "完整 SKILL.md 内容"}}, required=["content"]),
            handler=_import_skill,
            category="local",
            modes=("work",),
            require_confirm=True,
            tags=("skill", "import"),
        ),
        ToolSpec(
            name="get_agent_status",
            description="查询 Agent 当前模式、服务端连接状态、工具与技能数量",
            parameters=_schema({}),
            handler=_agent_status,
            category="read",
            tags=("status",),
        ),
    ]