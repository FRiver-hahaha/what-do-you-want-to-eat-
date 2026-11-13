from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterator, Sequence

from agent.config import APP_DB, ensure_dirs

SCHEMA_PATH = Path(__file__).resolve().parent / "schema.sql"


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def connect() -> sqlite3.Connection:
    ensure_dirs()
    conn = sqlite3.connect(str(APP_DB), timeout=10)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    return conn


@contextmanager
def transaction() -> Iterator[sqlite3.Connection]:
    conn = connect()
    try:
        yield conn
        conn.commit()
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()


def init_db() -> None:
    sql = SCHEMA_PATH.read_text(encoding="utf-8")
    with transaction() as conn:
        conn.executescript(sql)
        columns = {row["name"] for row in conn.execute("PRAGMA table_info(sessions)").fetchall()}
        if "compacted_until_id" not in columns:
            conn.execute("ALTER TABLE sessions ADD COLUMN compacted_until_id INTEGER NOT NULL DEFAULT 0")


def execute(sql: str, params: Sequence[Any] = ()) -> int:
    with transaction() as conn:
        cursor = conn.execute(sql, params)
        return int(cursor.lastrowid or 0)


def executemany(sql: str, rows: Sequence[Sequence[Any]]) -> None:
    with transaction() as conn:
        conn.executemany(sql, rows)


def query_all(sql: str, params: Sequence[Any] = ()) -> list[dict[str, Any]]:
    conn = connect()
    try:
        rows = conn.execute(sql, params).fetchall()
        return [dict(row) for row in rows]
    finally:
        conn.close()


def query_one(sql: str, params: Sequence[Any] = ()) -> dict[str, Any] | None:
    conn = connect()
    try:
        row = conn.execute(sql, params).fetchone()
        return dict(row) if row else None
    finally:
        conn.close()