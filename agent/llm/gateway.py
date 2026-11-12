from __future__ import annotations

import hashlib
import json
import os
import re
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from typing import Any

from agent.config import MODELS_CONFIG, available_models, default_model_id


@dataclass
class LLMResult:
    content: str = ""
    tool_calls: list[dict[str, Any]] = field(default_factory=list)
    usage: dict[str, Any] = field(default_factory=dict)
    model: str = ""
    provider: str = "rule"
    finish_reason: str = "stop"
    degraded: bool = False


class LLMError(RuntimeError):
    pass


class HashEmbedder:
    def __init__(self, dim: int = 512) -> None:
        self.dim = dim

    def embed(self, texts: list[str]) -> list[list[float]]:
        vectors: list[list[float]] = []
        for text in texts:
            vector = [0.0] * self.dim
            grams = self._grams(text)
            for gram in grams:
                digest = hashlib.blake2b(gram.encode("utf-8"), digest_size=8).digest()
                index = int.from_bytes(digest[:4], "little") % self.dim
                sign = 1.0 if digest[4] % 2 == 0 else -1.0
                vector[index] += sign
            norm = sum(value * value for value in vector) ** 0.5 or 1.0
            vectors.append([round(value / norm, 6) for value in vector])
        return vectors

    @staticmethod
    def _grams(text: str) -> list[str]:
        cleaned = re.sub(r"\s+", "", text.lower())
        latin = re.findall(r"[a-z0-9]+", cleaned)
        chinese = re.findall(r"[\u4e00-\u9fff]", cleaned)
        grams = list(latin)
        grams.extend("".join(chinese[index:index + 2]) for index in range(max(0, len(chinese) - 1)))
        return grams or [cleaned]


