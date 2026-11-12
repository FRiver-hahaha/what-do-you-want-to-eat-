from __future__ import annotations

import atexit
import json
import os
import tempfile
import unittest
import uuid

TMP_DIR = tempfile.mkdtemp(prefix="eat-agent-test-")
os.environ["EAT_AGENT_DATA_DIR"] = TMP_DIR
os.environ["EAT_AGENT_DB"] = os.path.join(TMP_DIR, "agent.db")
os.environ["EAT_AGENT_RAG_DB"] = os.path.join(TMP_DIR, "knowledge.db")
os.environ["EAT_MCP_DISABLE"] = "0"
os.environ.pop("EAT_SERVER_BASE_URL", None)

from agent.app import AgentApp, AgentError
from agent.config import MCP_CONFIG
from agent.core.memory import MemoryStore
from agent.data.db import init_db
from agent.mcp.manager import MCPManager
from agent.server import AgentHTTPRequestHandler
from agent.tools.base import ToolContext
from agent.tools.diet_tools import build_diet_tools
from agent.tools.registry import ToolRegistry
from agent.tools.system_tools import build_system_tools

_APP: AgentApp | None = None


def get_app() -> AgentApp:
    global _APP
    if _APP is None:
        init_db()
        _APP = AgentApp()
        atexit.register(_APP.close)
    return _APP


def new_session() -> str:
    return f"s_{uuid.uuid4().hex[:10]}"


class TestHTTP(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = get_app()
        cls.handler = object.__new__(AgentHTTPRequestHandler)
        cls.handler.app = cls.app

    def _route(
        self,
        method: str,
        path: str,
        payload: dict | None = None,
        query: dict | None = None,
    ) -> tuple[int, dict]:
        result = self.handler._route(method, path, query or {}, payload or {})
        if isinstance(result, tuple) and result[0] == "sse":
            text = "".join(event.to_sse() for event in result[1])
            return 200, {"sse": text}
        return result

    def test_health_and_meta(self) -> None:
        status, payload = self._route("GET", "/health")
        self.assertEqual(status, 200)
        self.assertTrue(payload["ok"])
        status, meta = self._route("GET", "/api/agent/meta")
        self.assertEqual(status, 200)
        self.assertIn("qa", [mode["id"] for mode in meta["modes"]])
        self.assertTrue(meta["models"])

    def test_chat_json(self) -> None:
        status, payload = self._route(
            "POST",
            "/api/chat",
            {"user_id": "u_demo", "session_id": new_session(), "message": "今天吃了什么", "mode": "qa"},
        )
        self.assertEqual(status, 200)
        self.assertTrue(payload["answer"])
        self.assertEqual(payload["done"]["finish_reason"], "stop")

    def test_chat_sse(self) -> None:
        _, payload = self._route(
            "POST",
            "/api/chat/stream",
            {"user_id": "u_demo", "session_id": new_session(), "message": "今天吃了什么", "mode": "qa"},
        )
        body = payload["sse"]
        self.assertIn("event: meta", body)
        self.assertIn("event: token", body)
        self.assertIn("event: done", body)

    def test_tool_permission_over_http(self) -> None:
        status, payload = self._route(
            "POST",
            "/api/tools/call",
            {"name": "get_menu", "user_id": "u_demo", "mode": "qa", "arguments": {}},
        )
        self.assertEqual(status, 200)
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["error"], "permission_denied")

    def test_skill_install_requires_work_mode(self) -> None:
        with self.assertRaises(AgentError) as captured:
            self._route("POST", "/api/skills/install", {"content": "---\nname: x\n---\nbody", "mode": "qa"})
        self.assertEqual(captured.exception.status, 403)


