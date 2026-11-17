# AGENT_Linux.md

> **2026-11-17 更新**：S0–S2 已在本机 Linux 落地并验证：`eat-server` 可构建，MySQL 迁移 `001→006` 成功，真实 TCP 链路（注册/登录/握手/`profile.get`/`profile.patch`）通过，C++ 12 项单测、Agent 51 项单测、IPC smoke 全绿。最新事实见 `docs/architecture.md`；本文件保留为历史交接与设计口径。

本文件是给「Linux 环境上的 AI/开发者」的交接说明。目标：让你在不重新翻聊天记录的情况下，知道项目为什么这样设计、当前代码做到哪、哪些能测、哪些不能宣称、下一步先补什么。

**先读顺序**：

1. 本文件
2. `.codeartsdoer/specs/app_transport_scale/spec.md`（R6 需求规格，3156 行）
3. `.codeartsdoer/specs/app_transport_scale/design.md`（实现方案，1833 行）
4. `.codeartsdoer/specs/app_transport_scale/tasks.md`（T0–T12，102 个子任务）
5. `docs/architecture-status.md`（本轮落地状态）
6. `docs/protocol/v1.md`（TCP 帧协议）

**不要做的事**：

- 不要读取、打印、提交 `.env` 中的真实 API Key。
- 不要在未实测前宣称“支持百万在线/百万并发”。
- 不要引入 Golang 分支、第二套网络库、Protobuf/MessagePack/CBOR、WebSocket 推送、Redis pub/sub 或多 Redis 实例。
- 不要合并远端 PR #2；它只作为 P8 代码基线使用。

---

## 1. 项目目标（最新口径）

- 客户端：**Windows App，完全替代现有 Web 前端**。
- 服务端：**Linux 单机**，**唯一 C++ + 唯一 Muduo** 主从 Reactor。
- 容量：**100 万注册用户 + 100 万同时在线连接**；后者仅为**压测上限**，不是日常运营承诺。
- 传输：自研 TCP 应用层协议，40B 定长头 + 长度字段 + **JSON body**（唯一序列化），处理粘包/半包/拆包/超长/非法帧；**60s 心跳**。
- 存储：MySQL + Redis，均只作为服务端存储后端；**仅服务端可连**。
- Redis 硬约束：单实例、禁 pub/sub、禁客户端直连、每环境最多一个 `redis-server`。
- 缓存：Cache-Aside，先 Redis 未命中查 MySQL 回写；空查询写负缓存；**写后删除**，接受短暂不一致。
- 身份：**新建统一身份**，不复用 P7 整型 id/无票据登录，也不复用 P8 静态 Token + `u_demo` 白名单。
- Agent：编排在客户端；**API Key 在客户端**（DPAPI 加密）；LLM 由客户端直连模型（HTTPS/SSE）；**服务端不持有 Key、不承载 LLM 流量、不主动回调 Agent**。
- 服务端不得出现 `llm_proxy` / `llm_stream_*` 帧。

---

## 2. 对话全记录（按轮次）

### 第 1 轮：审查远端 PR #2 与百万并发

用户要求同步远端，检查 `feat: implement P8 business HTTP backend- #2` 是否能跑通真实流程、是否能承受百万高速并发、三端适配与分发是否完成，先不合并。

结论：

- PR #2 = `origin/feat/backend-p8`，相对 main 新增 `chatroom-backend/` 整套。
- S1–S5 有真实证据（接口 33/33、82/82、56/56；端到端 41/41；Agent 契约 27/27、29/29、17/17）。
- S6 未执行：无 `evidence/p8-s6`，前端 9 步验收缺失，README/HANDOFF/KNOWN-ISSUES 未写 P8 章节。
- **百万并发不成立**：P8 是固定 `ThreadPool` + 同步 MySQL，并发≈worker 数（默认 `max(8, 核数-1)`），队列无上限、backlog=5；文档自认 QPS 是理论推导，无压测脚本。
- 三端：Agent `ServiceClient` 基本对齐；前端只经 `:8787`；但餐记录缺 PATCH/DELETE、部署未落地、PR 落后 main 5 个提交。
- 建议：**不合并**。

