from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable

ToolHandler = Callable[[dict[str, Any], "ToolContext"], "ToolResult"]


@dataclass
class ToolContext:
    user_id: str
    session_id: str
    mode: str
    confirmed: bool = False
    service: Any = None
    gateway: Any = None
    skills: Any = None
    registry: Any = None
    emit: Callable[[str, dict[str, Any]], None] | None = None


@dataclass
class ToolResult:
    ok: bool
    summary: str
    data: Any = None
    error: str = ""
    need_confirm: bool = False

    def to_content(self, max_chars: int = 6000) -> str:
        import json

        payload = {
            "ok": self.ok,
            "summary": self.summary,
            "data": self.data,
            "error": self.error,
            "need_confirm": self.need_confirm,
        }
        text = json.dumps(payload, ensure_ascii=False, default=str)
        if len(text) > max_chars:
            payload["data"] = f"[结果过长，已截断，原始长度 {len(text)}]"
            text = json.dumps(payload, ensure_ascii=False, default=str)
        return text


@dataclass
class ToolSpec:
    name: str
    description: str
    parameters: dict[str, Any]
    handler: ToolHandler
    category: str = "read"
    modes: tuple[str, ...] = ("qa", "work")
    require_confirm: bool = False
    source: str = "local"
    tags: tuple[str, ...] = field(default_factory=tuple)

    def openai_schema(self) -> dict[str, Any]:
        return {
            "type": "function",
            "function": {
                "name": self.name,
                "description": self.description,
                "parameters": self.parameters,
            },
        }

    def public_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "description": self.description,
            "category": self.category,
            "modes": list(self.modes),
            "require_confirm": self.require_confirm,
            "source": self.source,
            "tags": list(self.tags),
        }