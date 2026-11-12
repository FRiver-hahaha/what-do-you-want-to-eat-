from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from typing import Any, Iterable


@dataclass
class PlanStep:
    action: str
    tool: str = ""
    arguments: dict[str, Any] = field(default_factory=dict)
    rationale: str = ""
    status: str = "pending"


@dataclass
class Plan:
    task_type: str
    complexity: str
    steps: list[PlanStep]
    needs_confirmation: bool = False
    note: str = ""
    id: str = field(default_factory=lambda: f"plan_{uuid.uuid4().hex[:8]}")

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "task_type": self.task_type,
            "complexity": self.complexity,
            "needs_confirmation": self.needs_confirmation,
            "steps": [step.__dict__ for step in self.steps],
            "note": self.note,
        }

    def to_prompt(self) -> str:
        lines = [
            f"<plan task_type=\"{self.task_type}\" complexity=\"{self.complexity}\">",
            "执行计划：",
        ]
        for index, step in enumerate(self.steps, start=1):
            tool = f" 工具 {step.tool}" if step.tool else ""
            lines.append(f"{index}. {step.action}{tool}")
        if self.note:
            lines.append(f"备注：{self.note}")
        lines.append("</plan>")
        return "\n".join(lines)


def _step(action: str, tool: str = "", arguments: dict[str, Any] | None = None, rationale: str = "") -> PlanStep:
    return PlanStep(action=action, tool=tool, arguments=arguments or {}, rationale=rationale)


class Planner:
    def plan(
        self,
        message: str,
        mode: str,
        has_images: bool = False,
        profile: dict[str, Any] | None = None,
    ) -> Plan:
        text = message.lower()
        if has_images:
            return Plan(
                task_type="vision",
                complexity="complex",
                steps=[
                    _step("识别图片中的食物与份量"),
                    _step("查询健康档案与营养缺口", "get_health_profile"),
                    _step("估算热量与营养并给出建议"),
                ],
                note="图片只在本轮上下文中使用",
            )
        if any(word in text for word in ["记录", "早餐吃", "午餐吃", "晚餐吃", "加餐"]):
            return Plan(
                task_type="meal_log",
                complexity="complex",
                needs_confirmation=True,
                steps=[
                    _step("解析餐次与食物"),
                    _step("复述并等待用户确认", "record_meal"),
                    _step("写入饮食记录"),
                ],
                note=mode if mode == "work" else "需要工作模式",
            )
        if any(word in text for word in ["推荐", "吃什么", "吃啥", "该吃", "菜单", "配餐", "饿"]):
            return Plan(
                task_type="meal_plan",
                complexity="complex",
                steps=[
                    _step("读取健康档案", "get_health_profile"),
                    _step("计算今日营养缺口", "get_nutrition_gap"),
                    _step("检查近期重复", "get_recent_meals", {"days": 3}),
                    _step("筛选菜品", "search_foods" if mode == "qa" else "get_menu"),
                    _step("生成轻量结论与候选"),
                ],
            )
        if any(word in text for word in ["排名", "排行榜", "周报", "本周", "积分", "总结"]):
            return Plan(
                task_type="review",
                complexity="complex",
                steps=[
                    _step("读取本周统计", "get_weekly_stats"),
                    _step("读取好友排名", "get_friend_ranking", {"scope": "week"}),
                    _step("输出结构化复盘"),
                ],
            )
        if any(word in text for word in ["吃了什么", "查记录", "饮食记录", "猜", "推测"]):
            return Plan(
                task_type="meal_history",
                complexity="simple",
                steps=[
                    _step("读取近期饮食记录", "get_recent_meals", {"days": 7}),
                    _step("读取健康档案", "get_health_profile"),
                ],
            )
        return Plan(
            task_type="qa",
            complexity="simple",
            steps=[_step("直接回答，必要时检索知识库")],
        )

    def replan(self, plan: Plan, failed_tool: str, error: str) -> Plan:
        steps = [step for step in plan.steps if step.tool != failed_tool]
        steps.append(_step(f"工具 {failed_tool} 失败，改用可用数据回答", rationale=error[:120]))
        return Plan(
            task_type=plan.task_type,
            complexity=plan.complexity,
            steps=steps,
            needs_confirmation=plan.needs_confirmation,
            note="已根据失败结果重新规划",
            id=plan.id,
        )

    @staticmethod
    def mark(plan: Plan, tool: str, status: str) -> None:
        for step in plan.steps:
            if step.tool == tool:
                step.status = status
                break


def render_plan_prompt(plan: Plan | None) -> str:
    return plan.to_prompt() if plan else ""