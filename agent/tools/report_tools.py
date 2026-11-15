from __future__ import annotations

from datetime import date
from pathlib import Path
from typing import Any

from agent.core.exports import write_export
from agent.tools.base import ToolContext, ToolResult, ToolSpec
from agent.tools.diet_tools import merged_gap, merged_profile, recent_meals

NUTRIENT_LABELS = {"protein": "蛋白质", "fiber": "膳食纤维", "veggies": "蔬菜", "water": "饮水"}
NUTRIENT_UNITS = {"protein": "g", "fiber": "g", "veggies": "g", "water": "ml"}
DISCLAIMER = "本报告用于日常饮食记录与生活方式参考，不用于诊断或治疗。"


def _schema(properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    return {"type": "object", "properties": properties, "required": required or []}


def _value(source: dict[str, Any], key: str, default: Any = 0) -> Any:
    try:
        return float(source.get(key, default) or default)
    except (TypeError, ValueError):
        return default


def _advice(gap: dict[str, Any], meals: list[dict[str, Any]]) -> list[str]:
    intake = gap.get("intake") or {}
    targets = gap.get("targets") or {}
    gaps = gap.get("gap") or {}
    advice: list[str] = []
    if _value(gaps, "protein") > 20:
        advice.append(f"蛋白质还差 {int(_value(gaps, 'protein'))}g，下一餐优先安排鸡胸肉、鸡蛋、豆制品或酸奶。")
    if _value(gaps, "veggies") > 150:
        advice.append(f"蔬菜还差 {int(_value(gaps, 'veggies'))}g，加一份白灼或清炒绿叶菜即可补齐大部分。")
    elif _value(gaps, "fiber") > 8:
        advice.append(f"膳食纤维还差 {int(_value(gaps, 'fiber'))}g，主食可以换成全谷物或加一份豆类。")
    if _value(gaps, "water") > 400:
        advice.append(f"饮水还差 {int(_value(gaps, 'water'))}ml，分成两到三次补，不要一次喝完。")
    if not meals:
        advice.append("近几天还没有完整记录，先补今天这一餐，报告会更有参考价值。")
    if not advice:
        advice.append("今天四项营养都在目标附近，保持当前节奏就可以。")
    if intake and targets:
        advice.append("以上缺口按今日摄入与目标计算，目标来自健康档案与默认营养建议。")
    return advice


def _nutrition_table(gap: dict[str, Any]) -> list[str]:
    intake = gap.get("intake") or {}
    targets = gap.get("targets") or {}
    if not targets:
        return []
    lines = ["| 项目 | 已摄入 | 目标 | 缺口 |", "| --- | --- | --- | --- |"]
    for key in ("protein", "fiber", "veggies", "water"):
        target = _value(targets, key)
        taken = _value(intake, key)
        unit = NUTRIENT_UNITS[key]
        lines.append(f"| {NUTRIENT_LABELS[key]} | {int(taken)}{unit} | {int(target)}{unit} | {max(0, int(target - taken))}{unit} |")
    return lines


def _render_report(profile: dict[str, Any], meals: list[dict[str, Any]], gap: dict[str, Any], weekly: dict[str, Any], days: int) -> str:
    lines = [f"# 健康报告 · {date.today().isoformat()}", ""]
    lines.extend(["## 健康档案"])
    lines.append(f"- 称呼：{profile.get('name') or '未填写'}")
    lines.append(f"- 身高 / 体重：{profile.get('height') or '-'}cm / {profile.get('weight') or '-'}kg")
    lines.append(f"- 年龄：{profile.get('age') or '-'}")
    lines.append(f"- 健康目标：{profile.get('goal') or '-'}")
    lines.append(f"- 活动量：{profile.get('activity') or '-'}")
    lines.append(f"- 饮水目标：{profile.get('water_target') or profile.get('waterTarget') or '-'}ml")
    lines.append(f"- 睡眠：{profile.get('sleep') or '-'}")
    lines.append(f"- 过敏：{'、'.join(profile.get('allergies') or []) or '无'}")
    lines.append(f"- 忌口：{'、'.join(profile.get('dislikes') or []) or '无'}")
    lines.extend(["", "## 今日营养缺口"])
    table = _nutrition_table(gap)
    lines.extend(table or ["暂无今日营养数据。"])
    lines.extend(["", f"## 近 {days} 天饮食记录"])
    if meals:
        for meal in meals[:40]:
            text = str(meal.get("text") or "、".join(meal.get("items") or []))
            lines.append(f"- {meal.get('date') or date.today().isoformat()} {meal.get('type') or '加餐'} {meal.get('time') or ''}：{text}".rstrip())
    else:
        lines.append("- 暂无记录。")
    lines.extend(["", "## 本周趋势"])
    lines.append(f"- 记录天数：{weekly.get('days_recorded', 0)} 天")
    lines.append(f"- 三餐齐全：{weekly.get('three_meal_days', 0)} 天")
    lines.append(f"- 平均健康分：{weekly.get('avg_health_score', '-')}")
    lines.append(f"- 连续记录：{weekly.get('streak', 0)} 天")
    lines.extend(["", "## 调整建议"])
    lines.extend(f"- {item}" for item in _advice(gap, meals))
    lines.extend(["", "## 说明", DISCLAIMER, ""])
    return "\n".join(lines)


def _to_plain(markdown: str) -> str:
    lines = []
    for line in markdown.splitlines():
        if line.startswith("| ---"):
            continue
        cleaned = line.replace("|", " ").lstrip("#").strip()
        lines.append(cleaned)
    return "\n".join(lines)


def _export_report(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    try:
        days = int(args.get("days") or 7)
    except (TypeError, ValueError):
        return ToolResult(ok=False, summary="days 必须是整数", error="invalid_days")
    days = max(1, min(days, 30))
    fmt = str(args.get("format") or "md").lower()
    if fmt not in ("md", "txt"):
        return ToolResult(ok=False, summary="format 只支持 md 或 txt", error="invalid_format")
    profile = merged_profile(ctx)
    meal_payload = recent_meals({"days": days}, ctx).data or {}
    meals = meal_payload.get("meals") or []
    gap = merged_gap(ctx)
    weekly = ctx.service.get_weekly_stats(ctx.user_id)
    markdown = _render_report(profile, meals, gap, weekly, days)
    content = markdown if fmt == "md" else _to_plain(markdown)
    export = write_export("健康报告", content, f".{fmt}")
    return ToolResult(
        ok=True,
        summary=f"健康报告已生成，包含近 {days} 天 {len(meals)} 条记录，可下载 {export['filename']}",
        data={
            "filename": export["filename"],
            "download_url": export["download_url"],
            "bytes": export["bytes"],
            "format": fmt,
            "days": days,
            "records": len(meals),
        },
    )


def _skill_markdown(skill: Any) -> str:
    path = Path(skill.path) if getattr(skill, "path", "") else None
    if path is not None and path.is_file():
        try:
            return path.read_text(encoding="utf-8")
        except OSError:
            pass
    lines = [
        "---",
        f"name: {skill.name}",
        f"display_name: {skill.display_name}",
        f"description: {skill.description}",
        f"version: {skill.version}",
        f"author: {skill.author}",
        f"category: {skill.category}",
        f"tags: [{', '.join(skill.tags)}]",
        f"modes: [{', '.join(skill.modes)}]",
        "---",
        "",
        skill.body,
        "",
    ]
    return "\n".join(lines)


def _export_skill(args: dict[str, Any], ctx: ToolContext) -> ToolResult:
    skill_id = str(args.get("id") or "").strip()
    if not skill_id:
        return ToolResult(ok=False, summary="缺少要导出的技能 id", error="missing_id")
    skill = ctx.skills.get(skill_id) if ctx.skills is not None else None
    display = skill.display_name if skill is not None else skill_id
    content = _skill_markdown(skill) if skill is not None else ""
    if not content and ctx.skills is not None and getattr(ctx.skills, "market", None) is not None:
        row = ctx.skills.market.get(skill_id)
        if row:
            content = str(row.get("content") or "")
            display = str(row.get("display_name") or display)
    if not content:
        return ToolResult(ok=False, summary=f"没有找到技能 {skill_id}", error="skill_not_found")
    export = write_export(f"技能-{skill_id}", content, ".md")
    return ToolResult(
        ok=True,
        summary=f"技能 {display} 已导出，可下载 {export['filename']}",
        data={
            "id": skill_id,
            "display_name": display,
            "filename": export["filename"],
            "download_url": export["download_url"],
            "bytes": export["bytes"],
        },
    )


def build_report_tools() -> list[ToolSpec]:
    return [
        ToolSpec(
            name="export_health_report",
            description="生成当前用户的健康报告文件并返回下载链接，包含档案、今日营养缺口、近若干天饮食记录与本周趋势，仅工作模式可用",
            parameters=_schema(
                {
                    "days": {"type": "integer", "minimum": 1, "maximum": 30, "description": "纳入报告的天数，默认 7"},
                    "format": {"type": "string", "enum": ["md", "txt"], "description": "导出格式，默认 md"},
                }
            ),
            handler=_export_report,
            category="local",
            modes=("work",),
            tags=("report", "export"),
        ),
        ToolSpec(
            name="export_skill",
            description="把指定技能或市场技能的 SKILL.md 导出为可下载文件，返回下载链接，仅工作模式可用",
            parameters=_schema(
                {
                    "id": {"type": "string", "description": "技能 id，例如 nutritionist 或市场技能 id"},
                },
                required=["id"],
            ),
            handler=_export_skill,
            category="local",
            modes=("work",),
            tags=("skill", "export"),
        ),
    ]