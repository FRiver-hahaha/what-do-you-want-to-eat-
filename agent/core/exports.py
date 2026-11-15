from __future__ import annotations

import re
import unicodedata
from datetime import datetime
from pathlib import Path
from typing import Any
from urllib.parse import quote

from agent.config import DATA_DIR

EXPORT_DIR = DATA_DIR / "exports"
SAFE_NAME = re.compile(r"^[\w.\-\u4e00-\u9fff]+$")
UNSAFE_CHARS = re.compile(r"[^\w.\-\u4e00-\u9fff]+")


def ensure_export_dir() -> Path:
    EXPORT_DIR.mkdir(parents=True, exist_ok=True)
    return EXPORT_DIR


def slugify(value: str, limit: int = 40) -> str:
    text = unicodedata.normalize("NFKC", str(value or "")).strip()
    text = UNSAFE_CHARS.sub("-", text).strip(".-")
    return text[:limit] or "export"


def download_url(filename: str) -> str:
    return f"/api/exports/{quote(filename)}"


def write_export(base_name: str, content: str, suffix: str = ".md") -> dict[str, Any]:
    directory = ensure_export_dir()
    now = datetime.now()
    stamp = f"{now.strftime('%Y%m%d-%H%M%S')}-{now.microsecond // 1000:03d}"
    extension = suffix if suffix.startswith(".") else f".{suffix}"
    filename = f"{slugify(base_name)}-{stamp}{extension}"
    path = directory / filename
    payload = str(content or "").encode("utf-8")
    path.write_bytes(payload)
    return {
        "filename": filename,
        "path": str(path),
        "bytes": len(payload),
        "format": extension.lstrip("."),
        "download_url": download_url(filename),
    }


def resolve_export(filename: str) -> Path | None:
    name = Path(str(filename or "")).name
    if not name or not SAFE_NAME.fullmatch(name):
        return None
    root = ensure_export_dir().resolve()
    path = (root / name).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        return None
    return path


__all__ = ["EXPORT_DIR", "download_url", "ensure_export_dir", "resolve_export", "slugify", "write_export"]