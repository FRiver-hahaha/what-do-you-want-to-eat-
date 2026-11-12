from __future__ import annotations

import hashlib
from typing import Any

from agent.tools.base import ToolContext, ToolResult, ToolSpec


def _schema(properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    return {"type": "object", "properties": properties, "required": required or []}


def _profile(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    data = ctx.service.get_profile(ctx.user_id)
    summary = (
        f"{data.get('name')}：{data.get('height')}cm/{data.get('weight')}kg，"
        f"目标 {data.get('goal')}，活动量 {data.get('activity')}，"
        f"忌口 {', '.join(data.get('allergies') or []) or '无'}"
    )
    return ToolResult(ok=True, summary=summary, data=data)


def _recent_meals(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    days = int(args.get("days") or 7)
    data = ctx.service.get_recent_meals(ctx.user_id, days)
    meals = data.get("meals") or []
    summary = f"近 {days} 天查到 {len(meals)} 条饮食记录"
    if meals:
        latest = meals[-1]
        summary += f"，最近一条：{latest.get('date')} {latest.get('type')} {'、'.join(latest.get('items') or [])}"
    return ToolResult(ok=True, summary=summary, data={"days": days, "count": len(meals), "meals": meals, "source": data.get("source")})


def _nutrition_gap(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    data = ctx.service.get_nutrition_gap(ctx.user_id, args.get("date"))
    gap = data.get("gap") or {}
    summary = (
        f"今日缺口：蛋白 {gap.get('protein', 0)}g，纤维 {gap.get('fiber', 0)}g，"
        f"蔬菜 {gap.get('veggies', 0)}g，饮水 {gap.get('water', 0)}ml"
    )
    return ToolResult(ok=True, summary=summary, data=data)


def _search_foods(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    data = ctx.service.search_foods(
        ctx.user_id,
        query=str(args.get("query") or ""),
        tags=list(args.get("tags") or []),
        meal_type=str(args.get("meal_type") or ""),
        limit=int(args.get("limit") or 5),
    )
    foods = data.get("foods") or []
    summary = f"找到 {len(foods)} 道符合忌口与餐次的菜品"
    return ToolResult(ok=True, summary=summary, data={"count": len(foods), "foods": foods, "source": data.get("source")})


def _menu(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    data = ctx.service.get_menu(ctx.user_id, str(args.get("meal_type") or ""))
    foods = data.get("foods") or []
    summary = f"实时菜单当前有 {len(foods)} 道可售菜品"
    return ToolResult(ok=True, summary=summary, data={"meal_type": data.get("meal_type"), "count": len(foods), "foods": foods})


def _weekly_stats(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    data = ctx.service.get_weekly_stats(ctx.user_id)
    summary = (
        f"本周记录 {data.get('days_recorded')} 天，平均健康分 {data.get('avg_health_score')}，"
        f"积分 {data.get('points')}，连续 {data.get('streak')} 天"
    )
    return ToolResult(ok=True, summary=summary, data=data)


def _friend_ranking(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    scope = str(args.get("scope") or "week")
    data = ctx.service.get_friend_ranking(ctx.user_id, scope)
    ranking = data.get("ranking") or []
    summary = f"共 {len(ranking)} 人上榜，第一名 {ranking[0].get('name') if ranking else '暂无'}"
    return ToolResult(ok=True, summary=summary, data={"scope": scope, "ranking": ranking, "source": data.get("source")})


def _record_meal(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    items = args.get("items") or []
    if not items:
        return ToolResult(ok=False, summary="缺少要记录的食物", error="missing_items")
    meal_type = str(args.get("type") or "加餐")
    date = str(args.get("date") or "")
    raw_key = f"{ctx.session_id}:{date}:{meal_type}:{'|'.join(str(item) for item in items)}"
    idempotency_key = hashlib.sha1(raw_key.encode("utf-8")).hexdigest()[:16]
    payload = {
        "date": date,
        "type": meal_type,
        "items": items,
        "protein": args.get("protein") or 0,
        "fiber": args.get("fiber") or 0,
        "veggies": args.get("veggies") or 0,
        "water": args.get("water") or 0,
    }
    data = ctx.service.record_meal(ctx.user_id, payload, idempotency_key)
    return ToolResult(
        ok=True,
        summary=f"已记录{meal_type}：{'、'.join(str(item) for item in items)}",
        data=data,
    )


def build_diet_tools() -> list[ToolSpec]:
    return [
        ToolSpec(
            name="get_health_profile",
            description="查询当前用户的健康档案：身高体重、目标、活动量、睡眠、过敏与忌口",
            parameters=_schema({}),
            handler=_profile,
            category="read",
            tags=("profile",),
        ),
        ToolSpec(
            name="get_recent_meals",
            description="查询当前用户近若干天的饮食记录与营养估算",
            parameters=_schema({"days": {"type": "integer", "minimum": 1, "maximum": 30, "description": "天数，默认 7"}}),
            handler=_recent_meals,
            category="read",
            tags=("meals",),
        ),
        ToolSpec(
            name="get_nutrition_gap",
            description="查询当前用户今天的营养摄入与目标缺口：蛋白、纤维、蔬菜、饮水",
            parameters=_schema({"date": {"type": "string", "description": "日期 YYYY-MM-DD，默认今天"}}),
            handler=_nutrition_gap,
            category="read",
            tags=("nutrition",),
        ),
        ToolSpec(
            name="search_foods",
            description="按关键词、标签、餐次检索菜品，自动过滤用户过敏与不吃的食物",
            parameters=_schema(
                {
                    "query": {"type": "string", "description": "食物名称或标签关键词"},
                    "tags": {"type": "array", "items": {"type": "string"}, "description": "营养标签，如 高蛋白、减脂"},
                    "meal_type": {"type": "string", "enum": ["早餐", "午餐", "晚餐", "加餐", ""]},
                    "limit": {"type": "integer", "minimum": 1, "maximum": 10},
                }
            ),
            handler=_search_foods,
            category="read",
            tags=("food",),
        ),
        ToolSpec(
            name="get_menu",
            description="读取当前时段实时可售菜单，仅工作模式可用",
            parameters=_schema({"meal_type": {"type": "string", "enum": ["早餐", "午餐", "晚餐", "加餐", ""]}}),
            handler=_menu,
            category="local",
            modes=("work",),
            tags=("menu",),
        ),
        ToolSpec(
            name="get_weekly_stats",
            description="查询本周健康分、记录天数、三餐齐全天数、积分与连续记录天数",
            parameters=_schema({}),
            handler=_weekly_stats,
            category="read",
            tags=("stats",),
        ),
        ToolSpec(
            name="get_friend_ranking",
            description="查询好友健康均分排行榜",
            parameters=_schema({"scope": {"type": "string", "enum": ["week", "today"]}}),
            handler=_friend_ranking,
            category="read",
            tags=("ranking",),
        ),
        ToolSpec(
            name="record_meal",
            description="把一餐写入当前用户的饮食记录，必须经用户确认后执行",
            parameters=_schema(
                {
                    "type": {"type": "string", "enum": ["早餐", "午餐", "晚餐", "加餐"]},
                    "items": {"type": "array", "items": {"type": "string"}, "description": "食物名称列表"},
                    "date": {"type": "string", "description": "日期 YYYY-MM-DD，默认今天"},
                    "protein": {"type": "number"},
                    "fiber": {"type": "number"},
                    "veggies": {"type": "number"},
                    "water": {"type": "number"},
                },
                required=["type", "items"],
            ),
            handler=_record_meal,
            category="write",
            modes=("work",),
            require_confirm=True,
            tags=("meal", "write"),
        ),
    ]