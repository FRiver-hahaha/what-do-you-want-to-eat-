#!/usr/bin/env python3
"""P8-S4 HTTP 契约与真实 DB 行为测试（好友列表）。"""

from __future__ import annotations

import json
import sys
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))

FRIENDS = "/api/v1/users/u_demo/friends"
EXPECTED_IDS = {"u_test_rank_a", "u_test_rank_b", "u_test_rank_c"}
EXPECTED_NAMES = {
    "u_test_rank_a": "甲同学",
    "u_test_rank_b": "乙同学",
    "u_test_rank_c": "测试丙",
}
MOCK_IDS = {"u_lin", "u_zhou", "u_he"}

results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: object = "") -> bool:
    results.append((name, bool(cond), str(detail)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))
    return bool(cond)


def call(method: str, path: str, token: str | None = TOKEN) -> tuple[int, dict]:
    req = urllib.request.Request(BASE + path, method=method)
    if token is not None:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with OPENER.open(req, timeout=5) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return exc.code, {"raw": exc.read().decode("utf-8", "replace")}


def main() -> int:
    status, health = call("GET", "/health")
    check("health 200 且 version=p8-s4",
          status == 200 and health.get("version") == "p8-s4", health)

    status, body = call("GET", FRIENDS)
    check("friends 200 + ok", status == 200 and body.get("ok") is True, body)
    check("friends source=service", body.get("source") == "service", body.get("source"))
    check("friends 回显 user_id=u_demo", body.get("user_id") == "u_demo", body.get("user_id"))

    friends = body.get("friends")
    check("friends 为数组", isinstance(friends, list), type(friends).__name__)
    friends = friends or []
    friend_ids = [item.get("id") for item in friends]

    check("friends 返回 3 位正常好友", len(friends) == 3, len(friends))
    check("friends id 集合为 a/b/c", set(friend_ids) == EXPECTED_IDS, sorted(friend_ids))
    check("friends 排除 status=3 的 u_test_rank_d", "u_test_rank_d" not in friend_ids,
          sorted(friend_ids))
    check("friends 不含自己", "u_demo" not in friend_ids, sorted(friend_ids))
    check("friends id 无重复", len(set(friend_ids)) == len(friend_ids), friend_ids)
    check("friends 不含任何 mock 用户", not (set(friend_ids) & MOCK_IDS), sorted(friend_ids))

    check("friends 每条含 id/name/relation",
          all({"id", "name", "relation"} <= set(item) for item in friends),
          [sorted(item) for item in friends])
    check("friends id 均为非空字符串",
          all(isinstance(item.get("id"), str) and item["id"] for item in friends),
          friend_ids)
    check("friends relation 均为 friend",
          all(item.get("relation") == "friend" for item in friends),
          [item.get("relation") for item in friends])

    names = {item.get("id"): item.get("name") for item in friends}
    check("friends 展示名按档案名/用户名列回退", names == EXPECTED_NAMES, names)
    check("friends 按 agent_uid 升序稳定",
          friend_ids == sorted(EXPECTED_IDS), friend_ids)

    status, missing = call("GET", "/api/v1/users/u_nobody/friends")
    check("friends 非白名单用户返回 forbidden",
          status == 200 and missing.get("error") == "forbidden", missing)

    status, bad_uid = call("GET", "/api/v1/users/bad%20uid/friends")
    check("friends 非法 UID 形状返回 invalid_params",
          status == 200 and bad_uid.get("error") == "invalid_params", bad_uid)

    status, no_token = call("GET", FRIENDS, token=None)
    check("friends 缺 token 返回 unauthorized",
          status == 200 and no_token.get("error") == "unauthorized", no_token)

    status, wrong_token = call("GET", FRIENDS, token="wrong-token")
    check("friends 错误 token 返回 unauthorized",
          status == 200 and wrong_token.get("error") == "unauthorized", wrong_token)

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())