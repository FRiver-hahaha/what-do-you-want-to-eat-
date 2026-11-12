from __future__ import annotations

import json
import secrets
from datetime import datetime, timezone
from typing import Any

from agent.data.db import execute, query_all, query_one
from agent.skills.loader import SkillValidationError, parse_skill


class SkillMarket:
    def __init__(self, skills_registry: Any, service: Any = None) -> None:
        self.skills = skills_registry
        self.service = service

    def list(self, search: str = "", category: str = "", limit: int = 50) -> list[dict[str, Any]]:
        sql = "SELECT id, owner, name, display_name, description, category, tags, version, downloads, rating, created_at FROM market_skills WHERE status = 'published'"
        params: list[Any] = []
        if search:
            sql += " AND (name LIKE ? OR display_name LIKE ? OR description LIKE ? OR tags LIKE ?)"
            pattern = f"%{search}%"
            params.extend([pattern, pattern, pattern, pattern])
        if category:
            sql += " AND category = ?"
            params.append(category)
        sql += " ORDER BY downloads DESC, updated_at DESC LIMIT ?"
        params.append(max(1, min(limit, 200)))
        rows = query_all(sql, tuple(params))
        for row in rows:
            try:
                row["tags"] = json.loads(row.get("tags") or "[]")
            except json.JSONDecodeError:
                row["tags"] = []
        return rows

    def get(self, skill_id: str) -> dict[str, Any] | None:
        row = query_one("SELECT * FROM market_skills WHERE id = ?", (skill_id,))
        if row:
            try:
                row["tags"] = json.loads(row.get("tags") or "[]")
            except json.JSONDecodeError:
                row["tags"] = []
        return row

    def versions(self, skill_id: str) -> list[dict[str, Any]]:
        return query_all(
            "SELECT version, changelog, created_at FROM market_versions WHERE skill_id = ? ORDER BY id DESC",
            (skill_id,),
        )

    def publish(self, content: str, owner: str, changelog: str = "") -> tuple[bool, str, dict[str, Any]]:
        try:
            skill = parse_skill(content, source="market")
        except SkillValidationError as exc:
            return False, f"技能校验失败：{exc}", {}

        existing = self.get(skill.name)
        if existing and existing.get("owner") != owner:
            return False, f"技能名 {skill.name} 已被其他作者占用，请更换 name", {}

        version = skill.version
        if existing and existing.get("version") == version:
            parts = version.split(".")
            parts[-1] = str(int(parts[-1]) + 1) if parts[-1].isdigit() else "1"
            version = ".".join(parts)
        now = datetime.now(timezone.utc).isoformat(timespec="seconds")

        execute(
            "INSERT INTO market_skills (id, owner, name, display_name, description, category, tags, version, content, downloads, rating, status, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, 'published', ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET display_name = excluded.display_name, description = excluded.description, "
            "category = excluded.category, tags = excluded.tags, version = excluded.version, content = excluded.content, updated_at = excluded.updated_at",
            (
                skill.name,
                owner,
                skill.name,
                skill.display_name,
                skill.description,
                skill.category,
                json.dumps(skill.tags, ensure_ascii=False),
                version,
                content,
                now,
                now,
            ),
        )
        execute(
            "INSERT OR IGNORE INTO market_versions (skill_id, version, content, changelog, created_at) VALUES (?, ?, ?, ?, ?)",
            (skill.name, version, content, changelog, now),
        )
        meta = skill.to_dict()
        meta.update({"version": version, "owner": owner})
        return True, f"已发布技能 {skill.display_name} v{version}", meta

    def install(self, skill_id: str, user_id: str) -> tuple[bool, str, dict[str, Any]]:
        row = self.get(skill_id)
        if not row:
            return False, f"市场中不存在技能 {skill_id}", {}
        content = str(row.get("content") or "")
        ok, message, meta = self.skills.install_content(content, user_id, source="market")
        if ok:
            execute("UPDATE market_skills SET downloads = downloads + 1 WHERE id = ?", (skill_id,))
        return ok, message, meta

    def share(
        self,
        skill_id: str,
        from_user: str,
        to_users: list[str],
        allow_self: bool = False,
    ) -> tuple[bool, str, list[dict[str, Any]]]:
        row = self.get(skill_id)
        if not row:
            return False, f"技能 {skill_id} 不存在", []
        if not to_users:
            return False, "至少选择一位好友", []

        friend_ids: set[str] = set()
        if self.service is not None:
            friends = self.service.get_friends(from_user).get("friends") or []
            friend_ids = {str(item.get("id")) for item in friends}

        invalid = [user for user in to_users if user == from_user and not allow_self]
        invalid.extend(user for user in to_users if user not in friend_ids and user != from_user)
        if invalid:
            return False, "只能分享给已建立好友关系的用户：" + "、".join(sorted(set(invalid))), []

        shares = []
        now = datetime.now(timezone.utc).isoformat(timespec="seconds")
        for user in to_users:
            code = secrets.token_hex(6)
            execute(
                "INSERT INTO skill_shares (skill_id, from_user, to_user, code, created_at) VALUES (?, ?, ?, ?, ?)",
                (skill_id, from_user, user, code, now),
            )
            shares.append({"skill_id": skill_id, "to_user": user, "code": code})
        return True, f"已分享给 {len(shares)} 位好友", shares

    def shared_with_me(self, user_id: str) -> list[dict[str, Any]]:
        return query_all(
            "SELECT s.skill_id, s.from_user, s.code, s.created_at, m.display_name, m.description "
            "FROM skill_shares s LEFT JOIN market_skills m ON m.id = s.skill_id "
            "WHERE s.to_user = ? ORDER BY s.id DESC",
            (user_id,),
        )