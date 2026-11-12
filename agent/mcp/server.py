from __future__ import annotations

import argparse
import json
import sys
from typing import Any, Callable

from agent.config import ensure_dirs
from agent.data.db import init_db
from agent.data.service_client import ServiceClient
from agent.mcp.protocol import (
    INTERNAL_ERROR,
    INVALID_PARAMS,
    INVALID_REQUEST,
    MCP_PROTOCOL_VERSION,
    METHOD_NOT_FOUND,
    PARSE_ERROR,
    TOOL_EXECUTION_ERROR,
    TOOL_NOT_FOUND,
    decode_message,
    encode_message,
    error_message,
    response_message,
    tool_result,
)
from agent.rag.retriever import KnowledgeBase

SERVICE = ServiceClient()


def _schema(properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    return {"type": "object", "properties": properties, "required": required or []}


def _require_user(arguments: dict[str, Any]) -> str:
    user_id = str(arguments.get("user_id") or "")
    if not user_id:
        raise ValueError("缺少 user_id")
    return user_id


def _tool_profile(arguments: dict[str, Any]) -> tuple[str, Any]:
    user_id = _require_user(arguments)
    data = SERVICE.get_profile(user_id)
    text = (
        f"{data.get('name')}，{data.get('height')}cm/{data.get('weight')}kg，目标{data.get('goal')}，"
        f"活动量{data.get('activity')}，忌口：{'、'.join(data.get('allergies') or []) or '无'}"
    )
    return text, data


def _tool_meals(arguments: dict[str, Any]) -> tuple[str, Any]:
    user_id = _require_user(arguments)
    days = int(arguments.get("days") or 7)
    data = SERVICE.get_recent_meals(user_id, days)
    meals = data.get("meals") or []
    text = f"近 {days} 天共 {len(meals)} 条记录"
    if meals:
        text += f"，最近：{meals[-1].get('date')} {meals[-1].get('type')} {'、'.join(meals[-1].get('items') or [])}"
    return text, data


def _tool_gap(arguments: dict[str, Any]) -> tuple[str, Any]:
    user_id = _require_user(arguments)
    data = SERVICE.get_nutrition_gap(user_id, arguments.get("date"))
    gap = data.get("gap") or {}
    text = f"蛋白差 {gap.get('protein', 0)}g，纤维差 {gap.get('fiber', 0)}g，蔬菜差 {gap.get('veggies', 0)}g，饮水差 {gap.get('water', 0)}ml"
    return text, data


def _tool_weekly(arguments: dict[str, Any]) -> tuple[str, Any]:
    user_id = _require_user(arguments)
    data = SERVICE.get_weekly_stats(user_id)
    text = f"本周记录 {data.get('days_recorded')} 天，平均健康分 {data.get('avg_health_score')}，积分 {data.get('points')}"
    return text, data


def _tool_search_foods(arguments: dict[str, Any]) -> tuple[str, Any]:
    user_id = _require_user(arguments)
    data = SERVICE.search_foods(
        user_id,
        query=str(arguments.get("query") or ""),
        tags=list(arguments.get("tags") or []),
        meal_type=str(arguments.get("meal_type") or ""),
        limit=int(arguments.get("limit") or 5),
    )
    foods = data.get("foods") or []
    return f"找到 {len(foods)} 道菜", data


def _tool_ranking(arguments: dict[str, Any]) -> tuple[str, Any]:
    user_id = _require_user(arguments)
    data = SERVICE.get_friend_ranking(user_id, str(arguments.get("scope") or "week"))
    ranking = data.get("ranking") or []
    return f"共 {len(ranking)} 人上榜", data


def _tool_knowledge(arguments: dict[str, Any]) -> tuple[str, Any]:
    query = str(arguments.get("query") or "")
    if not query:
        raise ValueError("缺少 query")
    results = KnowledgeBase().search(query, top_k=int(arguments.get("top_k") or 4))
    text = f"命中 {len(results)} 条知识"
    if results:
        text += f"，最高相关：{results[0].get('title')}"
    return text, {"query": query, "results": results}


DIET_TOOLS: dict[str, tuple[dict[str, Any], Callable[[dict[str, Any]], tuple[str, Any]]]] = {
    "get_health_profile": (
        {
            "name": "get_health_profile",
            "description": "查询用户健康档案",
            "inputSchema": _schema({"user_id": {"type": "string"}}, ["user_id"]),
        },
        _tool_profile,
    ),
    "get_recent_meals": (
        {
            "name": "get_recent_meals",
            "description": "查询用户近期饮食记录",
            "inputSchema": _schema({"user_id": {"type": "string"}, "days": {"type": "integer"}}, ["user_id"]),
        },
        _tool_meals,
    ),
    "get_nutrition_gap": (
        {
            "name": "get_nutrition_gap",
            "description": "查询用户今日营养缺口",
            "inputSchema": _schema({"user_id": {"type": "string"}, "date": {"type": "string"}}, ["user_id"]),
        },
        _tool_gap,
    ),
    "get_weekly_stats": (
        {
            "name": "get_weekly_stats",
            "description": "查询用户本周健康统计",
            "inputSchema": _schema({"user_id": {"type": "string"}}, ["user_id"]),
        },
        _tool_weekly,
    ),
    "search_foods": (
        {
            "name": "search_foods",
            "description": "检索符合用户忌口与餐次的菜品",
            "inputSchema": _schema(
                {
                    "user_id": {"type": "string"},
                    "query": {"type": "string"},
                    "tags": {"type": "array", "items": {"type": "string"}},
                    "meal_type": {"type": "string"},
                    "limit": {"type": "integer"},
                },
                ["user_id"],
            ),
        },
        _tool_search_foods,
    ),
    "get_friend_ranking": (
        {
            "name": "get_friend_ranking",
            "description": "查询好友健康均分排行榜",
            "inputSchema": _schema({"user_id": {"type": "string"}, "scope": {"type": "string"}}, ["user_id"]),
        },
        _tool_ranking,
    ),
}

KNOWLEDGE_TOOLS: dict[str, tuple[dict[str, Any], Callable[[dict[str, Any]], tuple[str, Any]]]] = {
    "search_knowledge": (
        {
            "name": "search_knowledge",
            "description": "检索健康饮食知识库",
            "inputSchema": _schema(
                {"query": {"type": "string"}, "top_k": {"type": "integer"}},
                ["query"],
            ),
        },
        _tool_knowledge,
    ),
}


def _select_tools(profile: str) -> dict[str, tuple[dict[str, Any], Callable[[dict[str, Any]], tuple[str, Any]]]]:
    if profile == "knowledge":
        return KNOWLEDGE_TOOLS
    return DIET_TOOLS


def _log(message: str) -> None:
    print(f"[mcp-server] {message}", file=sys.stderr, flush=True)


class MCPServer:
    def __init__(self, profile: str = "diet") -> None:
        self.profile = profile
        self.tools = _select_tools(profile)

    def handle(self, message: dict[str, Any]) -> dict[str, Any] | None:
        request_id = message.get("id")
        method = message.get("method")
        params = message.get("params") or {}

        if request_id is None:
            return None

        if method == "initialize":
            return response_message(
                request_id,
                {
                    "protocolVersion": MCP_PROTOCOL_VERSION,
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": f"eat-{self.profile}-mcp", "version": "1.0.0"},
                },
            )

        if method == "ping":
            return response_message(request_id, {})

        if method == "tools/list":
            return response_message(request_id, {"tools": [tool for tool, _ in self.tools.values()]})

        if method == "tools/call":
            name = str(params.get("name") or "")
            arguments = params.get("arguments") or {}
            if name not in self.tools:
                return error_message(request_id, TOOL_NOT_FOUND, f"工具 {name} 不存在")
            if not isinstance(arguments, dict):
                return error_message(request_id, INVALID_PARAMS, "arguments 必须是对象")
            try:
                text, structured = self.tools[name][1](arguments)
                return response_message(request_id, tool_result(text, structured))
            except ValueError as exc:
                return response_message(request_id, tool_result(str(exc), is_error=True))
            except Exception as exc:
                _log(f"工具 {name} 执行失败：{exc}")
                return error_message(request_id, TOOL_EXECUTION_ERROR, str(exc))

        return error_message(request_id, METHOD_NOT_FOUND, f"未知方法 {method}")

    def serve(self) -> None:
        _log(f"profile={self.profile} 已启动，工具 {len(self.tools)} 个")
        for line in sys.stdin:
            if not line.strip():
                continue
            try:
                message = decode_message(line)
            except PARSE_ERROR:
                sys.stdout.write(encode_message(error_message(None, PARSE_ERROR, "消息解析失败")))
                sys.stdout.flush()
                continue
            except Exception as exc:
                sys.stdout.write(encode_message(error_message(None, INVALID_REQUEST, str(exc))))
                sys.stdout.flush()
                continue
            try:
                response = self.handle(message)
            except Exception as exc:
                response = error_message(message.get("id"), INTERNAL_ERROR, str(exc))
            if response is not None:
                sys.stdout.write(encode_message(response))
                sys.stdout.flush()


def main() -> None:
    parser = argparse.ArgumentParser(description="今天你吃了吗 MCP Server")
    parser.add_argument("--profile", default="diet", choices=["diet", "knowledge"])
    args = parser.parse_args()
    ensure_dirs()
    try:
        init_db()
    except Exception as exc:
        _log(f"数据库初始化失败，继续使用内存数据：{exc}")
    MCPServer(profile=args.profile).serve()


if __name__ == "__main__":
    main()