### 第 2 轮：客户端改成 App，是否还要 HTTP？走 TCP 能否百万并发？

结论：

- HTTP 本身就是 TCP 之上的应用层协议；换 App/换裸 TCP 不改变并发上限。
- 百万级瓶颈在服务端 IO 模型与存储，不在协议名。
- P7 Reactor 只有 4 线程且业务 SQL 在 reactor 线程内；Agent 是 `ThreadingHTTPServer` 每请求一线程，更早成为瓶颈。
- 真要百万级，需要异步非阻塞/协程、无状态化、连接网关、水平扩展、共享存储、限流、梯度压测；换 TCP 解决不了。

### 第 3 轮：确认方向（R2）

用户确认：

- Windows；App 完全替代 Web；
- 百万 = 100 万注册用户 + 100 万同时在线连接；
- 单机部署；允许自研 TCP；服务端 epoll + 数据库；
- C++/Golang 均可；MySQL + Redis 仅服务端；JSON/Protobuf 序列化；
- Redis 五条禁用项（禁 pub/sub、禁客户端连、禁多 Redis、每人最多一个、仅服务端连）；
- TCP 心跳；可直接用 Muduo；新建统一身份；App 后台常驻长连接；不用 WebSocket 做推送/状态同步。

### 第 4 轮：确认口径（R3）

- 服务端限定 Linux 单机；
- 心跳 60s；
- 100 万同时在线定义为**压测上限**；
- C++ + Muduo；
- 要求给出“现有代码距目标差多少”的差距矩阵（40 模块，后扩到 61）。

### 第 5 轮：真实 TCP + 缓存旁路 + Agent 链路（R4）

- 必须处理 TCP 粘包/半包/拆包、心跳；
- 查询先 Redis，未命中查 MySQL 并回写；空查询也写 Redis（负缓存）；
- Agent 请求链路：客户端先处理 → TCP 发服务端 → 服务端分发模块 → 返回客户端 → 再交 Agent。

### 第 6 轮：序列化与 Key 位置（R5）

- 帧序列化唯一选 **JSON**；
- 缓存接受**写后删除 + 短暂不一致**；
- 用户一度要求 API Key 放服务端。

### 第 7 轮：Key 回到客户端（R6）

- API Key 最终**放客户端**；
- 其他口径不变；
- 撤销服务端 LLM 代理与 `llm_stream_*` 帧；
- 服务端零 LLM 依赖。

### 第 8 轮：进入实现方案

产出 `design.md`：

- 单进程 Linux 服务，vendored Muduo，默认 16 sub Reactor + 16 业务线程有界池；
- 40B 头（`EAT1`、CRC32C、request_id 在头内）+ JSON body；
- 统一身份：`account`/`session`/`account_alias`，`user_key` ULID，不透明令牌哈希存储，scrypt；
- 缓存：键规范、正负同键、TTL 抖动、单飞、写后 DEL、熔断直降；
- 数据：餐记录/幂等账本按月分区；
- 客户端：Agent 内置子进程 + 命名管道 IPC（不监听 TCP）；Key 用 DPAPI；LLM HTTPS/SSE 与 TCP 隔离。

### 第 9 轮：进入任务规划

产出 `tasks.md`：13 个任务组、102 个子任务、11 个阶段门（G1–G11）。

### 第 10 轮：继续执行到代码

用户授权连续执行。本轮已落地协议、连接治理、缓存、身份基础、迁移、部署、客户端 KeyVault/离线队列/传输层骨架，并跑通 10 个可跨平台测试。

### 第 11 轮：提交 GitHub + 本文件

用户要求提交整个内容到 GitHub，并写本文件供 Linux 上 AI 读取测试。本次提交**推送到新分支**，不合并 PR，不改 main。

---

## 3. 当前分支与基线

