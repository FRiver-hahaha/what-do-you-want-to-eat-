#!/usr/bin/env python3
"""P8-S4 Agent 真实调用契约验证：ServiceClient.get_friends + market.share 好友校验。"""

from __future__ import annotations

import os
import shutil
import sys
import uuid

ROOT = os.environ.get("P8_WORK_ROOT", "/home/m/eat-p8-s1")
TMP_DB = "/tmp/p8s4_agent_contract.db"

os.environ["EAT_DISABLE_DOTENV"] = "1"
os.environ["EAT_AGENT_DB"] = TMP_DB
os.environ["EAT_AGENT_DATA_DIR"] = "/tmp/p8s4_agent_contract_data"
sys.path.insert(0, ROOT)

from agent.data.db import init_db  # noqa: E402
from agent.data.service_client import ServiceClient  # noqa: E402
from agent.skills.market import SkillMarket  # noqa: E402

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
EXPECTED_IDS = {"u_test_rank_a", "u_test_rank_b", "u_test_rank_c"}
EXPECTED_NAMES = {
    "u_test_rank_a": "甲同学",
    "u_test_rank_b": "乙同学",
    "u_test_rank_c": "测试丙",
}

results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: object = "") -> bool:
    results.append((name, bool(cond), str(detail)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))
    return bool(cond)


def main() -> int:
    # 独立 sqlite，避免污染 Agent 既有数据。
    if os.path.exists(TMP_DB):
        os.remove(TMP_DB)
    shutil.rmtree("/tmp/p8s4_agent_contract_data", ignore_errors=True)
    init_db()

    client = ServiceClient(BASE, TOKEN)
    check("ServiceClient 处于 service 模式", client.mode == "service", client.mode)

    payload = client.get_friends("u_demo")
    check("get_friends source=service", payload.get("source") == "service",
          payload.get("source"))
    check("get_friends 全链路无 mock 回退", client.last_error == "", client.last_error)

    friends = payload.get("friends") or []
    friend_ids = {item.get("id") for item in friends}
    check("get_friends 返回 3 位好友", len(friends) == 3, len(friends))
    check("get_friends id 集合与真实 DB 一致", friend_ids == EXPECTED_IDS, sorted(friend_ids))
    check("get_friends 含反向关系的好友 b（b→u_demo）", "u_test_rank_b" in friend_ids,
          sorted(friend_ids))
    check("get_friends 排除 status=3 的 d", "u_test_rank_d" not in friend_ids,
          sorted(friend_ids))
    check("get_friends 每条含 id/name/relation",
          all({"id", "name", "relation"} <= set(item) for item in friends),
          [sorted(item) for item in friends])
    check("get_friends relation=friend",
          all(item.get("relation") == "friend" for item in friends),
          [item.get("relation") for item in friends])
    check("get_friends 展示名正确（含缺档案回退）",
          {item.get("id"): item.get("name") for item in friends} == EXPECTED_NAMES,
          {item.get("id"): item.get("name") for item in friends})

    market = SkillMarket(None, client)
    market.seed()

    ok, message, shares = market.share("medical-nutrition", "u_demo", ["u_test_rank_a"])
    check("market.share 可分享给真实好友", ok and len(shares) == 1, message)
    check("market.share 返回分享码", bool(shares and shares[0].get("code")), shares)

    ok, message, shares = market.share("medical-nutrition", "u_demo", ["u_test_rank_d"])
    check("market.share 拒绝非好友 d", not ok and "好友" in message, message)
    check("market.share 拒绝时无分享码", shares == [], shares)

    ok, message, shares = market.share("medical-nutrition", "u_demo", ["u_lin"])
    check("market.share 拒绝 mock 用户 u_lin（未回退 mock）",
          not ok and "好友" in message, message)

    ok, message, shares = market.share("medical-nutrition", "u_demo", ["u_demo"])
    check("market.share 默认拒绝分享给自己", not ok, message)

    received = market.shared_with_me("u_test_rank_a")
    check("market.shared_with_me 能查到刚分享的技能",
          any(row.get("skill_id") == "medical-nutrition" for row in received), received)

    passed = sum(1 for _, ok_item, _ in results if ok_item)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())