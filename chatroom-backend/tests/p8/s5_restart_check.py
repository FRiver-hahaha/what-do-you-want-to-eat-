#!/usr/bin/env python3
"""P8-S5 重启前后数据持久性与幂等回放验证。

用法：S5_RUN=<run> python3 s5_restart_check.py write|verify
"""

from __future__ import annotations

import json
import os
import subprocess
import sys

ROOT = os.environ.get("P8_WORK_ROOT", "/home/m/eat-p8-s1")
sys.path.insert(0, ROOT)
os.environ["EAT_DISABLE_DOTENV"] = "1"

from agent.data.service_client import ServiceClient  # noqa: E402

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
STATE = "/tmp/s5_restart_state.json"
MYSQL = ["mysql", "--socket=/run/mysqld/mysqld.sock", "-uroot",
         "--default-character-set=utf8mb4", "chatroom_p8", "-N", "-B", "-e"]

RUN = os.environ.get("S5_RUN") or "0"
TAG = f"S5R{RUN}"
KEY = f"s5r{RUN}"
payload = {"idempotency_key": KEY, "type": "加餐", "text": f"{TAG}重启前记录",
           "items": [f"{TAG}酸奶"], "protein": 9, "fiber": 2,
           "veggies": 30, "water": 180}

results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: object = "") -> bool:
    results.append((name, bool(cond), str(detail)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))
    return bool(cond)


def scalar(query: str) -> str:
    out = subprocess.run(MYSQL + [query], capture_output=True, text=True, check=True)
    lines = [line for line in out.stdout.strip().splitlines() if line]
    return lines[0] if lines else ""


def main() -> int:
    phase = sys.argv[1] if len(sys.argv) > 1 else "write"
    client = ServiceClient(BASE, TOKEN)

    if phase == "write":
        data = client.record_meal("u_demo", payload, KEY)
        check("W1 重启前写入成功",
              data.get("ok") is True and data.get("idempotent") is False, data)
        check("W1 写入无 mock 回退", client.last_error == "", client.last_error)
        meal_id = scalar(
            "SELECT id FROM meal_records WHERE agent_uid='u_demo' "
            f"AND `text`='{TAG}重启前记录'")
        check("W1 数据已落库", meal_id.isdigit(), meal_id)
        state = {"run": RUN, "receipt": data.get("receipt_id"), "meal_id": meal_id,
                 "rows": scalar("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")}
        with open(STATE, "w", encoding="utf-8") as handle:
            json.dump(state, handle, ensure_ascii=False)
        print("STATE", state)
    else:
        with open(STATE, encoding="utf-8") as handle:
            state = json.load(handle)
        rows_now = scalar("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")
        check("V1 重启后记录行数不变", rows_now == state["rows"],
              (state["rows"], rows_now))
        meal_id = scalar(
            "SELECT id FROM meal_records WHERE agent_uid='u_demo' "
            f"AND `text`='{TAG}重启前记录'")
        check("V2 重启后原记录仍存在", meal_id == state["meal_id"],
              (state["meal_id"], meal_id))

        replay = client.record_meal("u_demo", payload, KEY)
        check("V3 重启后同 key 重放 idempotent=true",
              replay.get("idempotent") is True, replay.get("idempotent"))
        check("V4 重启后重放 receipt 与首次一致",
              str(replay.get("receipt_id")) == str(state["receipt"]),
              (replay.get("receipt_id"), state["receipt"]))
        check("V5 重启后重放未新增数据",
              scalar("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")
              == state["rows"], rows_now)

        recent = client.get_recent_meals("u_demo", 1)
        texts = [item.get("text") for item in recent.get("meals") or []]
        check("V6 重启后查询接口仍能读到该记录",
              any(str(text).startswith(TAG) for text in texts), texts[:5])
        check("V7 重启后服务仍为真实数据源",
              recent.get("source") == "service", recent.get("source"))

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())