- 本地分支：`feat/app-transport-scale`
- 基线：`origin/main`（`fc34b0f`）+ 引入 `origin/feat/backend-p8` 的 `chatroom-backend/` 作为 P8 代码基线
- 远端 PR #2 未合并
- 本次提交哈希：见 `git log -1 --oneline`

---

## 4. 代码地图（已落地）

| 模块 | 文件 | 职责 | 状态 |
|---|---|---|---|
| 协议常量 | `shared/protocol/protocol.h` | 40B 帧头、帧类型、错误码 | 已验证 |
| 协议文档 | `docs/protocol/v1.md` | 帧、握手、心跳、粘包、错误码 | 已落地 |
| 帧编解码 | `shared/protocol/FrameCodec.{h,cc}` | 粘包/半包/超长/CRC | 已验证 |
| CRC32C | `shared/protocol/Crc32c.{h,cc}` | Castagnoli 校验 | 已验证 |
| 严格 JSON | `shared/protocol/JsonValue.{h,cc}`、`StrictJson.{h,cc}` | 重复键/深度/尾随/BOM/大整数 | 已验证 |
| 方法白名单 | `shared/protocol/methods.h` | 业务 method 与写方法判定 | 已落地 |
| 帧分发 | `server/tcp/FrameDispatcher.{h,cc}` | 握手、未握手拒绝、身份伪证丢弃、`idem_key` | 已验证 |
| 连接状态 | `server/tcp/ConnContext.{h,cc}` | 状态机、幂等关闭 | 已验证 |
| 心跳 | `server/tcp/HeartbeatMonitor.{h,cc}` | 165s 空闲、最小堆 | 已验证 |
| 限流 | `server/tcp/RateLimiter.{h,cc}` | 令牌桶 | 已验证 |
| 背压 | `server/tcp/Backpressure.{h,cc}` | 每连接/全局水位 | 已验证 |
| Muduo 服务端 | `server/tcp/TcpServerApp.{h,cc}`、`server/tcp/main.cc` | 主从 Reactor、帧循环、业务池 | 代码已写，**未在 Linux 构建** |
| 配置 | `server/app/Config.{h,cc}` | `server.toml` 解析与校验 | 已验证 |
| 日志 | `server/app/Log.{h,cc}` | JSON 行、脱敏 | 已落地 |
| 指标 | `server/app/Metrics.{h,cc}` | 原子计数 + P50/P95/P99 | 已落地 |
| 业务池 | `server/app/BusinessExecutor.{h,cc}` | 有界队列、拒绝、超时 | 已验证 |
| 缓存键 | `server/cache/CacheKeyBuilder.{h,cc}` | 键规范、SHA-256、参数夹取 | 已验证 |
| 缓存值 | `server/cache/CacheValue.{h,cc}` | 正负同键结构 | 已验证 |
| TTL | `server/cache/TtlPolicy.{h,cc}` | 各域 TTL/抖动/负缓存 | 已验证 |
| 单飞 | `server/cache/SingleFlight.{h,cc}` | 同键回源合并 | 已验证 |
| 失效清单 | `server/cache/InvalidationSet.{h,cc}` | 写后 DEL 多键 | 已验证 |
| 密码哈希 | `server/identity/PasswordHasher.{h,cc}` | scrypt | 已验证 |
| 令牌验证 | `server/identity/TokenVerifier.{h,cc}` | SHA-256 摘要 + 60s LRU | 已验证 |
| 会话注册 | `server/identity/SessionRegistry.{h,cc}` | 设备顶替、3 设备上限 | 已验证 |
| 迁移 | `migrations/add/002–006*.sql` | 身份、会话、好友、分区、outbox | 已落地，待 MySQL 执行 |
| 客户端 KeyVault | `client/keyvault/KeyVault.{h,cc}` | Windows DPAPI | 已验证（Windows） |
| 客户端传输 | `client/transport/TcpTransport.{h,cc}` | 握手/心跳/重连/请求超时 | 编译通过，未连真实服务端 |
| 离线队列 | `client/transport/OfflineQueue.{h,cc}` | 幂等去重、有界、失败转人工 | 已验证 |
| 部署 | `deploy/systemd`、`deploy/sysctl`、`deploy/preflight.sh` | systemd/sysctl/预检 | 已落地 |
| 压测 | `tests/load/bench_tcp/bench_tcp.cc` | 冒烟版建连/握手 | 可编译 |

