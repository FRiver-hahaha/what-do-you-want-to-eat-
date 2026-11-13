from __future__ import annotations

import json
import uuid
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Any, Iterator

from agent.config import MAX_TOOL_STEPS, MODES_CONFIG
from agent.core.context import ContextManager, estimate_tokens
from agent.core.frontend import build_client_block, merge_profile, sanitize_client_context
from agent.core.memory import MemoryStore
from agent.core.planner import Planner
from agent.core.prompts import build_system_prompt
from agent.core.safety import detect_injection, filter_output, sanitize_images
from agent.core.trace import TRACER, TraceRecorder
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
ALWAYS_CONFIRM_TOOLS = {"record_meal"}
MODEL_REQUEST_TOKEN_LIMIT = 60000

IMAGE_REFERENCE_HINTS = (
    "这张图",
    "这图",
    "这张照片",
    "这个图",
    "图片里",
    "图中",
    "上图",
    "刚才那",
    "上一张",
    "刚发的图",
    "那张图",
    "这张图片",
)

def _mentions_image(message: str) -> bool:
    return any(hint in message for hint in IMAGE_REFERENCE_HINTS)



def _user_content(message: str, images: list[str]) -> Any:
    if not images:
        return message
    parts: list[dict[str, Any]] = [{"type": "text", "text": message or "请分析这张图片里的食物"}]
    for image in images:
        parts.append({"type": "image_url", "image_url": {"url": image}})
    return parts


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
        planner: Planner | None = None,
        tracer: TraceRecorder | None = None,
    ) -> None:
        self.gateway = gateway
        self.registry = registry
        self.skills = skills
        self.service = service
        self.mcp = mcp
        self.context = context or ContextManager()
        self.memory = memory or MemoryStore()
        self.knowledge = knowledge or KnowledgeBase()
        self.planner = planner or Planner()
        self.tracer = tracer or TRACER
        self._image_cache: OrderedDict[str, list[str]] = OrderedDict()
        self._image_cache_limit = 50

    def handle(self, request: dict[str, Any]) -> Iterator[AgentEvent]:
        user_id = str(request.get("user_id") or "u_demo")
        session_id = str(request.get("session_id") or f"s_{uuid.uuid4().hex[:12]}")
        message = str(request.get("message") or "").strip()
        mode = str(request.get("mode") or "qa")
        if mode not in MODES_CONFIG:
            mode = "qa"
        model_id = str(request.get("model_id") or "")
        skill_id = str(request.get("skill_id") or "")
        skill_ids = [str(item) for item in (request.get("skill_ids") or []) if str(item).strip()]
        tone_id = str(request.get("tone") or "normal")
        access_level = str(request.get("access_level") or "manual")
        if access_level not in ("manual", "full"):
            access_level = "manual"
        confirmed_tools = set(request.get("confirmed_tools") or [])
        use_rag = request.get("use_rag")
        client_context = sanitize_client_context(request.get("client_context"))
        images, image_error = sanitize_images(request.get("images"))
        image_source = "upload" if images else ""
        cache_key = f"{user_id}:{session_id}"
        cached_images = self._image_cache.get(cache_key) or []
        if not images and cached_images and (request.get("use_cached_image") or _mentions_image(message)):
            images = list(cached_images)
            image_source = "cache"
        if images:
            self._image_cache[cache_key] = list(images)
            self._image_cache.move_to_end(cache_key)
            while len(self._image_cache) > self._image_cache_limit:
                self._image_cache.popitem(last=False)
        injection = detect_injection(message)

        if not message and not images:
            yield AgentEvent("error", {"code": "empty_message", "message": "消息和图片不能同时为空"})
            return

        display_message = message or "请分析这张图片里的食物"
        trace_id = self.tracer.start(session_id, user_id, model_id or "")
        status = "ok"
        tokens = 0
        try:
            self.context.ensure_session(session_id, user_id, mode, model_id, skill_id)
            history = self.context.history_messages(session_id)
            history_tokens = sum(estimate_tokens(str(row.get("content") or "")) for row in history)
            if history_tokens > MODEL_REQUEST_TOKEN_LIMIT * 0.8:
                self.context.compact_all(session_id)
                history = self.context.history_messages(session_id)
            summary = self.context.session_summary(session_id)
            self.context.append(
                session_id,
                user_id,
                "user",
                display_message,
                meta={"images": len(images), "injection_risk": injection["risk"]},
            )
            new_memories = self.memory.extract(user_id, message)
            skills = self.skills.resolve_experts(skill_ids, skill_id, message, mode, user_id)
            skill = skills[0] if skills else None
            tone_skill = self.skills.resolve_tone(tone_id)
            profile = self.service.get_profile(user_id)
            allow_local_context = self.service.mode == "mock"
            if allow_local_context:
                profile = merge_profile(profile, client_context.get("profile"))

            plan = self.planner.plan(display_message, mode, bool(images), profile)
            sources = self._retrieve(message or display_message, skill, mode, use_rag)
            knowledge_block = self._knowledge_block(sources)
            tool_names = sorted(self.skills.allowed_tool_names_many(skills, mode))
            system_prompt = build_system_prompt(
                mode=mode,
                profile=profile,
                memory_block=self.memory.prompt_block(user_id),
                skill_prompt=self.skills.combo_prompt_for(skills, mode),
                tone_prompt=self.skills.tone_prompt_for(tone_skill),
                knowledge_block=knowledge_block,
                tool_names=tool_names,
                client_block=build_client_block(client_context, allow_local_context),
                plan_block=plan.to_prompt(),
                summary_block=summary,
                has_images=bool(images),
                access_level=access_level,
            )

            yield AgentEvent(
                "meta",
                {
                    "session_id": session_id,
                    "trace_id": trace_id,
                    "mode": mode,
                    "mode_name": MODES_CONFIG.get(mode, {}).get("name", mode),
                    "model": model_id or "auto",
                    "skill": skill.to_dict() if skill else None,
                    "skills": [item.to_dict() for item in skills],
                    "tone": {
                        "id": tone_id,
                        "skill": tone_skill.name if tone_skill else "",
                        "name": tone_skill.display_name if tone_skill else "",
                    },
                    "access_level": access_level,
                    "plan": plan.to_dict(),
                    "images": len(images),
                    "image_source": image_source,
                    "new_memories": new_memories,
                    "service": self.service.status(),
                    "client_context": bool(client_context),
                    "injection": injection,
                },
            )
            if image_error:
                yield AgentEvent("notice", {"level": "warning", "message": image_error})
            if images and not self.gateway.supports(model_id or None, "vision"):
                yield AgentEvent(
                    "notice",
                    {"level": "warning", "message": "当前模型未声明视觉能力，图片可能无法识别，建议切换到视觉模型"},
                )
            if injection["risk"] == "high":
                yield AgentEvent(
                    "notice",
                    {"level": "warning", "message": "检测到试图改变规则的指令，已按安全边界处理"},
                )
            yield AgentEvent("plan", plan.to_dict())
            for item in skills:
                yield AgentEvent("skill", {"id": item.name, "display_name": item.display_name, "category": item.category})
            for source in sources:
                yield AgentEvent("citation", source)

            messages: list[dict[str, Any]] = [{"role": "system", "content": system_prompt}]
            messages.extend(history)
            messages.append({"role": "user", "content": _user_content(message, images)})
            tools = self.skills.tools_for_skills(skills, mode)
            max_steps = int(MODES_CONFIG.get(mode, {}).get("max_tool_steps", MAX_TOOL_STEPS))

            usage: dict[str, Any] = {}
            degraded = False
            used_model = model_id or "auto"
            finish_reason = "stop"

            for step in range(max_steps + 1):
                step_content = ""
                tool_calls: list[dict[str, Any]] = []
                step_usage: dict[str, Any] = {}
                step_finish = "stop"
                step_degraded = False
                step_model = used_model
                sent_any = False
                with self.tracer.span(trace_id, f"llm:{step + 1}", "llm", {"model": model_id or "auto"}):
                    for chunk in self.gateway.stream_complete(model_id or None, messages, tools):
                        if chunk.delta:
                            step_content += chunk.delta
                            sent_any = True
                            yield AgentEvent("token", {"text": chunk.delta})
                        if chunk.tool_calls:
                            tool_calls = chunk.tool_calls
                        if chunk.usage:
                            step_usage = chunk.usage
                        if chunk.finish_reason:
                            step_finish = chunk.finish_reason
                        if chunk.model:
                            step_model = chunk.model
                        step_degraded = step_degraded or chunk.degraded

                usage = step_usage or usage
                used_model = step_model or used_model
                tokens = int(usage.get("total_tokens") or tokens)
                if step_degraded and not degraded:
                    degraded = True
                    yield AgentEvent(
                        "notice",
                        {"level": "warning", "message": "所选模型暂不可用，已切换到本地规则引擎继续回答"},
                    )

                if not tool_calls or step >= max_steps:
                    content = step_content or self._fallback_answer(message)
                    filtered, findings = filter_output(content)
                    if findings:
                        yield AgentEvent("notice", {"level": "warning", "message": "回答中的敏感信息已自动隐藏"})
                    if not sent_any:
                        for piece in self._chunks(filtered):
                            yield AgentEvent("token", {"text": piece})
                    self.context.append(
                        session_id,
                        user_id,
                        "assistant",
                        filtered,
                        meta={"model": used_model, "degraded": degraded, "trace_id": trace_id},
                    )
                    finish_reason = "stop"
                    break

                assistant_message: dict[str, Any] = {
                    "role": "assistant",
                    "content": step_content or "",
                    "tool_calls": tool_calls,
                }
                messages.append(assistant_message)
                self.context.append(
                    session_id,
                    user_id,
                    "assistant",
                    step_content or "",
                    meta={"tool_calls": [call.get("function", {}).get("name") for call in tool_calls]},
                )

                confirm_required = False
                failed_tools: list[tuple[str, str]] = []
                for call in tool_calls:
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
                        confirmed=name in confirmed_tools
                        or (access_level == "full" and name not in ALWAYS_CONFIRM_TOOLS),
                        service=self.service,
                        gateway=self.gateway,
                        skills=self.skills,
                        registry=self.registry,
                        client_context=client_context if allow_local_context else {},
                    )
                    with self.tracer.span(trace_id, f"tool:{name}", "tool", {"arguments": arguments}):
                        tool_result = self.registry.call(name, arguments, ctx)
                        self.planner.mark(plan, name, "ok" if tool_result.ok else "error")
                    yield AgentEvent(
                        "tool_result",
                        {
                            "id": call_id,
                            "name": name,
                            "ok": tool_result.ok,
                            "need_confirm": tool_result.need_confirm,
                            "summary": tool_result.summary,
                            "error": tool_result.error,
                            **(
                                {"data": {"profile": (tool_result.data or {}).get("profile", {})}}
                                if name == "update_health_profile" and tool_result.ok
                                else {}
                            ),
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
                    elif not tool_result.ok:
                        failed_tools.append((name, tool_result.error or tool_result.summary))

                if confirm_required:
                    finish_reason = "confirm_required"
                    yield AgentEvent(
                        "confirm_required",
                        {
                            "message": "这项操作需要你的确认",
                            "mode": mode,
                            "confirmed_tools": sorted(
                                {str((call.get("function") or {}).get("name")) for call in tool_calls}
                            ),
                        },
                    )
                    break

                if failed_tools:
                    name, error = failed_tools[0]
                    plan = self.planner.replan(plan, name, error)
                    yield AgentEvent("replan", {"plan": plan.to_dict(), "failed_tool": name, "error": error[:200]})
                    messages.append(
                        {
                            "role": "system",
                            "content": f"工具 {name} 执行失败：{error[:200]}。请换一种可用工具或如实说明，不要重复同一个失败调用。",
                        }
                    )
                    continue

            yield AgentEvent(
                "done",
                {
                    "session_id": session_id,
                    "trace_id": trace_id,
                    "model": used_model,
                    "degraded": degraded,
                    "finish_reason": finish_reason,
                    "usage": usage,
                    "tokens": tokens,
                    "plan": plan.to_dict(),
                    "citations": len(sources),
                },
            )
        except GeneratorExit:
            status = "cancelled"
            raise
        except Exception as exc:
            status = "error"
            yield AgentEvent("error", {"code": "agent_error", "message": str(exc)[:300]})
        finally:
            self.tracer.finish(trace_id, status, tokens)

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