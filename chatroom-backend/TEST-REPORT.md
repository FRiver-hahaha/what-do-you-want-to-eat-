# TEST-REPORT — 测试汇总报告

> 全部结果为本轮（P7 范围纠偏后）真实执行输出；evidence/p7b-*.log 为落盘原始日志。
> 环境差异与历史测试的边界已在文末如实标注。

## 1. 本轮（P7b，最终裁剪版：删聊天 + 删群组）真实执行结果

| 测试（tests/ 修订版） | 断言数 | 结果 | 证据 |
|---|---|---|---|
| smoke_cropped.py | 42 | 42/42 PASS | evidence/p7b-01-smoke-cropped.log |
| code_security_test.py basic | 16 | 16/16 PASS | evidence/p7b-02-code-security-basic.log |
| code_security_test.py ttl（8001, TTL=2s） | 17 | 17/17 PASS | evidence/p7b-05-code-security-ttl.log |
| attack_lifecycle_test.py | 8 | 8/8 PASS | evidence/p7b-03-attack-lifecycle.log |
| concurrency_test.py（8 路 bob 并发登录） | 3 | 3/3 PASS | evidence/p7b-04-concurrency-login.log |
| crash_recovery_test.py | 6 | 6/6 PASS | evidence/p7b-06-crash-recovery.log |
| config_guard_test.py | 5 | 5/5 PASS | evidence/p7b-07-config-guard.log |
| account_cascade_test.py（修订版，7 项） | 7 | 7/7 PASS | evidence/p7b-08-account-cascade.log |
| **合计** | **104** | **104/104 PASS，FAIL=0** |  |

全部在 `chatroom_p7` 独立验证库执行（不触碰既有业务库）。

### smoke_cropped 42 项结构（修订后）

- A 账号 9（注册带验证码 / 重复邮箱拒 / 登录 / SHA256 / 并发登录拒 / 心跳）
- B 好友 8（申请 / 实时推送 / 同意 / 列表 status+state / 拉黑 / DB status=3 / 解除）
- C 群组不可达 10（type 15–24 全部无响应或异常请求）
- D 聊天不可达 7（13/14/25/26/27 不可达 + message/file 表已删除）
- E 在线状态 3（退出 / DB state 归 0 / 好友看到离线）
- F 持久化 5（user/friend 落库 + group_info/group_user 表已删除）

### 结构核查（本轮实测，evidence/p7b-02-group-scan.log）

- 群组代码符号（GroupModel/group_model/createGroup/…/group_info/group_user）：代码区**零输出**
- 群组路由 15–24：**全部无路由**
- 最终路由注册数 **13**（type 0–12：心跳 + 账号 0–7 + 好友 8–12）
- 数据库最终表：**user / friend 两张**（group/message/file 表 0 张）
- chatserver 权限：**GRANT SELECT, INSERT, UPDATE, DELETE ON chatroom_p7.***（最小权限）
- 敏感信息：零命中（httplib.h/transit.cc 的 base64 字符表为误报，非凭据）

## 2. 历史测试结果（保留为整改记录，非本轮执行）

以下为此前阶段在**当时代码状态**（尚含群组功能）上的真实执行结果，仅作审计留痕：

| 阶段 | 测试 | 结果 | 说明 |
|---|---|---|---|
| P0 | smoke_baseline（原始基线） | 42/42 | 原始仓库 a1e654b |
| P4 | 9 组整合验收 | 113/113 | 当时代码含群组功能 |
| P5 | 落盘重跑 | 113/113 | 同上 |
| P6 | 集成环境验证 | 96/96 | 当时代码含群组功能，独立库 chatroom_int |

**历史测试不可替代本轮验收**：群组功能删除后，上述含群组断言的历史用例
（如"建群/邀请/成员角色"）已不适用，本轮验收以修订版 tests/ 的 104 项为准。

## 3. 测试脚本修订说明（tests/ 内，接手包原件未动）

- smoke_cropped.py：C 组 17 项群组管理断言 → C 组 10 项群组不可达断言；
  F 组群组落库断言 → group_info/group_user 表已删除断言
- account_cascade_test.py：删建群与群表级联断言（10→7 项）
- crash_recovery_test.py：counts() 改 2 表；--mysql-db 参数化
- concurrency_test.py：默认账号 carol→bob（carol 不再随测试注册）；--mysql-db 参数化

## 4. 过程失败记录（均已定位，非代码缺陷）

1. smoke 首跑连接被拒：WSL 发行版空闲回收重启（/tmp 清空、后台服务被终止）。
   处置：全部测试改为"启动服务→测试→停服务"单命令自包含执行，之后零失败。
2. 整改脚本两次断言中止（heredoc 转义折叠、单行函数形态）：脚本原子性保护生效，
   未产生半成品文件，修正脚本后重放成功。