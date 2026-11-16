#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
验证码安全策略测试（针对 CHATROOM_SMTP_DRY_RUN=1 的测试实例）

覆盖：
  1. 注册必须带验证码
  2. 验证码一次性（成功即作废，防重放）
  3. 发送频率限制
  4. 错误尝试次数上限
  5. TTL 过期
  6. 字段缺失/类型异常不致进程崩溃

前提：被测服务以 CHATROOM_SMTP_DRY_RUN=1 启动，且 --log 指向其 stdout 日志。
用法：
  python3 code_security_test.py --port 8000 --log /tmp/sec.log
  python3 code_security_test.py --port 8001 --log /tmp/sec_ttl.log --mode ttl
"""
import argparse
import json
import re
import socket
import sys
import time

PASS, FAIL = 0, 0
CASES = []


def check(name, ok, detail=""):
    global PASS, FAIL
    CASES.append((name, ok, detail))
    if ok:
        PASS += 1
        print(f"  [PASS] {name}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name}  <-- {detail}")


class Client:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=5)
        self.buf = b""

    def send_raw(self, text):
        self.s.sendall(text.encode())

    def send(self, obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())

    def recv(self, timeout=2.0):
        self.s.settimeout(timeout)
        while True:
            idx = self.buf.find(b"\n")
            if idx >= 0:
                line, self.buf = self.buf[:idx], self.buf[idx + 1:]
                try:
                    return json.loads(line.decode())
                except Exception:
                    return {"_raw": line.decode(errors="replace")}
            try:
                data = self.s.recv(65536)
            except socket.timeout:
                return None
            if not data:
                return None
            self.buf += data

    def call(self, obj, want_type=None, timeout=3.0):
        self.send(obj)
        want = want_type if want_type is not None else obj.get("type")
        deadline = time.time() + timeout
        while time.time() < deadline:
            resp = self.recv(max(0.1, deadline - time.time()))
            if resp is None:
                return None
            if want is not None and resp.get("type") != want:
                continue
            return resp
        return None

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def read_code(log_path, email):
    """从 dry-run 日志里取出该邮箱最近一次的验证码。"""
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    hits = re.findall(r"code for " + re.escape(email) + r" = (\d{6})", text)
    return hits[-1] if hits else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--log", default="/tmp/sec.log")
    ap.add_argument("--mode", default="basic", choices=["basic", "ttl"])
    a = ap.parse_args()

    tag = str(int(time.time()))
    H, P = a.host, a.port

    print(f"\n=== 验证码安全测试 mode={a.mode} {H}:{P} ===")

    # ---------- 0. 字段健壮性：不得崩溃 ----------
    print("\n[0] 字段缺失 / 类型异常")
    c = Client(H, P)
    for payload, desc in [
        ('{"name":"x","password":"p"}\n', "缺 type"),
        ('{"type":1,"name":"x","password":"p"}\n', "缺 email"),
        ('{"type":1,"email":"","name":"","password":""}\n', "全空字段"),
        ('{"type":2}\n', "登录缺字段"),
        ('{"type":99}\n', "不存在类型"),
        ('{"type":"abc"}\n', "type 非数字"),
    ]:
        c.send_raw(payload)
        r = c.recv(1.5)
        alive = True
        try:
            c.send({"type":0})
            alive = c.recv(1.5) is not None
        except Exception:
            alive = False
        check(f"{desc} 不崩溃且有响应", r is not None and alive,
              f"resp={r} alive={alive}")
    c.close()

    # ---------- 1. 注册必须带验证码 ----------
    print("\n[1] 注册强制校验验证码")
    email1 = f"nocode{tag}@t.com"
    c = Client(H, P)
    r = c.call({"type": 1, "name": "n1", "email": email1, "password": "p"})
    check("不带 code 注册被拒", r and r.get("code") == 0 and "验证码" in r.get("msg", ""), str(r))
    r = c.call({"type": 1, "name": "n1", "email": email1, "password": "p", "code": "000000"})
    check("未发送就填码被拒", r and r.get("code") == 0 and "请先获取" in r.get("msg", ""), str(r))
    c.close()

    # ---------- 2. 正常发码 + 正确注册 ----------
    print("\n[2] 发码与正确注册")
    email2 = f"ok{tag}@t.com"
    c = Client(H, P)
    r = c.call({"type": 3, "email": email2})
    check("发码成功", r and r.get("code") == 1, str(r))
    code2 = read_code(a.log, email2)
    check("能从 dry-run 日志取到 6 位码", bool(code2), f"code={code2}")

    if a.mode == "basic":
        r = c.call({"type": 3, "email": email2})
        check("60s 内重发被限频", r and r.get("code") == 0 and "频繁" in r.get("msg", ""), str(r))

    if code2:
        r = c.call({"type": 1, "name": "okuser", "email": email2, "password": "p", "code": code2})
        check("正确验证码注册成功", r and r.get("code") == 1, str(r))

        # 一次性：同一验证码再走验证码登录，应提示已失效
        r = c.call({"type": 4, "email": email2, "code": code2})
        check("同一验证码不可复用（防重放）",
              r and r.get("code") == 0 and "请先获取" in r.get("msg", ""), str(r))
    c.close()

    # ---------- 3. 错误尝试次数 ----------
    print("\n[3] 错误尝试次数上限")
    email3 = f"brute{tag}@t.com"
    c = Client(H, P)
    c.call({"type": 3, "email": email3})
    code3 = read_code(a.log, email3)
    check("发码成功", bool(code3), f"code={code3}")
    wrong = "111111" if code3 != "111111" else "222222"
    msgs = []
    for i in range(6):
        r = c.call({"type": 1, "name": "b", "email": email3, "password": "p", "code": wrong})
        msgs.append(r.get("msg") if r else "<none>")
    limit_hit = any("次数过多" in m for m in msgs)
    check("连续错码后触发次数上限", limit_hit, str(msgs))
    # 达到上限后即使输入正确码也不应通过（码已被销毁）
    if code3:
        r = c.call({"type": 1, "name": "b", "email": email3, "password": "p", "code": code3})
        check("锁定后正确码也失效", r and r.get("code") == 0, str(r))
    c.close()

    # ---------- 4. TTL ----------
    if a.mode == "ttl":
        print("\n[4] TTL 过期")
        email4 = f"ttl{tag}@t.com"
        c = Client(H, P)
        r = c.call({"type": 3, "email": email4})
        code4 = read_code(a.log, email4)
        check("发码成功", bool(code4), f"code={code4}")
        print("     等待 4s 让验证码过期 ...")
        time.sleep(4)
        if code4:
            r = c.call({"type": 4, "email": email4, "code": code4})
            check("过期码被拒绝",
                  r and r.get("code") == 0 and ("过期" in r.get("msg", "") or "请先获取" in r.get("msg", "")),
                  str(r))
        c.close()

    print(f"\n结果: PASS={PASS} FAIL={FAIL}")
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
