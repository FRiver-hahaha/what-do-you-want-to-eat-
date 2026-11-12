from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any

from agent.config import RAG_DB, RAG_DIR

SCHEMA = """
CREATE TABLE IF NOT EXISTS rag_docs (
  id TEXT PRIMARY KEY,
  title TEXT NOT NULL,
  source TEXT NOT NULL,
  meta TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS rag_chunks (
  id TEXT PRIMARY KEY,
  doc_id TEXT NOT NULL,
  title TEXT NOT NULL,
  ordinal INTEGER NOT NULL,
  text TEXT NOT NULL,
  tokens TEXT NOT NULL DEFAULT '[]',
  embedding TEXT NOT NULL DEFAULT '[]',
  meta TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX IF NOT EXISTS idx_rag_chunks_doc ON rag_chunks(doc_id);
"""


class RAGStore:
    def __init__(self, db_path: Path | None = None) -> None:
        self.db_path = Path(db_path or RAG_DB)

    def _connect(self) -> sqlite3.Connection:
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(self.db_path), timeout=10)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA journal_mode=WAL")
        return conn

    def init(self) -> None:
        conn = self._connect()
        try:
            conn.executescript(SCHEMA)
            conn.commit()
        finally:
            conn.close()

    def upsert_doc(
        self,
        doc_id: str,
        title: str,
        source: str,
        meta: dict[str, Any],
        chunks: list[dict[str, Any]],
        created_at: str,
    ) -> None:
        conn = self._connect()
        try:
            conn.execute(
                "INSERT INTO rag_docs (id, title, source, meta, created_at) VALUES (?, ?, ?, ?, ?) "
                "ON CONFLICT(id) DO UPDATE SET title = excluded.title, source = excluded.source, meta = excluded.meta",
                (doc_id, title, source, json.dumps(meta, ensure_ascii=False), created_at),
            )
            conn.execute("DELETE FROM rag_chunks WHERE doc_id = ?", (doc_id,))
            conn.executemany(
                "INSERT INTO rag_chunks (id, doc_id, title, ordinal, text, tokens, embedding, meta) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                [
                    (
                        chunk["id"],
                        doc_id,
                        title,
                        chunk["ordinal"],
                        chunk["text"],
                        json.dumps(chunk["tokens"], ensure_ascii=False),
                        json.dumps(chunk["embedding"]),
                        json.dumps(meta, ensure_ascii=False),
                    )
                    for chunk in chunks
                ],
            )
            conn.commit()
        finally:
            conn.close()

    def delete_doc(self, doc_id: str) -> None:
        conn = self._connect()
        try:
            conn.execute("DELETE FROM rag_chunks WHERE doc_id = ?", (doc_id,))
            conn.execute("DELETE FROM rag_docs WHERE id = ?", (doc_id,))
            conn.commit()
        finally:
            conn.close()

    def chunks(self) -> list[dict[str, Any]]:
        conn = self._connect()
        try:
            rows = conn.execute("SELECT * FROM rag_chunks").fetchall()
        finally:
            conn.close()
        result = []
        for row in rows:
            item = dict(row)
            try:
                item["tokens"] = json.loads(item.get("tokens") or "[]")
            except json.JSONDecodeError:
                item["tokens"] = []
            try:
                item["embedding"] = json.loads(item.get("embedding") or "[]")
            except json.JSONDecodeError:
                item["embedding"] = []
            result.append(item)
        return result

    def docs(self) -> list[dict[str, Any]]:
        conn = self._connect()
        try:
            rows = conn.execute("SELECT id, title, source, created_at FROM rag_docs ORDER BY id").fetchall()
        finally:
            conn.close()
        return [dict(row) for row in rows]

    def count(self) -> int:
        conn = self._connect()
        try:
            row = conn.execute("SELECT COUNT(*) AS total FROM rag_chunks").fetchone()
            return int(row["total"]) if row else 0
        finally:
            conn.close()


def knowledge_dir() -> Path:
    return Path(__file__).resolve().parent / "knowledge"


def ensure_knowledge_dir() -> Path:
    path = knowledge_dir()
    path.mkdir(parents=True, exist_ok=True)
    return path


__all__ = ["RAGStore", "knowledge_dir", "ensure_knowledge_dir", "RAG_DIR"]