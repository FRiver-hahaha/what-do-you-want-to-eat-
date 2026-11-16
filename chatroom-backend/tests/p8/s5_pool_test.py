#!/usr/bin/env python3
"""P8-S5 连接池耗尽行为验证（针对独立实例：CHATROOM_HTTP_DB_POOL=1）。

断言：并发突发下服务不返回 5xx、不挂死；池超时以 HTTP200 + service_unavailable
表达，且超时后服务可自行恢复。
"""

from __future__ import annotations

import json
import statistics
import threading
import time
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:18081"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
CONCURRENCY = 30

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
    except Exception as exc:  # noqa: BLE001
        body = {"error": type(exc).__name__}
    elapsed = time.perf_counter() - start
    with lock:
        samples.append({"status": status, "elapsed": elapsed,
                        "ok": body.get("ok"), "error": body.get("error")})


def main() -> int:
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
    median = statistics.median([s["elapsed"] for s in samples])

    print(f"samples={len(samples)} ok={ok_count} unavailable={unavailable} "
          f"wall={wall:.2f}s worst={worst:.2f}s median={median:.2f}s")
    check("30 并发请求全部收到响应（无挂死）", len(samples) == CONCURRENCY,
          len(samples))
    check("无任何 5xx/网络异常", not other_errors, other_errors[:3])
    check("all responses are HTTP 200 business envelope",
          all(s["status"] in (200, 0) for s in samples),
          sorted({s["status"] for s in samples}))
    check("池超时被限时兜底（worst < 8s）", worst < 8.0, worst)
    check("至少部分请求成功（池未完全饿死）", ok_count > 0, ok_count)

    time.sleep(1.0)
    req = urllib.request.Request(BASE + "/health")
    with OPENER.open(req, timeout=5) as resp:
        health = json.loads(resp.read().decode("utf-8"))
    check("突发后实例健康检查恢复 ok",
          health.get("status") == "ok" and health.get("database") == "ok", health)

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS "
          f"(ok={ok_count}/30, 限时拒绝={unavailable})")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())