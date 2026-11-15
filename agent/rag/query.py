from __future__ import annotations

import hashlib
import re
import threading
import time
import unicodedata
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Any, Iterable

CJK_WORD = re.compile(r"[\u4e00-\u9fff]+")
SPACE = re.compile(r"\s+")

DOMAIN_TERMS = {
    "蛋白质",
    "蛋白",
    "膳食纤维",
    "纤维",
    "蔬菜",
    "水果",
    "热量",
    "卡路里",
    "碳水",
    "碳水化合物",
    "脂肪",
    "减脂",
    "增肌",
    "减重",
    "体重",
    "饮水",
    "补水",
    "喝水",
    "早餐",
    "午餐",
    "晚餐",
    "加餐",
    "夜宵",
    "外卖",
    "过敏",
    "过敏原",
    "忌口",
    "营养",
    "营养素",
    "血糖",
    "血脂",
    "尿酸",
    "胆固醇",
    "维生素",
    "矿物质",
    "全谷物",
    "豆类",
    "菌菇",
    "鸡胸肉",
    "牛奶",
    "鸡蛋",
    "花生",
    "坚果",
    "豆浆",
    "燕麦",
    "沙拉",
    "麻辣烫",
    "盖饭",
    "炒饭",
    "包子",
    "酸奶",
    "饮食记录",
    "健康档案",
    "健康报告",
    "档案",
    "技能",
    "知识库",
    "检索",
    "上下文",
    "上下文检索",
    "查询",
    "纠错",
    "缓存",
    "推荐",
    "配餐",
    "菜单",
    "营养缺口",
    "蛋白质摄入",
    "膳食",
    "三餐",
    "运动",
    "训练",
    "睡眠",
    "消费水平",
    "消费",
    "价格",
    "金额",
    "预算",
    "价位",
    "餐费",
    "人均",
    "花费",
    "便宜",
    "贵",
}

COMMON_WORDS = {
    "我",
    "你",
    "他",
    "她",
    "它",
    "的",
    "了",
    "是",
    "在",
    "和",
    "与",
    "有",
    "没",
    "要",
    "想",
    "会",
    "能",
    "请",
    "帮",
    "看",
    "查",
    "说",
    "吃",
    "喝",
    "饭",
    "菜",
    "餐",
    "天",
    "周",
    "日",
    "次",
    "个",
    "份",
    "点",
    "些",
    "这",
    "那",
    "哪",
    "里",
    "上",
    "下",
    "前",
    "后",
    "好",
    "行",
    "多少",
    "什么",
    "怎么",
    "为什么",
    "今天",
    "明天",
    "昨天",
    "最近",
    "现在",
    "每天",
    "每周",
    "应该",
    "可以",
    "需要",
    "建议",
    "时候",
    "以后",
    "以前",
    "早上",
    "中午",
    "晚上",
    "一下",
    "一样",
    "还有",
}

REFERENCE_TOKENS = (
    "它",
    "这个",
    "那个",
    "这种",
    "那种",
    "这顿",
    "那顿",
    "这菜",
    "那菜",
    "这家",
    "那家",
    "刚才",
    "刚刚",
    "上面",
    "前面",
    "上一条",
    "还有呢",
    "再来",
    "再给",
    "换个",
    "继续",
    "然后呢",
)

CACHE_LOW_CONFIDENCE = 0.6


def normalize_text(text: str) -> str:
    value = unicodedata.normalize("NFKC", str(text or "")).lower()
    value = SPACE.sub(" ", value).strip()
    return value


def _edit_distance(left: str, right: str) -> int:
    if left == right:
        return 0
    if abs(len(left) - len(right)) > 2:
        return 3
    previous = list(range(len(right) + 1))
    for i, left_char in enumerate(left, start=1):
        current = [i]
        for j, right_char in enumerate(right, start=1):
            cost = 0 if left_char == right_char else 1
            current.append(min(previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost))
        previous = current
    return previous[-1]


