#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
网络安全与连接生命周期测试

覆盖：
  1. 超长请求（无换行 60MB）必须被服务端主动断开，RSS 不失控
  2. 连接释放：批量建连/断开后 fd 回落基线，CLOSE-WAIT 为 0
  3. 并发连接：50 条连接并发请求全部得到响应

用法（单实例运行，pgrep -x server 必须唯一）：
  python3 attack_lifecycle_test.py --port 8000
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

_ap = argparse.ArgumentParser()
_ap.add_argument("--host", default="127.0.0.1")
_ap.add_argument("--port", type=int, default=8000)
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


def server_pid():
    out = subprocess.run(["pgrep", "-x", "server"], capture_output=True, text=True).stdout.split()
    return int(out[0]) if out else None


def fd_count(pid):
    try:
        return len(os.listdir(f"/proc/{pid}/fd"))
    except OSError:
        return -1


def rss_kb(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS"):
                    return int(line.split()[1])
    except OSError:
        pass
    return -1


def close_wait():
    out = subprocess.run(["ss", "-tan"], capture_output=True, text=True).stdout
    return sum(1 for l in out.splitlines() if f":{PORT}" in l and "CLOSE-WAIT" in l)


pid = server_pid()
if pid is None:
    print("未找到运行中的 server 进程，请先启动服务")
    sys.exit(2)

print(f"\n== 目标 {HOST}:{PORT} pid={pid} ==")

# ---------- 1. 超长请求 ----------
print("\n[1] 超长请求攻击（60MB，无换行）")
base_rss = rss_kb(pid)
s = socket.create_connection((HOST, PORT), timeout=5)
chunk = b"A" * (1024 * 1024)
closed_by_server = False
try:
    for _ in range(60):
        s.sendall(chunk)
    s.settimeout(3)
    closed_by_server = (s.recv(16) == b"")
except (BrokenPipeError, ConnectionResetError):
    closed_by_server = True
except socket.timeout:
    closed_by_server = False
finally:
    try:
        s.close()
    except Exception:
        pass
time.sleep(0.8)
peak_rss = rss_kb(pid)
check("服务端主动断开超长连接", closed_by_server, f"closed={closed_by_server}")
check("RSS 未随请求线性增长", peak_rss - base_rss < 20 * 1024,
      f"{base_rss}kB -> {peak_rss}kB")

# ---------- 2. 连接生命周期 ----------
print("\n[2] 连接释放 / fd 回收")
time.sleep(0.5)
base_fd = fd_count(pid)
socks = [socket.create_connection((HOST, PORT), timeout=5) for _ in range(30)]
time.sleep(0.5)
peak_fd = fd_count(pid)
for x in socks:
    x.close()
time.sleep(2.0)
after_fd = fd_count(pid)
cw = close_wait()
check("建连时 fd 增长", peak_fd - base_fd >= 25, f"{base_fd} -> {peak_fd}")
check("断开后 fd 回落到基线", abs(after_fd - base_fd) <= 2, f"{base_fd} -> {after_fd}")
check("无 CLOSE-WAIT 残留", cw == 0, f"close-wait={cw}")

# ---------- 3. 并发连接 ----------
print("\n[3] 并发连接（50 条同时心跳）")
conns = []
try:
    for _ in range(50):
        c = socket.create_connection((HOST, PORT), timeout=5)
        c.sendall((json.dumps({"type": 0}) + "\n").encode())
        conns.append(c)
    time.sleep(0.5)
    okcnt = 0
    for c in conns:
        c.settimeout(3)
        try:
            if b'"type":0' in c.recv(4096):
                okcnt += 1
        except socket.timeout:
            pass
    check("50 条并发连接全部响应", okcnt == 50, f"responded={okcnt}/50")
finally:
    for c in conns:
        try:
            c.close()
        except Exception:
            pass
time.sleep(2.0)
fin_fd = fd_count(pid)
check("并发结束后 fd 回落", abs(fin_fd - base_fd) <= 3, f"{base_fd} -> {fin_fd}")
check("并发结束后 CLOSE-WAIT=0", close_wait() == 0, f"close-wait={close_wait()}")

print()
print(f"结果: PASS={ok} FAIL={fail}")
sys.exit(0 if fail == 0 else 1)
