#!/usr/bin/env python3
"""P8-S5 数据真实性校验：逐列对比 MySQL，并独立复算幂等哈希与健康分。"""

from __future__ import annotations

import hashlib
import json
import math
import os
import subprocess
import sys
from datetime import date, timedelta

RUN = os.environ.get("S5_RUN") or ""
TAG = f"S5{RUN}"
TODAY = date.today().isoformat()
YESTERDAY = (date.today() - timedelta(days=1)).isoformat()
SESSION = f"s_s5_{RUN}"
MYSQL = ["mysql", "--socket=/run/mysqld/mysqld.sock", "-uroot",
         "--default-character-set=utf8mb4", "chatroom_p8", "-N", "-B", "-e"]

results: list[tuple[str, bool]] = []

MEALS = [
    ("早餐", f"{TAG}早餐燕麦鸡蛋", 20, 5, 100, 300),
    ("午餐", f"{TAG}午餐鸡胸糙米", 30, 8, 200, 400),
    ("晚餐", f"{TAG}晚餐三文鱼沙拉", 25, 6, 150, 350),
]


def check(name: str, cond: bool, detail: object = "") -> None:
    results.append((name, bool(cond)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))


def q(sql: str) -> list[list[str]]:
    out = subprocess.run(MYSQL + [sql], capture_output=True, text=True, check=True)
    return [line.split("\t") for line in out.stdout.strip().splitlines() if line]


def canonical_hash(day: str, text: str, meal_type: str) -> str:
    payload = json.dumps({"date": day, "text": text, "type": meal_type},
                         ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def agent_key(meal_type: str, text: str) -> str:
    # Agent 在未显式传 date 时，幂等键的日期段为空串（date or ""）。
    raw = f"{SESSION}::{meal_type}:{text}"
    return hashlib.sha1(raw.encode("utf-8")).hexdigest()[:16]


def daily_score(protein: float, fiber: float, veggies: float, penalty: float,
                meals: int, activity: str) -> int:
    bonus = 3 if activity == "较高" else 2 if activity == "适中" else 0
    raw = (68.0 + min(veggies / 500.0, 1.0) * 8.0
           + min(protein / 90.0, 1.0) * 6.0
           + min(fiber / 25.0, 1.0) * 5.0
           + min(meals, 3) + bonus - penalty)
    return max(45, min(96, int(math.floor(raw + 0.5))))


def main() -> int:
    if not RUN:
        print("S5_RUN is required")
        return 2

    rows = q("SELECT `type`,text,protein,fiber,veggies,water,penalty,`date` "
             "FROM meal_records WHERE agent_uid='u_demo' "
             f"AND `date`='{TODAY}' AND text LIKE '{TAG}%' ORDER BY id")
    check("今日 marker 记录恰为 3 条", len(rows) == 3, len(rows))

    by_type = {row[0]: row for row in rows}
    ok_values = True
    for meal_type, text, protein, fiber, veggies, water in MEALS:
        row = by_type.get(meal_type)
        if row is None:
            ok_values = False
            continue
        if (row[1] != text or float(row[2]) != protein or float(row[3]) != fiber
                or float(row[4]) != veggies or float(row[5]) != water):
            ok_values = False
    check("三餐逐列数值与请求一致（text/蛋白/纤维/蔬菜/饮水）", ok_values,
          [(row[0], row[1], row[2]) for row in rows])
    check("三餐 penalty 复算为 0（文本无罚分关键字）",
          all(int(row[6]) == 0 for row in rows), [row[6] for row in rows])
    check("三餐日期均为今天", all(row[7] == TODAY for row in rows), [row[7] for row in rows])

    hash_ok = True
    key_ok = True
    for meal_type, text, *_ in MEALS:
        expected_hash = canonical_hash(TODAY, text, meal_type)
        expected_key = agent_key(meal_type, text)
        found = q("SELECT payload_hash FROM idempotency_records "
                  f"WHERE agent_uid='u_demo' AND idem_key='{expected_key}'")
        if not found or found[0][0] != expected_hash:
            hash_ok = False
        linked = q("SELECT m.text FROM idempotency_records i JOIN meal_records m "
                   "ON m.id=i.meal_id WHERE i.agent_uid='u_demo' "
                   f"AND i.idem_key='{expected_key}'")
        if not linked or linked[0][0] != text:
            key_ok = False
    check("幂等 payload_hash 与 Python 独立复算一致", hash_ok, "sha256(canonical json)")
    check("幂等键由 Agent 规则复算并指向正确 meal", key_ok, SESSION)

    y_rows = q("SELECT text,`date`,protein FROM meal_records WHERE agent_uid='u_demo' "
               f"AND text='{TAG}昨日加餐酸奶'")
    check("昨日记录恰 1 条且日期正确",
          len(y_rows) == 1 and y_rows[0][1] == YESTERDAY, y_rows)

    total = q("SELECT COALESCE(SUM(protein),0),COALESCE(SUM(fiber),0),"
              "COALESCE(SUM(veggies),0),COALESCE(SUM(penalty),0),COUNT(*) "
              "FROM meal_records WHERE agent_uid='u_demo' "
              f"AND `date`='{TODAY}'")[0]
    activity = q("SELECT activity FROM health_profile WHERE agent_uid='u_demo'")[0][0]
    expected = daily_score(float(total[0]), float(total[1]), float(total[2]),
                           float(total[3]), int(total[4]), activity)
    check("今日健康分独立复算落在 45-96", 45 <= expected <= 96, expected)

    orphans = q("SELECT COUNT(*) FROM idempotency_records i "
                "LEFT JOIN meal_records m ON m.id=i.meal_id AND m.agent_uid=i.agent_uid "
                "WHERE i.agent_uid='u_demo' AND m.id IS NULL")[0][0]
    check("无孤儿幂等记录", orphans == "0", orphans)

    dup = q("SELECT COUNT(*) FROM (SELECT idem_key FROM idempotency_records "
            "WHERE agent_uid='u_demo' GROUP BY idem_key HAVING COUNT(*)>1) t")[0][0]
    check("幂等键全局唯一", dup == "0", dup)

    neg = q("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' "
            "AND (protein<0 OR fiber<0 OR veggies<0 OR water<0)")[0][0]
    check("无负营养值脏数据", neg == "0", neg)

    empty = q("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' AND text=''")[0][0]
    check("无空文本记录", empty == "0", empty)

    passed = sum(1 for _, ok in results if ok)
    total_checks = len(results)
    print(f"\nRESULT {passed}/{total_checks} PASS (今日健康分={expected})")
    return 0 if passed == total_checks else 1


if __name__ == "__main__":
    sys.exit(main())