#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
崩溃恢复测试：进程被 kill -9 后，残留的 state=1 不应把账号永久锁死。

原缺陷：UserModel::resetState() 定义了却从未调用。进程崩溃后 user.state 停在 1，
        重启后用户再次登录会一直得到「该账号已在其他设备登录」。
修复后：main() 显式构造单例，启动时执行 UPDATE user SET state=0。

用法：
  python3 crash_recovery_test.py \
      --bin /path/to/build/bin/server \
      --workdir /path/to/build/bin \
      --port 8000 \
      --email bob@t.com --password p2
环境变量沿用当前 shell（CHATROOM_MYSQL_* / CHATROOM_REDIS_* 等）。
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

_ap = argparse.ArgumentParser()
_ap.add_argument("--bin", required=True)
_ap.add_argument("--workdir", default=".")
_ap.add_argument("--host", default="127.0.0.1")
_ap.add_argument("--port", type=int, default=8000)
_ap.add_argument("--email", default="bob@t.com")
_ap.add_argument("--password", default="p2")
_ap.add_argument("--mysql-socket", default="/tmp/mysql.sock")
_ap.add_argument("--mysql-db", default="chatroom")
_ap.add_argument("--log", default="/tmp/crash_recovery.log")
_A = _ap.parse_args()

ok = fail = 0


def check(name, cond, extra=""):
    global ok, fail
    if cond:
        ok += 1
        print(f"  [PASS] {name} {extra}")
    else:
        fail += 1
        print(f"  [FAIL] {name} {extra}")


def state():
    return subprocess.run(
        ["mysql", "--socket=" + _A.mysql_socket, "-uroot", "-N", "-B", "-e",
         f"SELECT state FROM {_A.mysql_db}.user WHERE email='{_A.email}'"],
        capture_output=True, text=True).stdout.strip()


def counts():
    q = ("SELECT (SELECT COUNT(*) FROM %s.user),"
         "(SELECT COUNT(*) FROM %s.friend);" % (_A.mysql_db, _A.mysql_db))
    return subprocess.run(["mysql", "--socket=" + _A.mysql_socket, "-uroot", "-N", "-B",
                           "-e", q], capture_output=True, text=True).stdout.strip()


def wait_port(timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        try:
            s = socket.create_connection((_A.host, _A.port), timeout=1)
            s.close()
            return True
        except OSError:
            time.sleep(0.2)
    return False


def start():
    log = open(_A.log, "ab")
    p = subprocess.Popen([_A.bin], cwd=_A.workdir, stdout=log, stderr=subprocess.STDOUT,
                         env=os.environ.copy(), start_new_session=True)
    if not wait_port():
        print("  [FAIL] 服务未在超时内监听端口")
        sys.exit(1)
    return p


def login(keep=False, timeout=4):
    s = socket.create_connection((_A.host, _A.port), timeout=5)
    s.settimeout(timeout)
    s.sendall((json.dumps({"type": 2, "email": _A.email,
                           "password": _A.password}) + "\n").encode())
    time.sleep(0.4)
    try:
        resp = s.recv(8192).decode().strip()
    except socket.timeout:
        resp = "<timeout>"
    if not keep:
        s.close()
    return s, resp


print(f"\n== 崩溃恢复 {_A.email} @ {_A.host}:{_A.port} ==")

proc = start()
sock, resp = login(keep=True)
check("初始登录成功", '"code":1' in resp, resp[:70])
check("登录后 state=1", state() == "1", "state=" + state())

data_before = counts()
print("  kill -9 ...")
proc.kill()
proc.wait(timeout=10)
time.sleep(1.0)
check("崩溃后 state 残留为 1", state() == "1", "state=" + state())
sock.close()

print("  重启 ...")
proc = start()
time.sleep(1.5)
check("重启后自动复位 state=0", state() == "0", "state=" + state())

sock, resp = login(keep=False)
check("重启后可正常登录（账号未被锁死）", '"code":1' in resp, resp[:70])

data_after = counts()
check("重启后业务数据完整", data_before == data_after,
      f"{data_before} -> {data_after}")

time.sleep(0.8)
proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

print()
print(f"结果: PASS={ok} FAIL={fail}")
sys.exit(0 if fail == 0 else 1)
