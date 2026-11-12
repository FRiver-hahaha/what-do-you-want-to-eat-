from __future__ import annotations

import json
from typing import Any

PROFILE_FIELDS = {"name", "height", "weight", "age", "goal", "activity", "note", "waterTarget", "sleep"}
NUTRITION_FIELDS = {"protein", "fiber", "veggies", "water"}
MEAL_FIELDS = {"type", "time", "text", "protein", "fiber", "veggies", "water"}
MAX_CONTEXT_CHARS = 8000


def sanitize_client_context(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        return {}
    try:
        if len(json.dumps(value, ensure_ascii=False)) > MAX_CONTEXT_CHARS:
            return {}
    except (TypeError, ValueError):
        return {}

    result: dict[str, Any] = {}
    profile = value.get("profile")
    if isinstance(profile, dict):
        cleaned = {}
        for key in PROFILE_FIELDS:
            item = profile.get(key)
            if isinstance(item, (str, int, float)) and not isinstance(item, bool):
                cleaned[key] = str(item)[:60] if isinstance(item, str) else item
        if cleaned:
            result["profile"] = cleaned

    meals = value.get("meals")
    if isinstance(meals, list):
        cleaned_meals = []
        for meal in meals[:12]:
            if not isinstance(meal, dict):
                continue
            cleaned = {}
            for key in MEAL_FIELDS:
                item = meal.get(key)
                if isinstance(item, (str, int, float)) and not isinstance(item, bool):
                    cleaned[key] = str(item)[:200] if isinstance(item, str) else item
            if cleaned:
                cleaned_meals.append(cleaned)
        if cleaned_meals:
            result["meals"] = cleaned_meals

    today = value.get("today")
    if isinstance(today, dict):
        cleaned_today: dict[str, Any] = {}
        score = today.get("score")
        if isinstance(score, (int, float)) and not isinstance(score, bool):
            cleaned_today["score"] = score
        status = today.get("status")
        if isinstance(status, str):
            cleaned_today["status"] = status[:40]
        nutrition = today.get("nutrition")
        if isinstance(nutrition, dict):
            cleaned_nutrition = {}
            for key in NUTRITION_FIELDS:
                item = nutrition.get(key)
                if isinstance(item, (int, float)) and not isinstance(item, bool):
                    cleaned_nutrition[key] = item
            if cleaned_nutrition:
                cleaned_today["nutrition"] = cleaned_nutrition
        if cleaned_today:
            result["today"] = cleaned_today
    return result


def merge_profile(data: dict[str, Any], client_profile: Any) -> dict[str, Any]:
    merged = dict(data)
    if not isinstance(client_profile, dict):
        return merged
    for key, value in client_profile.items():
        if key not in PROFILE_FIELDS or value is None:
            continue
        if key in ("height", "weight", "age", "waterTarget", "sleep"):
            try:
                number = float(value)
            except (TypeError, ValueError):
                continue
            if key == "waterTarget":
                merged["water_target"] = int(number)
                merged["waterTarget"] = int(number)
            elif key == "sleep":
                merged["sleep"] = f"{number:g}小时"
                merged["sleep_hours"] = number
            else:
                merged[key] = number
        else:
            merged[key] = str(value)[:60]
    return merged


def build_client_block(client_context: dict[str, Any], allow_local: bool) -> str:
    if not client_context:
        return ""
    lines = ["<client_state>"]
    if allow_local:
        lines.append("来源：浏览器 localStorage，与当前页面显示一致；用于对齐演示数据，服务端数据可用时以服务端为准。")
    else:
        lines.append("来源：浏览器 localStorage，仅用于理解用户当前页面状态；真实数据以服务端工具结果为准。")

    profile = client_context.get("profile")
    if isinstance(profile, dict):
        fields = []
        for key, label in (
            ("name", "称呼"),
            ("height", "身高cm"),
            ("weight", "体重kg"),
            ("age", "年龄"),
            ("goal", "目标"),
            ("activity", "活动量"),
            ("waterTarget", "饮水目标ml"),
            ("sleep", "睡眠小时"),
            ("note", "备注"),
        ):
            if key in profile:
                fields.append(f"{label}={profile[key]}")
        if fields:
            lines.append("档案：" + "，".join(fields))

    meals = client_context.get("meals")
    if isinstance(meals, list) and meals:
        lines.append("今日餐次：")
        for meal in meals[:8]:
            text = str(meal.get("text") or "").strip()
            lines.append(
                f"- {meal.get('type', '加餐')} {meal.get('time', '')} {text} "
                f"(蛋白 {meal.get('protein', 0)}g，纤维 {meal.get('fiber', 0)}g，"
                f"蔬菜 {meal.get('veggies', 0)}g，饮水 {meal.get('water', 0)}ml)"
            )

    today = client_context.get("today")
    if isinstance(today, dict):
        nutrition = today.get("nutrition") or {}
        if nutrition:
            lines.append(
                "今日营养："
                f"蛋白 {nutrition.get('protein', 0)}g，纤维 {nutrition.get('fiber', 0)}g，"
                f"蔬菜 {nutrition.get('veggies', 0)}g，饮水 {nutrition.get('water', 0)}ml"
            )
        if "score" in today:
            lines.append(f"今日评分：{today.get('score')}（{today.get('status', '')}）")

    lines.append("</client_state>")
    block = "\n".join(lines)
    return block[:1800]


def contract() -> dict[str, Any]:
    return {
        "version": "1.0.0",
        "frontend_baseline": "origin/main 32ec689",
        "aligned": {
            "food_fields": ["id", "name", "category", "description", "calories", "protein", "fiber", "veggies", "water", "nature", "keywords", "image", "match"],
            "profile_fields": sorted(PROFILE_FIELDS),
            "meal_fields": sorted(MEAL_FIELDS),
            "match_algorithm": "与 app.js getFoodMatch 同源：基础 54，按蔬菜/蛋白/纤维缺口加权，热量档位，放纵 -14，目标修正，20 到 98",
            "chat_entry": "#chatForm + #chatInput",
        },
        "dom": {
            "composer": "#chatForm",
            "chat_input": "#chatInput",
            "conversation": "#conversation",
            "user_message": ".message.user-message",
            "agent_message": ".message.agent-message",
            "agent_avatar": ".agent-avatar",
            "message_meta": ".message-meta",
            "message_bubble": ".message-bubble",
            "typing": "#typingMessage",
            "guess": {
                "name": "#guessName",
                "reason": "#guessReason",
                "match": "#guessMatch",
                "bar": "#guessBar",
                "accept": "#acceptGuess",
                "next": "#nextGuess"
            },
            "food": {
                "search_input": "#foodSearchInput",
                "search_results": "#foodSearchResults",
                "sidebar_list": "#sidebarFoodList",
                "masonry": "#expandedMenuList",
                "random_picker": "#randomPicker"
            },
            "profile": "#profileForm, [data-option-group], #waterRange, #sleepRange",
            "report": "#reportSummary, #scoreValue, #mealTimeline, #historyList",
            "ranking": "#leaderboardList, #championName",
        },
        "event_mapping": {
            "meta": "写入当前会话状态，不直接渲染",
            "skill": "可在消息元信息显示当前技能名",
            "citation": "追加到回答下方，作为可展开来源",
            "tool_call": "在 .message-meta 显示正在查询的状态",
            "tool_result": "更新工具状态；need_confirm 时打开确认框",
            "token": "向 .message-bubble 追加文本",
            "notice": "在 .message-meta 或 toast 显示降级提示",
            "confirm_required": "打开确认弹窗，确认后带 confirmed_tools 重发",
            "done": "结束流式状态，允许下一次发送",
            "error": "在 .message-bubble 显示错误并保底降级到本地规则",
        },
        "client_context_schema": {
            "profile": {"name": "string", "height": "number", "weight": "number", "age": "number", "goal": "string", "activity": "string", "waterTarget": "number", "sleep": "number", "note": "string"},
            "meals": [{"type": "string", "time": "string", "text": "string", "protein": "number", "fiber": "number", "veggies": "number", "water": "number"}],
            "today": {"score": "number", "status": "string", "nutrition": {"protein": "number", "fiber": "number", "veggies": "number", "water": "number"}},
        },
        "gaps": [
            {"id": "mode_switch", "name": "问答/工作模式切换", "needed_ui": "在 #chatForm 附近增加模式切换", "agent_api": "POST /api/chat/stream 的 mode 字段"},
            {"id": "model_select", "name": "模型选择", "needed_ui": "在设置弹窗或输入区增加模型下拉", "agent_api": "GET /api/models + model_id 字段"},
            {"id": "sse_adapter", "name": "SSE 流式适配层", "needed_ui": "新增独立 JS 模块，不修改 app.js 主流程", "agent_api": "POST /api/chat/stream"},
            {"id": "confirm_dialog", "name": "写操作确认弹窗", "needed_ui": "新增通用确认 dialog", "agent_api": "confirm_required 事件 + confirmed_tools"},
            {"id": "skill_market", "name": "Skill 市场与安装", "needed_ui": "新增市场视图或弹窗", "agent_api": "GET /api/skills/market、POST /api/skills/market/install、share"},
            {"id": "citation_panel", "name": "知识引用展示", "needed_ui": "在回答下方增加来源展开区", "agent_api": "citation 事件"},
            {"id": "session_manager", "name": "会话列表与清空", "needed_ui": "新增会话入口", "agent_api": "GET /api/sessions/{id}/messages、DELETE /api/sessions/{id}"},
            {"id": "health_status", "name": "Agent 连接状态", "needed_ui": "在 .agent-state 显示 mock/service、MCP 状态", "agent_api": "GET /api/agent/meta、GET /api/mcp/status"}
        ],
    }