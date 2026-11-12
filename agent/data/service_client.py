from __future__ import annotations

import copy
import json
import urllib.error
import urllib.parse
import urllib.request
from datetime import date, datetime, timedelta, timezone
from typing import Any

from agent.config import SERVER_BASE_URL, SERVER_TOKEN, SERVICE_TIMEOUT

LOCAL_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def _today() -> str:
    return date.today().isoformat()


MOCK_PROFILE = {
    "user_id": "u_demo",
    "name": "阿岑",
    "height": 168,
    "weight": 62,
    "age": 26,
    "gender": "女",
    "goal": "减脂",
    "activity": "中等",
    "water_target": 1800,
    "sleep": "7小时",
    "allergies": ["花生"],
    "dislikes": ["香菜"],
    "diet_style": "普通饮食",
    "bmi": 21.97,
    "note": "久坐办公，每周三次力量训练",
}

MOCK_RECENT_MEALS = [
    {"date": "2026-11-11", "type": "早餐", "items": ["燕麦", "鸡蛋", "牛奶"], "protein": 22, "fiber": 5, "veggies": 0, "water": 300},
    {"date": "2026-11-11", "type": "午餐", "items": ["鸡胸肉沙拉", "糙米饭"], "protein": 35, "fiber": 7, "veggies": 120, "water": 400},
    {"date": "2026-11-11", "type": "晚餐", "items": ["西兰花", "豆腐", "糙米饭"], "protein": 24, "fiber": 6, "veggies": 150, "water": 300},
    {"date": "2026-11-10", "type": "早餐", "items": ["全麦面包", "豆浆"], "protein": 15, "fiber": 4, "veggies": 0, "water": 250},
    {"date": "2026-11-10", "type": "午餐", "items": ["牛肉面"], "protein": 26, "fiber": 3, "veggies": 40, "water": 200},
    {"date": "2026-11-10", "type": "晚餐", "items": ["清蒸鱼", "青菜", "米饭"], "protein": 30, "fiber": 5, "veggies": 130, "water": 300},
    {"date": "2026-11-09", "type": "早餐", "items": ["包子", "豆浆"], "protein": 12, "fiber": 2, "veggies": 0, "water": 200},
    {"date": "2026-11-09", "type": "午餐", "items": ["番茄牛腩", "米饭"], "protein": 28, "fiber": 4, "veggies": 80, "water": 300},
    {"date": "2026-11-09", "type": "晚餐", "items": ["蔬菜汤", "鸡胸肉"], "protein": 30, "fiber": 6, "veggies": 160, "water": 250},
]

MOCK_FOODS = [
    {"id": "f001", "name": "香煎鸡胸肉藜麦碗", "kcal": 420, "protein": 38, "fiber": 9, "veggies": 180, "tags": ["高蛋白", "减脂", "低GI"], "meal_types": ["午餐", "晚餐"], "price": 28, "available": True},
    {"id": "f002", "name": "三文鱼牛油果沙拉", "kcal": 460, "protein": 32, "fiber": 11, "veggies": 220, "tags": ["高蛋白", "优质脂肪", "减脂"], "meal_types": ["午餐", "晚餐"], "price": 36, "available": True},
    {"id": "f003", "name": "菌菇豆腐煲配糙米饭", "kcal": 390, "protein": 22, "fiber": 10, "veggies": 200, "tags": ["素食", "高纤维", "温和"], "meal_types": ["午餐", "晚餐"], "price": 24, "available": True},
    {"id": "f004", "name": "燕麦鸡蛋早餐盘", "kcal": 330, "protein": 21, "fiber": 8, "veggies": 60, "tags": ["早餐", "高纤维", "饱腹"], "meal_types": ["早餐"], "price": 18, "available": True},
    {"id": "f005", "name": "希腊酸奶坚果杯", "kcal": 260, "protein": 18, "fiber": 5, "veggies": 0, "tags": ["加餐", "高蛋白", "含坚果"], "meal_types": ["加餐"], "price": 15, "available": True},
    {"id": "f006", "name": "番茄牛肉荞麦面", "kcal": 480, "protein": 30, "fiber": 7, "veggies": 120, "tags": ["暖胃", "高蛋白"], "meal_types": ["午餐", "晚餐"], "price": 26, "available": True},
    {"id": "f007", "name": "清蒸鲈鱼时蔬套餐", "kcal": 410, "protein": 34, "fiber": 8, "veggies": 240, "tags": ["高蛋白", "低油", "减脂"], "meal_types": ["午餐", "晚餐"], "price": 32, "available": True},
    {"id": "f008", "name": "南瓜小米粥配鸡蛋", "kcal": 290, "protein": 14, "fiber": 6, "veggies": 40, "tags": ["早餐", "养胃"], "meal_types": ["早餐"], "price": 16, "available": True},
    {"id": "f009", "name": "虾仁西兰花糙米碗", "kcal": 400, "protein": 33, "fiber": 9, "veggies": 210, "tags": ["高蛋白", "减脂", "高纤维"], "meal_types": ["午餐", "晚餐"], "price": 30, "available": True},
    {"id": "f010", "name": "低脂鸡肉卷饼", "kcal": 380, "protein": 27, "fiber": 6, "veggies": 100, "tags": ["便携", "减脂"], "meal_types": ["早餐", "午餐"], "price": 22, "available": False},
]

