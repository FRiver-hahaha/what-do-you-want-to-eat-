#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
并发登录测试：同一账号 N 个连接同时登录，必须只有 1 个成功。

背景：原实现「先查 state 再更新」，并发下可多端同时登录（实测 8 并发有 4 个成功）。
修复后用 `UPDATE user SET state=1 WHERE id=? AND state=0` 做原子抢占。

用法：
  python3 concurrency_test.py --port 8000 --email bob@t.com --password p2 --n 8
"""
import argparse
import json
import socket
import subprocess
import sys
import threading
import time

_ap = argparse.ArgumentParser()
_ap.add_argument("--host", default="127.0.0.1")
_ap.add_argument("--port", type=int, default=8000)
_ap.add_argument("--email", default="bob@t.com")
_ap.add_argument("--password", default="p2")
_ap.add_argument("--n", type=int, default=8)
_ap.add_argument("--mysql-socket", default="/tmp/mysql.sock")
_ap.add_argument("--mysql-db", default="chatroom")
_A = _ap.parse_args()

HOST, PORT = _A.host, _A.port
ok = fail = 0


def check(name, cond, extra=""):
    global ok, fail
    if cond:
        ok += 1
        print(f"  [PASS] {name} {extra}")
    else:
        fail += 1
        print(f"  [FAIL] {name} {extra}")


def state(email):
    return subprocess.run(
        ["mysql", "--socket=" + _A.mysql_socket, "-uroot", "-N", "-B", "-e",
         f"SELECT state FROM {_A.mysql_db}.user WHERE email='{email}'"],
        capture_output=True, text=True).stdout.strip()


# 确保账号处于离线，避免上一轮残留影响
subprocess.run(["mysql", "--socket=" + _A.mysql_socket, "-uroot", "-e",
                f"UPDATE {_A.mysql_db}.user SET state=0 WHERE email='{_A.email}'"],
               capture_output=True)

N = _A.n
barrier = threading.Barrier(N)
results = [None] * N
sockets = []
lock = threading.Lock()


def worker(i):
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.settimeout(4)
    with lock:
        sockets.append(s)
    barrier.wait()
    s.sendall((json.dumps({"type": 2, "email": _A.email,
                           "password": _A.password}) + "\n").encode())
    time.sleep(1.0)
    try:
        results[i] = s.recv(4096).decode().strip()
    except Exception as e:
        results[i] = f"<{type(e).__name__}>"


print(f"\n== 并发登录 {N} 路 {_A.email} @ {HOST}:{PORT} ==")
ts = [threading.Thread(target=worker, args=(i,)) for i in range(N)]
[t.start() for t in ts]
[t.join() for t in ts]

succ = sum(1 for r in results if r and '"code":1' in r and '"type":2' in r)
print(f"  成功登录数 = {succ}")
for r in results:
    print("   ", (r or "<none>")[:80])

check("并发登录仅 1 路成功", succ == 1, f"success={succ}/{N}")
check("DB state=1", state(_A.email) == "1", "state=" + state(_A.email))

for s in sockets:
    try:
        s.close()
    except Exception:
        pass
time.sleep(1.0)
check("全部断开后 state 归 0", state(_A.email) == "0", "state=" + state(_A.email))

print()
print(f"结果: PASS={ok} FAIL={fail}")
sys.exit(0 if fail == 0 else 1)
