#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
注销账号级联清理测试：user / friend / group_info / group_user 必须一并清除。

要求服务以 CHATROOM_SMTP_DRY_RUN=1 启动（注册需真实验证码）。
用法：
  python3 account_cascade_test.py --port 8000 --log /tmp/final.log
"""
import argparse
import json
import re
import socket
import subprocess
import sys
import time

_ap = argparse.ArgumentParser()
_ap.add_argument("--host", default="127.0.0.1")
_ap.add_argument("--port", type=int, default=8000)
_ap.add_argument("--log", default="/tmp/final.log")
_ap.add_argument("--mysql-socket", default="/tmp/mysql.sock")
_ap.add_argument("--mysql-db", default="chatroom")
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


def sql(q):
    q = q.replace("chatroom.", _A.mysql_db + ".")
    return subprocess.run(["mysql", "--socket=" + _A.mysql_socket, "-uroot", "-N", "-B",
                           "-e", q], capture_output=True, text=True).stdout.strip()


def read_code(email):
    try:
        with open(_A.log, "r", encoding="utf-8", errors="replace") as f:
            hits = re.findall(r"code for " + re.escape(email) + r" = (\d{6})", f.read())
    except OSError:
        return None
    return hits[-1] if hits else None


class C:
    def __init__(self):
        self.s = socket.create_connection((_A.host, _A.port), timeout=5)
        self.buf = b""

    def call(self, obj, expect=None, timeout=4.0):
        self.s.sendall((json.dumps(obj) + "\n").encode())
        t = obj.get("type")
        end = time.time() + timeout
        while time.time() < end:
            self.s.settimeout(max(0.1, end - time.time()))
            while True:
                i = self.buf.find(b"\n")
                if i < 0:
                    break
                line, self.buf = self.buf[:i], self.buf[i + 1:]
                try:
                    r = json.loads(line.decode())
                except Exception:
                    continue
                if r.get("type") == t and (expect is None or r.get("code") == expect):
                    return r
            try:
                d = self.s.recv(65536)
            except socket.timeout:
                return None
            if not d:
                return None
            self.buf += d
        return None


tag = str(int(time.time()))
email = f"zed{tag}@t.com"
print(f"\n== 注销级联 {email} @ {_A.host}:{_A.port} ==")

c = C()
r = c.call({"type": 3, "email": email})
code = read_code(email)
check("发码", r and r.get("code") == 1 and bool(code), str(r))
r = c.call({"type": 1, "name": "zed", "email": email, "password": "pz", "code": code}, 1)
check("注册", r and r.get("code") == 1, str(r))
r = c.call({"type": 2, "email": email, "password": "pz"}, 1)
uid = r.get("id") if r else None
check("登录", bool(uid), f"uid={uid}")

before = {
    "user": sql(f"SELECT COUNT(*) FROM chatroom.user WHERE id={uid}"),
}
check("注销前数据存在", before["user"] == "1", str(before))

r = c.call({"type": 7}, 1)
check("注销成功", r and r.get("code") == 1, str(r))
time.sleep(0.5)

after = {
    "user": sql(f"SELECT COUNT(*) FROM chatroom.user WHERE id={uid}"),
    "friend": sql(f"SELECT COUNT(*) FROM chatroom.friend WHERE user_id={uid} OR friend_id={uid}"),
}
check("user 已删除", after["user"] == "0", str(after))
check("friend 关系已删除", after["friend"] == "0", str(after))

print()
print(f"结果: PASS={ok} FAIL={fail}")
sys.exit(0 if fail == 0 else 1)
