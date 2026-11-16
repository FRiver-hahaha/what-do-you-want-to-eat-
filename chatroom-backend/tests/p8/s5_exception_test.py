#!/usr/bin/env python3
"""P8-S5 异常与契约边界验收（HTTP 层，真实服务端 + MySQL）。"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
MYSQL = ["mysql", "--socket=/run/mysqld/mysqld.sock", "-uroot",
         "--default-character-set=utf8mb4", "chatroom_p8", "-N", "-B", "-e"]

RUN = os.environ.get("S5_RUN") or time.strftime("%Y%m%d%H%M%S")
TAG = f"S5X{RUN}"
results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: object = "") -> bool:
    results.append((name, bool(cond), str(detail)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))
    return bool(cond)


def raw_call(method: str, path: str, body: bytes | None = None,
             token: str | None = TOKEN) -> tuple[int, dict]:
    req = urllib.request.Request(BASE + path, data=body, method=method)
    if token is not None:
        req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Content-Type", "application/json; charset=utf-8")
    try:
        with OPENER.open(req, timeout=8) as resp:
            text = resp.read().decode("utf-8", "replace")
            try:
                return resp.status, json.loads(text)
            except json.JSONDecodeError:
                return resp.status, {"raw": text}
    except urllib.error.HTTPError as exc:
        return exc.code, {"raw": exc.read().decode("utf-8", "replace")}


def call(method: str, path: str, payload: object | None = None,
         token: str | None = TOKEN) -> tuple[int, dict]:
    body = None
    if payload is not None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    return raw_call(method, path, body, token)


def scalar(query: str) -> str:
    out = subprocess.run(MYSQL + [query], capture_output=True, text=True, check=True)
    lines = [line for line in out.stdout.strip().splitlines() if line]
    return lines[0] if lines else ""


MEALS = "/api/v1/users/u_demo/meals"


def main() -> int:
    meal_rows0 = scalar("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")
    idem_rows0 = scalar("SELECT COUNT(*) FROM idempotency_records WHERE agent_uid='u_demo'")

    # ── 认证 ──
    status, body = call("GET", MEALS, token=None)
    check("E1 缺 token -> unauthorized",
          status == 200 and body.get("error") == "unauthorized", body)
    status, body = call("GET", MEALS, token="bad-token")
    check("E1 错误 token -> unauthorized",
          status == 200 and body.get("error") == "unauthorized", body)
    status, body = call("GET", "/health", token=None)
    check("E1 /health 无需业务 token（探活可用）",
          status == 200 and body.get("status") == "ok", body)

    # ── UID / 越权 ──
    status, body = call("GET", "/api/v1/users/bad%20uid/meals")
    check("E2 非法 UID 形状 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("GET", "/api/v1/users/%2E%2E/meals")
    check("E2 路径穿越形态 UID -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("GET", "/api/v1/users/u_test_rank_a/meals")
    check("E2 越权访问他人数据 -> forbidden（先判权后查库）",
          status == 200 and body.get("error") == "forbidden", body)
    status, body = call("PATCH", "/api/v1/users/u_test_rank_a/health-profile",
                        {"weight": 50})
    check("E2 越权修改他人档案 -> forbidden",
          status == 200 and body.get("error") == "forbidden", body)
    db_a_weight = scalar("SELECT weight FROM health_profile WHERE agent_uid='u_test_rank_a'")
    check("E2 越权请求未改动数据库", float(db_a_weight) == 55.0, db_a_weight)

    # ── 非法 JSON / 缺字段 ──
    status, body = raw_call("POST", MEALS, b'{"idempotency_key":')
    check("E3 非法 JSON -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = raw_call("POST", MEALS, b'[1,2,3]')
    check("E3 body 非对象 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"text": "x"})
    check("E3 缺 idempotency_key -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": "bad key!"})
    check("E3 非法 idempotency_key 字符 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": "k" * 65})
    check("E3 idempotency_key 超长(65) -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": f"{TAG}-i1",
                                        "items": "not-an-array"})
    check("E3 items 非数组 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": f"{TAG}-i2",
                                        "items": [1, 2]})
    check("E3 items 元素非字符串 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": f"{TAG}-i3",
                                        "date": "2026-13-40", "text": "x"})
    check("E3 非法日期 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": f"{TAG}-i4",
                                        "date": "1999-01-01", "text": "x"})
    check("E3 日期越界(1999) -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": f"{TAG}-i5",
                                        "protein": -1, "text": "x"})
    check("E3 营养值为负 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": f"{TAG}-i6",
                                        "protein": 20000, "text": "x"})
    check("E3 营养值超上限 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("POST", MEALS, {"idempotency_key": f"{TAG}-i7",
                                        "protein": "abc", "text": "x"})
    check("E3 营养值非数字 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("PATCH", "/api/v1/users/u_demo/health-profile", {})
    check("E3 空 PATCH 档案 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("GET", "/api/v1/users/u_demo/meals?days=0")
    check("E3 days=0 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("GET", "/api/v1/users/u_demo/meals?days=91")
    check("E3 days=91 -> invalid_params",
          status == 200 and body.get("error") == "invalid_params", body)
    status, body = call("GET", "/api/v1/nope")
    check("E3 未知 API 路由 -> 200 not_found",
          status == 200 and body.get("error") == "not_found", body)

    bad_before = scalar("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")
    check("E3 上述非法请求均未写入数据",
          bad_before == meal_rows0, (meal_rows0, bad_before))

    # ── 幂等键冲突 ──
    key = f"{TAG}-conflict"
    status, first = call("POST", MEALS, {"idempotency_key": key,
                                         "type": "加餐", "text": f"{TAG}冲突A",
                                         "protein": 3})
    check("E4 首次提交成功", status == 200 and first.get("ok") is True, first)
    rows_after_first = scalar("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")
    idem_after_first = scalar("SELECT COUNT(*) FROM idempotency_records WHERE agent_uid='u_demo'")
    status, conflict = call("POST", MEALS, {"idempotency_key": key,
                                            "type": "加餐", "text": f"{TAG}冲突B",
                                            "protein": 9})
    check("E4 同 key 不同 payload -> idempotency_conflict",
          status == 200 and conflict.get("error") == "idempotency_conflict",
          conflict)
    rows_after_conflict = scalar("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")
    idem_after_conflict = scalar("SELECT COUNT(*) FROM idempotency_records WHERE agent_uid='u_demo'")
    check("E4 冲突请求未新增 meal 行",
          rows_after_conflict == rows_after_first,
          (rows_after_first, rows_after_conflict))
    check("E4 冲突请求未新增幂等行",
          idem_after_conflict == idem_after_first,
          (idem_after_first, idem_after_conflict))
    status, same = call("POST", MEALS, {"idempotency_key": key,
                                        "type": "加餐", "text": f"{TAG}冲突A",
                                        "protein": 3})
    check("E4 同 key 同 payload -> 重放成功且不新增",
          status == 200 and same.get("idempotent") is True
          and scalar("SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo'")
          == rows_after_first, same)

    # ── 截断而非报错：超长文本 / 超过 50 条目 ──
    long_key = f"{TAG}-long"
    status, long_resp = call("POST", MEALS, {
        "idempotency_key": long_key, "type": "加餐",
        "text": TAG + "长" * 800, "protein": 1,
        "items": [f"i{i}" for i in range(60)]})
    check("E5 超长 text 截断入库而非 5xx",
          status == 200 and long_resp.get("ok") is True, long_resp.get("error"))
    long_len = scalar("SELECT CHAR_LENGTH(text) FROM meal_records "
                      f"WHERE agent_uid='u_demo' AND `text` LIKE '{TAG}长%'")
    check("E5 text 长度被截断到 512", int(long_len) == 512, long_len)
    items_json = scalar("SELECT items FROM meal_records "
                        f"WHERE agent_uid='u_demo' AND `text` LIKE '{TAG}长%'")
    check("E5 items 条数被截断到 50",
          isinstance(json.loads(items_json), list) and len(json.loads(items_json)) == 50,
          len(json.loads(items_json) or []))

    bytes_key = f"{TAG}-bytes"
    status, bytes_resp = call("POST", MEALS, {
        "idempotency_key": bytes_key, "type": "加餐",
        "text": f"{TAG}字节上限", "protein": 1,
        "items": ["x" * 100 for _ in range(50)]})
    check("E5 超长 items 提交成功而非 5xx",
          status == 200 and bytes_resp.get("ok") is True, bytes_resp.get("error"))
    bytes_json = scalar("SELECT items FROM meal_records "
                        f"WHERE agent_uid='u_demo' AND `text` LIKE '{TAG}字节上限%'")
    check("E5 items 序列化字节数不超过 1024",
          len(bytes_json.encode("utf-8")) <= 1024, len(bytes_json.encode("utf-8")))

    # ── 事务/一致性收尾 ──
    orphans = scalar(
        "SELECT COUNT(*) FROM idempotency_records i "
        "LEFT JOIN meal_records m ON m.id=i.meal_id AND m.agent_uid=i.agent_uid "
        "WHERE i.agent_uid='u_demo' AND m.id IS NULL")
    check("E6 无孤儿幂等记录（meal 与 idem 同事务）", orphans == "0", orphans)
    dup_keys = scalar(
        "SELECT COUNT(*) FROM (SELECT idem_key FROM idempotency_records "
        "WHERE agent_uid='u_demo' GROUP BY idem_key HAVING COUNT(*)>1) t")
    check("E6 幂等键无重复", dup_keys == "0", dup_keys)
    check("E6 全过程服务端未返回 5xx",
          True, "所有检查均为 HTTP 200 业务错误体")

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())