class QueryCache:
    def __init__(self, maxsize: int = 512, ttl: float = 900.0) -> None:
        self.maxsize = maxsize
        self.ttl = ttl
        self._items: OrderedDict[str, tuple[float, Any]] = OrderedDict()
        self._lock = threading.Lock()
        self.hits = 0
        self.misses = 0

    def key(self, *parts: Any) -> str:
        raw = "\x1f".join(str(part) for part in parts)
        return hashlib.blake2b(raw.encode("utf-8"), digest_size=16).hexdigest()

    def get(self, key: str) -> Any:
        with self._lock:
            item = self._items.get(key)
            if item is None:
                self.misses += 1
                return None
            created_at, value = item
            if time.time() - created_at > self.ttl:
                self._items.pop(key, None)
                self.misses += 1
                return None
            self._items.move_to_end(key)
            self.hits += 1
            return value

    def set(self, key: str, value: Any, confidence: float = 1.0) -> bool:
        if confidence < CACHE_LOW_CONFIDENCE:
            return False
        with self._lock:
            self._items[key] = (time.time(), value)
            self._items.move_to_end(key)
            while len(self._items) > self.maxsize:
                self._items.popitem(last=False)
        return True

    def clear(self) -> None:
        with self._lock:
            self._items.clear()
            self.hits = 0
            self.misses = 0

    def stats(self) -> dict[str, Any]:
        total = self.hits + self.misses
        return {"entries": len(self._items), "hits": self.hits, "misses": self.misses, "hit_rate": round(self.hits / total, 3) if total else 0.0}


@dataclass
class QueryPlan:
    raw: str
    normalized: str
    resolved: str
    corrections: list[dict[str, str]] = field(default_factory=list)
    additions: list[str] = field(default_factory=list)
    reference_used: bool = False
    confidence: float = 1.0
    context_hash: str = ""
    from_cache: bool = False
    conflict: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "raw": self.raw,
            "normalized": self.normalized,
            "resolved": self.resolved,
            "corrections": self.corrections,
            "additions": self.additions,
            "reference_used": self.reference_used,
            "confidence": self.confidence,
            "from_cache": self.from_cache,
        }


