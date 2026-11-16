#!/usr/bin/env python3
"""P8-S5 连接池耗尽与恢复验证（独立实例 18081，CHATROOM_HTTP_DB_POOL=1）。

做法：用一条独立 MySQL 会话对 meal_records 加写锁并保持，令唯一连接被阻塞；
此时并发请求应被池的限时借用兜底为 service_unavailable（HTTP 200 业务封装），
不产生 5xx、不挂死；释放锁后实例必须自行恢复。
"""

from __future__ import annotations

import json
import subprocess
import threading
import time
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:18081"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
CONCURRENCY = 10

results: list[tuple[str, bool, str]] = []
lock = threading.Lock()
samples: list[dict] = []


def check(name: str, cond: bool, detail: object = "") -> None:
    results.append((name, bool(cond), str(detail)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))


def one() -> None:
    start = time.perf_counter()
    status = 0
    body: dict = {}
    try:
        req = urllib.request.Request(BASE + "/api/v1/users/u_demo/weekly-stats")
        req.add_header("Authorization", f"Bearer {TOKEN}")
        with OPENER.open(req, timeout=20) as resp:
            status = resp.status
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        status = exc.code
        body = {"error": f"http_{exc.code}"}
    except Exception as exc:  # noqa: BLE001
        body = {"error": type(exc).__name__}
    elapsed = time.perf_counter() - start
    with lock:
        samples.append({"status": status, "elapsed": elapsed,
                        "ok": body.get("ok"), "error": body.get("error")})


def health() -> dict:
    req = urllib.request.Request(BASE + "/health")
    with OPENER.open(req, timeout=8) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main() -> int:
    check("实例初始健康", health().get("database") == "ok")

    holder = subprocess.Popen(
        ["mysql", "--socket=/run/mysqld/mysqld.sock", "-uroot", "chatroom_p8",
         "-e", "LOCK TABLES meal_records WRITE; SELECT SLEEP(9); UNLOCK TABLES;"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)

    threads = [threading.Thread(target=one) for _ in range(CONCURRENCY)]
    started = time.perf_counter()
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    wall = time.perf_counter() - started

    ok_count = sum(1 for s in samples if s["ok"] is True)
    unavailable = sum(1 for s in samples if s["error"] == "service_unavailable")
    other_errors = [s for s in samples if s["ok"] is not True
                    and s["error"] != "service_unavailable"]
    worst = max(s["elapsed"] for s in samples)
    reject_worst = max([s["elapsed"] for s in samples
                        if s["error"] == "service_unavailable"] or [0.0])
    print(f"samples={len(samples)} ok={ok_count} unavailable={unavailable} "
          f"other={len(other_errors)} wall={wall:.2f}s worst={worst:.2f}s "
          f"reject_worst={reject_worst:.2f}s")

    check("锁表期间全部请求收到响应（无挂死）", len(samples) == CONCURRENCY, len(samples))
    check("锁表期间无 5xx/网络异常", not other_errors, other_errors[:3])
    check("锁表期间以 HTTP 200 业务封装返回",
          all(s["status"] == 200 for s in samples),
          sorted({s["status"] for s in samples}))
    check("锁表期间出现 service_unavailable（池耗尽被限时兜底）",
          unavailable > 0, unavailable)
    check("借用超时拒绝在 5s 内返回（DbConn 3s 期限）",
          reject_worst < 5.0, reject_worst)
    check("整体无超长挂起（worst < 12s，慢查询受 DB 锁释放约束）",
          worst < 12.0, worst)

    holder.terminate()
    try:
        holder.wait(timeout=5)
    except subprocess.TimeoutExpired:
        holder.kill()
    time.sleep(1.5)

    recovered = health()
    check("释放锁后数据库探活恢复 ok", recovered.get("database") == "ok", recovered)
    one()
    check("释放锁后业务请求恢复正常", samples[-1]["ok"] is True, samples[-1])

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS "
          f"(ok={ok_count}/{CONCURRENCY}, 限时拒绝={unavailable})")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())