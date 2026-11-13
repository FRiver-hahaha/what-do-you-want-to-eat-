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
    "name": "我",
    "height": 170,
    "weight": 60,
    "age": 28,
    "gender": "女",
    "goal": "均衡饮食",
    "activity": "久坐",
    "water_target": 1800,
    "waterTarget": 1800,
    "sleep": "7小时",
    "sleep_hours": 7,
    "allergies": ["花生"],
    "dislikes": ["香菜"],
    "diet_style": "普通饮食",
    "bmi": 20.76,
    "note": "下午容易口渴，晚餐不宜过饱",
}

MOCK_TODAY_MEALS = [
    {"type": "早餐", "time": "08:20", "text": "燕麦酸奶、蓝莓、水煮蛋", "items": ["燕麦酸奶", "蓝莓", "水煮蛋"], "protein": 22, "fiber": 6, "veggies": 0, "water": 300, "penalty": 0},
    {"type": "午餐", "time": "12:18", "text": "柠檬鸡胸能量碗、无糖茶", "items": ["柠檬鸡胸能量碗", "无糖茶"], "protein": 38, "fiber": 9, "veggies": 320, "water": 650, "penalty": 0},
]

MOCK_PAST_MEALS = [
    {"offset": 1, "type": "早餐", "items": ["全麦面包", "豆浆"], "protein": 15, "fiber": 4, "veggies": 0, "water": 250},
    {"offset": 1, "type": "午餐", "items": ["牛肉面"], "protein": 26, "fiber": 3, "veggies": 40, "water": 200},
    {"offset": 1, "type": "晚餐", "items": ["清蒸鱼", "青菜", "米饭"], "protein": 30, "fiber": 5, "veggies": 130, "water": 300},
    {"offset": 2, "type": "早餐", "items": ["包子", "豆浆"], "protein": 12, "fiber": 2, "veggies": 0, "water": 200},
    {"offset": 2, "type": "午餐", "items": ["番茄牛腩", "米饭"], "protein": 28, "fiber": 4, "veggies": 80, "water": 300},
    {"offset": 2, "type": "晚餐", "items": ["蔬菜汤", "鸡胸肉"], "protein": 30, "fiber": 6, "veggies": 160, "water": 250},
    {"offset": 3, "type": "午餐", "items": ["鸡胸蔬菜沙拉", "糙米饭"], "protein": 33, "fiber": 8, "veggies": 280, "water": 300},
    {"offset": 3, "type": "晚餐", "items": ["菌菇鸡汤面"], "protein": 24, "fiber": 6, "veggies": 160, "water": 450},
]

