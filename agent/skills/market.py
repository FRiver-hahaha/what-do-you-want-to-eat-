from __future__ import annotations

import json
import secrets
import urllib.parse
from datetime import datetime, timezone
from typing import Any

from agent.data.db import execute, query_all, query_one
from agent.skills.loader import SkillValidationError, parse_skill


SEED_SKILLS = [
    {
        "id": "medical-nutrition",
        "owner": "u_lin",
        "display_name": "医学营养顾问",
        "description": "面向血糖、血脂、尿酸等常见指标的饮食结构调整建议，不做诊断",
        "category": "health",
        "tags": ["医学", "指标", "慢病饮食"],
        "version": "1.2.0",
        "downloads": 1286,
        "rating": 4.8,
        "content": """---
name: medical-nutrition
display_name: 医学营养顾问
description: 面向血糖、血脂、尿酸等常见指标的饮食结构调整建议，不做诊断
version: 1.2.0
author: u_lin
category: health
tags: [医学, 指标, 慢病饮食]
allowed-tools: [get_health_profile, get_recent_meals, get_nutrition_gap, search_knowledge, search_foods]
permissions: [read:diet, read:knowledge]
modes: [qa, work]
triggers: [血糖, 血脂, 尿酸, 胆固醇, 指标, 体检, 医生]
priority: 14
---

# 医学营养顾问

## 工作步骤
- 先确认用户提到的指标与医生给出的范围，再读取档案与近 7 天记录。
- 只做饮食结构建议，不解读化验单、不调整用药、不判断病情。
- 推荐菜品时过滤过敏与忌口，并给出一餐的具体替换方案。

## 输出约束
- 开头声明这只是饮食参考，不能替代医生或营养师的诊疗意见。
- 建议控制在 3 条以内，每条对应一个可执行动作。
- 涉及明显异常指标时，建议携带记录就医。

## 失败分支
- 缺少指标范围时，只给通用饮食方向，不臆测数值。
- 工具失败时说明失败原因，不给未经验证的数字。
""",
    },
    {
        "id": "fitness-coach",
        "owner": "u_zhou",
        "display_name": "健身增肌教练",
        "description": "按训练日与体重目标安排蛋白、碳水和加餐时机",
        "category": "fitness",
        "tags": ["健身", "增肌", "训练", "蛋白"],
        "version": "1.4.1",
        "downloads": 2043,
        "rating": 4.9,
        "content": """---
name: fitness-coach
display_name: 健身增肌教练
description: 按训练日与体重目标安排蛋白、碳水和加餐时机
version: 1.4.1
author: u_zhou
category: fitness
tags: [健身, 增肌, 训练, 蛋白]
allowed-tools: [get_health_profile, get_recent_meals, get_nutrition_gap, get_menu, search_foods]
permissions: [read:diet]
modes: [qa, work]
triggers: [增肌, 训练, 健身, 蛋白粉, 碳水, 练后]
priority: 13
---

# 健身增肌教练

## 工作步骤
- 先问清训练日还是休息日，以及目标体重和当前体重。
- 读取档案与今日缺口，按每公斤体重 1.6–2.0g 蛋白估算当日目标。
- 给出练后 1 小时内的一餐或加餐，优先真实菜单中可点的菜。

## 输出约束
- 数字来自档案与工具结果，不凭印象给热量。
- 一次只给一个训练日方案，不堆一周计划。
- 不推荐蛋白粉以外的补剂，不做医疗承诺。

## 失败分支
- 缺少体重或训练信息时，先给通用比例并说明假设。
- 菜单读取失败时，用常见食材给替代方案。
""",
    },
    {
        "id": "fat-loss-companion",
        "owner": "u_chen",
        "display_name": "减脂陪跑",
        "description": "低压力减脂节奏管理，关注可持续而不是极低热量",
        "category": "nutrition",
        "tags": ["减脂", "节奏", "复盘"],
        "version": "1.1.0",
        "downloads": 987,
        "rating": 4.7,
        "content": """---
name: fat-loss-companion
display_name: 减脂陪跑
description: 低压力减脂节奏管理，关注可持续而不是极低热量
version: 1.1.0
author: u_chen
category: nutrition
tags: [减脂, 节奏, 复盘]
allowed-tools: [get_health_profile, get_recent_meals, get_nutrition_gap, get_weekly_stats, search_foods]
permissions: [read:diet]
modes: [qa, work]
triggers: [减脂, 减肥, 掉秤, 平台期, 热量, 体重]
priority: 12
---

# 减脂陪跑

## 工作步骤
- 先看近 7 天记录与周统计，再判断这是节奏问题还是记录缺口。
- 不主张极低热量，优先调整晚餐结构与饮水。
- 每次只给一个本周可执行的小调整。

## 输出约束
- 用趋势描述体重，不做单日数字对比。
- 不承诺具体掉秤速度。
- 食物只描述营养事实，不贴失败或放纵标签。

## 失败分支
- 记录不足时先补记录，再谈调整。
- 用户情绪明显低落时先共情，暂停减脂建议。
""",
    },
    {
        "id": "office-lunch-planner",
        "owner": "u_gao",
        "display_name": "工作日午餐规划",
        "description": "按办公区附近菜单和会议时间安排 15 分钟能吃完的午餐",
        "category": "planning",
        "tags": ["工作日", "午餐", "快手"],
        "version": "1.0.3",
        "downloads": 654,
        "rating": 4.6,
        "content": """---
name: office-lunch-planner
display_name: 工作日午餐规划
description: 按办公区附近菜单和会议时间安排 15 分钟能吃完的午餐
version: 1.0.3
author: u_gao
category: planning
tags: [工作日, 午餐, 快手]
allowed-tools: [get_health_profile, get_nutrition_gap, get_menu, search_foods]
permissions: [read:diet]
modes: [qa, work]
triggers: [工作餐, 午餐, 外卖, 会议, 快手, 便当]
priority: 11
---

# 工作日午餐规划

## 工作步骤
- 读取当前时段菜单，按 15 分钟内可吃完、可外带优先筛选。
- 先补今日缺口最大的两项营养，再考虑口味。
- 给出一个主选和一个明确说明的备选。

## 输出约束
- 只推荐菜单里真实存在的菜。
- 名称、价格、营养数字必须来自工具结果。
- 不推荐需要长时间排队或现做的菜。

## 失败分支
- 菜单为空时说明原因，并给便利店组合方案。
- 过敏原不确定时直接排除该菜。
""",
    },
]


