#!/usr/bin/env python3
"""P8-S5 完整 Agent 业务链路 + 跨接口数据一致性（真实 C++ 服务端 + MySQL）。

覆盖：档案读写 → 三餐记录 → 幂等重放 → 记录查询 → 营养缺口 → 周统计 →
菜单/搜索 → 好友列表 → 好友排行 → 技能分享给真实好友。
所有断言同时对比 MySQL 真实数据，任何 mock 回退都判失败。
"""

from __future__ import annotations

import json
import math
import os
import subprocess
import sys
import time

ROOT = os.environ.get("P8_WORK_ROOT", "/home/m/eat-p8-s1")
sys.path.insert(0, ROOT)
os.environ["EAT_DISABLE_DOTENV"] = "1"

from datetime import date, timedelta  # noqa: E402

from agent.data.db import init_db  # noqa: E402
from agent.data.service_client import ServiceClient  # noqa: E402
from agent.skills.market import SkillMarket  # noqa: E402
from agent.tools import diet_tools  # noqa: E402
from agent.tools.base import ToolContext  # noqa: E402

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
MYSQL = ["mysql", "--socket=/run/mysqld/mysqld.sock", "-uroot",
         "--default-character-set=utf8mb4", "chatroom_p8", "-N", "-B", "-e"]

RUN = os.environ.get("S5_RUN") or time.strftime("%Y%m%d%H%M%S")
TAG = f"S5{RUN}"
TODAY = date.today().isoformat()
YESTERDAY = (date.today() - timedelta(days=1)).isoformat()
RESTORE_WEIGHT = os.environ.get("S5_RESTORE_WEIGHT", "60")

results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: object = "") -> bool:
    results.append((name, bool(cond), str(detail)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))
    return bool(cond)


def sql(query: str) -> list[list[str]]:
    out = subprocess.run(MYSQL + [query], capture_output=True, text=True, check=True)
    return [line.split("\t") for line in out.stdout.strip().splitlines() if line]


def scalar(query: str) -> str:
    rows = sql(query)
    return rows[0][0] if rows else ""


def day_nutrition(day: str) -> dict[str, float]:
    row = sql(
        "SELECT COALESCE(SUM(protein),0),COALESCE(SUM(fiber),0),"
        "COALESCE(SUM(veggies),0),COALESCE(SUM(water),0),"
        "COALESCE(SUM(penalty),0),COUNT(*) FROM meal_records "
        f"WHERE agent_uid='u_demo' AND `date`='{day}'")[0]
    return {
        "protein": float(row[0]),
        "fiber": float(row[1]),
        "veggies": float(row[2]),
        "water": float(row[3]),
        "penalty": float(row[4]),
        "meals": int(row[5]),
    }


def daily_score(n: dict[str, float], activity: str) -> int:
    bonus = 3 if activity == "较高" else 2 if activity == "适中" else 0
    raw = (68.0 + min(n["veggies"] / 500.0, 1.0) * 8.0
           + min(n["protein"] / 90.0, 1.0) * 6.0
           + min(n["fiber"] / 25.0, 1.0) * 5.0
           + min(n["meals"], 3) + bonus - n["penalty"])
    return max(45, min(96, int(math.floor(raw + 0.5))))


