from __future__ import annotations

import json
import re
from typing import Any

from agent.config import MAX_CONTEXT_CHARS, MAX_CONTEXT_MESSAGES
from agent.data.db import execute, now_iso, query_all, query_one


def estimate_tokens(text: str) -> int:
    chinese = len(re.findall(r"[\u4e00-\u9fff]", text))
    other = max(0, len(text) - chinese)
    return chinese + other // 4 + 1


class ContextManager:
    def ensure_session(
        self,
        session_id: str,
        user_id: str,
        mode: str = "qa",
        model_id: str = "",
        skill_id: str = "",
    ) -> dict[str, Any]:
        row = query_one("SELECT * FROM sessions WHERE id = ?", (session_id,))
        if row is None:
            execute(
                "INSERT INTO sessions (id, user_id, mode, model_id, skill_id, summary, created_at, updated_at) VALUES (?, ?, ?, ?, ?, '', ?, ?)",
                (session_id, user_id, mode, model_id, skill_id, now_iso(), now_iso()),
            )
        else:
            execute(
                "UPDATE sessions SET mode = ?, model_id = ?, skill_id = ?, updated_at = ? WHERE id = ?",
                (mode, model_id, skill_id, now_iso(), session_id),
            )
        return query_one("SELECT * FROM sessions WHERE id = ?", (session_id,)) or {}

    def session(self, session_id: str) -> dict[str, Any] | None:
        return query_one("SELECT * FROM sessions WHERE id = ?", (session_id,))

    def append(
        self,
        session_id: str,
        user_id: str,
        role: str,
        content: str,
        tool_name: str | None = None,
        tool_call_id: str | None = None,
        meta: dict[str, Any] | None = None,
    ) -> None:
        execute(
            "INSERT INTO messages (session_id, user_id, role, content, tool_name, tool_call_id, meta, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            (
                session_id,
                user_id,
                role,
                content,
                tool_name,
                tool_call_id,
                json.dumps(meta or {}, ensure_ascii=False),
                now_iso(),
            ),
        )
        execute("UPDATE sessions SET updated_at = ? WHERE id = ?", (now_iso(), session_id))

    def history(self, session_id: str, limit: int = MAX_CONTEXT_MESSAGES) -> list[dict[str, Any]]:
        rows = query_all(
            "SELECT role, content, tool_name, tool_call_id, meta, created_at FROM messages "
            "WHERE session_id = ? AND role IN ('user','assistant') AND content <> '' "
            "ORDER BY id DESC LIMIT ?",
            (session_id, limit),
        )
        rows.reverse()
        return rows

    def raw_messages(self, session_id: str, limit: int = 200) -> list[dict[str, Any]]:
        return query_all(
            "SELECT id, role, content, tool_name, tool_call_id, meta, created_at FROM messages WHERE session_id = ? ORDER BY id LIMIT ?",
            (session_id, limit),
        )

    def history_messages(self, session_id: str, limit: int = MAX_CONTEXT_MESSAGES) -> list[dict[str, Any]]:
        messages = []
        budget = MAX_CONTEXT_CHARS
        for row in reversed(self.history(session_id, limit)):
            content = str(row.get("content") or "")
            cost = len(content)
            if cost > budget:
                content = content[: max(200, budget)]
            budget -= len(content)
            messages.append({"role": row["role"], "content": content})
            if budget <= 0:
                break
        messages.reverse()
        return messages

    def clear(self, session_id: str) -> None:
        execute("DELETE FROM messages WHERE session_id = ?", (session_id,))
        execute("DELETE FROM sessions WHERE id = ?", (session_id,))

    def stats(self, session_id: str) -> dict[str, Any]:
        rows = query_all("SELECT COUNT(*) AS total FROM messages WHERE session_id = ?", (session_id,))
        total = int(rows[0]["total"]) if rows else 0
        session = self.session(session_id) or {}
        return {"session_id": session_id, "messages": total, "mode": session.get("mode"), "summary": session.get("summary", "")}