class SkillMarket:
    def __init__(self, skills_registry: Any, service: Any = None) -> None:
        self.skills = skills_registry
        self.service = service

    def list(self, search: str = "", category: str = "", limit: int = 50) -> list[dict[str, Any]]:
        remote = self._remote_list(search, category, limit)
        if remote is not None:
            return remote
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
    def _remote_list(self, search: str, category: str, limit: int) -> list[dict[str, Any]] | None:
        if self.service is None or getattr(self.service, "mode", "mock") != "service":
            return None
        query = urllib.parse.urlencode({"search": search, "category": category, "limit": limit})
        data = self.service._request("GET", f"/api/v1/skills/market?{query}")
        if not isinstance(data, dict):
            return None
        skills = data.get("skills")
        return skills if isinstance(skills, list) else None

    def seed(self) -> None:
        now = datetime.now(timezone.utc).isoformat(timespec="seconds")
        for item in SEED_SKILLS:
            try:
                meta = parse_skill(item["content"], source="market")
            except SkillValidationError:
                continue
            execute(
                "INSERT OR IGNORE INTO market_skills (id, owner, name, display_name, description, category, tags, version, content, downloads, rating, status, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'published', ?, ?)",
                (
                    item["id"],
                    item["owner"],
                    meta.name,
                    item["display_name"],
                    item["description"],
                    item["category"],
                    json.dumps(item["tags"], ensure_ascii=False),
                    item["version"],
                    item["content"],
                    item["downloads"],
                    item["rating"],
                    now,
                    now,
                ),
            )
            execute(
                "INSERT OR IGNORE INTO market_versions (skill_id, version, content, changelog, created_at) VALUES (?, ?, ?, ?, ?)",
                (item["id"], item["version"], item["content"], "社区精选", now),
            )

    def delete(self, skill_id: str, owner: str) -> tuple[bool, str]:
        row = self.get(skill_id)
        if not row:
            return False, f"技能 {skill_id} 不存在"
        if row.get("owner") == "builtin":
            return False, "社区精选技能不能删除"
        if row.get("owner") != owner:
            return False, "只能删除自己发布的技能"
        execute("DELETE FROM market_versions WHERE skill_id = ?", (skill_id,))
        execute("DELETE FROM market_skills WHERE id = ?", (skill_id,))
        return True, f"已删除技能 {row.get('display_name') or skill_id}"