class RuleProvider:
    name = "rule"
    embedder = HashEmbedder()

    def chat(
        self,
        messages: list[dict[str, Any]],
        model_cfg: dict[str, Any],
        tools: list[dict[str, Any]] | None = None,
    ) -> LLMResult:
        user_index = self._last_user_index(messages)
        if user_index < 0:
            return LLMResult(content="你好，我可以帮你查饮食记录、推荐今天吃什么，或分析本周健康情况。", finish_reason="stop")
        tool_messages = [message for message in messages[user_index + 1:] if message.get("role") == "tool"]
        if tool_messages:
            return LLMResult(content=self._compose_final(messages), finish_reason="stop")

        user_text = str(messages[user_index].get("content", ""))
        available_names = {tool["function"]["name"] for tool in (tools or [])}
        calls = self._plan_calls(user_text, available_names)
        if not calls:
            return LLMResult(content=self._compose_without_tools(user_text, messages), finish_reason="stop")
        return LLMResult(
            content="",
            tool_calls=calls,
            finish_reason="tool_calls",
        )

    def embed(self, texts: list[str]) -> list[list[float]]:
        return self.embedder.embed(texts)

    @staticmethod
    def _last_user_index(messages: list[dict[str, Any]]) -> int:
        for index in range(len(messages) - 1, -1, -1):
            if messages[index].get("role") == "user":
                return index
        return -1

    @staticmethod
    def _call(call_id: str, name: str, arguments: dict[str, Any]) -> dict[str, Any]:
        return {
            "id": call_id,
            "type": "function",
            "function": {"name": name, "arguments": json.dumps(arguments, ensure_ascii=False)},
        }

    def _plan_calls(self, text: str, available_names: set[str]) -> list[dict[str, Any]]:
        tools = available_names
        calls: list[dict[str, Any]] = []
        counter = 0

        def add(name: str, arguments: dict[str, Any]) -> None:
            nonlocal counter
            if name in tools:
                counter += 1
                calls.append(self._call(f"rule_call_{counter}", name, arguments))

        if any(word in text for word in ["猜", "推测"]):
            add("get_recent_meals", {"days": 7})
            add("get_health_profile", {})
            add("get_nutrition_gap", {})
            return calls

        if any(word in text for word in ["吃了什么", "查记录", "饮食记录", "昨天吃", "今天吃了"]):
            add("get_recent_meals", {"days": 7})
            add("get_health_profile", {})
            return calls

        if any(word in text for word in ["排名", "排行榜", "好友"]):
            add("get_friend_ranking", {"scope": "week"})
            add("get_weekly_stats", {})
            return calls

        if any(word in text for word in ["周报", "本周", "积分", "总结"]):
            add("get_weekly_stats", {})
            add("get_recent_meals", {"days": 7})
            return calls

        if any(word in text for word in ["菜单", "食堂", "外卖", "有什么菜"]):
            add("get_menu", {})
            add("get_health_profile", {})
            return calls

        if any(word in text for word in ["记录", "早餐吃", "午餐吃", "晚餐吃", "加餐"]):
            return calls

        if any(word in text for word in ["推荐", "吃什么", "吃啥", "该吃", "饿", "午餐", "晚餐", "早餐"]):
            add("get_health_profile", {})
            add("get_nutrition_gap", {})
            add("get_recent_meals", {"days": 3})
            add("search_foods", {"meal_type": self._meal_type(text)})
            return calls

        if any(word in text for word in ["蛋白质", "热量", "纤维", "营养", "减脂", "增肌"]):
            add("get_nutrition_gap", {})
            add("search_foods", {})
            return calls

        return calls

    @staticmethod
    def _meal_type(text: str) -> str:
        if "早餐" in text or "早上" in text:
            return "早餐"
        if "午餐" in text or "中午" in text:
            return "午餐"
        if "晚餐" in text or "晚上" in text:
            return "晚餐"
        if "加餐" in text or "下午茶" in text:
            return "加餐"
        return ""

    def _tool_payloads(self, messages: list[dict[str, Any]]) -> dict[str, Any]:
        call_names: dict[str, str] = {}
        for message in messages:
            if message.get("role") != "assistant":
                continue
            for call in message.get("tool_calls") or []:
                call_names[str(call.get("id") or "")] = str((call.get("function") or {}).get("name") or "")

        payloads: dict[str, Any] = {}
        for message in messages:
            if message.get("role") != "tool":
                continue
            name = message.get("name") or call_names.get(str(message.get("tool_call_id") or ""), "")
            try:
                payload = json.loads(message.get("content", "{}"))
            except (TypeError, json.JSONDecodeError):
                continue
            if name:
                payloads[name] = payload
        return payloads

    def _compose_final(self, messages: list[dict[str, Any]]) -> str:
        payloads = self._tool_payloads(messages)
        user_index = self._last_user_index(messages)
        user_text = str(messages[user_index].get("content", "")) if user_index >= 0 else ""

        if "get_menu" in payloads:
            return self._compose_menu(payloads, user_text)
        if "get_friend_ranking" in payloads:
            return self._compose_ranking(payloads)
        if "get_weekly_stats" in payloads and "get_nutrition_gap" not in payloads:
            return self._compose_weekly(payloads)
        if ("猜" in user_text or "推测" in user_text) and "get_recent_meals" in payloads:
            return self._compose_guess(payloads)
        if "search_foods" in payloads or "get_nutrition_gap" in payloads:
            return self._compose_recommendation(payloads)
        if "get_recent_meals" in payloads:
            return self._compose_meals(payloads)
        return self._compose_without_tools(user_text, messages)

    @staticmethod
    def _data(payload: dict[str, Any]) -> Any:
        if not isinstance(payload, dict):
            return {}
        value = payload.get("data", payload)
        return value if isinstance(value, dict) else {}

    def _compose_menu(self, payloads: dict[str, Any], user_text: str) -> str:
        data = self._data(payloads["get_menu"])
        foods = data.get("foods", [])[:4]
        profile = self._data(payloads.get("get_health_profile", {}))
        goal = profile.get("goal", "健康饮食")
        lines = [f"实时菜单里有 {data.get('count', len(foods))} 道可选，按你的「{goal}」目标，我优先建议："]
        for index, food in enumerate(foods, start=1):
            lines.append(
                f"{index}. {food.get('name')}：约 {food.get('calories') or food.get('kcal')} kcal，蛋白 {food.get('protein')}g，"
                f"蔬菜 {food.get('veggies')}g"
            )
        lines.append("如果告诉我这一餐想控制在多少热量，我可以再缩小范围。")
        return "\n".join(lines)

    def _compose_ranking(self, payloads: dict[str, Any]) -> str:
        ranking = self._data(payloads["get_friend_ranking"]).get("ranking", [])
        weekly = self._data(payloads.get("get_weekly_stats", {}))
        lines = ["本周健康均分排名："]
        for item in ranking[:5]:
            lines.append(f"{item.get('rank')}. {item.get('name')}  {item.get('score')}")
        if weekly:
            lines.append(f"你已记录 {weekly.get('days_recorded')} 天，平均分 {weekly.get('avg_health_score')}。")
        lines.append("排名按本周每日健康分平均值计算，不是记录次数。")
        return "\n".join(lines)

    def _compose_weekly(self, payloads: dict[str, Any]) -> str:
        weekly = self._data(payloads["get_weekly_stats"])
        meals = self._data(payloads.get("get_recent_meals", {}))
        lines = [
            f"本周你记录了 {weekly.get('days_recorded')} 天，三餐齐全 {weekly.get('three_meal_days')} 天。",
            f"平均健康分 {weekly.get('avg_health_score')}，当前连续记录 {weekly.get('streak')} 天，积分 {weekly.get('points')}。",
        ]
        if meals.get("meals"):
            lines.append("从近几天看，早餐和午餐比较稳定，蔬菜与饮水还有提升空间。")
        lines.append("下周可以先只定一个小目标：每天多补一份蔬菜，或把饮水补到 1500ml 以上。")
        return "\n".join(lines)

    def _compose_guess(self, payloads: dict[str, Any]) -> str:
        meals = self._data(payloads["get_recent_meals"]).get("meals", [])
        profile = self._data(payloads.get("get_health_profile", {}))
        gap = self._data(payloads.get("get_nutrition_gap", {}))
        names: list[str] = []
        for meal in meals[:5]:
            for item in meal.get("items", []):
                if item not in names:
                    names.append(item)
        guesses = names[:3] or ["燕麦鸡蛋早餐", "鸡胸肉沙拉", "清蒸鱼配青菜"]
        lines = ["我猜你今天可能吃过：" + "、".join(guesses) + "。"]
        if meals:
            lines.append(f"依据：最近 {len(meals)} 条记录里，早餐偏燕麦、鸡蛋和面包，午晚餐常有鸡胸肉、鱼和青菜。")
        if gap:
            lines.append(
                f"今天蛋白还差 {gap.get('gap', {}).get('protein', 0)}g、蔬菜差 "
                f"{gap.get('gap', {}).get('veggies', 0)}g，所以高蛋白配蔬菜的组合概率更高。"
            )
        if profile.get("goal"):
            lines.append(f"这是结合你「{profile.get('goal')}」目标的推测，不是真实概率。猜对了吗？")
        return "\n".join(lines)

    def _compose_recommendation(self, payloads: dict[str, Any]) -> str:
        profile = self._data(payloads.get("get_health_profile", {}))
        gap = self._data(payloads.get("get_nutrition_gap", {}))
        foods = self._data(payloads.get("search_foods", {})).get("foods", [])
        lines: list[str] = []
        if profile:
            lines.append(
                f"按你的档案（{profile.get('goal', '健康饮食')}、{profile.get('activity', '中等')}活动量、"
                f"目标每日蛋白约 {gap.get('targets', {}).get('protein', 90)}g）："
            )
        if gap:
            lines.append(
                f"今天蛋白还差 {gap.get('gap', {}).get('protein', 0)}g，蔬菜差 "
                f"{gap.get('gap', {}).get('veggies', 0)}g，饮水差 {gap.get('gap', {}).get('water', 0)}ml。"
            )
        if foods:
            lines.append("今天更合适的选择：")
            for index, food in enumerate(foods[:3], start=1):
                lines.append(
                    f"{index}. {food.get('name')}：约 {food.get('calories') or food.get('kcal')} kcal，蛋白 {food.get('protein')}g，"
                    f"蔬菜 {food.get('veggies')}g"
                )
        else:
            lines.append("优先选高蛋白 + 高蔬菜 + 少油的组合，比如鸡胸肉、鱼虾配西兰花和糙米。")
        lines.append("建议先补 300–500ml 水，晚餐把蔬菜放在主食之前吃。")
        return "\n".join(lines)

    def _compose_meals(self, payloads: dict[str, Any]) -> str:
        meals = self._data(payloads["get_recent_meals"]).get("meals", [])
        profile = self._data(payloads.get("get_health_profile", {}))
        if not meals:
            return "我在近 7 天没有查到你的饮食记录。你可以先告诉我今天早餐吃了什么，我再帮你补全分析。"
        lines = [f"近 {len(meals)} 条饮食记录："]
        for meal in meals[:6]:
            lines.append(
                f"{meal.get('date')} {meal.get('type')}：{'、'.join(meal.get('items', []))}"
                f"（蛋白 {meal.get('protein', 0)}g，蔬菜 {meal.get('veggies', 0)}g）"
            )
        if profile:
            lines.append(f"结合你「{profile.get('goal', '健康饮食')}」的目标，建议下一餐优先补蔬菜和蛋白质。")
        return "\n".join(lines)

    def _compose_without_tools(self, user_text: str, messages: list[dict[str, Any]]) -> str:
        knowledge = ""
        for message in messages:
            if message.get("role") == "system":
                match = re.search(r"<knowledge>(.*?)</knowledge>", str(message.get("content", "")), re.S)
                if match:
                    knowledge = match.group(1).strip()
                    break
        if knowledge:
            return "根据知识库：" + knowledge[:600]
        if any(word in user_text for word in ["你好", "hi", "hello", "嗨"]):
            return "你好，我是今天你吃了吗的 Agent。你可以问我：今天吃了什么、今天该吃什么、本周健康情况，或让我按实时菜单配一餐。"
        if any(word in user_text for word in ["skill", "技能"]):
            return "内置技能包括健康专家、饮食专家、营养师、页面设计师、猜餐和配餐。工作模式下还可以从 Skill 市场安装或导入本地技能。"
        return (
            "我可以直接帮你做三件事：\n"
            "1. 查今天/近 7 天吃了什么；\n"
            "2. 根据你的健康档案和营养缺口推荐下一餐；\n"
            "3. 结合本周记录做健康复盘。\n"
            "你可以说得更具体一点，比如“晚餐吃什么，蛋白还差多少”。"
        )


