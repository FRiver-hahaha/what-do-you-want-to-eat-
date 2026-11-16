#!/usr/bin/env python3
"""P8-S4 数据真实性校验：好友列表响应逐条对比 friend/user/health_profile 表。"""

from __future__ import annotations

import json
import subprocess
import sys
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


def relation_pairs(agent_uid: str, forward: bool) -> set[tuple[str, str]]:
    # 与 DAO 独立实现：分正/反两条独立语句取关系，再由 Python 合并。
    if forward:
        cond = "u1.agent_uid='{}' AND f.user_id=u1.id AND f.friend_id=u2.id"
    else:
        cond = "u1.agent_uid='{}' AND f.friend_id=u1.id AND f.user_id=u2.id"
    rows = q(
        "SELECT u2.agent_uid FROM friend f, user u1, user u2 "
        "WHERE " + cond.format(agent_uid) + " AND f.status=2 "
        "AND u2.agent_uid IS NOT NULL")
    return {row[0] for row in rows}


def main() -> int:
    body = call("/api/v1/users/u_demo/friends")
    friends = body.get("friends") or []
    http_ids = {item.get("id") for item in friends}

    forward = relation_pairs("u_demo", True)
    reverse = relation_pairs("u_demo", False)
    expected = forward | reverse

    check("friend 表 status=2 正向关系 2 条（a/c）", len(forward) == 2, sorted(forward))
    check("friend 表 status=2 反向关系 1 条（b）", reverse == {"u_test_rank_b"},
          sorted(reverse))
    check("HTTP 好友集合等于正/反关系并集", http_ids == expected,
          (sorted(http_ids), sorted(expected)))
    check("HTTP 好友数等于关系并集大小", len(friends) == len(expected),
          (len(friends), len(expected)))

    status_rows = q("SELECT f.status FROM friend f JOIN user u1 ON u1.id=f.user_id "
                    "JOIN user u2 ON u2.id=f.friend_id "
                    "WHERE u1.agent_uid='u_demo' AND u2.agent_uid='u_test_rank_d'")
    check("u_demo→d 关系为 status=3", status_rows == [["3"]], status_rows)
    check("status=3 的 d 未出现在好友列表", "u_test_rank_d" not in http_ids,
          sorted(http_ids))
    d_forward = relation_pairs("u_test_rank_d", True)
    d_reverse = relation_pairs("u_test_rank_d", False)
    check("u_test_rank_d 无 status=2 好友（空列表情形）",
          d_forward | d_reverse == set(), sorted(d_forward | d_reverse))

    rows = q("SELECT u.agent_uid, u.name, COALESCE(p.name,'') FROM user u "
             "LEFT JOIN health_profile p ON p.agent_uid=u.agent_uid "
             "WHERE u.agent_uid IN ('u_test_rank_a','u_test_rank_b','u_test_rank_c',"
             "'u_test_rank_d')")
    expected_names = {uid: (profile or user_name)
                      for uid, user_name, profile in (tuple(r) for r in rows)}
    actual_names = {item.get("id"): item.get("name") for item in friends}
    expected_for_http = {uid: expected_names[uid] for uid in http_ids}
    check("好友展示名与 user/health_profile 表一致",
          actual_names == expected_for_http, (actual_names, expected_for_http))
    check("缺档案的 c 回退 user.name=测试丙",
          actual_names.get("u_test_rank_c") == "测试丙",
          actual_names.get("u_test_rank_c"))
    check("relation 字段全部为 friend",
          all(item.get("relation") == "friend" for item in friends),
          [item.get("relation") for item in friends])

    mock_ids = {"u_lin", "u_zhou", "u_he"}
    check("响应不含任何 mock 好友", not (http_ids & mock_ids), sorted(http_ids & mock_ids))

    passed = sum(1 for _, ok in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())