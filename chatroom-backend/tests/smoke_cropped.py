#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
裁剪后保留功能全量回归（账号 / 好友 / 在线状态 / 群组不可达 / 聊天不可达 / 持久化）

要求被测服务以 CHATROOM_SMTP_DRY_RUN=1 启动（注册需要真实验证码）。
用法：
  python3 smoke_cropped.py --port 8000 --log /tmp/sec.log
  python3 smoke_cropped.py --port 8000 --log /tmp/sec.log --mysql-socket /tmp/mysql.sock
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
_ap.add_argument("--log", default="/tmp/sec.log", help="服务端 stdout 日志（dry-run 验证码来源）")
_ap.add_argument("--mysql-socket", default="/tmp/mysql.sock")
_ap.add_argument("--mysql-db", default="chatroom")
_A = _ap.parse_args()

HOST, PORT, LOG = _A.host, _A.port, _A.log


class C:
    def __init__(self, tag):
        self.tag = tag
        self.s = socket.create_connection((HOST, PORT), timeout=5)
        self.buf = b""
        self.uid = None

    def send(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())

    def recv(self, timeout=2.0):
        self.s.settimeout(timeout)
        while True:
            p = self.buf.find(b"\n")
            if p >= 0:
                line = self.buf[:p]
                self.buf = self.buf[p + 1:]
                try:
                    return json.loads(line.decode())
                except Exception:
                    return {"_raw": line.decode(errors="replace")}
            try:
                d = self.s.recv(65536)
            except socket.timeout:
                return None
            if not d:
                return None
            self.buf += d

    def call(self, obj, expect=None, timeout=3.0):
        self.send(obj)
        t = obj.get("type")
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = self.recv(max(0.1, deadline - time.time()))
            if r is None:
                return None
            if r.get("type") != t:
                continue
            if expect is not None and r.get("code") != expect:
                continue
            return r
        return None


def sql(q):
    return subprocess.run(
        ["mysql", "--socket=" + _A.mysql_socket, "-uroot", "-N", "-B", "-e", q],
        capture_output=True, text=True).stdout.strip()


def read_code(email):
    try:
        with open(LOG, "r", encoding="utf-8", errors="replace") as f:
            hits = re.findall(r"code for " + re.escape(email) + r" = (\d{6})", f.read())
    except OSError:
        return None
    return hits[-1] if hits else None


def register(c, name, email, password, expect=1):
    """先发码再注册（验证码已是注册必填项）。"""
    c.call({"type": 3, "email": email})
    code = read_code(email)
    if not code:
        return {"code": 0, "msg": "<未取到验证码>"}
    return c.call({"type": 1, "name": name, "email": email,
                   "password": password, "code": code}, expect)


ok = fail = 0


def check(name, cond, extra=""):
    global ok, fail
    if cond:
        ok += 1
        print(f"  [PASS] {name} {extra}")
    else:
        fail += 1
        print(f"  [FAIL] {name} {extra}")


print(f"== 目标 {HOST}:{PORT} ==")

print("== A. 账号管理 ==")
a, b = C("a"), C("b")
r = register(a, "alice", "alice@t.com", "p1")
check("注册 alice（带验证码）", r and r.get("code") == 1, str(r))
r = a.call({"type": 1, "name": "alice2", "email": "alice@t.com",
            "password": "p1", "code": "000000"}, 0)
check("重复邮箱被拒", r and r.get("code") == 0, str(r and r.get("msg")))
r = register(b, "bob", "bob@t.com", "p2")
check("注册 bob", r and r.get("code") == 1, str(r))

r = a.call({"type": 2, "email": "alice@t.com", "password": "wrong"}, 0)
check("错密码被拒", r and r.get("code") == 0, str(r and r.get("msg")))
r = a.call({"type": 2, "email": "alice@t.com", "password": "p1"}, 1)
check("登录 alice", r and r.get("code") == 1 and r.get("id"), str(r and r.get("id")))
a.uid = r.get("id")
r = b.call({"type": 2, "email": "bob@t.com", "password": "p2"}, 1)
b.uid = r.get("id")
check("登录 bob", r and r.get("code") == 1, str(r and r.get("id")))

pw_hash = sql("SELECT password FROM %s.user WHERE id=%d" % (_A.mysql_db, a.uid))
check("密码为 SHA256（非明文）", len(pw_hash) == 64 and pw_hash != "p1", pw_hash[:16] + "...")

a2 = C("a2")
r = a2.call({"type": 2, "email": "alice@t.com", "password": "p1"}, 0)
check("并发登录被拒", r and r.get("code") == 0, str(r and r.get("msg")))
a2.s.close()

