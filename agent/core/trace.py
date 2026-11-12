from __future__ import annotations

import json
import time
import uuid
from contextlib import contextmanager
from typing import Any, Iterator

from agent.data.db import execute, now_iso, query_all, query_one


class TraceRecorder:
    def start(self, thread_id: str, user_id: str, model: str = "") -> str:
        trace_id = f"tr_{uuid.uuid4().hex[:14]}"
        execute(
            "INSERT INTO traces (id, thread_id, user_id, status, model, tokens, started_at) VALUES (?, ?, ?, 'running', ?, 0, ?)",
            (trace_id, thread_id, user_id, model, now_iso()),
        )
        return trace_id

    @contextmanager
    def span(self, trace_id: str, name: str, kind: str = "internal", detail: dict[str, Any] | None = None) -> Iterator[dict[str, Any]]:
        span_id = f"sp_{uuid.uuid4().hex[:10]}"
        started = time.monotonic()
        record = {"id": span_id, "name": name, "kind": kind, "status": "ok", "error": ""}
        try:
            yield record
        except Exception as exc:
            record["status"] = "error"
            record["error"] = str(exc)[:300]
            raise
        finally:
            duration = int((time.monotonic() - started) * 1000)
            payload = dict(detail or {})
            payload["error"] = record["error"]
            execute(
                "INSERT INTO spans (id, trace_id, name, kind, status, duration_ms, detail, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (span_id, trace_id, name, kind, record["status"], duration, json.dumps(payload, ensure_ascii=False)[:2000], now_iso()),
            )

    def finish(self, trace_id: str, status: str = "ok", tokens: int = 0, error: str = "") -> None:
        execute(
            "UPDATE traces SET status = ?, tokens = ?, error = ?, finished_at = ? WHERE id = ?",
            (status, int(tokens or 0), error[:500], now_iso(), trace_id),
        )

    def get(self, trace_id: str) -> dict[str, Any] | None:
        trace = query_one("SELECT * FROM traces WHERE id = ?", (trace_id,))
        if trace is None:
            return None
        trace["spans"] = query_all("SELECT * FROM spans WHERE trace_id = ? ORDER BY created_at", (trace_id,))
        return trace

    def list(self, limit: int = 20) -> list[dict[str, Any]]:
        return query_all("SELECT * FROM traces ORDER BY started_at DESC LIMIT ?", (max(1, min(limit, 100)),))


TRACER = TraceRecorder()