MOCK_FOODS = [
    {"id": "lemon-chicken", "name": "柠檬鸡胸能量碗", "category": "高蛋白 · 清爽", "description": "烤鸡胸、牛油果、藜麦与时令蔬菜", "calories": 460, "protein": 38, "fiber": 9, "veggies": 320, "water": 250, "nature": "recommend", "keywords": "鸡胸 沙拉 碗 轻食 蛋白", "meal_types": ["午餐", "晚餐"], "price": 30, "available": True, "image": "https://images.unsplash.com/photo-1546069901-ba9599a7e63c?auto=format&fit=crop&w=600&q=82"},
    {"id": "tomato-beef", "name": "番茄牛腩盖饭", "category": "热乎 · 饱腹", "description": "番茄牛腩、杂粮米饭与焯拌时蔬", "calories": 620, "protein": 32, "fiber": 7, "veggies": 180, "water": 180, "nature": "recommend", "keywords": "米饭 饭 牛肉 牛腩 盖饭 番茄", "meal_types": ["午餐", "晚餐"], "price": 28, "available": True, "image": "https://images.unsplash.com/photo-1512058564366-18510be2db19?auto=format&fit=crop&w=600&q=82"},
    {"id": "mushroom-soup", "name": "菌菇鸡汤面", "category": "清淡 · 汤食", "description": "清鸡汤、三种菌菇、细面与小青菜", "calories": 430, "protein": 24, "fiber": 6, "veggies": 160, "water": 450, "nature": "recommend", "keywords": "面 汤 菌菇 鸡汤 清淡", "meal_types": ["早餐", "午餐", "晚餐"], "price": 24, "available": True, "image": "https://images.unsplash.com/photo-1547592166-23ac45744acd?auto=format&fit=crop&w=600&q=82"},
    {"id": "shrimp-pasta", "name": "蒜香虾仁意面", "category": "高蛋白 · 换口味", "description": "虾仁、樱桃番茄与全麦意面", "calories": 560, "protein": 35, "fiber": 8, "veggies": 140, "water": 160, "nature": "recommend", "keywords": "面 意面 虾 海鲜 番茄", "meal_types": ["午餐", "晚餐"], "price": 36, "available": True, "image": "https://images.unsplash.com/photo-1563379926898-05f4575a45d8?auto=format&fit=crop&w=600&q=82"},
    {"id": "vegetable-rice", "name": "时蔬豆腐拌饭", "category": "植物蛋白 · 多蔬菜", "description": "香煎豆腐、烤南瓜、西兰花与杂粮饭", "calories": 410, "protein": 22, "fiber": 12, "veggies": 360, "water": 180, "nature": "recommend", "keywords": "米饭 饭 豆腐 素食 拌饭 蔬菜", "meal_types": ["午餐", "晚餐"], "price": 25, "available": True, "image": "https://images.unsplash.com/photo-1512621776951-a57141f2eefd?auto=format&fit=crop&w=600&q=82"},
    {"id": "pepper-chicken", "name": "藤椒鸡丝拌面", "category": "微辣 · 快手", "description": "嫩鸡丝、黄瓜丝与荞麦面", "calories": 520, "protein": 30, "fiber": 7, "veggies": 140, "water": 180, "nature": "recommend", "keywords": "面 鸡丝 辣 荞麦 凉面", "meal_types": ["午餐", "晚餐"], "price": 26, "available": True, "image": "https://images.unsplash.com/photo-1473093295043-cdd812d0e601?auto=format&fit=crop&w=600&q=82"},
    {"id": "tomato-egg-rice", "name": "番茄鸡蛋盖饭", "category": "家常 · 顺口", "description": "番茄炒蛋、杂粮米饭与小份青菜", "calories": 480, "protein": 18, "fiber": 5, "veggies": 160, "water": 150, "nature": "recommend", "keywords": "米饭 饭 番茄 鸡蛋 盖饭 家常", "meal_types": ["早餐", "午餐", "晚餐"], "price": 20, "available": True, "image": "https://images.unsplash.com/photo-1504674900247-0877df9cc836?auto=format&fit=crop&w=600&q=82"},
    {"id": "salmon-quinoa", "name": "三文鱼藜麦碗", "category": "高蛋白 · 优质脂肪", "description": "烤三文鱼、藜麦、牛油果与羽衣甘蓝", "calories": 520, "protein": 34, "fiber": 8, "veggies": 240, "water": 200, "nature": "recommend", "keywords": "三文鱼 鱼 藜麦 沙拉 轻食", "meal_types": ["午餐", "晚餐"], "price": 42, "available": True, "image": "https://images.unsplash.com/photo-1467003909585-2f8a72700288?auto=format&fit=crop&w=600&q=82"},
    {"id": "steamed-fish-rice", "name": "清蒸鲈鱼配米饭", "category": "清淡 · 蒸食", "description": "清蒸鲈鱼、杂粮米饭与白灼菜心", "calories": 470, "protein": 33, "fiber": 4, "veggies": 200, "water": 200, "nature": "recommend", "keywords": "米饭 饭 鱼 清蒸 少油", "meal_types": ["午餐", "晚餐"], "price": 34, "available": True, "image": "https://images.unsplash.com/photo-1467003909585-2f8a72700288?auto=format&fit=crop&w=600&q=82"},
    {"id": "chicken-salad", "name": "鸡胸蔬菜沙拉", "category": "低脂 · 高纤维", "description": "煎鸡胸、混合生菜、番茄与油醋汁", "calories": 380, "protein": 32, "fiber": 10, "veggies": 300, "water": 220, "nature": "recommend", "keywords": "沙拉 蔬菜 鸡胸 轻食 减脂", "meal_types": ["早餐", "午餐", "晚餐"], "price": 28, "available": True, "image": "https://images.unsplash.com/photo-1512621776951-a57141f2eefd?auto=format&fit=crop&w=600&q=82"},
    {"id": "yangzhou-fried-rice", "name": "扬州炒饭", "category": "热乎 · 放纵", "description": "火腿、虾仁、鸡蛋与香米炒饭", "calories": 680, "protein": 20, "fiber": 3, "veggies": 90, "water": 100, "nature": "indulge", "keywords": "米饭 饭 炒饭 火腿 虾仁", "meal_types": ["午餐", "晚餐"], "price": 22, "available": True, "image": "https://images.unsplash.com/photo-1512058564366-18510be2db19?auto=format&fit=crop&w=600&q=82"},
    {"id": "beef-fried-rice", "name": "牛肉炒饭", "category": "热乎 · 放纵", "description": "黑椒牛肉、香米与葱花", "calories": 720, "protein": 26, "fiber": 2, "veggies": 60, "water": 100, "nature": "indulge", "keywords": "米饭 饭 炒饭 牛肉 黑椒", "meal_types": ["午餐", "晚餐"], "price": 26, "available": True, "image": "https://images.unsplash.com/photo-1544025162-d76694265947?auto=format&fit=crop&w=600&q=82"},
    {"id": "fried-chicken-burger", "name": "炸鸡汉堡", "category": "快餐 · 放纵", "description": "脆皮炸鸡、芝士与汉堡面包", "calories": 780, "protein": 30, "fiber": 2, "veggies": 40, "water": 80, "nature": "indulge", "keywords": "炸鸡 汉堡 快餐 薯条", "meal_types": ["午餐", "晚餐"], "price": 32, "available": True, "image": "https://images.unsplash.com/photo-1568901346375-23c9450c58cd?auto=format&fit=crop&w=600&q=82"},
    {"id": "cheese-cake", "name": "芝士蛋糕", "category": "甜点 · 放纵", "description": "浓郁芝士、饼干底与莓果", "calories": 420, "protein": 8, "fiber": 1, "veggies": 0, "water": 40, "nature": "indulge", "keywords": "甜点 蛋糕 芝士 甜品", "meal_types": ["加餐"], "price": 18, "available": True, "image": "https://images.unsplash.com/photo-1551024506-0bccd828d307?auto=format&fit=crop&w=600&q=82"},
    {"id": "bubble-tea", "name": "珍珠奶茶", "category": "饮品 · 放纵", "description": "红茶、奶盖与黑糖珍珠", "calories": 460, "protein": 4, "fiber": 0, "veggies": 0, "water": 100, "nature": "indulge", "keywords": "奶茶 饮料 甜 珍珠 黑糖", "meal_types": ["加餐"], "price": 16, "available": True, "image": "https://images.unsplash.com/photo-1544145945-f90425340c7e?auto=format&fit=crop&w=600&q=82"},
    {"id": "malatang", "name": "麻辣烫", "category": "热辣 · 放纵", "description": "自选蔬菜、丸子与麻辣汤底", "calories": 640, "protein": 24, "fiber": 6, "veggies": 260, "water": 300, "nature": "indulge", "keywords": "麻辣烫 辣 火锅 丸子 汤", "meal_types": ["午餐", "晚餐"], "price": 30, "available": True, "image": "https://images.unsplash.com/photo-1547592166-23ac45744acd?auto=format&fit=crop&w=600&q=82"},
]

