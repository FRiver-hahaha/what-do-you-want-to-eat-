from __future__ import annotations

import json
import math
import re
import threading
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from agent.llm.gateway import HashEmbedder
from agent.rag.store import RAGStore, knowledge_dir

STOPWORDS = {"的", "了", "是", "在", "和", "与", "就", "都", "也", "很", "还", "有", "吗", "呢", "吧", "啊", "这", "那", "一个", "什么", "怎么"}
LATIN_PATTERN = re.compile(r"[a-z0-9]+")
CJK_PATTERN = re.compile(r"[\u4e00-\u9fff]")
SEED_LOCK = threading.Lock()


def tokenize(text: str) -> list[str]:
    lowered = text.lower()
    tokens = LATIN_PATTERN.findall(lowered)
    chars = CJK_PATTERN.findall(lowered)
    tokens.extend(chars[index] + chars[index + 1] for index in range(max(0, len(chars) - 1)))
    tokens.extend(chars)
    return [token for token in tokens if token not in STOPWORDS]


def split_chunks(text: str, max_chars: int = 460, overlap: int = 60) -> list[str]:
    sections = [section.strip() for section in re.split(r"(?=\n## )", text) if section.strip()]
    chunks: list[str] = []
    for section in sections:
        if len(section) <= max_chars:
            chunks.append(section)
            continue
        buffer = ""
        for paragraph in [item.strip() for item in section.split("\n\n") if item.strip()]:
            if buffer and len(buffer) + len(paragraph) > max_chars:
                chunks.append(buffer.strip())
                buffer = buffer[-overlap:] + "\n" + paragraph
            else:
                buffer = (buffer + "\n" + paragraph).strip()
        if buffer.strip():
            chunks.append(buffer.strip())
    return chunks


class KnowledgeBase:
    def __init__(self, store: RAGStore | None = None) -> None:
        self.store = store or RAGStore()
        self.embedder = HashEmbedder()
        self.store.init()

    def stats(self) -> dict[str, Any]:
        return {"documents": len(self.store.docs()), "chunks": self.store.count(), "path": str(self.store.db_path)}

    def ensure_seeded(self) -> None:
        if self.store.count() > 0:
            return
        with SEED_LOCK:
            if self.store.count() > 0:
                return
            self.ingest_dir(knowledge_dir())

    def ingest_dir(self, directory: Path | str) -> dict[str, Any]:
        path = Path(directory)
        if not path.exists():
            return {"ingested": 0, "skipped": "目录不存在"}
        count = 0
        for file in sorted(path.glob("*.md")):
            text = file.read_text(encoding="utf-8")
            self.ingest_text(file.stem, self._title_of(text, file.stem), text, source=str(file))
            count += 1
        return {"ingested": count, "docs": [doc["id"] for doc in self.store.docs()]}

    def ingest_text(
        self,
        doc_id: str,
        title: str,
        text: str,
        source: str = "inline",
        meta: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        pieces = split_chunks(text)
        embeddings = self.embedder.embed(pieces)
        chunks = []
        for index, piece in enumerate(pieces):
            chunks.append(
                {
                    "id": f"{doc_id}#{index}",
                    "ordinal": index,
                    "text": piece,
                    "tokens": tokenize(piece),
                    "embedding": embeddings[index],
                }
            )
        created_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
        self.store.upsert_doc(doc_id, title, source, meta or {}, chunks, created_at)
        return {"doc_id": doc_id, "title": title, "chunks": len(chunks)}

    def search(self, query: str, top_k: int = 4, min_score: float = 0.02) -> list[dict[str, Any]]:
        self.ensure_seeded()
        cleaned = query.strip()
        if not cleaned:
            return []
        chunks = self.store.chunks()
        if not chunks:
            return []
        query_tokens = tokenize(cleaned)
        query_vector = self.embedder.embed([cleaned])[0]
        document_frequency: Counter[str] = Counter()
        for chunk in chunks:
            for token in set(chunk["tokens"]):
                document_frequency[token] += 1

        total = len(chunks)
        average_length = sum(len(chunk["tokens"]) for chunk in chunks) / max(1, total)
        k1, b = 1.5, 0.75
        scored: list[tuple[float, dict[str, Any]]] = []
        for chunk in chunks:
            frequencies = Counter(chunk["tokens"])
            length = max(1, len(chunk["tokens"]))
            bm25 = 0.0
            for token in query_tokens:
                frequency = frequencies.get(token, 0)
                if frequency == 0:
                    continue
                df = document_frequency.get(token, 0)
                idf = math.log(1 + (total - df + 0.5) / (df + 0.5))
                bm25 += idf * (frequency * (k1 + 1)) / (frequency + k1 * (1 - b + b * length / average_length))
            cosine = self._cosine(query_vector, chunk["embedding"])
            scored.append((0.72 * bm25 + 0.28 * cosine, chunk))

        if not scored:
            return []
        best = max(score for score, _ in scored) or 1.0
        results = []
        for score, chunk in sorted(scored, key=lambda item: item[0], reverse=True):
            normalized = score / best
            if normalized < min_score:
                continue
            results.append(
                {
                    "id": chunk["id"],
                    "doc_id": chunk["doc_id"],
                    "title": chunk["title"],
                    "text": chunk["text"][:700],
                    "score": round(normalized, 4),
                    "source": chunk.get("meta") and json.loads(chunk["meta"]).get("source", "") or "",
                }
            )
            if len(results) >= max(1, top_k):
                break
        return results

    @staticmethod
    def _cosine(left: list[float], right: list[float]) -> float:
        if not left or not right or len(left) != len(right):
            return 0.0
        return float(sum(a * b for a, b in zip(left, right)))

    @staticmethod
    def _title_of(text: str, fallback: str) -> str:
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("# "):
                return stripped[2:].strip()
        return fallback