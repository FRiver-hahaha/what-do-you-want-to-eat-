#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
原始仓库（裁剪前）基线回归：42 项真实请求。
用途：仅用于在 a1e654b 原始代码上复现「改造前功能完好」，作为对比基线。
      注意：该版本注册不校验验证码，故本脚本故意不带 code 字段。

用法：
  python3 smoke_baseline.py --port 8000
"""
import argparse, socket, json, time, subprocess, sys

_ap = argparse.ArgumentParser()
_ap.add_argument("--host", default="127.0.0.1")
_ap.add_argument("--port", type=int, default=8000)
_ap.add_argument("--mysql-socket", default="/tmp/mysql.sock")
_ap.add_argument("--mysql-db", default="chatroom")
_A = _ap.parse_args()
HOST, PORT = _A.host, _A.port

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
                self.buf = self.buf[p+1:]
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

def redis(*a):
    return subprocess.run(["redis-cli"] + list(a), capture_output=True, text=True).stdout.strip()

def sql(q):
    return subprocess.run(["mysql", "--socket=" + _A.mysql_socket, "-uroot", "-N", "-B",
                           "-e", q], capture_output=True, text=True).stdout.strip()

ok = fail = 0
def check(name, cond, extra=""):
    global ok, fail
    if cond:
        ok += 1
        print(f"  [PASS] {name} {extra}")
    else:
        fail += 1
        print(f"  [FAIL] {name} {extra}")

print("== A. 账号管理 ==")
a, b = C("a"), C("b")
r = a.call({"type": 1, "name": "alice", "email": "alice@t.com", "password": "p1"}, 1)
check("注册 alice", r and r.get("code") == 1, str(r))
r = a.call({"type": 1, "name": "alice2", "email": "alice@t.com", "password": "p1"}, 0)
check("重复邮箱被拒", r and r.get("code") == 0, str(r.get("msg")))
r = b.call({"type": 1, "name": "bob", "email": "bob@t.com", "password": "p2"}, 1)
check("注册 bob", r and r.get("code") == 1, str(r))

r = a.call({"type": 2, "email": "alice@t.com", "password": "wrong"}, 0)
check("错密码被拒", r and r.get("code") == 0, str(r.get("msg")))
r = a.call({"type": 2, "email": "alice@t.com", "password": "p1"}, 1)
check("登录 alice", r and r.get("code") == 1 and r.get("id"), str(r.get("id")))
a.uid = r.get("id")
r = b.call({"type": 2, "email": "bob@t.com", "password": "p2"}, 1)
b.uid = r.get("id")

a2 = C("a2")
r = a2.call({"type": 2, "email": "alice@t.com", "password": "p1"}, 0)
check("重复登录被拒", r and r.get("code") == 0, str(r.get("msg")))

r = a.call({"type": 0})
check("心跳", r and r.get("type") == 0, str(r))

print("== B. 好友管理 ==")
r = a.call({"type": 9, "email": "bob@t.com", "my_email": "alice@t.com", "name": "alice"}, 1)
check("alice 发起好友申请", r and r.get("code") == 1, str(r.get("msg")))
n = b.recv(1.5)
check("bob 实时收到申请(type9 code2)", n and n.get("type") == 9 and n.get("code") == 2, str(n))
r = b.call({"type": 10, "id": a.uid, "agree": True}, 1)
check("bob 同意", r and r.get("code") == 1, str(r.get("msg")))
r = b.call({"type": 8})
fl = r.get("friends", [])
check("bob 好友列表含 alice 且 status=2", any(f["id"] == a.uid and f["status"] == 2 for f in fl), str(fl))
check("好友列表 state=1 (alice 在线)", any(f["id"] == a.uid and f["state"] == 1 for f in fl), str(fl))
r = a.call({"type": 11, "id": b.uid, "block": True}, 1)
check("alice 拉黑 bob", r and r.get("code") == 1, str(r.get("msg")))
check("DB 中 status=3", sql("SELECT status FROM %s.friend WHERE user_id=%d AND friend_id=%d" % (_A.mysql_db, a.uid, b.uid)) == "3")
r = a.call({"type": 11, "id": b.uid, "block": False}, 1)
check("alice 解除拉黑", r and r.get("code") == 1, str(r.get("msg")))

print("== C. 群组管理 ==")
r = a.call({"type": 16, "name": "研发群"}, 1)
gid = r.get("group_id") if r else None
check("建群", r and r.get("code") == 1 and gid, f"gid={gid}")
r = a.call({"type": 15})
check("我的群列表", r and any(g["id"] == gid for g in r.get("msg", [])), str(r.get("msg")))
r = a.call({"type": 17, "group_id": gid, "id": b.uid}, 1)
check("邀请 bob 入群", r and r.get("code") == 1, str(r.get("msg")))
n = b.recv(1.5)
check("bob 收到邀请通知(type17 code2)", n and n.get("type") == 17 and n.get("code") == 2, str(n))

c = C("c")
c.call({"type": 1, "name": "carol", "email": "carol@t.com", "password": "p3"}, 1)
r = c.call({"type": 2, "email": "carol@t.com", "password": "p3"}, 1)
c.uid = r.get("id")
r = c.call({"type": 18, "group_id": gid, "email": "carol@t.com", "name": "carol"}, 1)
check("carol 申请入群", r and r.get("code") == 1, str(r.get("msg")))
n = a.recv(1.5)
check("群主收到入群申请(type18 code2)", n and n.get("type") == 18 and n.get("code") == 2, str(n))
r = c.call({"type": 20, "group_id": gid})
check("未入群者查成员被拒", r and r.get("code") == 0, str(r.get("msg")))
r = a.call({"type": 19, "group_id": gid, "id": c.uid, "agree": True}, 1)
check("群主批准入群", r and r.get("code") == 1, str(r.get("msg")))
r = a.call({"type": 20, "group_id": gid})
mem = r.get("members", [])
roles = {m["id"]: m["role"] for m in mem}
check("成员列表 3 人且角色正确", len(mem) == 3 and roles.get(a.uid) == 3 and roles.get(b.uid) == 1 and roles.get(c.uid) == 1, str(roles))
r = a.call({"type": 21, "group_id": gid, "id": b.uid, "admin": True}, 1)
check("设 bob 为管理员", r and r.get("code") == 1, str(r.get("msg")))
r = b.call({"type": 21, "group_id": gid, "id": c.uid, "admin": True}, 0)
check("管理员无权设管理员", r and r.get("code") == 0, str(r.get("msg")))
r = b.call({"type": 22, "group_id": gid, "id": a.uid})
check("管理员不能踢群主", r and r.get("code") == 0, str(r.get("msg")))
r = b.call({"type": 22, "group_id": gid, "id": c.uid}, 1)
check("管理员踢出成员", r and r.get("code") == 1, str(r.get("msg")))
r = c.call({"type": 20, "group_id": gid})
check("被踢者查成员被拒", r and r.get("code") == 0, str(r.get("msg")))
r = a.call({"type": 21, "group_id": gid, "id": b.uid, "admin": False}, 1)
check("撤销 bob 管理员", r and r.get("code") == 1, str(r.get("msg")))
r = b.call({"type": 23, "group_id": gid}, 1)
check("bob 退群", r and r.get("code") == 1, str(r.get("msg")))
r = a.call({"type": 23, "group_id": gid})
check("群主不能直接退群", r and r.get("code") == 0, str(r.get("msg")))

print("== D. 聊天功能(待裁剪) ==")
b.call({"type": 17, "group_id": gid, "id": a.uid}, 0)  # bob 非管理员, 预期被拒
a.call({"type": 17, "group_id": gid, "id": b.uid}, 1)  # 重新邀请 bob
time.sleep(0.2); b.recv(0.3)
r = a.call({"type": 13, "rid": b.uid, "msg": "hello bob", "msg_type": False}, 1)
check("私聊发送成功", r and r.get("code") == 1, str(r))
n = b.recv(1.5)
check("bob 实时收到私聊(type13 code2)", n and n.get("type") == 13, str(n))
r = a.call({"type": 14, "id": b.uid, "scope": 0})
check("私聊历史", r and isinstance(r.get("msg"), list) and len(r["msg"]) >= 1, str(r.get("msg"))[:120])
r = a.call({"type": 25, "rid": gid, "msg": "群消息", "msg_type": False}, 1)
check("群聊发送成功", r and r.get("code") == 1, str(r))
r = a.call({"type": 26, "group_id": gid, "scope": 0})
check("群聊历史", r and isinstance(r.get("msg"), list) and len(r["msg"]) >= 1, str(r.get("msg"))[:120])

print("== E. 在线状态 ==")
r = a.call({"type": 6}, 1)
check("alice 退出登录", r and r.get("code") == 1, str(r.get("msg")))
time.sleep(0.4)
check("DB state 归 0", sql("SELECT state FROM %s.user WHERE id=%d" % (_A.mysql_db, a.uid)) == "0")
r = b.call({"type": 8})
fl = r.get("friends", [])
check("bob 看到 alice 离线", any(f["id"] == a.uid and f["state"] == 0 for f in fl), str(fl))

print("== F. 离线通知链路 ==")
c2 = C("c2")  # carol 已离线(被踢后未登出, 用新连接测)
d = C("d")
d.call({"type": 1, "name": "dave", "email": "dave@t.com", "password": "p4"}, 1)
d.s.close()          # dave 不登录 -> 完全离线
time.sleep(0.4)
d.uid = sql("SELECT id FROM %s.user WHERE email='dave@t.com'" % _A.mysql_db)
r = b.call({"type": 9, "email": "dave@t.com", "my_email": "bob@t.com", "name": "bob"}, 1)
check("给离线用户发好友申请", r and r.get("code") == 1, str(r.get("msg")))
key = "offline_request:" + str(d.uid)
check("Redis 有离线申请", redis("llen", key) not in ("0", ""), "llen=" + redis("llen", key))
raw = redis("lrange", key, "0", "-1")
print("      Redis 原始内容:", raw[:200])

print()
print(f"结果: PASS={ok} FAIL={fail}")
sys.exit(0 if fail == 0 else 1)
