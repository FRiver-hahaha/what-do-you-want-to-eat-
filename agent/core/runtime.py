from __future__ import annotations

import json
import uuid
from dataclasses import dataclass, field
from typing import Any, Iterator

from agent.config import MAX_TOOL_STEPS, MODES_CONFIG
from agent.core.context import ContextManager
from agent.core.memory import MemoryStore
from agent.core.prompts import build_system_prompt
from agent.data.db import execute, now_iso
from agent.llm.gateway import LLMGateway
from agent.rag.retriever import KnowledgeBase
from agent.tools.base import ToolContext


@dataclass
class AgentEvent:
    type: str
    data: dict[str, Any] = field(default_factory=dict)

    def to_sse(self) -> str:
        return f"event: {self.type}\ndata: {json.dumps(self.data, ensure_ascii=False, default=str)}\n\n"


RAG_HINTS = ("营养", "蛋白", "纤维", "蔬菜", "热量", "减脂", "增肌", "怎么吃", "能不能吃", "健康", "搭配", "外卖")
RAG_SKILL_CATEGORIES = {"nutrition", "health", "planning", "guess"}


class AgentRuntime:
    def __init__(
        self,
        gateway: LLMGateway,
        registry: Any,
        skills: Any,
        service: Any,
        mcp: Any = None,
        context: ContextManager | None = None,
        memory: MemoryStore | None = None,
        knowledge: KnowledgeBase | None = None,
    ) -> None:
        self.gateway = gateway
        self.registry = registry
        self.skills = skills
        self.service = service
        self.mcp = mcp
        self.context = context or ContextManager()
        self.memory = memory or MemoryStore()
        self.knowledge = knowledge or KnowledgeBase()

    def handle(self, request: dict[str, Any]) -> Iterator[AgentEvent]:
        user_id = str(request.get("user_id") or "u_demo")
        session_id = str(request.get("session_id") or f"s_{uuid.uuid4().hex[:12]}")
        message = str(request.get("message") or "").strip()
        mode = str(request.get("mode") or "qa")
        if mode not in MODES_CONFIG:
            mode = "qa"
        model_id = str(request.get("model_id") or "")
        skill_id = str(request.get("skill_id") or "")
        confirmed_tools = set(request.get("confirmed_tools") or [])
        use_rag = request.get("use_rag")

        if not message:
            yield AgentEvent("error", {"code": "empty_message", "message": "消息不能为空"})
            return

        self.context.ensure_session(session_id, user_id, mode, model_id, skill_id)
        self.context.append(session_id, user_id, "user", message)
        new_memories = self.memory.extract(user_id, message)
        skill = self.skills.resolve_for_request(skill_id, message, mode, user_id)
        profile = self.service.get_profile(user_id)

        sources = self._retrieve(message, skill, mode, use_rag)
        knowledge_block = self._knowledge_block(sources)
        tool_names = sorted(self.skills.allowed_tool_names(skill, mode)) if skill else [
            spec.name for spec in self.registry.specs_for_mode(mode)
        ]
        system_prompt = build_system_prompt(
            mode=mode,
            profile=profile,
            memory_block=self.memory.prompt_block(user_id),
            skill_prompt=self.skills.prompt_for(skill, mode) if skill else "",
            knowledge_block=knowledge_block,
            tool_names=tool_names,
        )

        yield AgentEvent(
            "meta",
            {
                "session_id": session_id,
                "mode": mode,
                "mode_name": MODES_CONFIG.get(mode, {}).get("name", mode),
                "model": model_id or "auto",
                "skill": skill.to_dict() if skill else None,
                "new_memories": new_memories,
                "service": self.service.status(),
            },
        )
        if skill:
            yield AgentEvent("skill", {"id": skill.name, "display_name": skill.display_name, "category": skill.category})
        for source in sources:
            yield AgentEvent("citation", source)

        messages: list[dict[str, Any]] = [{"role": "system", "content": system_prompt}]
        messages.extend(self.context.history_messages(session_id))
        tools = self.skills.tools_for_skill(skill, mode)
        max_steps = int(MODES_CONFIG.get(mode, {}).get("max_tool_steps", MAX_TOOL_STEPS))

        usage: dict[str, Any] = {}
        degraded = False
        used_model = model_id or "auto"
        finish_reason = "stop"

        for step in range(max_steps + 1):
            result = self.gateway.complete(model_id or None, messages, tools)
            usage = result.usage or usage
            used_model = result.model or used_model
            if result.degraded and not degraded:
                degraded = True
                yield AgentEvent(
                    "notice",
                    {"level": "warning", "message": "所选模型暂不可用，已切换到本地规则引擎继续回答"},
                )

            if not result.tool_calls or step >= max_steps:
                content = result.content or self._fallback_answer(message)
                for chunk in self._chunks(content):
                    yield AgentEvent("token", {"text": chunk})
                self.context.append(session_id, user_id, "assistant", content, meta={"model": used_model, "degraded": degraded})
                break

            assistant_message: dict[str, Any] = {
                "role": "assistant",
                "content": result.content or "",
                "tool_calls": result.tool_calls,
            }
            messages.append(assistant_message)
            self.context.append(
                session_id,
                user_id,
                "assistant",
                result.content or "",
                meta={"tool_calls": [call.get("function", {}).get("name") for call in result.tool_calls]},
            )

            confirm_required = False
            for call in result.tool_calls:
                call_id = str(call.get("id") or "")
                function = call.get("function") or {}
                name = str(function.get("name") or "")
                raw_arguments = function.get("arguments") or "{}"
                try:
                    arguments = json.loads(raw_arguments) if isinstance(raw_arguments, str) else dict(raw_arguments)
                except (json.JSONDecodeError, TypeError):
                    arguments = {}
                yield AgentEvent("tool_call", {"id": call_id, "name": name, "arguments": arguments})

                ctx = ToolContext(
                    user_id=user_id,
                    session_id=session_id,
                    mode=mode,
                    confirmed=name in confirmed_tools,
                    service=self.service,
                    gateway=self.gateway,
                    skills=self.skills,
                    registry=self.registry,
                )
                tool_result = self.registry.call(name, arguments, ctx)
                yield AgentEvent(
                    "tool_result",
                    {
                        "id": call_id,
                        "name": name,
                        "ok": tool_result.ok,
                        "need_confirm": tool_result.need_confirm,
                        "summary": tool_result.summary,
                        "error": tool_result.error,
                    },
                )

                content = tool_result.to_content()
                messages.append({"role": "tool", "tool_call_id": call_id, "content": content})
                self.context.append(
                    session_id,
                    user_id,
                    "tool",
                    content,
                    tool_name=name,
                    tool_call_id=call_id,
                    meta={"ok": tool_result.ok, "need_confirm": tool_result.need_confirm},
                )
                if tool_result.need_confirm:
                    confirm_required = True

            if confirm_required:
                finish_reason = "confirm_required"
                yield AgentEvent(
                    "confirm_required",
                    {
                        "message": "这项操作需要你的确认",
                        "mode": mode,
                        "confirmed_tools": sorted(
                            {str((call.get("function") or {}).get("name")) for call in result.tool_calls}
                        ),
                    },
                )
                break

        yield AgentEvent(
            "done",
            {
                "session_id": session_id,
                "model": used_model,
                "degraded": degraded,
                "finish_reason": finish_reason,
                "usage": usage,
                "citations": len(sources),
            },
        )

    def _retrieve(self, message: str, skill: Any, mode: str, use_rag: bool | None) -> list[dict[str, Any]]:
        if use_rag is False:
            return []
        enabled = use_rag is True
        if not enabled:
            if skill is not None and skill.category in RAG_SKILL_CATEGORIES:
                enabled = True
            elif any(hint in message for hint in RAG_HINTS):
                enabled = True
        if not enabled:
            return []
        try:
            return self.knowledge.search(message, top_k=3)
        except Exception:
            return []

    @staticmethod
    def _knowledge_block(sources: list[dict[str, Any]]) -> str:
        if not sources:
            return ""
        parts = []
        for index, source in enumerate(sources, start=1):
            parts.append(f"[{index}] {source.get('title')}：{source.get('text')}")
        return "\n\n".join(parts)

    @staticmethod
    def _chunks(text: str, size: int = 28) -> Iterator[str]:
        for index in range(0, len(text), size):
            yield text[index:index + size]

    @staticmethod
    def _fallback_answer(message: str) -> str:
        return (
            "我这轮没有取到需要的数据，先给你一个保守建议：优先保证蛋白质和蔬菜，"
            "饮水补到 1500ml 以上。你可以告诉我具体餐次，我再查记录和实时菜单。"
        )

    def record_feedback(self, user_id: str, session_id: str, kind: str, value: str) -> None:
        self.memory.add(user_id, "note", f"{kind}:{value}", confidence=0.9, source="feedback")
        execute(
            "INSERT INTO tool_audit (user_id, session_id, tool_name, args, ok, detail, created_at) VALUES (?, ?, ?, ?, 1, ?, ?)",
            (user_id, session_id, "user_feedback", json.dumps({"kind": kind, "value": value}, ensure_ascii=False), "feedback", now_iso()),
        )