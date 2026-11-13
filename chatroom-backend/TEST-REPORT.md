# TEST-REPORT — 测试汇总报告（P0–P5）

> 所有结果为真实执行输出；evidence/ 内为本轮落盘的原始日志。
> 每阶段测试失败均先定位再处置，未带着失败进入下一阶段。

## 1. 分阶段结果

| 阶段 | 测试 | 断言数 | 结果 | 备注 |
|---|---|---|---|---|
| P0 | smoke_baseline.py（原始基线 a1e654b） | 42 | 42/42 PASS | A7+B8+C17+D5+E3+F2 全过，作为裁剪前基线 |
| P1 | smoke_baseline.py（仅补丁 01 后对照） | 42 | 37 PASS + 5 FAIL | 5 项 FAIL 均为 D 组聊天不可达 = 裁剪目标达成，非缺陷；接手包脚本自身在 r=None 时崩溃（设计假定原始版），用 /tmp 临时副本仅补 None 防护跑完，原件未动 |
| P1 | 协议级探测 13/14/25/26/27 | — | 全部返回 {"type":0,"code":0,"msg":"异常请求"} | message 表 0→0 |
| P2 | crash_recovery_test.py | 6 | 6/6 PASS | kill -9→state 残留 1→重启归 0→可登录→数据不变 |
| P2 | attack_lifecycle_test.py | 8 | 8/8 PASS | 60MB 断开、RSS +192kB、fd 31→61→31、CLOSE-WAIT=0、50/50 并发 |
| P3 | smoke_cropped.py | 48 | 48/48 PASS | DRY_RUN=1 实例 |
| P3 | code_security_test.py basic | 16 | 16/16 PASS | 畸形 JSON×6 不崩溃、强制验证码、限频、错 5 次锁定、防重放 |
| P3 | code_security_test.py ttl（8001, TTL=2s） | 17 | 17/17 PASS | 含过期码被拒 |
| P3 | concurrency_test.py | 3 | 3/3 PASS | 8 路并发登录成功数=1 |
| P3 | config_guard_test.py | 5 | 5/5 PASS | 错密码启动失败且日志可定位 |
| P3 | account_cascade_test.py | 10 | 10/10 PASS | 注销级联 4 表归零 |
| P3 | Redis AUTH 成功场景（手动实测） | — | 正确密码实例启动+心跳正常 | 测毕恢复无密码 |
| P4 | 上述 9 组全部重跑（整体验收） | 113 | 113/113 PASS | 非引用历史，全部本轮重跑 |
| P5 | 9 组测试 + 结构核查 + 协议探测重跑落盘 | 113+ | 全部 PASS | 见 evidence/00–13 |

## 2. 汇总统计

- 协议级断言总计：48+16+8+3+17+6+5+10 = 113，通过 113，失败 0（P4/P5 两轮全绿）
- 基线对照：P0 42/42；P1 对照 37+5（5 项为预期不可达）
- 过程中出现的失败均已定位根因：
  1. P1 脚本 None 崩溃 → 接手包脚本设计假定，临时副本防护，原件未动
  2. P4 crash_recovery 首跑失败 → root shell 未 source .env，服务空密码连库 FATAL（恰为凭据外置生效的实证）
  3. P5 crash_recovery 一次 2 FAIL → 8000 旧实例未停，脚本端口探测被既有实例欺骗 + 自管实例启动期 resetState 干扰（执行方式失误，停实例后重跑 6/6；该注意事项已写入 KNOWN-ISSUES）

## 3. 测试环境与入口

- 实例：./server 以 .env（DRY_RUN=1）启动于 8000；TTL 实例 8001（CHATROOM_CODE_TTL=2）；config_guard 自管 8099
- DB 断言脚本以 root + --mysql-socket /run/mysqld/mysqld.sock 执行
- 自管服务脚本（crash_recovery/config_guard）先 source .env（或见 KNOWN-ISSUES 的说明）

## 4. 证据清单（evidence/）

| 文件 | 内容 |
|---|---|
| 00-baseline-original-server.log | P0 原始基线服务运行日志 |
| 00-baseline-notes.md | P0 smoke_baseline 42/42 说明（stdout 当时未落盘，完整逐项输出见会话验收记录） |
| 01-smoke-cropped.log | P5 重跑 smoke_cropped 48/48 完整输出 |
| 01-smoke-cropped-server.log | 对应服务端日志 |
| 02-code-security-basic.log | P5 重跑 16/16 完整输出 |
| 03-attack-lifecycle.log / -server.log | P5 重跑 8/8 + 服务日志 |
| 04-concurrency-login.log | P5 重跑 3/3（8 路成功数=1） |
| 05-code-security-ttl.log / -server.log | P5 重跑 17/17 + 8001 实例日志 |
| 06-crash-recovery.log / -server.log | 6/6 完整输出 + 服务日志 |
| 07-config-guard.log | 5/5 完整输出 |
| 08-account-cascade.log | 10/10 完整输出 |
| 09-redis-auth-fail.log | 错误密码实例日志（Redis AUTH failed: wrong password → FATAL） |
| 10-redis-auth-ok.log | 正确密码实例日志（启动成功） |
| 11-structure-check.log | 结构核查 7 项输出 |
| 12-git-status.log | 最终 Git 状态与 diff --stat |
| 13-chat-unreachable.log | 协议级 5 类不可达探测 + message 表行数 |
| build-cmake.log / build-make.log | P4 干净构建日志 |