class TestMarket(unittest.TestCase):
    def test_publish_install_and_friend_share(self) -> None:
        app = get_app()
        name = f"market_test_{uuid.uuid4().hex[:6]}"
        content = f"""---
name: {name}
display_name: 市场测试技能
description: 用于验证 Skill 市场的发布、安装与好友分享
version: 1.0.0
author: u_demo
category: test
tags: [测试]
allowed-tools: [get_health_profile]
permissions: [read:diet]
modes: [qa, work]
triggers: [市场测试]
---

## 步骤

1. 读取档案。
2. 返回测试结果。
"""
        ok, message, meta = app.market.publish(content, "u_demo", "首次发布")
        self.assertTrue(ok, message)
        listed = [item["id"] for item in app.market_list(name)]
        self.assertIn(name, listed)

        ok, message, meta = app.market.install(name, "u_demo")
        self.assertTrue(ok, message)
        app.skills.reload()
        self.assertIsNotNone(app.skills.get(name))

        ok, message, _ = app.market.share(name, "u_demo", ["u_lin"])
        self.assertTrue(ok, message)

        ok, message, _ = app.market.share(name, "u_demo", ["u_stranger"])
        self.assertFalse(ok)
        self.assertIn("好友", message)


class TestMCP(unittest.TestCase):
    def test_server_started_and_tool_call(self) -> None:
        app = get_app()
        status = app.mcp.status()
        diet = status["servers"]["diet-data"]
        if not diet.get("running"):
            self.skipTest(f"MCP 未启动：{diet.get('error')}")
        self.assertEqual(diet["protocol"], "2024-11-05")

        response = app.mcp.call("diet-data", "get_health_profile", {"user_id": "u_demo"})
        self.assertTrue(response["ok"], response.get("error"))
        text = response["result"]["content"][0]["text"]
        self.assertIn("阿岑", text)

        names = [tool["name"] for tool in app.registry.public_list("qa")]
        self.assertIn("mcp_diet_data_get_health_profile", names)

    def test_direct_client_pipeline(self) -> None:
        registry = ToolRegistry()
        registry.register_many(build_diet_tools())
        manager = MCPManager(MCP_CONFIG, registry)
        try:
            manager.start()
            status = manager.status()["servers"]["diet-data"]
            if not status.get("running"):
                self.skipTest(f"MCP 未启动：{status.get('error')}")
            self.assertGreater(len(status.get("tools") or []), 3)
            response = manager.call("diet-data", "get_weekly_stats", {"user_id": "u_demo"})
            self.assertTrue(response["ok"], response.get("error"))
        finally:
            manager.close()
            registry.shutdown()


class TestRuntime(unittest.TestCase):
    def _run(self, message: str, mode: str = "qa", **extra) -> tuple[list, str]:
        app = get_app()
        events = list(
            app.chat_events(
                {"user_id": "u_demo", "session_id": new_session(), "message": message, "mode": mode, **extra}
            )
        )
        answer = "".join(event.data.get("text", "") for event in events if event.type == "token")
        return events, answer

    def test_recommendation_uses_tools(self) -> None:
        events, answer = self._run("今天该吃什么")
        types = [event.type for event in events]
        self.assertIn("tool_call", types)
        self.assertIn("tool_result", types)
        self.assertIn("done", types)
        self.assertIn("蛋白", answer)
        done = next(event.data for event in events if event.type == "done")
        self.assertEqual(done["finish_reason"], "stop")

    def test_meal_history(self) -> None:
        events, answer = self._run("今天吃了什么")
        names = [event.data.get("name") for event in events if event.type == "tool_call"]
        self.assertIn("get_recent_meals", names)
        self.assertIn("饮食记录", answer)

    def test_guess_intent(self) -> None:
        events, answer = self._run("猜我今天吃了什么")
        names = [event.data.get("name") for event in events if event.type == "tool_call"]
        self.assertIn("get_recent_meals", names)
        self.assertIn("get_nutrition_gap", names)
        self.assertIn("我猜", answer)

    def test_work_mode_menu(self) -> None:
        events, answer = self._run("帮我看看实时菜单有什么", mode="work")
        names = [event.data.get("name") for event in events if event.type == "tool_call"]
        self.assertIn("get_menu", names)
        self.assertIn("实时菜单", answer)

    def test_rag_citation(self) -> None:
        events, _ = self._run("减脂期蛋白质每天要吃多少")
        citations = [event for event in events if event.type == "citation"]
        self.assertGreater(len(citations), 0)
        titles = [event.data.get("title") for event in citations]
        self.assertTrue(any("蛋白" in str(title) for title in titles))

    def test_mode_isolation(self) -> None:
        events, _ = self._run("帮我看看实时菜单有什么", mode="qa")
        names = [event.data.get("name") for event in events if event.type == "tool_call"]
        self.assertNotIn("get_menu", names)


