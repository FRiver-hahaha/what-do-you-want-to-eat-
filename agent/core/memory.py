from __future__ import annotations

import re
from typing import Any

from agent.data.db import execute, now_iso, query_all

PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("allergy", re.compile(r"我对(.{1,20}?)(?:过敏)")),
    ("allergy", re.compile(r"我(?!对)(.{1,20}?)过敏")),
    ("dislike", re.compile(r"我(?:不吃|不喜欢吃|讨厌吃|讨厌)(.{1,20}?)(?:[，。！,.!]|$)")),
    ("preference", re.compile(r"我(?:喜欢|爱吃)(.{1,20}?)(?:[，。！,.!]|$)")),
    ("goal", re.compile(r"我(?:在|正在)(减脂|增肌|控糖|保持体重|增重)")),
    ("goal", re.compile(r"我的目标是(.{1,20}?)(?:[，。！,.!]|$)")),
]

KIND_LABELS = {
    "allergy": "过敏",
    "dislike": "忌口",
    "preference": "偏好",
    "goal": "目标",
    "note": "备注",
}


class MemoryStore:
    def add(self, user_id: str, kind: str, value: str, confidence: float = 0.8, source: str = "chat") -> bool:
        cleaned = value.strip().strip("，。,.! ")
        if not cleaned or len(cleaned) > 40:
            return False
        before = query_all("SELECT id FROM memories WHERE user_id = ? AND kind = ? AND value = ?", (user_id, kind, cleaned))
        if before:
            return False
        execute(
            "INSERT OR IGNORE INTO memories (user_id, kind, value, confidence, source, created_at) VALUES (?, ?, ?, ?, ?, ?)",
            (user_id, kind, cleaned, confidence, source, now_iso()),
        )
        return True

    def extract(self, user_id: str, text: str) -> list[dict[str, Any]]:
        added = []
        for kind, pattern in PATTERNS:
            for match in pattern.finditer(text):
                value = match.group(1).strip()
                if self.add(user_id, kind, value):
                    added.append({"kind": kind, "value": value})
        return added

    def list(self, user_id: str) -> list[dict[str, Any]]:
        return query_all(
            "SELECT id, kind, value, confidence, source, created_at FROM memories WHERE user_id = ? ORDER BY kind, id",
            (user_id,),
        )

    def delete(self, memory_id: int, user_id: str) -> bool:
        row = query_all("SELECT id FROM memories WHERE id = ? AND user_id = ?", (memory_id, user_id))
        if not row:
            return False
        execute("DELETE FROM memories WHERE id = ? AND user_id = ?", (memory_id, user_id))
        return True

    def prompt_block(self, user_id: str, limit: int = 20) -> str:
        rows = self.list(user_id)[:limit]
        if not rows:
            return ""
        lines = ["<memory>长期记忆（来自历史对话，可在设置中删除）："]
        for row in rows:
            label = KIND_LABELS.get(str(row.get("kind")), str(row.get("kind")))
            lines.append(f"- {label}：{row.get('value')}")
        lines.append("</memory>")
        return "\n".join(lines)