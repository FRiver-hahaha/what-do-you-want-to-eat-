#!/usr/bin/env python3
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {
    ".git",
    "build",
    ".codeartsdoer",
    "third_party",
    "chatroom-backend",
    "__pycache__",
}
SKIP_FILES = {
    ".env",
}

RULES = [
    ("protobuf", re.compile(r"\.proto\b|protobuf|MessagePack|messagepack|CBOR|cbor")),
    ("websocket", re.compile(r"websocket|WebSocket|ws://|wss://")),
    ("redis_pubsub", re.compile(r"\bPUBLISH\b|\bSUBSCRIBE\b|\bPSUBSCRIBE\b|redis[^\n;]{0,60}\.(?:publish|subscribe)\s*\(")),
    ("secret", re.compile(r"sk-[A-Za-z0-9]{16,}|AKIA[0-9A-Z]{16}")),
]

ALLOW = {
    "websocket": {"docs/protocol/v1.md", "tools/scan_repo.py", "AGENT_Linux.md", "docs/architecture-status.md"},
    "protobuf": {"tools/scan_repo.py", "docs/protocol/v1.md", "AGENT_Linux.md", "docs/architecture-status.md"},
    "redis_pubsub": {"tools/scan_repo.py", "docs/protocol/v1.md", "AGENT_Linux.md", "docs/architecture-status.md"},
    "secret": {"agent/tests/test_smoke.py"},
}


def masked(token: str) -> str:
    if len(token) <= 8:
        return "***"
    return token[:4] + "***"


def iter_files():
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT).as_posix()
        if any(rel == d or rel.startswith(d + "/") for d in SKIP_DIRS):
            continue
        if rel in SKIP_FILES or rel.startswith(".env."):
            if rel != ".env.example":
                continue
        if path.suffix.lower() in {".png", ".jpg", ".jpeg", ".gif", ".ico", ".pdf", ".zip", ".exe", ".dll", ".so", ".a", ".o", ".pyc"}:
            continue
        yield rel, path


def main() -> int:
    findings = []
    for rel, path in iter_files():
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for name, pattern in RULES:
            if rel in ALLOW.get(name, set()):
                continue
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                token = masked(match.group(0)) if name == "secret" else match.group(0)[:40]
                findings.append((name, rel, line, token))

    if findings:
        for name, rel, line, token in findings:
            print(f"{name}: {rel}:{line}: {token}")
        print(f"total={len(findings)}")
        return 1
    print("scan-ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())