class OpenAICompatibleProvider:
    name = "openai-compatible"

    def chat(
        self,
        messages: list[dict[str, Any]],
        model_cfg: dict[str, Any],
        tools: list[dict[str, Any]] | None = None,
    ) -> LLMResult:
        base_url = str(model_cfg.get("base_url", "")).rstrip("/")
        if not base_url:
            raise LLMError("模型缺少 base_url")
        api_key = os.environ.get(str(model_cfg.get("api_key_env", "")), "")
        payload: dict[str, Any] = {
            "model": model_cfg.get("model") or model_cfg.get("id"),
            "messages": messages,
            "temperature": float(model_cfg.get("temperature", 0.4)),
            "stream": False,
        }
        if tools:
            payload["tools"] = tools
            payload["tool_choice"] = model_cfg.get("tool_choice", "auto")
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        request = urllib.request.Request(base_url + "/chat/completions", data=body, method="POST")
        request.add_header("Content-Type", "application/json; charset=utf-8")
        if api_key:
            request.add_header("Authorization", f"Bearer {api_key}")
        try:
            with urllib.request.urlopen(request, timeout=float(model_cfg.get("timeout", 60))) as response:
                raw = response.read().decode("utf-8")
            parsed = json.loads(raw)
            choice = (parsed.get("choices") or [{}])[0]
            message = choice.get("message") or {}
            return LLMResult(
                content=message.get("content") or "",
                tool_calls=message.get("tool_calls") or [],
                usage=parsed.get("usage") or {},
                model=payload["model"],
                provider=self.name,
                finish_reason=choice.get("finish_reason", "stop"),
            )
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError, json.JSONDecodeError) as exc:
            raise LLMError(f"模型调用失败：{exc}") from exc


