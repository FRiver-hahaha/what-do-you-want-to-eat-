from __future__ import annotations

import shutil
from pathlib import Path
from typing import Any

from agent.config import MODES_CONFIG, SKILLS_USER_DIR
from agent.data.db import execute, now_iso, query_all, query_one
from agent.skills.loader import SkillMeta, SkillValidationError, load_builtin_skills, load_skill_file, parse_skill

BUILTIN_DIR = Path(__file__).resolve().parent / "builtin"

TONE_CATEGORY = "tone"
DEFAULT_TONE = "normal"
TONE_ALIASES = {
    "normal": "tone-normal",
    "gentle": "tone-gentle",
    "efficient": "tone-efficient",
}


class SkillsRegistry:
    def __init__(self, tools_registry: Any, market: Any = None) -> None:
        self.tools_registry = tools_registry
        self.market = market
        self._skills: dict[str, SkillMeta] = {}
        self.reload()

    def reload(self) -> None:
        skills: dict[str, SkillMeta] = {}
        for skill in load_builtin_skills(BUILTIN_DIR):
            skills[skill.name] = skill
        if SKILLS_USER_DIR.exists():
            for path in sorted(SKILLS_USER_DIR.glob("*/SKILL.md")):
                try:
                    skill = load_skill_file(path, source="user")
                except (SkillValidationError, OSError):
                    continue
                skills[skill.name] = skill
        self._skills = skills

    def all(self) -> list[SkillMeta]:
        return list(self._skills.values())

    def get(self, skill_id: str) -> SkillMeta | None:
        return self._skills.get(skill_id)

    def is_enabled(self, skill_id: str, user_id: str) -> bool:
        skill = self._skills.get(skill_id)
        if skill is None:
            return False
        if skill.source == "builtin":
            return True
        row = query_one("SELECT enabled FROM installed_skills WHERE user_id = ? AND skill_id = ?", (user_id, skill_id))
        return bool(row and row.get("enabled"))

    def list_for_mode(self, mode: str, user_id: str = "") -> list[dict[str, Any]]:
        result = []
        for skill in self._skills.values():
            if mode not in skill.modes or skill.category == TONE_CATEGORY:
                continue
            item = skill.to_dict()
            item["enabled"] = self.is_enabled(skill.name, user_id) if user_id else True
            result.append(item)
        return result

    def match(self, text: str, mode: str, limit: int = 2) -> list[SkillMeta]:
        lowered = text.lower()
        scored: list[tuple[int, int, SkillMeta]] = []
        for skill in self._skills.values():
            if mode not in skill.modes or skill.category == TONE_CATEGORY:
                continue
            score = 0
            for trigger in skill.triggers:
                if trigger and trigger.lower() in lowered:
                    score += 3
            for tag in skill.tags:
                if tag and tag.lower() in lowered:
                    score += 2
            for token in (skill.name, skill.display_name):
                if token and token.lower() in lowered:
                    score += 2
            if skill.category and skill.category.lower() in lowered:
                score += 1
            if score >= 2:
                scored.append((score, skill.priority, skill))
        scored.sort(key=lambda item: (item[0], item[1]), reverse=True)
        return [skill for _, _, skill in scored[:limit]]

    def resolve_tone(self, tone_id: str | None) -> SkillMeta | None:
        key = str(tone_id or DEFAULT_TONE).strip().lower()
        skill = self._skills.get(TONE_ALIASES.get(key, key))
        if skill is None or skill.category != TONE_CATEGORY:
            skill = self._skills.get(TONE_ALIASES[DEFAULT_TONE])
        return skill

    def tones(self) -> list[dict[str, Any]]:
        result: list[dict[str, Any]] = []
        for key, skill_name in TONE_ALIASES.items():
            skill = self._skills.get(skill_name)
            if skill is None:
                continue
            result.append(
                {
                    "id": key,
                    "skill": skill.name,
                    "name": skill.display_name,
                    "description": skill.description,
                }
            )
        return result

    def resolve_for_request(self, skill_id: str | None, text: str, mode: str, user_id: str) -> SkillMeta | None:
        if skill_id:
            skill = self.get(skill_id)
            if skill and mode in skill.modes and self.is_enabled(skill.name, user_id):
                return skill
        for skill in self.match(text, mode):
            if self.is_enabled(skill.name, user_id):
                return skill
        return None

    def allowed_tool_names(self, skill: SkillMeta, mode: str) -> set[str]:
        available = {spec.name for spec in self.tools_registry.specs_for_mode(mode)}
        if not skill.allowed_tools:
            return available
        return available.intersection(skill.allowed_tools)

    def tools_for_skill(self, skill: SkillMeta | None, mode: str) -> list[dict[str, Any]]:
        specs = self.tools_registry.specs_for_mode(mode)
        if skill is not None and skill.allowed_tools:
            allowed = self.allowed_tool_names(skill, mode)
            specs = [spec for spec in specs if spec.name in allowed]
        return [spec.openai_schema() for spec in specs]

    def prompt_for(self, skill: SkillMeta, mode: str) -> str:
        names = sorted(self.allowed_tool_names(skill, mode))
        tools_line = "、".join(names) if names else "无"
        return (
            f'<skill name="{skill.name}" version="{skill.version}">\n'
            f"{skill.body}\n"
            f"本技能允许使用的工具：{tools_line}\n"
            f"</skill>"
        )

    def tone_prompt_for(self, skill: SkillMeta | None) -> str:
        if skill is None:
            return ""
        return f'<tone name="{skill.name}" version="{skill.version}">\n{skill.body}\n</tone>'

    def set_enabled(self, skill_id: str, user_id: str, enabled: bool) -> tuple[bool, str]:
        skill = self.get(skill_id)
        if skill is None:
            return False, f"技能 {skill_id} 不存在"
        if skill.source == "builtin":
            return True, "内置技能默认启用，无需修改"
        execute(
            "INSERT INTO installed_skills (user_id, skill_id, version, enabled, source, path, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(user_id, skill_id) DO UPDATE SET enabled = excluded.enabled",
            (user_id, skill_id, skill.version, 1 if enabled else 0, skill.source, skill.path, now_iso()),
        )
        return True, f"技能 {skill.display_name} 已{'启用' if enabled else '停用'}"

    def resolve_experts(
        self,
        skill_ids: list[str],
        fallback_skill_id: str | None,
        text: str,
        mode: str,
        user_id: str,
    ) -> list[SkillMeta]:
        result: list[SkillMeta] = []
        seen: set[str] = set()
        for raw in skill_ids or []:
            skill = self.get(str(raw))
            if skill is None or mode not in skill.modes or skill.category == TONE_CATEGORY:
                continue
            if not self.is_enabled(skill.name, user_id) or skill.name in seen:
                continue
            seen.add(skill.name)
            result.append(skill)
        if not result:
            skill = self.resolve_for_request(fallback_skill_id, text, mode, user_id)
            if skill is not None:
                result.append(skill)
        return result

    def combo_prompt_for(self, skills: list[SkillMeta], mode: str) -> str:
        prompts = [self.prompt_for(skill, mode) for skill in skills]
        return "\n\n".join(prompt for prompt in prompts if prompt)

    def allowed_tool_names_many(self, skills: list[SkillMeta], mode: str) -> set[str]:
        available = {spec.name for spec in self.tools_registry.specs_for_mode(mode)}
        if not skills:
            return available
        allowed: set[str] = set()
        for skill in skills:
            if not skill.allowed_tools:
                return available
            allowed |= available.intersection(skill.allowed_tools)
        return allowed or available

    def tools_for_skills(self, skills: list[SkillMeta], mode: str) -> list[dict[str, Any]]:
        specs = self.tools_registry.specs_for_mode(mode)
        names = self.allowed_tool_names_many(skills, mode)
        return [spec.openai_schema() for spec in specs if spec.name in names]

    def install_from_market(self, skill_id: str, user_id: str) -> tuple[bool, str, dict[str, Any]]:
        if self.market is None:
            return False, "Skill 市场未初始化", {}
        ok, message, meta = self.market.install(skill_id, user_id)
        if ok:
            self.reload()
        return ok, message, meta

    def install_content(self, content: str, user_id: str, source: str = "user") -> tuple[bool, str, dict[str, Any]]:
        try:
            skill = parse_skill(content, source=source)
        except SkillValidationError as exc:
            return False, f"技能校验失败：{exc}", {}
        target = SKILLS_USER_DIR / skill.name
        target.mkdir(parents=True, exist_ok=True)
        path = target / "SKILL.md"
        path.write_text(content, encoding="utf-8")
        skill.path = str(path)
        skill.source = "user"
        execute(
            "INSERT INTO installed_skills (user_id, skill_id, version, enabled, source, path, created_at) "
            "VALUES (?, ?, ?, 1, ?, ?, ?) "
            "ON CONFLICT(user_id, skill_id) DO UPDATE SET version = excluded.version, path = excluded.path, enabled = 1",
            (user_id, skill.name, skill.version, source, str(path), now_iso()),
        )
        self.reload()
        return True, f"已导入技能 {skill.display_name}", skill.to_dict()

    def import_text(self, content: str, user_id: str) -> tuple[bool, str, dict[str, Any]]:
        return self.install_content(content, user_id, source="local")

    def installed(self, user_id: str) -> list[SkillMeta]:
        rows = query_all("SELECT skill_id FROM installed_skills WHERE user_id = ? ORDER BY id DESC", (user_id,))
        result: list[SkillMeta] = []
        for row in rows:
            skill = self._skills.get(str(row.get("skill_id") or ""))
            if skill is not None:
                result.append(skill)
        return result

    def uninstall(self, skill_id: str, user_id: str) -> tuple[bool, str]:
        skill = self.get(skill_id)
        if skill is None:
            return False, f"技能 {skill_id} 不存在"
        if skill.source == "builtin":
            return False, "内置技能不能卸载"
        path = Path(skill.path) if skill.path else SKILLS_USER_DIR / skill.name
        try:
            resolved = path.resolve()
            if resolved.is_relative_to(SKILLS_USER_DIR.resolve()):
                if resolved.is_dir():
                    shutil.rmtree(resolved)
                elif resolved.exists():
                    resolved.unlink()
        except (OSError, ValueError):
            pass
        execute("DELETE FROM installed_skills WHERE user_id = ? AND skill_id = ?", (user_id, skill_id))
        self.reload()
        return True, f"已卸载技能 {skill.display_name}"

    def security_summary(self) -> dict[str, Any]:
        modes = {}
        for mode_id, cfg in MODES_CONFIG.items():
            modes[mode_id] = {
                "allow_local_tools": cfg.get("allow_local_tools"),
                "allow_write": cfg.get("allow_write"),
                "allow_skill_install": cfg.get("allow_skill_install"),
            }
        return {"modes": modes, "user_skill_dir": str(SKILLS_USER_DIR)}