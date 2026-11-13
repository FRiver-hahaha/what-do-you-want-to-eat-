from __future__ import annotations

from typing import Any, Iterator

from agent import __version__
from agent.config import (
    DEFAULT_USER_ID,
    MCP_CONFIG,
    MODES_CONFIG,
    available_models,
    default_model_id,
)
from agent.core.context import ContextManager
from agent.core.memory import MemoryStore
from agent.core.planner import Planner
from agent.core.runtime import AgentEvent, AgentRuntime
from agent.core.super_agent import SuperAgent
from agent.core.trace import TRACER
from agent.data.db import init_db
from agent.data.service_client import ServiceClient
from agent.llm.gateway import LLMGateway
from agent.mcp.manager import MCPManager
from agent.rag.retriever import KnowledgeBase
from agent.rag.store import knowledge_dir
from agent.skills.market import SkillMarket
from agent.skills.registry import SkillsRegistry
from agent.tools.base import ToolContext
from agent.tools.diet_tools import build_diet_tools
from agent.tools.registry import ToolRegistry
from agent.tools.system_tools import build_system_tools


class AgentError(RuntimeError):
    def __init__(self, status: int, code: str, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message


class AgentApp:
    def __init__(self) -> None:
        init_db()
        self.service = ServiceClient()
        self.gateway = LLMGateway()
        self.registry = ToolRegistry()
        self.registry.register_many(build_diet_tools())
        self.registry.register_many(build_system_tools())
        self.market = SkillMarket(None, self.service)
        self.skills = SkillsRegistry(self.registry, self.market)
        self.market.skills = self.skills
        self.mcp = MCPManager(MCP_CONFIG, self.registry)
        self.mcp.start()
        self.knowledge = KnowledgeBase()
        self.knowledge.ensure_seeded()
        self.context = ContextManager()
        self.memory = MemoryStore()
        self.runtime = AgentRuntime(
            gateway=self.gateway,
            registry=self.registry,
            skills=self.skills,
            service=self.service,
            mcp=self.mcp,
            context=self.context,
            memory=self.memory,
            knowledge=self.knowledge,
            planner=Planner(),
            tracer=TRACER,
        )
        self.super_agent = SuperAgent(self.runtime)

    def meta(self) -> dict[str, Any]:
        return {
            "name": "今天你吃了吗 Agent 服务",
            "version": __version__,
            "default_model": default_model_id(),
            "models": available_models(),
            "modes": list(MODES_CONFIG.values()),
            "tools": self.registry.public_list(),
            "skills": [self._skill_public(skill, "") for skill in self.skills.all()],
            "mcp": self.mcp.status(),
            "service": self.service.status(),
            "rag": self.knowledge.stats(),
            "frontend": {
                "baseline": "origin/main c83068c",
                "contract": "/api/frontend/contract",
                "client_context": True,
            },
            "super_agent": self.super_agent.status(),
            "traces": {"list": "/api/traces", "detail": "/api/traces/{id}"},
            "capabilities": {
                "images": True,
                "streaming_tokens": True,
                "planning": True,
                "replanning": True,
                "trace": True,
                "watchdog": True,
            },
            "streaming": {
                "transport": "sse",
                "endpoint": "/api/chat/stream",
                "events": [
                    "meta",
                    "plan",
                    "skill",
                    "citation",
                    "tool_call",
                    "tool_result",
                    "token",
                    "notice",
                    "replan",
                    "confirm_required",
                    "worker_start",
                    "worker_done",
                    "watchdog",
                    "done",
                    "error",
                ],
            },
            "boundaries": {
                "database": "Agent 仅通过 ServiceClient 访问服务端；不连接 MySQL/Redis",
                "agent_storage": "会话、记忆、Skill 市场、RAG 索引、Trace 存放在 Agent 自有 SQLite",
                "frontend": "前端通过 agent-adapter.js 接入；离线时回退本地规则",
            },
        }

    def _skill_public(self, skill: Any, user_id: str) -> dict[str, Any]:
        item = skill.to_dict()
        item["enabled"] = self.skills.is_enabled(skill.name, user_id) if user_id else True
        return item

    def chat_events(self, request: dict[str, Any]) -> Iterator[AgentEvent]:
        request.setdefault("user_id", DEFAULT_USER_ID)
        yield from self.super_agent.handle(request)

    def chat(self, request: dict[str, Any]) -> dict[str, Any]:
        events = list(self.chat_events(request))
        answer = "".join(event.data.get("text", "") for event in events if event.type == "token")
        done = next((event.data for event in reversed(events) if event.type == "done"), {})
        errors = [event.data for event in events if event.type == "error"]
        return {
            "answer": answer,
            "events": [{"type": event.type, "data": event.data} for event in events],
            "done": done,
            "error": errors[0] if errors else None,
        }

    def models(self) -> list[dict[str, Any]]:
        return self.gateway.list_models()

    def frontend_contract(self) -> dict[str, Any]:
        from agent.core.frontend import contract

        return contract()

    def traces(self, limit: int = 20) -> list[dict[str, Any]]:
        return TRACER.list(limit)

    def trace(self, trace_id: str) -> dict[str, Any]:
        trace = TRACER.get(trace_id)
        if trace is None:
            raise AgentError(404, "trace_not_found", "Trace 不存在")
        return trace

    def session_messages(self, session_id: str) -> dict[str, Any]:
        session = self.context.session(session_id)
        if session is None:
            raise AgentError(404, "session_not_found", f"会话 {session_id} 不存在")
        return {
            "session": session,
            "messages": self.context.raw_messages(session_id, limit=500),
        }

    def delete_session(self, session_id: str) -> dict[str, Any]:
        self.context.clear(session_id)
        return {"ok": True, "session_id": session_id}

    def list_skills(self, mode: str, user_id: str) -> list[dict[str, Any]]:
        return [self._skill_public(skill, user_id) for skill in self.skills.all() if mode in skill.modes]

    def enable_skill(self, payload: dict[str, Any]) -> dict[str, Any]:
        mode = str(payload.get("mode") or "qa")
        if mode != "work":
            raise AgentError(403, "mode_forbidden", "启用或停用技能需要工作模式")
        skill_id = str(payload.get("id") or "")
        user_id = str(payload.get("user_id") or DEFAULT_USER_ID)
        enabled = bool(payload.get("enabled", True))
        ok, message = self.skills.set_enabled(skill_id, user_id, enabled)
        if not ok:
            raise AgentError(400, "skill_error", message)
        return {"ok": ok, "message": message, "skill": self._skill_public(self.skills.get(skill_id), user_id)}

    def install_skill(self, payload: dict[str, Any]) -> dict[str, Any]:
        mode = str(payload.get("mode") or "qa")
        if mode != "work":
            raise AgentError(403, "mode_forbidden", "导入技能需要工作模式")
        content = str(payload.get("content") or "")
        if not content:
            raise AgentError(400, "empty_content", "缺少 SKILL.md 内容")
        user_id = str(payload.get("user_id") or DEFAULT_USER_ID)
        ok, message, meta = self.skills.import_text(content, user_id)
        if not ok:
            raise AgentError(400, "skill_invalid", message)
        return {"ok": ok, "message": message, "skill": meta}

    def market_list(self, search: str = "", category: str = "") -> list[dict[str, Any]]:
        return self.market.list(search=search, category=category)

    def market_publish(self, payload: dict[str, Any]) -> dict[str, Any]:
        content = str(payload.get("content") or "")
        owner = str(payload.get("owner") or DEFAULT_USER_ID)
        if not content:
            raise AgentError(400, "empty_content", "缺少 SKILL.md 内容")
        ok, message, meta = self.market.publish(content, owner, str(payload.get("changelog") or ""))
        if not ok:
            raise AgentError(400, "publish_failed", message)
        return {"ok": ok, "message": message, "skill": meta}

    def market_install(self, payload: dict[str, Any]) -> dict[str, Any]:
        mode = str(payload.get("mode") or "qa")
        if mode != "work":
            raise AgentError(403, "mode_forbidden", "安装市场技能需要工作模式")
        skill_id = str(payload.get("id") or "")
        user_id = str(payload.get("user_id") or DEFAULT_USER_ID)
        ok, message, meta = self.skills.install_from_market(skill_id, user_id)
        if not ok:
            raise AgentError(400, "install_failed", message)
        return {"ok": ok, "message": message, "skill": meta}

    def market_share(self, payload: dict[str, Any]) -> dict[str, Any]:
        skill_id = str(payload.get("id") or "")
        from_user = str(payload.get("from_user") or DEFAULT_USER_ID)
        to_users = [str(item) for item in (payload.get("to_users") or [])]
        ok, message, shares = self.market.share(skill_id, from_user, to_users)
        if not ok:
            raise AgentError(403, "share_forbidden", message)
        return {"ok": ok, "message": message, "shares": shares}

    def shared_with_me(self, user_id: str) -> list[dict[str, Any]]:
        return self.market.shared_with_me(user_id)

    def memory_list(self, user_id: str) -> list[dict[str, Any]]:
        return self.memory.list(user_id)

    def memory_delete(self, memory_id: int, user_id: str) -> dict[str, Any]:
        ok = self.memory.delete(memory_id, user_id)
        if not ok:
            raise AgentError(404, "memory_not_found", "记忆不存在或不属于当前用户")
        return {"ok": True, "id": memory_id}

    def rag_search(self, payload: dict[str, Any]) -> dict[str, Any]:
        query = str(payload.get("query") or "")
        if not query:
            raise AgentError(400, "empty_query", "缺少 query")
        top_k = int(payload.get("top_k") or 4)
        results = self.knowledge.search(query, top_k=top_k)
        return {"query": query, "count": len(results), "results": results}

    def rag_ingest(self, payload: dict[str, Any]) -> dict[str, Any]:
        if payload.get("directory"):
            raise AgentError(400, "directory_forbidden", "出于路径安全考虑，接口只接受文本入库")
        text = str(payload.get("text") or "")
        title = str(payload.get("title") or "")
        doc_id = str(payload.get("doc_id") or "")
        if not text or not doc_id or not title:
            raise AgentError(400, "invalid_ingest", "需要 doc_id、title 与 text")
        result = self.knowledge.ingest_text(doc_id, title, text, source="api")
        return {"ok": True, "result": result, "stats": self.knowledge.stats()}

    def rag_stats(self) -> dict[str, Any]:
        return {**self.knowledge.stats(), "builtin_dir": str(knowledge_dir())}

    def mcp_status(self) -> dict[str, Any]:
        return self.mcp.status()

    def tools(self, mode: str) -> list[dict[str, Any]]:
        return self.registry.public_list(mode)

    def call_tool(self, payload: dict[str, Any]) -> dict[str, Any]:
        name = str(payload.get("name") or "")
        mode = str(payload.get("mode") or "qa")
        user_id = str(payload.get("user_id") or DEFAULT_USER_ID)
        session_id = str(payload.get("session_id") or "s_debug")
        ctx = ToolContext(
            user_id=user_id,
            session_id=session_id,
            mode=mode,
            confirmed=bool(payload.get("confirmed", False)),
            service=self.service,
            gateway=self.gateway,
            skills=self.skills,
            registry=self.registry,
        )
        result = self.registry.call(name, payload.get("arguments") or {}, ctx)
        return {
            "ok": result.ok,
            "summary": result.summary,
            "data": result.data,
            "error": result.error,
            "need_confirm": result.need_confirm,
        }

    def feedback(self, payload: dict[str, Any]) -> dict[str, Any]:
        user_id = str(payload.get("user_id") or DEFAULT_USER_ID)
        session_id = str(payload.get("session_id") or "s_feedback")
        self.runtime.record_feedback(
            user_id,
            session_id,
            str(payload.get("kind") or "note"),
            str(payload.get("value") or ""),
        )
        return {"ok": True}

    def close(self) -> None:
        self.super_agent.close()
        self.mcp.close()
        self.registry.shutdown()