MOCK_FRIENDS = [
    {"id": "u_lin", "name": "林一", "relation": "friend"},
    {"id": "u_zhou", "name": "周舟", "relation": "friend"},
    {"id": "u_he", "name": "何苗", "relation": "friend"},
]

MOCK_RANKING = [
    {"rank": 1, "user_id": "u_lin", "name": "林一", "score": 88.6},
    {"rank": 2, "user_id": "u_demo", "name": "阿岑", "score": 82.4},
    {"rank": 3, "user_id": "u_zhou", "name": "周舟", "score": 79.1},
    {"rank": 4, "user_id": "u_he", "name": "何苗", "score": 76.8},
]


class ServiceClient:
    def __init__(self, base_url: str | None = None, token: str | None = None) -> None:
        self.base_url = (base_url if base_url is not None else SERVER_BASE_URL).rstrip("/")
        self.token = token if token is not None else SERVER_TOKEN
        self.last_error = ""
        self._mock_meals = copy.deepcopy(MOCK_RECENT_MEALS)

    @property
    def mode(self) -> str:
        return "service" if self.base_url else "mock"

    def _request(self, method: str, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any] | None:
        if not self.base_url:
            return None
        body = None
        if payload is not None:
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        request = urllib.request.Request(self.base_url + path, data=body, method=method)
        request.add_header("Content-Type", "application/json; charset=utf-8")
        if self.token:
            request.add_header("Authorization", f"Bearer {self.token}")
        try:
            with LOCAL_OPENER.open(request, timeout=SERVICE_TIMEOUT) as response:
                raw = response.read().decode("utf-8")
            parsed = json.loads(raw)
            if isinstance(parsed, dict):
                parsed.setdefault("source", "service")
                return parsed
            return {"data": parsed, "source": "service"}
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError, json.JSONDecodeError) as exc:
            self.last_error = str(exc)
            return None

    def get_profile(self, user_id: str) -> dict[str, Any]:
        remote = self._request("GET", f"/api/v1/users/{user_id}/health-profile")
        if remote:
            return remote
        profile = copy.deepcopy(MOCK_PROFILE)
        profile["user_id"] = user_id
        profile["source"] = "mock"
        return profile

    def get_recent_meals(self, user_id: str, days: int = 7) -> dict[str, Any]:
        remote = self._request("GET", f"/api/v1/users/{user_id}/meals?days={days}")
        if remote:
            return remote
        cutoff = (datetime.now(timezone.utc).date() - timedelta(days=days - 1)).isoformat()
        meals = [item for item in self._mock_meals if item["date"] >= cutoff]
        return {"user_id": user_id, "days": days, "meals": meals, "count": len(meals), "source": "mock"}

    def get_nutrition_gap(self, user_id: str, target_date: str | None = None) -> dict[str, Any]:
        target = target_date or _today()
        remote = self._request("GET", f"/api/v1/users/{user_id}/nutrition-gap?date={target}")
        if remote:
            return remote
        return {
            "user_id": user_id,
            "date": target,
            "targets": {"protein": 90, "fiber": 25, "veggies": 400, "water": 1800},
            "intake": {"protein": 41, "fiber": 12, "veggies": 130, "water": 700},
            "gap": {"protein": 49, "fiber": 13, "veggies": 270, "water": 1100},
            "source": "mock",
        }

    def get_weekly_stats(self, user_id: str) -> dict[str, Any]:
        remote = self._request("GET", f"/api/v1/users/{user_id}/weekly-stats")
        if remote:
            return remote
        return {
            "user_id": user_id,
            "week": "2026-11-09",
            "avg_health_score": 82.4,
            "days_recorded": 6,
            "three_meal_days": 5,
            "points": 210,
            "streak": 4,
            "source": "mock",
        }

    def search_foods(
        self,
        user_id: str,
        query: str = "",
        tags: list[str] | None = None,
        meal_type: str = "",
        limit: int = 5,
    ) -> dict[str, Any]:
        remote = self._request(
            "GET",
            f"/api/v1/foods/search?q={urllib.parse.quote(query)}&meal_type={urllib.parse.quote(meal_type)}&limit={limit}",
        )
        if remote:
            return remote
        profile = self.get_profile(user_id)
        allergies = set(profile.get("allergies") or [])
        dislikes = set(profile.get("dislikes") or [])
        results = []
        for food in MOCK_FOODS:
            if not food["available"]:
                continue
            if meal_type and meal_type not in food["meal_types"]:
                continue
            if tags and not set(tags).intersection(food["tags"]):
                continue
            if allergies and any(item in food["name"] for item in allergies):
                continue
            if dislikes and any(item in food["name"] for item in dislikes):
                continue
            if query and query not in food["name"] and not any(query in tag for tag in food["tags"]):
                continue
            results.append(copy.deepcopy(food))
            if len(results) >= limit:
                break
        return {"user_id": user_id, "foods": results, "count": len(results), "source": "mock"}

    def get_menu(self, user_id: str, meal_type: str = "") -> dict[str, Any]:
        remote = self._request("GET", f"/api/v1/menu?meal_type={urllib.parse.quote(meal_type)}")
        if remote:
            return remote
        foods = [copy.deepcopy(food) for food in MOCK_FOODS if food["available"]]
        if meal_type:
            foods = [food for food in foods if meal_type in food["meal_types"]]
        return {"user_id": user_id, "meal_type": meal_type or "全部", "foods": foods, "count": len(foods), "source": "mock"}

    def get_friend_ranking(self, user_id: str, scope: str = "week") -> dict[str, Any]:
        remote = self._request("GET", f"/api/v1/users/{user_id}/ranking?scope={scope}")
        if remote:
            return remote
        return {"user_id": user_id, "scope": scope, "ranking": copy.deepcopy(MOCK_RANKING), "source": "mock"}

    def get_friends(self, user_id: str) -> dict[str, Any]:
        remote = self._request("GET", f"/api/v1/users/{user_id}/friends")
        if remote:
            return remote
        return {"user_id": user_id, "friends": copy.deepcopy(MOCK_FRIENDS), "source": "mock"}

    def record_meal(self, user_id: str, payload: dict[str, Any], idempotency_key: str) -> dict[str, Any]:
        body = dict(payload)
        body["idempotency_key"] = idempotency_key
        remote = self._request("POST", f"/api/v1/users/{user_id}/meals", body)
        if remote:
            return remote
        meal = {
            "date": payload.get("date") or _today(),
            "type": payload.get("type", "加餐"),
            "items": payload.get("items", []),
            "protein": payload.get("protein", 0),
            "fiber": payload.get("fiber", 0),
            "veggies": payload.get("veggies", 0),
            "water": payload.get("water", 0),
        }
        self._mock_meals.append(meal)
        return {
            "ok": True,
            "receipt_id": f"mock-{idempotency_key}",
            "user_id": user_id,
            "meal": meal,
            "source": "mock",
        }

    def search_knowledge(self, query: str, top_k: int = 4) -> dict[str, Any]:
        from agent.rag.retriever import KnowledgeBase

        results = KnowledgeBase().search(query, top_k=top_k)
        return {"query": query, "results": results, "count": len(results), "source": "local-rag"}

    def status(self) -> dict[str, Any]:
        return {
            "mode": self.mode,
            "base_url": self.base_url,
            "last_error": self.last_error,
            "note": "服务端未配置时自动使用本地 mock 数据，生产环境仅服务端可连接 MySQL/Redis",
        }