class LLMGateway:
    def __init__(self) -> None:
        self.models: dict[str, dict[str, Any]] = {
            str(model["id"]): dict(model) for model in MODELS_CONFIG.get("models", [])
        }
        self.providers: dict[str, Any] = {
            "rule": RuleProvider(),
            "openai-compatible": OpenAICompatibleProvider(),
        }
        self.embedder = HashEmbedder()

    def list_models(self) -> list[dict[str, Any]]:
        return available_models()

    def resolve(self, model_id: str | None) -> dict[str, Any]:
        chosen = model_id or default_model_id()
        model = self.models.get(chosen)
        if not model:
            model = self.models.get(default_model_id()) or {
                "id": "rule-fallback",
                "provider": "rule",
            }
        return dict(model)

    def complete(self, model_id: str | None, messages: list[dict[str, Any]], tools: list[dict[str, Any]] | None) -> LLMResult:
        model_cfg = self.resolve(model_id)
        provider = self.providers.get(str(model_cfg.get("provider", "rule")))
        if provider is None:
            provider = self.providers["rule"]
        try:
            result = provider.chat(messages, model_cfg, tools)
            result.model = str(model_cfg.get("id"))
            return result
        except LLMError:
            fallback = self.providers["rule"]
            result = fallback.chat(messages, {"id": "rule-fallback", "provider": "rule"}, tools)
            result.degraded = True
            result.model = "rule-fallback"
            return result

    def embed(self, texts: list[str]) -> list[list[float]]:
        route = MODELS_CONFIG.get("routing", {}).get("embedding", "hash-embedding")
        if route == "hash-embedding":
            return self.embedder.embed(texts)
        return self.embedder.embed(texts)