---

## 5. 已验证事实（本机 Windows + MSYS2 g++ 16.1.0）

已通过：

- `frame_codec_test`
- `strict_json_test`
- `connection_governance_test`
- `config_test`
- `frame_dispatcher_test`
- `cache_test`
- `identity_test`
- `token_verifier_test`
- `offline_queue_test`
- `keyvault_test`（Windows DPAPI）

**未验证**：

- Muduo 服务端构建与运行；
- MySQL 迁移执行；
- Redis 连接、熔断、WAL、写后删除；
- 真实注册/登录/业务帧链路；
- TLS；
- Windows App UI、命名管道 Agent IPC；
- 100 万连接压测。

---

## 6. 未完成与阻断（按任务）

| 任务 | 状态 | 缺什么 / 解除条件 |
|---|---|---|
| T0.2 vendored Muduo + OpenSSL | 未完成 | Linux 上拉取 Muduo v2.0.x，或 `apt install libmuduo-dev`；补 CMake 链接 |
| T2.1/T2.3/T2.8 Muduo 构建/线程归属/优雅关闭 | 阻断 | 同 T0.2；代码已写但未编译 |
| T2.9 fd/内核参数实测 | 阻断 | Linux root + 压测机；`deploy/sysctl/eat-server.conf` 已给 |
| T2.10 IO 阻塞门禁 | 部分 | 需 CI 符号扫描；目前只有评审规则 |
| T3.3 IdentityService（DB） | 未完成 | 需 MySQL DAO：注册/登录/刷新/登出/撤销/改密 |
| T4.5/T4.6 DAO 改造 + 百万数据集 | 未完成 | 需 MySQL 8 与数据生成器 |
| T5.1/T5.8 Redis 客户端/熔断/WAL | 未完成 | 需 hiredis；当前只有键/TTL/单飞/失效清单 |
| T6 业务 Handler 全部帧化 | 未完成 | 依赖 T3/T5；P8 `service/`+`dao/` 可复用 |
| T7.2–T7.8 LlmEgress/SSE/指标 | 未完成 | 需 Windows 目标机与模型出网 |
| T8.1/T8.4–T8.10 Windows App/命名管道/Agent | 未完成 | 需 App 工程与双用户 ACL 测试 |
| T10/T11 100 万压测 | 阻断 | 3 台压测机 + 32C/64G Linux 被测机 |
| T12 文档收尾/Web 退场 | 部分 | 需 App 功能对等证据 |

---

## 7. Linux 环境准备（Ubuntu 22.04/24.04）

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
  libssl-dev libmysqlclient-dev libhiredis-dev \
  mysql-server redis-server

# 可选：系统包 Muduo
sudo apt install -y libmuduo-dev
```

如果系统没有 `libmuduo-dev`：

```bash
git clone https://github.com/chenshuo/muduo.git third_party/muduo
cd third_party/muduo
./build.sh install
```

Muduo 装到 `/usr/local/include/muduo`、`/usr/local/lib` 后，`CMakeLists.txt` 的 `find_path(muduo/net/TcpServer.h)` 应能命中；链接名通常为 `muduo_net`、`muduo_base`。

**注意**：当前 CMake 还没有 vendored Muduo 的 `add_subdirectory`，也没有 OpenSSL 的 `find_package(OpenSSL)`；如果 Muduo 路径找不到，`eat-server` 目标不会生成，但协议/缓存/身份测试仍可构建。

---

## 8. MySQL 与 Redis

### 8.1 启动

```bash
sudo systemctl enable --now mysql
sudo systemctl enable --now redis-server
sudo systemctl status redis-server --no-pager
```

### 8.2 数据库

```bash
sudo mysql -e "CREATE DATABASE IF NOT EXISTS eat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
sudo mysql -e "CREATE USER IF NOT EXISTS 'eat'@'127.0.0.1' IDENTIFIED BY 'change-me';"
sudo mysql -e "GRANT ALL PRIVILEGES ON eat.* TO 'eat'@'127.0.0.1'; FLUSH PRIVILEGES;"
```

### 8.3 迁移顺序

先执行 P8 基线（`001`），再执行新迁移：

```bash
mysql -ueat -p eat < chatroom-backend/server/http/migrations/001_p8_business.sql
for f in migrations/add/002_identity.sql \
         migrations/add/003_session.sql \
         migrations/add/004_friend_relation.sql \
         migrations/add/005_meal_partition.sql \
         migrations/add/006_event_outbox.sql; do
  mysql -ueat -p eat < "$f"
