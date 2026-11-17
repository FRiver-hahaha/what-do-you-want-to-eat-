#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
配置外置验证：凭据错误时必须启动失败，且失败原因可定位。

覆盖：
  1. MySQL 密码错误 -> 进程非 0 退出，日志含 "MySQL pool init failed"
  2. Redis 密码错误 -> 进程非 0 退出，日志含 "Redis AUTH failed"
  3. 不带任何 CHATROOM_* 变量时使用内置默认值（空密码）启动 -> 连接失败并退出
     （证明凭据确实来自环境变量，而不是源码里的常量）
用法：
  python3 config_guard_test.py --bin /path/to/server --workdir /path/to/bin
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

_ap = argparse.ArgumentParser()
_ap.add_argument("--bin", required=True)
_ap.add_argument("--workdir", default=".")
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


BASE = {
    "CHATROOM_MYSQL_HOST": "127.0.0.1",
    "CHATROOM_MYSQL_PORT": "3306",
    "CHATROOM_MYSQL_USER": "chatv2",
    "CHATROOM_MYSQL_PASSWORD": "Str0ngPass_2026",
    "CHATROOM_MYSQL_DB": "chatroom",
    "CHATROOM_REDIS_HOST": "127.0.0.1",
    "CHATROOM_REDIS_PORT": "6379",
    "CHATROOM_REDIS_PASSWORD": "Rad1s_Pass#2026",
    "CHATROOM_SMTP_DRY_RUN": "1",
    "CHATROOM_PORT": "8099",
}


def run_case(env_overrides, drop_keys=()):
    env = dict(os.environ)
    for k in list(env.keys()):
        if k.startswith("CHATROOM_"):
            env.pop(k)
    env.update(BASE)
    env.update(env_overrides)
    for k in drop_keys:
        env.pop(k, None)
    log = tempfile.NamedTemporaryFile(delete=False, suffix=".log")
    p = subprocess.Popen([_A.bin], cwd=_A.workdir, stdout=log, stderr=subprocess.STDOUT,
                         env=env)
    try:
        code = p.wait(timeout=12)
    except subprocess.TimeoutExpired:
        p.kill()
        code = None
    log.close()
    with open(log.name, "r", errors="replace") as f:
        text = f.read()
    return code, text


print("\n== 配置外置验证 ==")

code, text = run_case({"CHATROOM_MYSQL_PASSWORD": "WRONG_PASSWORD"})
check("错误 MySQL 密码 -> 启动失败", code is not None and code != 0,
      f"exit={code}")
check("日志可定位到 MySQL", "MySQL pool init failed" in text,
      [l for l in text.splitlines() if "MySQL pool init failed" in l][:1])

code, text = run_case({"CHATROOM_REDIS_PASSWORD": "WRONG_PASSWORD"})
check("错误 Redis 密码 -> 启动失败", code is not None and code != 0,
      f"exit={code}")
check("日志可定位到 Redis AUTH", "Redis AUTH failed" in text,
      [l for l in text.splitlines() if "Redis AUTH" in l][:1])

code, text = run_case({}, drop_keys=("CHATROOM_MYSQL_PASSWORD",
                                    "CHATROOM_REDIS_PASSWORD"))
check("未提供密码时按默认空值连接（源码内无可用凭据）",
      code is not None and code != 0,
      f"exit={code}")

print()
print(f"结果: PASS={ok} FAIL={fail}")
sys.exit(0 if fail == 0 else 1)