MOCK_FRIENDS = [
    {"id": "u_lin", "name": "林一", "relation": "friend"},
    {"id": "u_zhou", "name": "周舟", "relation": "friend"},
    {"id": "u_he", "name": "何苗", "relation": "friend"},
]

MOCK_RANKING = [
    {"rank": 1, "user_id": "u_lin", "name": "林一", "score": 88.6},
    {"rank": 2, "user_id": "u_demo", "name": "我", "score": 82.4},
    {"rank": 3, "user_id": "u_zhou", "name": "周舟", "score": 79.1},
    {"rank": 4, "user_id": "u_he", "name": "何苗", "score": 76.8},
]


def _build_mock_meals() -> list[dict[str, Any]]:
    meals: list[dict[str, Any]] = []
    for meal in MOCK_TODAY_MEALS:
        item = copy.deepcopy(meal)
        item["date"] = _today()
        meals.append(item)
    for meal in MOCK_PAST_MEALS:
        item = copy.deepcopy(meal)
        offset = int(item.pop("offset"))
        item["date"] = (date.today() - timedelta(days=offset)).isoformat()
        meals.append(item)
    return meals


class ServiceClient:
    def __init__(self, base_url: str | None = None, token: str | None = None) -> None:
        self.base_url = (base_url if base_url is not None else SERVER_BASE_URL).rstrip("/")
        self.token = token if token is not None else SERVER_TOKEN
        self.last_error = ""
        self._mock_meals = _build_mock_meals()
        self._profile_overrides: dict[str, dict[str, Any]] = {}

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
        overrides = self._profile_overrides.get(user_id, {})
        profile.update(overrides)
        profile["user_id"] = user_id
        profile["source"] = "mock"
        profile["overridden_fields"] = sorted(overrides)
        height = profile.get("height")
        weight = profile.get("weight")
        if height and weight:
            profile["bmi"] = round(float(weight) / (float(height) / 100) ** 2, 2)
        return profile

    def update_profile(self, user_id: str, fields: dict[str, Any]) -> dict[str, Any]:
        remote = self._request("PATCH", f"/api/v1/users/{user_id}/health-profile", fields)
        if remote:
            return remote
        current = self._profile_overrides.setdefault(user_id, {})
        current.update(fields)
        profile = self.get_profile(user_id)
        return {
            "ok": True,
            "user_id": user_id,
            "profile": profile,
            "updated": sorted(fields),
            "source": "mock",
        }

    def get_recent_meals(self, user_id: str, days: int = 7) -> dict[str, Any]:
        remote = self._request("GET", f"/api/v1/users/{user_id}/meals?days={days}")
        if remote:
            return remote
        cutoff = (date.today() - timedelta(days=days - 1)).isoformat()
        meals = [item for item in self._mock_meals if item["date"] >= cutoff]
        return {"user_id": user_id, "days": days, "meals": meals, "count": len(meals), "source": "mock"}

    def get_nutrition_gap(self, user_id: str, target_date: str | None = None) -> dict[str, Any]:
        target = target_date or _today()
        remote = self._request("GET", f"/api/v1/users/{user_id}/nutrition-gap?date={target}")
        if remote:
            return remote
        profile = self.get_profile(user_id)
        water_target = int(profile.get("water_target") or profile.get("waterTarget") or 1800)
        targets = {"protein": 90, "fiber": 25, "veggies": 500, "water": water_target}
        intake = {"protein": 60, "fiber": 15, "veggies": 320, "water": 950}
        gap = {key: max(0, targets[key] - intake[key]) for key in targets}
        return {
            "user_id": user_id,
            "date": target,
            "targets": targets,
            "intake": intake,
            "gap": gap,
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
        nature: str = "",
    ) -> dict[str, Any]:
        remote = self._request(
            "GET",
            f"/api/v1/foods/search?q={urllib.parse.quote(query)}&meal_type={urllib.parse.quote(meal_type)}&limit={limit}&nature={urllib.parse.quote(nature)}",
        )
        if remote:
            return remote
        profile = self.get_profile(user_id)
        allergies = set(profile.get("allergies") or [])
        dislikes = set(profile.get("dislikes") or [])
        tokens = [token for token in query.lower().split() if token]
        scored = []
        for food in MOCK_FOODS:
            if not food["available"]:
                continue
            if meal_type and meal_type not in food["meal_types"]:
                continue
            if nature and food["nature"] != nature:
                continue
            if tags and not set(tags).intersection(food["keywords"].split()):
                continue
            haystack = f"{food['name']} {food['category']} {food['description']} {food['keywords']}".lower()
            hit = len([token for token in tokens if token in haystack])
            if tokens and hit == 0:
                continue
            if allergies and any(item in haystack for item in allergies):
                continue
            if dislikes and any(item in haystack for item in dislikes):
                continue
            scored.append((hit, copy.deepcopy(food)))
        scored.sort(key=lambda item: item[0], reverse=True)
        foods = [food for _, food in scored[:limit]]
        return {"user_id": user_id, "foods": foods, "count": len(foods), "source": "mock"}

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
            "text": payload.get("text") or "、".join(payload.get("items", [])),
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