r = a.call({"type": 0})
check("心跳", r and r.get("type") == 0, str(r))

print("== B. 好友管理 ==")
r = a.call({"type": 9, "email": "bob@t.com", "my_email": "alice@t.com", "name": "alice"}, 1)
check("alice 发起好友申请", r and r.get("code") == 1, str(r and r.get("msg")))
n = b.recv(1.5)
check("bob 实时收到申请(type9 code2)", n and n.get("type") == 9 and n.get("code") == 2, str(n))
r = b.call({"type": 10, "id": a.uid, "agree": True}, 1)
check("bob 同意", r and r.get("code") == 1, str(r and r.get("msg")))
r = b.call({"type": 8})
fl = r.get("friends", [])
check("bob 好友列表含 alice 且 status=2",
      any(f["id"] == a.uid and f["status"] == 2 for f in fl), str(fl))
check("好友列表 state=1 (alice 在线)",
      any(f["id"] == a.uid and f["state"] == 1 for f in fl), str(fl))
r = a.call({"type": 11, "id": b.uid, "block": True}, 1)
check("alice 拉黑 bob", r and r.get("code") == 1, str(r and r.get("msg")))
check("DB 中 status=3",
      sql("SELECT status FROM %s.friend WHERE user_id=%d AND friend_id=%d"
          % (_A.mysql_db, a.uid, b.uid)) == "3")
r = a.call({"type": 11, "id": b.uid, "block": False}, 1)
check("alice 解除拉黑", r and r.get("code") == 1, str(r and r.get("msg")))

print("== C. 群组功能必须不可达 ==")
for t in (15, 16, 17, 18, 19, 20, 21, 22, 23, 24):
    r = a.call({"type": t, "group_id": 1, "name": "x", "id": b.uid,
                "email": "bob@t.com", "agree": True, "admin": True})
    check(f"群组路由 {t} 不可达", r is None or r.get("type") == 0, str(r))

print("== D. 聊天功能必须不可达 ==")
r = a.call({"type": 13, "rid": b.uid, "msg": "hello bob", "msg_type": False})
check("私聊 13 不可达", r is None or r.get("type") == 0, str(r))
n = b.recv(1.0)
check("bob 不再收到私聊推送", n is None, str(n))
r = a.call({"type": 14, "id": b.uid, "scope": 0})
check("历史 14 不可达", r is None or r.get("type") == 0, str(r))
r = a.call({"type": 25, "rid": 1, "msg": "群消息", "msg_type": False})
check("群聊 25 不可达", r is None or r.get("type") == 0, str(r))
r = a.call({"type": 26, "group_id": 1, "scope": 0})
check("群历史 26 不可达", r is None or r.get("type") == 0, str(r))
r = a.call({"type": 27, "rid": b.uid, "is_group": False})
check("待收文件 27 不可达", r is None or r.get("type") == 0, str(r))
check("message 表已删除",
      sql("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='%s' AND table_name='message'" % _A.mysql_db) == "0")
check("file 表已删除",
      sql("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='%s' AND table_name='file'" % _A.mysql_db) == "0")

print("== E. 在线状态 ==")
r = a.call({"type": 6}, 1)
check("alice 退出登录", r and r.get("code") == 1, str(r and r.get("msg")))
time.sleep(0.4)
check("DB state 归 0", sql("SELECT state FROM %s.user WHERE id=%d" % (_A.mysql_db, a.uid)) == "0")
r = b.call({"type": 8})
fl = r.get("friends", [])
check("bob 看到 alice 离线", any(f["id"] == a.uid and f["state"] == 0 for f in fl), str(fl))

print("== F. 数据持久化 ==")
check("user 表落库", int(sql("SELECT COUNT(*) FROM %s.user" % _A.mysql_db) or 0) >= 2,
      sql("SELECT COUNT(*) FROM %s.user" % _A.mysql_db))
check("friend 双向关系落库",
      int(sql("SELECT COUNT(*) FROM %s.friend WHERE user_id=%d" % (_A.mysql_db, a.uid)) or 0) >= 1,
      sql("SELECT COUNT(*) FROM %s.friend" % _A.mysql_db))
check("group_info 表已删除",
      sql("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='%s' AND table_name='group_info'" % _A.mysql_db) == "0")
check("group_user 表已删除",
      sql("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='%s' AND table_name='group_user'" % _A.mysql_db) == "0")

print()
print(f"结果: PASS={ok} FAIL={fail}")
sys.exit(0 if fail == 0 else 1)
