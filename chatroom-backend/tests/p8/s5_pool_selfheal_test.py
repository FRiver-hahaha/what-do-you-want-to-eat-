#!/usr/bin/env python3
"""P8-S5 连接池自愈与容量约束验证（独立实例 18082，CHATROOM_HTTP_DB_POOL=4）。

覆盖三项重点：
1) 停库期间并发请求打空连接池后，起库能自动重建连接（服务不重启）；
2) 多线程同时补建时连接数不超过配置容量（information_schema.processlist 采样）；
3) 多轮故障-恢复后连接数不持续增长（无名额泄漏/重复计数）。

前置：18080 实例必须已停止，避免其连接被计入 processlist。
"""

from __future__ import annotations

import json
import subprocess
import threading
import time
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:18082"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
CAPACITY = 4
BURST = 12
ROUNDS = 3
MYSQL = ["mysql", "--socket=/run/mysqld/mysqld.sock", "-uroot", "-N", "-B", "-e"]

results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: object = "") -> None:
    results.append((name, bool(cond), str(detail)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))


def one() -> dict:
    start = time.perf_counter()
    try:
        req = urllib.request.Request(BASE + "/api/v1/users/u_demo/weekly-stats")
        req.add_header("Authorization", f"Bearer {TOKEN}")
        with OPENER.open(req, timeout=20) as resp:
            body = json.loads(resp.read().decode("utf-8"))
            return {"status": resp.status, "ok": body.get("ok"),
                    "error": body.get("error"), "elapsed": time.perf_counter() - start}
    except urllib.error.HTTPError as exc:
        return {"status": exc.code, "ok": None, "error": f"http_{exc.code}",
                "elapsed": time.perf_counter() - start}
    except Exception as exc:  # noqa: BLE001
        return {"status": 0, "ok": None, "error": type(exc).__name__,
                "elapsed": time.perf_counter() - start}


def burst(n: int) -> list[dict]:
    out: list[dict] = []
    lock = threading.Lock()

    def worker() -> None:
        got = one()
        with lock:
            out.append(got)

    threads = [threading.Thread(target=worker) for _ in range(n)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return out


def burst_with_sampling(n: int) -> tuple[list[dict], int]:
    peak = [0]
    stop = threading.Event()

    def sampler() -> None:
        while not stop.is_set():
            try:
                cur = int(scalar("SELECT COUNT(*) FROM information_schema.processlist "
                                 "WHERE user='root' AND db='chatroom_p8'") or 0)
                peak[0] = max(peak[0], cur)
            except Exception:  # noqa: BLE001
                pass
            time.sleep(0.02)

    watcher = threading.Thread(target=sampler, daemon=True)
    watcher.start()
    try:
        got = burst(n)
    finally:
        stop.set()
        watcher.join(timeout=3)
    return got, peak[0]


def scalar(sql: str) -> str:
    out = subprocess.run(MYSQL + [sql], capture_output=True, text=True, check=False)
    lines = [line for line in out.stdout.strip().splitlines() if line]
    return lines[0] if lines else ""


def mysql_alive() -> bool:
    out = subprocess.run(
        ["mysqladmin", "--socket=/run/mysqld/mysqld.sock", "-uroot", "ping"],
        capture_output=True, text=True)
    return "alive" in out.stdout


def systemctl(action: str) -> None:
    subprocess.run(["systemctl", action, "mysql"], check=True,
                   capture_output=True, text=True)


def wait_mysql(up: bool, seconds: float = 40) -> bool:
    deadline = time.time() + seconds
    while time.time() < deadline:
        if mysql_alive() is up:
            return True
        time.sleep(0.5)
    return mysql_alive() is up


def main() -> int:
    initial = one()
    check("H0 实例初始可用",
          initial["status"] == 200 and initial["ok"] is True, initial)

    conns0 = int(scalar("SELECT COUNT(*) FROM information_schema.processlist "
                        "WHERE user='root' AND db='chatroom_p8'") or 0)
    check("H1 稳态连接数不超过容量", conns0 <= CAPACITY, conns0)

    healed_all = True
    for rnd in range(1, ROUNDS + 1):
        systemctl("stop")
        if not wait_mysql(False, 20):
            check(f"H{rnd} MySQL 已停止", False, "stop timeout")
            return 1

        down = burst(BURST)
        down_unavailable = sum(1 for s in down if s["error"] == "service_unavailable")
        down_5xx = [s for s in down if s["status"] not in (200, 0)]
        check(f"H{rnd} 停库并发无 5xx/网络异常", not down_5xx, down_5xx[:3])
        check(f"H{rnd} 停库并发全部限时兜底",
              down_unavailable == BURST, down_unavailable)
        check(f"H{rnd} 停库并发无超长挂起",
              max(s["elapsed"] for s in down) < 8.0,
              max(s["elapsed"] for s in down))

        systemctl("start")
        if not wait_mysql(True, 40):
            check(f"H{rnd} MySQL 已恢复运行", False, "start timeout")
            return 1

        up, peak = burst_with_sampling(BURST)
        up_ok = sum(1 for s in up if s["ok"] is True)
        check(f"H{rnd} 起库后自动重建连接（{up_ok}/{BURST} 成功）",
              up_ok == BURST, [s["error"] for s in up if s["ok"] is not True][:3])
        check(f"H{rnd} 并发补建连接数不超过容量({CAPACITY})",
              peak <= CAPACITY, peak)
        healed_all = healed_all and up_ok == BURST

        time.sleep(0.5)
        conns = int(scalar("SELECT COUNT(*) FROM information_schema.processlist "
                           "WHERE user='root' AND db='chatroom_p8'") or 0)
        check(f"H{rnd} 恢复后连接数不超过容量且无泄漏",
              conns <= CAPACITY, conns)

    check("H9 多轮故障-恢复均自愈", healed_all)
    check("H10 最终健康检查 ok",
          one()["ok"] is True and json.loads(
              OPENER.open(urllib.request.Request(BASE + "/health"), timeout=5)
              .read().decode("utf-8")).get("database") == "ok")

    passed = sum(1 for _, ok_, _ in results if ok_)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())