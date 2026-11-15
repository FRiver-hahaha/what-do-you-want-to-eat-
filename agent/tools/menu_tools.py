from __future__ import annotations

from typing import Any

from agent import menu as menu_lib
from agent.data import menu_store
from agent.tools.base import ToolContext, ToolResult, ToolSpec
from agent.tools.diet_tools import match_score, merged_gap, merged_profile


def _schema(properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    return {"type": "object", "properties": properties, "required": required or []}


def current_menu(ctx: ToolContext, meal_type: str = "") -> tuple[list[dict[str, Any]], str, dict[str, Any]]:
    imported = menu_store.latest(ctx.user_id)
    if imported and imported.get("items"):
        foods = []
        for item in imported["items"]:
            if not isinstance(item, dict) or not item.get("available", True):
                continue
            types = item.get("meal_types") or []
            if meal_type and meal_type not in types:
                continue
            foods.append(dict(item))
        return foods, "import", {
            "price_min": imported.get("price_min"),
            "price_max": imported.get("price_max"),
            "created_at": imported.get("created_at"),
        }
    data = ctx.service.get_menu(ctx.user_id, meal_type) if ctx.service is not None else {"foods": [], "source": "none"}
    return list(data.get("foods") or []), str(data.get("source") or "mock"), {}


def decorate(ctx: ToolContext, foods: list[dict[str, Any]]) -> dict[str, Any]:
    profile = merged_profile(ctx)
    gap = merged_gap(ctx)
    for food in foods:
        food["match"] = match_score(food, profile, gap)
    return profile


def _get_menu(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    meal_type = str(args.get("meal_type") or "")
    foods, source, imported = current_menu(ctx, meal_type)
    profile = decorate(ctx, foods)
    payload = menu_lib.menu_payload(foods, profile, source)
    payload["meal_type"] = meal_type or "全部"
    payload["menu_price"] = {
        "min": imported.get("price_min") or 0,
        "max": imported.get("price_max") or 0,
    }
    summary = (
        f"日常菜 {payload['counts']['daily']} 道，其他菜品 {payload['counts']['other']} 道"
        f"（消费水平 {payload['spend_level']['min']}-{payload['spend_level']['max']} 元/餐）"
    )
    return ToolResult(ok=True, summary=summary, data=payload)


def _import_menu(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    items = menu_store.clean_items(args.get("items"))
    if not items:
        return ToolResult(ok=False, summary="菜单里没有可用菜品", error="empty_menu")
    source = str(args.get("source") or "import")
    record = menu_store.save_import(ctx.user_id, items, source)
    groups = menu_lib.group_menu(items, merged_profile(ctx))
    menu_price = f"{record['price_min']:.0f}-{record['price_max']:.0f} 元" if record["price_min"] else "未提供金额"
    return ToolResult(
        ok=True,
        summary=f"已导入 {record['count']} 道菜，菜单金额 {menu_price}，其中日常菜 {groups['counts']['daily']} 道",
        data={
            "count": record["count"],
            "source": record["source"],
            "menu_price": {"min": record["price_min"], "max": record["price_max"]},
            "counts": groups["counts"],
            "spend_level": groups["spend_level"],
            "created_at": record["created_at"],
        },
    )


def _pick_random(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    meal_type = str(args.get("meal_type") or "")
    foods, source, _ = current_menu(ctx, meal_type)
    if not foods:
        return ToolResult(ok=False, summary="当前没有可随机抽取的菜品", error="empty_menu")
    profile = decorate(ctx, foods)
    groups = menu_lib.group_menu(foods, profile)
    picked = menu_lib.pick_random(groups, args.get("seed"))
    food = picked.get("food")
    if not food:
        return ToolResult(ok=False, summary="当前没有可随机抽取的菜品", error="empty_menu")
    label = "日常菜" if picked["group"] == "daily" else "其他菜品"
    price = food.get("price")
    return ToolResult(
        ok=True,
        summary=f"随机抽到{label}：{food.get('name')}（{price if price is not None else '-'} 元）",
        data={
            "food": food,
            "group": picked["group"],
            "ratio": picked["ratio"],
            "counts": picked["counts"],
            "spend_level": groups["spend_level"],
            "source": source,
        },
    )


def _update_spend_level(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    values: dict[str, int] = {}
    for key in ("spend_min", "spend_max"):
        value = args.get(key)
        if value in (None, ""):
            continue
        try:
            number = int(round(float(value)))
        except (TypeError, ValueError):
            return ToolResult(ok=False, summary=f"{key} 必须是整数金额", error="invalid_number")
        if number < 0 or number > 999:
            return ToolResult(ok=False, summary=f"{key} 需在 0-999 元之间", error="out_of_range")
        values[key] = number
    if not values:
        return ToolResult(ok=False, summary="没有提供消费水平区间", error="missing_fields")
    current = merged_profile(ctx)
    low = values.get("spend_min", menu_lib.spend_band(current)[0])
    high = values.get("spend_max", menu_lib.spend_band(current)[1])
    if low > high:
        low, high = high, low
    fields = {"spend_min": low, "spend_max": high}
    data = ctx.service.update_profile(ctx.user_id, fields)
    if not data.get("ok"):
        return ToolResult(ok=False, summary="消费水平更新失败", error=data.get("error", "update_failed"))
    return ToolResult(
        ok=True,
        summary=f"消费水平已更新为每餐 {low}-{high} 元",
        data={
            "profile": data.get("profile") or {},
            "spend_level": {"min": low, "max": high, "unit": "元/餐"},
        },
    )


def build_menu_tools() -> list[ToolSpec]:
    return [
        ToolSpec(
            name="get_menu",
            description="读取当前时段实时菜单，按用户消费水平分为日常菜与其他菜品两组，并返回每道菜的价格、分组与匹配度，仅工作模式可用",
            parameters=_schema({"meal_type": {"type": "string", "enum": ["早餐", "午餐", "晚餐", "加餐", ""]}}),
            handler=_get_menu,
            category="local",
            modes=("work",),
            tags=("menu",),
        ),
        ToolSpec(
            name="import_menu",
            description="导入当前菜单及其每道菜金额，覆盖之前导入的菜单，需用户确认，仅工作模式可用",
            parameters=_schema(
                {
                    "items": {
                        "type": "array",
                        "items": {"type": "object"},
                        "description": "菜品数组，至少包含 name，金额字段 price/amount/cost 任选其一",
                    },
                    "source": {"type": "string", "description": "菜单来源，默认 import"},
                },
                required=["items"],
            ),
            handler=_import_menu,
            category="local",
            modes=("work",),
            require_confirm=True,
            tags=("menu", "import"),
        ),
        ToolSpec(
            name="pick_random_food",
            description="按 70% 日常菜、30% 其他菜品抽一道菜，仅工作模式可用",
            parameters=_schema(
                {
                    "meal_type": {"type": "string", "enum": ["早餐", "午餐", "晚餐", "加餐", ""]},
                    "seed": {"type": "integer", "description": "随机种子，便于复现"},
                }
            ),
            handler=_pick_random,
            category="local",
            modes=("work",),
            tags=("menu", "random"),
        ),
        ToolSpec(
            name="update_spend_level",
            description="修改用户每餐消费水平区间，单位为元，需用户确认，仅工作模式可用",
            parameters=_schema(
                {
                    "spend_min": {"type": "integer", "description": "每餐消费水平下限，单位元"},
                    "spend_max": {"type": "integer", "description": "每餐消费水平上限，单位元"},
                }
            ),
            handler=_update_spend_level,
            category="write",
            modes=("work",),
            require_confirm=True,
            tags=("profile", "spend"),
        ),
    ]