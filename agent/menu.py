from __future__ import annotations

import random
from typing import Any, Iterable

DEFAULT_SPEND_MIN = 20
DEFAULT_SPEND_MAX = 35
DAILY_RATIO = 0.6
BAND_TOLERANCE_MIN = 2.0


def _to_int(value: Any, default: int) -> int:
    try:
        return int(round(float(value)))
    except (TypeError, ValueError):
        return default


def spend_band(profile: dict[str, Any] | None) -> tuple[int, int]:
    data = profile or {}
    low = _to_int(data.get("spend_min", data.get("spendMin")), DEFAULT_SPEND_MIN)
    high = _to_int(data.get("spend_max", data.get("spendMax")), DEFAULT_SPEND_MAX)
    low = max(0, min(low, 999))
    high = max(0, min(high, 999))
    if low > high:
        low, high = high, low
    if not high:
        high = max(low, DEFAULT_SPEND_MAX)
    if not low and high:
        low = min(high, DEFAULT_SPEND_MIN)
    return low, high


def food_price(food: dict[str, Any]) -> float | None:
    for key in ("price", "amount", "cost"):
        value = food.get(key)
        if isinstance(value, bool):
            continue
        if isinstance(value, (int, float)):
            number = float(value)
        elif isinstance(value, str) and value.strip():
            try:
                number = float(value.strip())
            except ValueError:
                continue
        else:
            continue
        if number > 0:
            return round(number, 2)
    return None


def classify(food: dict[str, Any], band: tuple[int, int]) -> dict[str, Any]:
    item = dict(food)
    low, high = band
    tolerance = max(BAND_TOLERANCE_MIN, (high - low) * 0.25)
    price = food_price(food)
    item["price"] = price
    if price is None:
        item["group"] = "other"
        item["price_gap"] = None
        item["price_reason"] = "missing_price"
        return item
    if low - tolerance <= price <= high + tolerance:
        item["group"] = "daily"
        gap = 0.0 if low <= price <= high else round(min(abs(price - low), abs(price - high)), 2)
        item["price_gap"] = gap
        item["price_reason"] = "in_band" if gap == 0 else "near_band"
    else:
        item["group"] = "other"
        item["price_gap"] = round(min(abs(price - low), abs(price - high)), 2)
        item["price_reason"] = "far_from_band"
    return item


def _daily_order(item: dict[str, Any]) -> tuple[float, float]:
    gap = item.get("price_gap")
    return (0.0 if gap is None else float(gap), -float(item.get("match") or 0))


def _other_order(item: dict[str, Any]) -> float:
    gap = item.get("price_gap")
    return -(0.0 if gap is None else float(gap))


def group_menu(foods: Iterable[dict[str, Any]], profile: dict[str, Any] | None) -> dict[str, Any]:
    band = spend_band(profile)
    items = [classify(food, band) for food in foods]
    daily = sorted([item for item in items if item["group"] == "daily"], key=_daily_order)
    other = sorted([item for item in items if item["group"] == "other"], key=_other_order)
    return {
        "all": items,
        "daily": daily,
        "other": other,
        "counts": {"all": len(items), "daily": len(daily), "other": len(other)},
        "spend_level": {"min": band[0], "max": band[1], "unit": "元/餐"},
        "ratio": DAILY_RATIO,
    }


def pick_random(groups: dict[str, Any], seed: Any = None) -> dict[str, Any]:
    daily = list(groups.get("daily") or [])
    other = list(groups.get("other") or [])
    rng = random.Random(seed) if seed is not None else random
    if not daily and not other:
        return {"food": None, "group": "", "reason": "empty_menu", "ratio": DAILY_RATIO, "counts": {"daily": 0, "other": 0}}
    if not other:
        chosen, group = daily, "daily"
    elif not daily:
        chosen, group = other, "other"
    elif rng.random() < DAILY_RATIO:
        chosen, group = daily, "daily"
    else:
        chosen, group = other, "other"
    return {
        "food": rng.choice(chosen),
        "group": group,
        "ratio": DAILY_RATIO,
        "counts": {"daily": len(daily), "other": len(other)},
    }


def menu_payload(foods: Iterable[dict[str, Any]], profile: dict[str, Any] | None, source: str = "mock") -> dict[str, Any]:
    groups = group_menu(foods, profile)
    return {
        "foods": groups["all"],
        "daily": groups["daily"],
        "other": groups["other"],
        "counts": groups["counts"],
        "spend_level": groups["spend_level"],
        "ratio": DAILY_RATIO,
        "source": source,
    }


__all__ = [
    "DAILY_RATIO",
    "DEFAULT_SPEND_MAX",
    "DEFAULT_SPEND_MIN",
    "classify",
    "food_price",
    "group_menu",
    "menu_payload",
    "pick_random",
    "spend_band",
]