done
```

**执行前必须全库备份**。`005` 会把餐记录/幂等账本改为分区表；其中幂等唯一键因 MySQL 分区约束调整为 `(user_key, idem_key, created_day)`，详见 `migrations/add/README.md`。

### 8.4 Redis 硬约束

```bash
redis-cli INFO server | grep redis_version
redis-cli CONFIG GET cluster-enabled   # 必须为 no
redis-cli INFO clients | grep pubsub    # 必须为 0
pgrep -c redis-server                   # 必须为 1
```

---

## 9. 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

预期：10 个测试全绿（见第 5 节）。

`bench_tcp` 已加入 CMake：

```bash
./build/bench_tcp --help
./build/bench_tcp --host 127.0.0.1 --port 9443 --connections 100
```

**当前 `bench_tcp` 只是冒烟版**：串行建连、发握手、不读回执、不做 1M。它不是 T10.1 的完整压测客户端。

---

## 10. 服务端首次真实链路（当前还不能直接跑通）

原因：`T3.3 IdentityService` 与 `T6` 业务 Handler 尚未实现。当前 `main.cc` 创建的 `FrameDispatcher` 没有注册任何业务 method，握手也没有真实 TokenVerifier 注入。

Linux 上第一优先级（按顺序）：

1. 补 T0.2：让 `eat-server` 真正可构建。
2. 补 T3.3：`IdentityService` + `AccountDao`/`SessionDao`，实现注册/登录/刷新/登出/撤销。
3. 把 `TokenVerifier` 接到握手分支；握手成功绑定 `ConnContext.user_key`。
4. 补 T6.3 Handler 框架，注册 `profile.get` 等最小 method。
5. 用 `bench_tcp` 或 `TcpTransport` 发一次真实握手 + `profile.get`，确认：
   - 未握手业务帧返回 `2001` 并断连；
   - 握手后 `profile.get` 返回 `ok=true`；
   - 写方法缺 `idem_key` 返回 `3001`；
   - 心跳期间 MySQL `Questions` 与 Redis `commandstats` 零增长。

在这些完成前，不要做 100 万压测；否则压的是一个没有业务链路的空壳。

---

## 10.1 局域网联调：能测哪部分客户端

**结论**：同一局域网内“Linux 服务端 + 同学客户端”的拓扑可行，但当前代码还不能做到“同学从 GitHub 下载就能连”。必须先补齐构建、握手和可执行客户端。

### 10.1.1 能测与不能测

| 能力 | Linux 联调可测 | 说明 |
|---|---|---|
| `TcpTransport` 传输层 | 可以 | 代码用 `#ifdef _WIN32` 兼容 POSIX；`access_token` 由 `TransportConfig` 注入，不依赖 DPAPI |
| 握手 / 心跳 / 断线重连 / 请求超时 | 可以 | 需服务端先有握手 Handler |
| `OfflineQueue` | 可以 | 纯逻辑 |
| `bench_tcp` 建连 + 握手 | 可以 | 冒烟版，不读回执、不做 1M |
| 协议/缓存/身份/配置测试 | 可以 | `ctest` 直接跑 |
| Windows App UI / 托盘 / 单实例 / 安装包 | 不可以 | Windows 专属 |
| KeyVault | 不可以 | Windows DPAPI；Linux 返回 `unsupported_platform` |
| 命名管道 Agent IPC / 双用户 ACL | 不可以 | Windows 专属 |
| 真实注册/登录/业务帧 | 暂不可以 | `IdentityService` 与 T6 Handler 未实现 |