class TestRAG(unittest.TestCase):
    def test_search_hits_protein_doc(self) -> None:
        app = get_app()
        results = app.knowledge.search("减脂期每天蛋白质吃多少", top_k=3)
        self.assertTrue(results)
        self.assertTrue(any("蛋白" in str(item.get("title")) + str(item.get("text")) for item in results))

    def test_ingest_text(self) -> None:
        app = get_app()
        doc_id = f"api_doc_{uuid.uuid4().hex[:6]}"
        result = app.knowledge.ingest_text(doc_id, "测试知识", "# 测试知识\n\n## 小节\n\n饮用温水有助于日常补水。", source="test")
        self.assertGreaterEqual(result["chunks"], 1)
        self.assertGreaterEqual(app.knowledge.stats()["documents"], 5)


class TestSkills(unittest.TestCase):
    def test_builtin_skills_loaded(self) -> None:
        app = get_app()
        names = {skill.name for skill in app.skills.all()}
        for expected in ("health-expert", "diet-expert", "nutritionist", "page-designer", "meal-guess", "meal-planning"):
            self.assertIn(expected, names)

    def test_match_guess_intent(self) -> None:
        app = get_app()
        matched = app.skills.match("猜我今天吃了什么", "qa")
        self.assertTrue(matched)
        self.assertEqual(matched[0].name, "meal-guess")

    def test_match_planning_intent(self) -> None:
        app = get_app()
        matched = app.skills.match("今天该吃什么", "qa")
        self.assertTrue(matched)
        self.assertEqual(matched[0].name, "meal-planning")

    def test_skill_tool_scope(self) -> None:
        app = get_app()
        skill = app.skills.get("nutritionist")
        allowed = app.skills.allowed_tool_names(skill, "qa")
        self.assertIn("get_nutrition_gap", allowed)
        self.assertNotIn("record_meal", allowed)


class TestTools(unittest.TestCase):
    def test_mode_isolation_and_confirmation(self) -> None:
        registry = ToolRegistry()
        registry.register_many(build_diet_tools())
        registry.register_many(build_system_tools())
        app = get_app()
        qa_names = {spec.name for spec in registry.specs_for_mode("qa")}
        work_names = {spec.name for spec in registry.specs_for_mode("work")}
        self.assertNotIn("get_menu", qa_names)
        self.assertNotIn("record_meal", qa_names)
        self.assertIn("get_menu", work_names)
        self.assertIn("record_meal", work_names)

        qa_ctx = ToolContext(user_id="u_demo", session_id="s_test", mode="qa", service=app.service, skills=app.skills, registry=registry)
        denied = registry.call("get_menu", {}, qa_ctx)
        self.assertFalse(denied.ok)
        self.assertEqual(denied.error, "permission_denied")

        read = registry.call("get_health_profile", {}, qa_ctx)
        self.assertTrue(read.ok)

        work_ctx = ToolContext(user_id="u_demo", session_id="s_test", mode="work", service=app.service, skills=app.skills, registry=registry)
        confirm = registry.call("record_meal", {"type": "午餐", "items": ["测试鸡胸肉沙拉"]}, work_ctx)
        self.assertTrue(confirm.need_confirm)
        work_ctx.confirmed = True
        written = registry.call("record_meal", {"type": "午餐", "items": ["测试鸡胸肉沙拉"]}, work_ctx)
        self.assertTrue(written.ok, written.error)
        registry.shutdown()


class TestMemory(unittest.TestCase):
    def test_extract_and_delete(self) -> None:
        store = MemoryStore()
        user_id = f"u_mem_{uuid.uuid4().hex[:6]}"
        added = store.extract(user_id, "我对花生过敏，我不吃香菜，我在减脂")
        self.assertGreaterEqual(len(added), 2)
        values = {item["value"] for item in store.list(user_id)}
        self.assertIn("花生", values)
        self.assertIn("香菜", values)
        first = store.list(user_id)[0]
        self.assertTrue(store.delete(int(first["id"]), user_id))


if __name__ == "__main__":
    unittest.main(verbosity=2)