def main() -> int:
    client = ServiceClient(BASE, TOKEN)
    ctx = ToolContext(user_id="u_demo", session_id=f"s_s5_{RUN}",
                      mode="work", service=client)
    check("ServiceClient 处于 service 模式（非 mock）",
          client.mode == "service", client.mode)

    # ── 1. 身份认证与健康档案读取 ──
    profile = diet_tools._profile({}, ctx)
    data = profile.data or {}
    check("1 健康档案读取成功", profile.ok and data.get("user_id") == "u_demo",
          profile.summary)
    check("1 档案来自真实服务端", data.get("source") == "service",
          data.get("source"))
    check("1 档案字段齐全且无 mock 回退",
          {"name", "height", "weight", "bmi", "goal", "activity",
           "allergies", "dislikes"} <= set(data) and client.last_error == "",
          sorted(data))

    # ── 2. 更新健康档案并重新读取 ──
    before_weight = float(data.get("weight") or 0)
    updated = diet_tools._update_health_profile({"weight": 64.5}, ctx)
    check("2 档案更新返回成功", updated.ok, updated.summary)
    reread = (diet_tools._profile({}, ctx).data or {})
    check("2 更新后重新读取生效 weight=64.5",
          float(reread.get("weight") or 0) == 64.5, reread.get("weight"))
    db_weight = scalar("SELECT weight FROM health_profile WHERE agent_uid='u_demo'")
    check("2 档案更新已落库", float(db_weight) == 64.5, db_weight)
    check("2 BMI 随体重重算",
          abs(float(reread.get("bmi") or 0)
              - round(64.5 / ((float(reread["height"]) / 100) ** 2), 2)) <= 0.01,
          reread.get("bmi"))

    # ── 3. 记录早餐 / 午餐 / 晚餐 ──
    meals = [
        ({"type": "早餐", "text": f"{TAG}早餐燕麦鸡蛋",
          "items": [f"{TAG}燕麦", "鸡蛋"],
          "protein": 20, "fiber": 5, "veggies": 100, "water": 300}, "早餐"),
        ({"type": "午餐", "text": f"{TAG}午餐鸡胸糙米",
          "items": [f"{TAG}鸡胸", "糙米"],
          "protein": 30, "fiber": 8, "veggies": 200, "water": 400}, "午餐"),
        ({"type": "晚餐", "text": f"{TAG}晚餐三文鱼沙拉",
          "items": [f"{TAG}三文鱼", "沙拉"],
          "protein": 25, "fiber": 6, "veggies": 150, "water": 350}, "晚餐"),
    ]
    receipts: dict[str, str] = {}
    for args, label in meals:
        result = diet_tools._record_meal(args, ctx)
        ok = result.ok and (result.data or {}).get("idempotent") is False
        check(f"3 {label}记录成功且非重放", ok, result.summary)
        receipts[label] = str((result.data or {}).get("receipt_id"))
    check("3 三条记录 receipt 互不相同", len(set(receipts.values())) == 3, receipts)
    db_count = scalar(
        f"SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' "
        f"AND `date`='{TODAY}' AND text LIKE '{TAG}%'")
    check("3 三条记录真实落库", db_count == "3", db_count)

    # ── 4. 重复提交相同幂等请求 ──
    today_before_replay = day_nutrition(TODAY)
    replay = diet_tools._record_meal(meals[0][0], ctx)
    replay_data = replay.data or {}
    check("4 重复请求返回 idempotent=true",
          replay_data.get("idempotent") is True, replay_data.get("idempotent"))
    check("4 重复请求回放同一 receipt",
          str(replay_data.get("receipt_id")) == receipts["早餐"],
          (replay_data.get("receipt_id"), receipts["早餐"]))
    check("4 重复请求未新增数据行",
          day_nutrition(TODAY) == today_before_replay,
          (day_nutrition(TODAY), today_before_replay))
    idem_rows = scalar(
        f"SELECT COUNT(*) FROM idempotency_records WHERE agent_uid='u_demo' "
        f"AND meal_id={receipts['早餐'].split('-')[-1]}")
    check("4 幂等表只保留一条记录", idem_rows == "1", idem_rows)

    # ── 5. 查询饮食记录 ──
    recent = diet_tools._recent_meals({"days": 1}, ctx).data or {}
    recent_texts = [item.get("text") for item in recent.get("meals") or []]
    check("5 新增记录立即可见（当日查询命中 3 条）",
          sum(1 for text in recent_texts if str(text).startswith(TAG)) == 3,
          recent_texts[:6])
    check("5 记录 source=service", recent.get("source") == "service",
          recent.get("source"))
    latest = [item for item in recent.get("meals") or []
              if str(item.get("text")).startswith(TAG)]
    check("5 记录营养值来自请求而非 mock",
          len(latest) == 3
          and {item.get("type") for item in latest} == {"早餐", "午餐", "晚餐"},
          [(item.get("type"), item.get("protein")) for item in latest])

    # ── 6. 查询营养缺口 ──
    gap = diet_tools._nutrition_gap({}, ctx).data or {}
    intake = gap.get("intake") or {}
    nutrition_today = day_nutrition(TODAY)
    check("6 营养缺口 intake 与 DB 当日汇总一致",
          int(intake.get("protein", -1)) == int(nutrition_today["protein"])
          and int(intake.get("veggies", -1)) == int(nutrition_today["veggies"]),
          (intake, nutrition_today))
    check("6 营养缺口含 targets 与 gap",
          isinstance(gap.get("targets"), dict) and isinstance(gap.get("gap"), dict),
          gap.get("gap"))

    # ── 7. 查询周统计 ──
    stats = diet_tools._weekly_stats({}, ctx).data or {}
    daily = {row.get("date"): row for row in stats.get("daily") or []}
    today_row = daily.get(TODAY) or {}
    expected_score = daily_score(nutrition_today, "久坐")
    check("7 周统计包含今日",
          today_row.get("meals") == nutrition_today["meals"], today_row)
    check("7 周统计今日分数与 DB 独立复算一致",
          int(today_row.get("score", -1)) == expected_score,
          (today_row.get("score"), expected_score))
    check("7 周统计 days_recorded 覆盖今日记录",
          int(stats.get("days_recorded") or 0) >= 1, stats.get("days_recorded"))

    # ── 8. 搜索食物并获取菜单 ──
    search = diet_tools._search_foods({"query": "鸡胸", "limit": 5}, ctx)
    search_foods = (search.data or {}).get("foods") or []
    check("8 菜品搜索走真实服务端",
          search.ok and search_foods
          and all(isinstance(f.get("match"), int) for f in search_foods),
          (search.summary, len(search_foods)))
    menu = diet_tools._menu({"meal_type": "午餐"}, ctx)
    menu_foods = (menu.data or {}).get("foods") or []
    check("8 菜单走真实服务端", menu.ok and menu.data.get("count") == 14,
          (menu.data or {}).get("count"))

    # ── 9. 查询好友列表 ──
    friends = client.get_friends("u_demo")
    friend_ids = {item.get("id") for item in friends.get("friends") or []}
    check("9 好友列表来自真实服务端",
          friends.get("source") == "service"
          and friend_ids == {"u_test_rank_a", "u_test_rank_b", "u_test_rank_c"},
          sorted(friend_ids))

    # ── 10. 查询好友排行榜 ──
    ranking = diet_tools._friend_ranking({"scope": "today"}, ctx).data or {}
    rows = ranking.get("ranking") or []
    self_row = next((row for row in rows if row.get("user_id") == "u_demo"), {})
    check("10 排行榜来自真实服务端且含自己",
          ranking.get("source") == "service" and bool(self_row), ranking.get("source"))
    check("10 排行榜自己分数与周统计今日分一致",
          int(self_row.get("score", -1)) == expected_score,
          (self_row.get("score"), expected_score))

    # ── 11. 向真实好友分享并验证对方可查询 ──
    init_db()
    market = SkillMarket(None, client)
    market.seed()
    share_ok, message, shares = market.share(
        "medical-nutrition", "u_demo", ["u_test_rank_a"])
    check("11 分享给真实好友成功", share_ok and len(shares) == 1, message)
    share_code = (shares[0].get("code") if shares else "")
    received = market.shared_with_me("u_test_rank_a")
    hit = [row for row in received
           if row.get("skill_id") == "medical-nutrition"
           and row.get("code") == share_code]
    check("11 接收方可查询到该分享", len(hit) == 1, received[-3:])
    check("11 分享码非空且可复用于接收方",
          bool(share_code) and hit and hit[0].get("from_user") == "u_demo",
          hit[:1])

    # ── 跨接口一致性：不同日期不串 ──
    gap_before_yesterday = dict((diet_tools._nutrition_gap({}, ctx).data or {}).get("intake") or {})
    stats_before = (diet_tools._weekly_stats({}, ctx).data or {})
    yday_before = dict(day_nutrition(YESTERDAY))
    y_result = diet_tools._record_meal(
        {"type": "加餐", "date": YESTERDAY, "text": f"{TAG}昨日加餐酸奶",
         "items": [f"{TAG}酸奶"], "protein": 7, "fiber": 1,
         "veggies": 0, "water": 150}, ctx)
    check("T1 昨日记录写入成功", y_result.ok, y_result.summary)
    gap_after_yesterday = (diet_tools._nutrition_gap({}, ctx).data or {}).get("intake") or {}
    check("T1 昨日记录未串入今日营养缺口",
          gap_after_yesterday == gap_before_yesterday,
          (gap_before_yesterday, gap_after_yesterday))
    check("T1 昨日记录已落库",
          day_nutrition(YESTERDAY)["meals"] == yday_before["meals"] + 1,
          (day_nutrition(YESTERDAY), yday_before))
    one_day = diet_tools._recent_meals({"days": 1}, ctx).data or {}
    two_days = diet_tools._recent_meals({"days": 2}, ctx).data or {}
    one_texts = [item.get("text") for item in one_day.get("meals") or []]
    two_texts = [item.get("text") for item in two_days.get("meals") or []]
    check("T1 昨日记录不出现在 days=1 结果",
          not any("昨日加餐" in str(text) for text in one_texts), one_texts[:5])
    check("T1 昨日记录出现在 days=2 结果",
          any("昨日加餐" in str(text) for text in two_texts), len(two_texts))
    stats_after = (diet_tools._weekly_stats({}, ctx).data or {})
    daily_after = {row.get("date"): row for row in stats_after.get("daily") or []}
    check("T1 周统计昨日餐数 +1，今日餐数不变",
          int((daily_after.get(YESTERDAY) or {}).get("meals", -1))
          == int((daily.get(YESTERDAY) or {}).get("meals", 0)) + 1
          and int((daily_after.get(TODAY) or {}).get("meals", -1))
          == int(today_row.get("meals", -1)),
          (daily_after.get(YESTERDAY), daily.get(YESTERDAY)))
    total_before = sum(int(row.get("meals") or 0) for row in daily.values())
    total_after = sum(int(row.get("meals") or 0) for row in daily_after.values())
    check("T1 周统计总餐数增量恰为 1（无重复计入）",
          total_after - total_before == 1, (total_before, total_after))

    # 恢复档案，避免影响 S1 基线
    restore = diet_tools._update_health_profile({"weight": RESTORE_WEIGHT}, ctx)
    check("T2 档案已恢复基线 weight", restore.ok, restore.summary)

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())