### 10.1.2 网络前提

1. 服务端监听 `0.0.0.0:9443`（当前 `InetAddress(config.tcp_port)` 默认绑定任意地址）。
2. Linux 防火墙放行 `9443/tcp`：
   ```bash
   sudo ufw allow 9443/tcp
   sudo ufw status
   ```
3. 客户端把 `host` 从 `127.0.0.1` 改为服务端局域网 IP，例如 `192.168.1.20`。
4. 两台机器在同一子网；关闭路由器/AP 的“AP 隔离”；关闭会劫持内网流量的 VPN/代理。
5. **仅限内网测试**：当前是明文 TCP，没有 TLS，不要暴露到公网。

### 10.1.3 当前四个硬缺口

| 缺口 | 现状 | 影响 |
|---|---|---|
| 服务端构建 | Muduo 未 vendored；本机未构建 | Linux 上先补 T0.2，才有 `eat-server` |
| 握手鉴权 | `main.cc` 未注册 HandshakeHandler | 握手返回 `handshake_unavailable`，连不上 |
| 身份/业务 | `IdentityService`、T6 Handler 未实现 | 没有注册/登录，也没有 `profile.get` |
| 客户端产物 | 无 Windows App 安装包；`TcpTransport` 只有库；`bench_tcp` 是冒烟版 | 同学下载的是源码，不能双击运行 |

### 10.1.4 最小可测路径

1. Linux 上补 T0.2，构建 `eat-server`。
2. 加一个**测试用握手 Handler**（固定测试 token，或临时接 `TokenVerifier`），让握手返回 `handshake_resp`。
3. 同学在 Windows 上用 `bench_tcp` 或 `client_smoke`（待补）连 `服务器IP:9443`，验证：
   - 握手成功；
   - 60s 心跳 ack；
   - 断开后客户端重连；
   - 未握手直接发业务帧被 `2001` 拒绝并断连。
4. 等 T3.3 + T6.3 完成，再测注册/登录/档案/餐记录/统计。

### 10.1.5 `client_smoke`（待补目标）

建议新增：

```bash
./build/client_smoke --host 192.168.1.20 --port 9443 --token test-token
```

行为：用 `TcpTransport` 握手 → 发心跳 → 发 `profile.get` → 打印 `ok/error_code`。Linux 上用 CLI token 注入，不编译 DPAPI。该目标当前尚未加入 CMake，Linux 上第一优先补齐。

### 10.1.6 同学侧准备

- Windows 机器；与服务器同局域网；
- 当前没有安装包，需要先编译 `bench_tcp`/`client_smoke`，或等 Windows App 产物；
- 只验证协议时，`bench_tcp` 最省事；要验证 UI/KeyVault，必须有 Windows App 可执行文件。

### 10.1.7 一个容易踩的坑

`TcpTransport::Start()` 首次连接失败会直接返回失败，不会自动进入重连。联调时必须**先启动服务端**，再让同学启动客户端。

### 10.1.8 联调命令示例

服务端：

```bash
ip -4 addr show
sudo ./build/eat-server --config /etc/eat/server.toml
```

同学客户端：

```bash
./build/bench_tcp --host 192.168.1.20 --port 9443 --connections 10 --token test-token
```

### 10.1.9 联调验收判据

- 握手返回 `handshake_resp`，不是 `handshake_unavailable`；
- 心跳帧收到 `heartbeat_ack`；
- 断开服务端后客户端进入退避重连；
- 未握手业务帧返回 `2001 handshake_required`；
- 纯心跳期间 MySQL/Redis 计数零增长；
- 全程使用测试 token，不使用真实模型 Key。

---

## 11. 100 万同时在线压测方案（后续）