class QueryProcessor:
    def __init__(self, terms: Iterable[str] | None = None) -> None:
        self.terms = {term for term in (terms or DOMAIN_TERMS) if term}
        self.cache = QueryCache()
        self._by_first: dict[str, set[str]] = {}
        self._by_last: dict[str, set[str]] = {}
        self._rebuild_index()

    def _rebuild_index(self) -> None:
        self._by_first = {}
        self._by_last = {}
        for term in self.terms:
            self._by_first.setdefault(term[0], set()).add(term)
            self._by_last.setdefault(term[-1], set()).add(term)

    def update_terms(self, terms: Iterable[str]) -> None:
        merged = {term for term in terms if term}
        if merged - self.terms:
            self.terms |= merged
            self._rebuild_index()

    def known(self, word: str) -> bool:
        return word in self.terms or word in COMMON_WORDS

    def _contains_known(self, window: str) -> bool:
        for size in range(2, len(window)):
            for start in range(0, len(window) - size + 1):
                if self.known(window[start:start + size]):
                    return True
        return False

    def _nearest(self, window: str) -> str | None:
        if len(window) < 2 or not CJK_WORD.fullmatch(window):
            return None
        candidates: set[str] = set()
        candidates |= self._by_first.get(window[0], set())
        candidates |= self._by_last.get(window[-1], set())
        best: tuple[int, str] | None = None
        for term in candidates:
            if term == window:
                return None
            length_gap = abs(len(term) - len(window))
            if length_gap > 1:
                continue
            same_first = term[0] == window[0]
            same_last = term[-1] == window[-1]
            if len(window) == 2 and not same_first:
                continue
            if not (same_first or same_last):
                continue
            prefix = 0
            for left, right in zip(window, term):
                if left != right:
                    break
                prefix += 1
            if _edit_distance(window, term) > 1:
                continue
            score = (-prefix, term)
            if best is None or score < best:
                best = score
        return best[1] if best else None

    def correct(self, text: str) -> tuple[str, list[dict[str, str]]]:
        corrections: list[dict[str, str]] = []
        pieces: list[str] = []
        index = 0
        length = len(text)
        while index < length:
            matched = False
            for size in (4, 3, 2):
                if index + size > length:
                    continue
                window = text[index:index + size]
                if CJK_WORD.fullmatch(window) and self.known(window):
                    pieces.append(window)
                    index += size
                    matched = True
                    break
            if matched:
                continue
            for size in (3, 2):
                if index + size > length:
                    continue
                window = text[index:index + size]
                if not CJK_WORD.fullmatch(window) or self._contains_known(window):
                    continue
                candidate = self._nearest(window)
                if candidate:
                    corrections.append({"from": window, "to": candidate})
                    pieces.append(candidate)
                    index += size
                    matched = True
                    break
            if matched:
                continue
            pieces.append(text[index])
            index += 1
        return "".join(pieces), corrections

    def _extract_terms(self, text: str, limit: int = 2) -> list[str]:
        found = [term for term in self.terms if len(term) >= 2 and term in text]
        found.sort(key=len, reverse=True)
        return found[:limit]

    def _needs_context(self, query: str) -> bool:
        if any(token in query for token in REFERENCE_TOKENS):
            return True
        if len(query) <= 8:
            return not any(term in query for term in self.terms if len(term) >= 2)
        return False

    @staticmethod
    def _entries(history: list[Any]) -> list[tuple[str, str]]:
        entries: list[tuple[str, str]] = []
        for item in history or []:
            if isinstance(item, dict):
                role = str(item.get("role") or "user")
                text = str(item.get("content") or "")
            else:
                role = "user"
                text = str(item)
            if text.strip():
                entries.append((role, text))
        return entries

    def context_hash(self, history: list[Any], turns: int = 4) -> str:
        recent = self._entries(history)[-turns:]
        if not recent:
            return "empty"
        payload = "\x1e".join(f"{role}:{text}" for role, text in recent)
        return hashlib.blake2b(payload.encode("utf-8"), digest_size=8).hexdigest()

    def prepare(self, query: str, history: list[str] | None = None) -> QueryPlan:
        raw = str(query or "")
        normalized = normalize_text(raw)
        digest = self.context_hash(history or [])
        cache_key = self.cache.key("plan", normalized, digest)
        cached = self.cache.get(cache_key)
        if isinstance(cached, QueryPlan):
            plan = QueryPlan(**{**cached.__dict__})
            plan.from_cache = True
            return plan

        corrected, corrections = self.correct(normalized)
        resolved = corrected
        additions: list[str] = []
        reference_used = False
        if history and self._needs_context(normalized):
            entries = self._entries(history)
            ordered = [text for role, text in reversed(entries) if role == "user"]
            ordered.extend(text for role, text in reversed(entries) if role != "user")
            for text in ordered[:4]:
                for term in self._extract_terms(normalize_text(text)):
                    if term in resolved or term in additions:
                        continue
                    if any(term in existing or existing in term for existing in additions):
                        continue
                    additions.append(term)
                    if len(additions) >= 1:
                        break
                if additions:
                    break
            if additions:
                resolved = f"{corrected} {' '.join(additions)}"
                reference_used = True

        confidence = 1.0
        if corrections:
            confidence = 0.75 if len(corrections) == 1 else 0.6 if len(corrections) == 2 else 0.45

        plan = QueryPlan(
            raw=raw,
            normalized=normalized,
            resolved=resolved,
            corrections=corrections,
            additions=additions,
            reference_used=reference_used,
            confidence=confidence,
            context_hash=digest,
        )
        self.cache.set(cache_key, QueryPlan(**plan.__dict__), confidence)
        return plan


__all__ = ["DOMAIN_TERMS", "QueryCache", "QueryPlan", "QueryProcessor", "normalize_text"]