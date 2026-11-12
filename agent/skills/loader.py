from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

NAME_PATTERN = re.compile(r"^[a-z0-9][a-z0-9_-]{1,63}$")
MAX_SKILL_BYTES = 64 * 1024


class SkillValidationError(ValueError):
    pass


@dataclass
class SkillMeta:
    name: str
    display_name: str
    description: str
    version: str = "1.0.0"
    author: str = "builtin"
    category: str = "general"
    tags: list[str] = field(default_factory=list)
    allowed_tools: list[str] = field(default_factory=list)
    permissions: list[str] = field(default_factory=list)
    modes: list[str] = field(default_factory=lambda: ["qa", "work"])
    triggers: list[str] = field(default_factory=list)
    priority: int = 10
    body: str = ""
    source: str = "builtin"
    path: str = ""

    def to_dict(self) -> dict[str, object]:
        return {
            "id": self.name,
            "name": self.name,
            "display_name": self.display_name,
            "description": self.description,
            "version": self.version,
            "author": self.author,
            "category": self.category,
            "tags": self.tags,
            "allowed_tools": self.allowed_tools,
            "permissions": self.permissions,
            "modes": self.modes,
            "triggers": self.triggers,
            "priority": self.priority,
            "source": self.source,
            "path": self.path,
        }


def _clean(value: str) -> str:
    return value.strip().strip('"').strip("'")


def _parse_list(raw: str) -> list[str]:
    text = raw.strip()
    if not text:
        return []
    if text.startswith("[") and text.endswith("]"):
        text = text[1:-1]
        return [_clean(item) for item in text.split(",") if _clean(item)]
    return [_clean(item) for item in text.split(",") if _clean(item)]


def _parse_frontmatter(text: str) -> tuple[dict[str, object], str]:
    lines = text.splitlines()
    if not lines or lines[0].strip() != "---":
        raise SkillValidationError("SKILL.md 必须包含 frontmatter")
    end_index = -1
    for index in range(1, len(lines)):
        if lines[index].strip() == "---":
            end_index = index
            break
    if end_index < 0:
        raise SkillValidationError("frontmatter 未正确闭合")

    meta: dict[str, object] = {}
    list_key: str | None = None
    for line in lines[1:end_index]:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped.startswith("- ") and list_key:
            current = meta.get(list_key)
            if isinstance(current, list):
                current.append(_clean(stripped[2:]))
            continue
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()
        if value == "":
            meta[key] = []
            list_key = key
            continue
        list_key = None
        if value.startswith("[") or "," in value:
            meta[key] = _parse_list(value)
        else:
            meta[key] = _clean(value)
    body = "\n".join(lines[end_index + 1:]).strip()
    return meta, body


def parse_skill(text: str, source: str = "builtin", path: str = "") -> SkillMeta:
    if len(text.encode("utf-8")) > MAX_SKILL_BYTES:
        raise SkillValidationError("SKILL.md 超过 64KB 限制")
    meta, body = _parse_frontmatter(text)
    name = str(meta.get("name") or "").strip().lower()
    if not NAME_PATTERN.match(name):
        raise SkillValidationError("name 必须是小写字母/数字/下划线/短横线，长度 2-64")
    description = str(meta.get("description") or "").strip()
    if not description:
        raise SkillValidationError("缺少 description")
    if not body:
        raise SkillValidationError("技能正文不能为空")

    def as_list(key: str, default: list[str] | None = None) -> list[str]:
        value = meta.get(key)
        if isinstance(value, list):
            return [str(item) for item in value]
        if isinstance(value, str) and value:
            return [value]
        return list(default or [])

    return SkillMeta(
        name=name,
        display_name=str(meta.get("display_name") or meta.get("title") or name),
        description=description,
        version=str(meta.get("version") or "1.0.0"),
        author=str(meta.get("author") or "builtin"),
        category=str(meta.get("category") or "general"),
        tags=as_list("tags"),
        allowed_tools=as_list("allowed-tools") or as_list("allowed_tools"),
        permissions=as_list("permissions"),
        modes=as_list("modes", ["qa", "work"]),
        triggers=as_list("triggers"),
        priority=int(str(meta.get("priority") or "10").strip() or "10"),
        body=body,
        source=source,
        path=path,
    )


def load_skill_file(path: Path, source: str = "builtin") -> SkillMeta:
    text = path.read_text(encoding="utf-8")
    return parse_skill(text, source=source, path=str(path))


def load_builtin_skills(directory: Path) -> list[SkillMeta]:
    skills: list[SkillMeta] = []
    if not directory.exists():
        return skills
    for path in sorted(directory.glob("*/SKILL.md")):
        try:
            skills.append(load_skill_file(path, source="builtin"))
        except (SkillValidationError, OSError):
            continue
    return skills