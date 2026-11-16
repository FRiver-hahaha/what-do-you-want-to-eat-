#!/usr/bin/env python3
"""P8-S5 MySQL 异常与恢复验证（真实停库/起库，实例 18080）。

断言：停库期间以 HTTP 200 + service_unavailable 表达、无 5xx、无挂死；
/health 明确报告 database 异常；起库后无需重启服务即可自愈。
"""

from __future__ import annotations

import json
import subprocess
import time
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))

results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: object = "") -> None:
    results.append((name, bool(cond)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))


def call(path: str, timeout: float = 20.0) -> tuple[int, dict, float]:
    start = time.perf_counter()
    try:
        req = urllib.request.Request(BASE + path)
        req.add_header("Authorization", f"Bearer {TOKEN}")
        with OPENER.open(req, timeout=timeout) as resp:
            body = json.loads(resp.read().decode("utf-8"))
            return resp.status, body, time.perf_counter() - start
    except urllib.error.HTTPError as exc:
        return exc.code, {"raw": exc.read().decode("utf-8", "replace")}, \
            time.perf_counter() - start
    except Exception as exc:  # noqa: BLE001
        return 0, {"error": type(exc).__name__}, time.perf_counter() - start


def systemctl(action: str) -> None:
    subprocess.run(["systemctl", action, "mysql"], check=True,
                   capture_output=True, text=True)


def mysql_alive() -> bool:
    out = subprocess.run(
        ["mysqladmin", "--socket=/run/mysqld/mysqld.sock", "-uroot", "ping"],
        capture_output=True, text=True)
    return "alive" in out.stdout


def main() -> int:
    status, body, _ = call("/api/v1/users/u_demo/weekly-stats")
    check("D0 停库前业务请求正常", status == 200 and body.get("ok") is True,
          (status, body.get("error")))
    check("D0 停库前 health ok",
          call("/health")[1].get("database") == "ok")

    systemctl("stop")
    for _ in range(20):
        if not mysql_alive():
            break
        time.sleep(0.5)
    check("D1 MySQL 已停止", not mysql_alive())

    down = [call("/api/v1/users/u_demo/weekly-stats") for _ in range(3)]
    check("D2 停库期间无 5xx/网络异常",
          all(s == 200 for s, _, _ in down), [s for s, _, _ in down])
    check("D3 停库期间返回 service_unavailable",
          all(b.get("ok") is False and b.get("error") == "service_unavailable"
              for _, b, _ in down),
          [b.get("error") for _, b, _ in down])
    check("D4 停库期间限时返回（< 8s）",
          max(t for _, _, t in down) < 8.0, max(t for _, _, t in down))

    status, body, _ = call("/health")
    check("D5 health 明确报告数据库异常",
          body.get("ok") is False or body.get("database") != "ok", body)

    systemctl("start")
    deadline = time.time() + 40
    while time.time() < deadline and not mysql_alive():
        time.sleep(1.0)
    check("D6 MySQL 已恢复运行", mysql_alive())

    recovered = None
    for _ in range(20):
        status, body, elapsed = call("/api/v1/users/u_demo/weekly-stats")
        if status == 200 and body.get("ok") is True:
            recovered = (status, body, elapsed)
            break
        time.sleep(1.0)
    check("D7 起库后业务请求自愈（服务未重启）",
          recovered is not None, (status, body.get("error")))
    if recovered:
        check("D8 自愈后仍走真实数据源",
              recovered[1].get("source") == "service", recovered[1].get("source"))
        print(f"恢复耗时 {recovered[2]:.2f}s")

    passed = sum(1 for _, ok in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    raise SystemExit(main())