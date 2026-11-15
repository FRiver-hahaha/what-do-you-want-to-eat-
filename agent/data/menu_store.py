from __future__ import annotations

import json
from datetime import datetime, timezone
from typing import Any

from agent.data.db import execute, query_one

MAX_MENU_ITEMS = 80
MAX_PRICE = 9999.0
ITEM_FIELDS = {
    "id",
    "name",
    "category",
    "description",
    "calories",
    "kcal",
    "protein",
    "fiber",
    "veggies",
    "water",
    "nature",
    "keywords",
    "image",
    "meal_types",
    "available",
    "price",
    "amount",
    "cost",
}
NUMERIC_FIELDS = {"calories", "kcal", "protein", "fiber", "veggies", "water", "price", "amount", "cost"}


def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def _number(value: Any) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return 0.0
    if number < 0 or number > MAX_PRICE:
        return 0.0
    return number


def clean_item(raw: Any) -> dict[str, Any] | None:
    if not isinstance(raw, dict):
        return None
    cleaned: dict[str, Any] = {}
    for key, value in raw.items():
        if key not in ITEM_FIELDS or value is None:
            continue
        if key in NUMERIC_FIELDS:
            cleaned[key] = _number(value)
        elif key == "meal_types":
            cleaned[key] = [str(item)[:20] for item in value if str(item).strip()][:6] if isinstance(value, list) else []
        elif key == "available":
            cleaned[key] = bool(value)
        elif isinstance(value, (str, int, float)):
            cleaned[key] = str(value)[:300] if isinstance(value, str) else value
    if not str(cleaned.get("name") or "").strip():
        return None
    cleaned.setdefault("available", True)
    if "kcal" in cleaned and "calories" not in cleaned:
        cleaned["calories"] = cleaned["kcal"]
    return cleaned


def clean_items(raw: Any) -> list[dict[str, Any]]:
    if not isinstance(raw, list):
        return []
    result: list[dict[str, Any]] = []
    for item in raw[:MAX_MENU_ITEMS]:
        cleaned = clean_item(item)
        if cleaned is not None:
            result.append(cleaned)
    return result


def price_range(items: list[dict[str, Any]]) -> tuple[float, float]:
    values: list[float] = []
    for item in items:
        for key in ("price", "amount", "cost"):
            value = item.get(key)
            if isinstance(value, (int, float)) and float(value) > 0:
                values.append(float(value))
                break
    if not values:
        return 0.0, 0.0
    return round(min(values), 2), round(max(values), 2)


def save_import(user_id: str, items: list[dict[str, Any]], source: str = "import") -> dict[str, Any]:
    low, high = price_range(items)
    created_at = _now()
    execute(
        "INSERT INTO menu_imports (user_id, source, price_min, price_max, items, created_at) VALUES (?, ?, ?, ?, ?, ?)",
        (user_id, source[:40] or "import", low, high, json.dumps(items, ensure_ascii=False), created_at),
    )
    return {
        "user_id": user_id,
        "source": source[:40] or "import",
        "count": len(items),
        "price_min": low,
        "price_max": high,
        "created_at": created_at,
    }


def latest(user_id: str) -> dict[str, Any] | None:
    row = query_one(
        "SELECT id, user_id, source, price_min, price_max, items, created_at FROM menu_imports WHERE user_id = ? ORDER BY id DESC LIMIT 1",
        (user_id,),
    )
    if row is None:
        return None
    try:
        items = json.loads(row.get("items") or "[]")
    except json.JSONDecodeError:
        items = []
    return {
        "id": row.get("id"),
        "user_id": row.get("user_id"),
        "source": row.get("source"),
        "price_min": row.get("price_min"),
        "price_max": row.get("price_max"),
        "items": items if isinstance(items, list) else [],
        "created_at": row.get("created_at"),
    }


__all__ = ["clean_item", "clean_items", "latest", "price_range", "save_import"]