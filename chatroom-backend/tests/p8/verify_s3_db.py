#!/usr/bin/env python3
"""P8-S3 数据真实性校验：HTTP 响应逐条对比 foods 表，并独立复算排行榜分数。"""

from __future__ import annotations

import json
import math
import subprocess
import sys
import urllib.parse
import urllib.request

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
MYSQL = ["mysql", "--socket=/run/mysqld/mysqld.sock", "-uroot", "chatroom_p8",
         "-N", "-B", "-e"]

results: list[tuple[str, bool]] = []


def check(name: str, cond: bool, detail: object = "") -> None:
    results.append((name, bool(cond)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))


def q(sql: str) -> list[list[str]]:
    out = subprocess.run(MYSQL + [sql], capture_output=True, text=True, check=True)
    return [line.split("\t") for line in out.stdout.strip().splitlines() if line]


def call(path: str) -> dict:
    req = urllib.request.Request(BASE + path)
    req.add_header("Authorization", f"Bearer {TOKEN}")
    with OPENER.open(req, timeout=5) as resp:
        return json.loads(resp.read().decode("utf-8"))


def num(value: float) -> float:
    return float(value)


def daily_score(protein: float, fiber: float, veggies: float, penalty: float,
                meals: int, activity: str) -> int:
    bonus = 3 if activity == "较高" else 2 if activity == "适中" else 0
    raw = (68.0 + min(veggies / 500.0, 1.0) * 8.0 + min(protein / 90.0, 1.0) * 6.0
           + min(fiber / 25.0, 1.0) * 5.0 + min(meals, 3) + bonus - penalty)
    return max(45, min(96, int(round(raw))))


def main() -> int:
    db_rows = q("SELECT id,name,calories,protein,fiber,veggies,water,nature,"
                "price,meal_types,available FROM foods WHERE available=1 ORDER BY id")
    check("foods 表在售菜品 16 行", len(db_rows) == 16, len(db_rows))

    menu = call("/api/v1/menu")
    by_id = {item["id"]: item for item in menu.get("foods") or []}
    check("菜单返回条目数等于 foods 在售行数", len(by_id) == len(db_rows),
          (len(by_id), len(db_rows)))

    mismatches = []
    for row in db_rows:
        fid, name, calories, protein, fiber, veggies, water, nature, price, meal_types, available = row
        item = by_id.get(fid)
        if item is None:
            mismatches.append(f"{fid}:missing")
            continue
        expected = {
            "name": name,
            "calories": num(calories),
            "protein": num(protein),
            "fiber": num(fiber),
            "veggies": num(veggies),
            "water": num(water),
            "nature": nature,
            "price": int(price),
            "meal_types": json.loads(meal_types),
            "available": bool(int(available)),
        }
        for key, value in expected.items():
            if item.get(key) != value:
                mismatches.append(f"{fid}.{key}={item.get(key)!r}!={value!r}")
    check("菜单每条菜品字段与 foods 表逐列一致", not mismatches, mismatches[:6])

    search = call("/api/v1/foods/search?q=" + urllib.parse.quote("三文鱼") + "&limit=16")
    salmon = next((f for f in search.get("foods") or [] if f["id"] == "salmon-quinoa"), None)
    check("搜索结果来自真实 foods 表（三文鱼）", salmon is not None
          and salmon["calories"] == 520 and salmon["protein"] == 34, salmon)

    friends = q("SELECT u.agent_uid FROM friend f JOIN user u ON u.id=f.friend_id "
                "JOIN user d ON d.id=f.user_id WHERE d.agent_uid='u_demo' AND f.status=2 "
                "UNION "
                "SELECT u.agent_uid FROM friend f JOIN user u ON u.id=f.user_id "
                "JOIN user d ON d.id=f.friend_id WHERE d.agent_uid='u_demo' AND f.status=2")
    expected_friends = {row[0] for row in friends}
    ranking = call("/api/v1/users/u_demo/ranking?scope=today").get("ranking") or []
    ranking_ids = {item["user_id"] for item in ranking} - {"u_demo"}
    check("排行榜成员等于 friend(status=2) 查询结果",
          ranking_ids == expected_friends, (sorted(ranking_ids), sorted(expected_friends)))

    # 独立复算 u_demo 今日分数
    row = q("SELECT COALESCE(SUM(protein),0),COALESCE(SUM(fiber),0),"
            "COALESCE(SUM(veggies),0),COALESCE(SUM(penalty),0),COUNT(*) "
            "FROM meal_records WHERE agent_uid='u_demo' AND date=CURDATE()")[0]
    activity_rows = q("SELECT activity FROM health_profile WHERE agent_uid='u_demo'")
    activity = activity_rows[0][0] if activity_rows else "久坐"
    expected_score = daily_score(float(row[0]), float(row[1]), float(row[2]),
                                 float(row[3]), int(row[4]), activity)
    demo_score = next((item["score"] for item in ranking if item["user_id"] == "u_demo"), None)
    check("排行榜 u_demo 今日分与 DB 独立复算一致",
          demo_score == expected_score, (demo_score, expected_score, row, activity))

    # 无饮食记录好友：按活动量基线
    for uid, expected in (("u_test_rank_a", 71), ("u_test_rank_b", 71), ("u_test_rank_c", 68)):
        count = int(q(f"SELECT COUNT(*) FROM meal_records WHERE agent_uid='{uid}' AND date=CURDATE()")[0][0])
        score = next((item["score"] for item in ranking if item["user_id"] == uid), None)
        check(f"{uid} 无记录({count}) 今日基线分 {expected}", score == expected,
              (score, expected))

    blocked = q("SELECT COUNT(*) FROM friend f JOIN user d ON d.id=f.user_id "
                "WHERE d.agent_uid='u_demo' AND f.status<>2")[0][0]
    check("非正常好友关系存在且未进入榜单",
          int(blocked) >= 1 and "u_test_rank_d" not in {item["user_id"] for item in ranking},
          blocked)

    passed = sum(1 for _, ok in results if ok)
    total = len(results)
    print(f"\nSUMMARY passed={passed} failed={total - passed} total={total}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())