- 被测端：单机 32C/64G/NVMe，Linux。
- 压测端：3 台，每台 ≤40 万连接，10GbE，独立端口段。
- 阶梯：1K / 10K / 100K / 500K / 1M；每档稳态 ≥10 分钟。
- 记录：建连成功率、`ss -s` 总连接、进程 fd、RSS、空载 CPU、心跳 CPU、P50/P95/P99、`ListenOverflows`。
- 心跳：60s；纯心跳期间 MySQL `Questions`/`Com_select` 与 Redis `commandstats` 必须零增长。
- 业务 QPS：读 500/1,000/2,000/5,000；写 100/300/500/1,000（含幂等）。
- 结论：未达标必须写“未达标 + BLK-1/BLK-3”，不得用“理论可达”充当结论。

---

## 12. 安全与合规

- API Key 只在客户端，使用 Windows DPAPI；不入库、不进日志、不进 TCP 帧、不写进仓库。
- 仓库只保留 `.env.example`；`.env` 被 `.gitignore` 忽略。
- **本次在 Windows 上的门禁扫描曾把 `.env` 中真实 Key 打印到终端**；该 Key 未进入 Git，但建议轮换。
- TLS 尚未实现；当前是明文 TCP，公网部署前必须补 Muduo SSL + 证书校验 + 客户端 SPKI 固定。
- 单机 MySQL/Redis 是单点；Redis 故障需降级直查 MySQL，MySQL 故障需保持连接与心跳不崩。
- 禁止：Redis pub/sub、客户端直连 Redis、多 Redis 实例、WebSocket 推送、服务端 LLM 代理。

---

## 13. 已知实现与设计的差异

1. **幂等账本分区唯一键**：设计写 `(user_key, idem_key)`，但 MySQL 分区要求唯一键包含分区列；实现为 `(user_key, idem_key, created_day)`。同日唯一、跨日可复用；幂等窗口内仍能拦截重复。
2. **心跳巡检**：设计写“每 sub loop `runEvery` + 最小堆”；当前 `TcpServerApp` 用主 loop 全局 1s sweep。功能等价，但 1M 连接时需要改为分 loop 或分片，避免单点巡检开销。
3. **Muduo 未 vendored**：CMake 只做 `find_path`；Linux 上需补依赖。
4. **bench_tcp 是冒烟版**：不是 100 万压测工具。
5. **TLS 未实现**：只有明文帧。
6. **KeyVault 仅 Windows**：Linux 上不构建该模块。

---

## 14. 给 Linux AI 的执行提示

1. 先运行 `git log -1 --oneline`、`git status --short`、`cmake --version`、`mysql --version`、`redis-server --version`，把环境结果汇报给用户。
2. 按第 7–9 节安装依赖、构建、跑 10 个测试；任何失败先修构建，不要跳到业务。
3. 按第 10 节补 T0.2 → T3.3 → T6.3，完成一次真实握手 + `profile.get`。
4. 每完成一项，给出：命令、原始输出、通过/失败、下一步。
5. 未实测的容量结论一律标注“未验证/阻断”。
6. 不要读取或输出 `.env` 内容；不要提交密钥。
7. 不要合并 PR #2；本分支已包含 `chatroom-backend/` 代码作为基线。
8. 不要直接改 main；在新分支上继续。
9. 如果用户要“局域网联调/同学客户端连服务端”，先按 10.1 检查四个硬缺口；没有测试握手 Handler 前，不要让同学连接并误判为“网络问题”。
10. 联调只在内网、明文 TCP 下进行；测试 token 可用，真实模型 Key 不进服务端、不进日志、不进帧。

---

## 15. 远端与 clone

```bash
git clone git@github.com:FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
git checkout feat/app-transport-scale
git pull --ff-only
```

- 分支：`feat/app-transport-scale`
- 基线：`origin/main` + `chatroom-backend/`
- PR #2 未合并
- 本次提交见 `git log -1 --oneline`

如果 Linux 上需要只拉本次分支：

```bash
git fetch origin feat/app-transport-scale
git checkout -b feat/app-transport-scale origin/feat/app-transport-scale
git pull --ff-only
```