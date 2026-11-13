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

    def session_summary(self, session_id: str) -> str:
        session = self.session(session_id) or {}
        return str(session.get("summary") or "")

    def _history_rows(self, session_id: str, after_id: int = 0) -> list[dict[str, Any]]:
        return query_all(
            "SELECT id, role, content FROM messages WHERE session_id = ? AND id > ? "
            "AND role IN ('user','assistant') AND content <> '' ORDER BY id ASC",
            (session_id, after_id),
        )

    def compact(self, session_id: str, keep_recent: int = 6) -> str:
        session = self.session(session_id) or {}
        rows = self._history_rows(session_id, int(session.get("compacted_until_id") or 0))
        if len(rows) <= keep_recent:
            return str(session.get("summary") or "")
        old = rows[:-keep_recent]
        lines = []
        for row in old:
            text = re.sub(r"\s+", " ", str(row.get("content") or "")).strip()
            if text:
                lines.append(f"{'用户' if row['role'] == 'user' else '助手'}：{text[:140]}")
        previous = str(session.get("summary") or "").strip()
        combined = (previous + "\n" + "\n".join(lines)).strip()
        if len(combined) > 6000:
            combined = combined[-6000:]
        execute(
            "UPDATE sessions SET summary = ?, compacted_until_id = ? WHERE id = ?",
            (combined, int(old[-1]["id"]), session_id),
        )
        return combined

    def history_messages(self, session_id: str, limit: int = 200) -> list[dict[str, Any]]:
        session = self.session(session_id) or {}
        rows = self._history_rows(session_id, int(session.get("compacted_until_id") or 0))
        total = sum(len(str(row.get("content") or "")) for row in rows)
        if total > MAX_CONTEXT_CHARS * 0.75:
            self.compact(session_id, keep_recent=6)
            session = self.session(session_id) or {}
            rows = self._history_rows(session_id, int(session.get("compacted_until_id") or 0))
        messages = [
            {"role": row["role"], "content": str(row.get("content") or "")[:6000]}
            for row in rows
        ]
        if len(messages) > limit:
            messages = messages[-limit:]
        return messages

    def clear(self, session_id: str) -> None:
        execute("DELETE FROM messages WHERE session_id = ?", (session_id,))
        execute("DELETE FROM sessions WHERE id = ?", (session_id,))

    def stats(self, session_id: str) -> dict[str, Any]:
        rows = query_all("SELECT COUNT(*) AS total FROM messages WHERE session_id = ?", (session_id,))
        total = int(rows[0]["total"]) if rows else 0
        session = self.session(session_id) or {}
        return {"session_id": session_id, "messages": total, "mode": session.get("mode"), "summary": session.get("summary", "")}