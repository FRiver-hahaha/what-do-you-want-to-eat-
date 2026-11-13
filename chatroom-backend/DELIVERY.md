# DELIVERY — ChatRoom 后端裁剪改造交付说明

> 依据接手包 codearts-handoff-repo3/DELIVERY.md 模板填写，所有数据为本轮实测真实值。

## 1. 源码与版本

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/lzh070702/ChatRoom |
| 基线 commit | a1e654bde68ee9731c8254fed61ddc030ca926dc |
| 交付分支 | refactor/backend-crop |
| 交付 commit | a1e654b（HEAD 仍为基线，三个补丁的应用结果保留在工作区，未提交——待验收授权后统一提交） |
| Tag | 未创建（待提交授权后建议 v1.0-cropped） |
| 交付日期 | 2026-11-13 |
| 交付人 | 华为云 CodeArts 代码智能体（执行）+ M（验收） |
| 补丁来源 | codearts-handoff-repo3/patches/01..03（MANIFEST.sha256 校验 30/30 通过，原件零改动） |

核对命令：

```bash
git rev-parse HEAD        # a1e654bde68ee9731c8254fed61ddc030ca926dc
git diff --stat           # 16 files changed, 348 insertions(+), 543 deletions(-)
```

## 2. 构建环境（本轮实测）

| 项 | 值 |
|---|---|
| OS | Ubuntu 26.04.1 LTS x86_64（WSL2） |
| 编译器 | g++ 15.2.0（C++17） |
| 构建 | CMake 4.2.3 + GNU Make 4.4.1 |
| MySQL | 8.4.13（比接手包验证环境 MariaDB 10.11 更新的解析器，见 KNOWN-ISSUES 环境差异） |
| Redis | 8.0.5（验证时先后以无密码 / requirepass 两种模式实测） |
| hiredis | 1.2.0 |
| nlohmann/json | 3.11.3 |
| glog | 0.6.0 |
| libcurl | 8.18.0 |
| OpenSSL | 3.5.5 |
| Python | 3.14.4（跑回归脚本） |

依赖安装：

```bash
sudo apt-get install -y build-essential cmake pkg-config \
    default-libmysqlclient-dev libhiredis-dev libcurl4-openssl-dev \
    libgoogle-glog-dev nlohmann-json3-dev libssl-dev libreadline-dev \
    mysql-server redis-server
```

## 3. 构建

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

P4 干净构建（rm -rf build 后）实测：cmake 退出码 0、make 退出码 0、error 0 行。

| 产物 | 说明 | 本次是否交付 |
|---|---|---|
| build/bin/server | 主服务（默认 8000，可经 CHATROOM_PORT 配置） | 交付 |
| build/bin/client | 命令行客户端 | 未适配验证码（K-03） |
| build/bin/web | 静态网页服务 | 含聊天入口，未适配 |
| build/bin/transit | WebSocket 中转 | 依赖未改前端 |
| file_server | 文件服务 | 已移除（01 补丁，产物中已不存在） |

## 4. 启动

### 4.1 配置

```bash
cp .env.example .env && chmod 600 .env && vi .env
set -a && . ./.env && set +a
```

.env 必填项同接手包模板（MySQL 连接 / Redis 连接 / SMTP 发码账号）。
本轮测试配置：MySQL 使用 init.sql 创建的默认开发账号；SMTP 留空 + CHATROOM_SMTP_DRY_RUN=1（验证码打日志，仅限自动化测试）。

### 4.2 启动命令

```bash
cd build/bin && ./server
# 日志: Server started on port 8000 worker_threads=4
```

systemd 示例见接手包模板（Restart=always + 启动时 resetState() 保证账号不锁死）。

## 5. 数据库依赖

同接手包：库 chatroom、6 张表、本轮 schema 无变更、message/file 只保留结构禁止写入。
初始化时注意 MySQL 8.4 对 init.sql 中 `--` 紧贴文字注释报 1064（见 KNOWN-ISSUES）。

## 6. 功能清单

保留 25 项能力、路由注册数 23（0–12 + 15–24）；删除 5 类消息路由（13/14/25/26/27）、
MessageModel.{h,cc} 文件删除、file_server 目标移除。明细同接手包模板第 6 节，不再赘抄。

## 7. 接口协议

传输格式 / 通用响应 / 请求一览 / 推送 / 错误码同接手包模板第 7 节（本轮未改任何接口约定）。

## 8. 测试结果（本轮实测，证据见 evidence/）

| 脚本 | 断言数 | 结果 | 证据文件 |
|---|---|---|---|
| smoke_baseline.py（P0 原始基线） | 42 | PASS=42 FAIL=0 | evidence/00-baseline-original-server.log + 00-baseline-notes.md |
| smoke_baseline.py（P1 裁剪后对照） | 42 | PASS=37，5 项聊天 FAIL 为裁剪预期 | P1 会话记录（见 TEST-REPORT） |
| smoke_cropped.py | 48 | PASS=48 FAIL=0 | evidence/01-smoke-cropped.log |
| code_security_test.py --mode basic | 16 | PASS=16 FAIL=0 | evidence/02-code-security-basic.log |
| attack_lifecycle_test.py | 8 | PASS=8 FAIL=0 | evidence/03-attack-lifecycle.log |
| concurrency_test.py --n 8 | 3 | PASS=3 FAIL=0 | evidence/04-concurrency-login.log |
| code_security_test.py --mode ttl | 17 | PASS=17 FAIL=0 | evidence/05-code-security-ttl.log |
| crash_recovery_test.py | 6 | PASS=6 FAIL=0 | evidence/06-crash-recovery.log |
| config_guard_test.py | 5 | PASS=5 FAIL=0 | evidence/07-config-guard.log |
| account_cascade_test.py | 10 | PASS=10 FAIL=0 | evidence/08-account-cascade.log |

关键指标（本轮实测）：

| 指标 | 值 |
|---|---|
| 消息类型不可达 | 13/14/25/26/27 全部返回 {"type":0,"code":0,"msg":"异常请求"}，无 code:1 |
| message 表增量 | 0（全流程跑完后 0 行） |
| 60MB 攻击下 RSS | 25908kB → 26032kB（+124kB），连接被主动断开 |
| 30 连接 fd | 31 → 61 → 31，CLOSE-WAIT=0（两轮） |
| 8 路并发登录 | 成功 1 路 |
| kill -9 后重启 | state 1 → 0，可正常登录，数据不变 |
| 注销级联 | 4 张表全部归零 |
| Redis AUTH | 错误密码 FATAL 退出（日志可定位）；正确密码启动+心跳正常 |

## 9. 未完成项

同接手包模板第 9 节（客户端/Web 适配、在线状态主动推送、待审列表、群主转让、bcrypt、TLS、多实例、文件服务不恢复）。完整清单见 KNOWN-ISSUES.md。

## 10. 交付清单

本轮交付（位于仓库根目录与 evidence/）：
- 交付文档 6 份：DELIVERY.md / BUILD-REPORT.md / TEST-REPORT.md / CHANGELOG.md / HANDOFF.md / KNOWN-ISSUES.md
- evidence/ 本轮实测证据 13 组（00–13，含构建、9 组测试输出、Redis AUTH 成败、结构核查、协议探测、Git 状态）
- 代码变更：16 文件 +348/−543（未提交，待授权）
- 接手包原件（codearts-handoff-repo3/）MANIFEST 30/30 完好随附

验收签署：

| 角色 | 姓名 | 日期 | 结论 |
|---|---|---|---|
| 开发 | CodeArts Agent | 2026-11-13 | 待签 |
| 验收 | M | 2026-11-13 | 待签 |
