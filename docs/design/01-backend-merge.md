# 后端合并方案：② P7/P8 基线并入 ③ 新 TCP 架构

## 1. 合并原则

1. **单一进程**：只保留 `eat-server`（TCP 9443）。业务不再有 HTTP 入口；仅保留回环 `127.0.0.1:9080` 的 `healthz`/`metrics`。
2. **单一构建**：根 `CMakeLists.txt` 是唯一入口；`chatroom-backend/` 不参与构建，最终退役。
3. **单一 schema**：P8 `001_p8_business.sql` 作为基线，`002`–`006` 演进；P7 `init.sql` 只作迁移源，不再建表。
4. **单一网络栈**：Muduo 主从 Reactor + 40B 帧 + JSON；P7 自研 Reactor、`\n` 分帧、httplib 全部退役。
5. **语义零丢失**：P8 的字段名、校验、截断、默认值、错误映射逐条移植；P7 的账号/好友业务规则逐条移植。

## 2. 模块映射（来源 → 目标 → 处置）

| 来源 | 目标 | 处置 | 说明 |
|---|---|---|---|
| `chatroom-backend/server/http/service/*`（6 个） | `server/business/{profile,meal,stats,food,friend,ranking}/` | **迁移** | 去掉 httplib/Controller 依赖；构造注入 `user_key`、`MySQLPool`、`CacheClient` |
| `chatroom-backend/server/http/dao/*`（7 个） | `server/business/*/dao/` | **迁移+改造** | `agent_uid`→`user_key`；新增 `version`/`updated_at` 条件更新与软删条件 |
| `chatroom-backend/server/database/MySQLPool.*` | `server/database/MySQLPool.*` | **迁移+改造** | 默认 `borrow_for(200ms)`；`PoolStats`；池上限 64 |
| `chatroom-backend/server/database/Redis.*` | `server/database/RedisClient.*` + `server/cache/CacheClient.*` | **改造** | 只保留连接/命令层；命令白名单 GET/SETEX/DEL/MGET/EXISTS/PING；禁 PUBLISH/SUBSCRIBE/SCAN/KEYS |
| `chatroom-backend/server/email/EmailSender.*` | `server/business/account/EmailSender.*` | **迁移** | 保留 SMTP/DRY_RUN；凭据只走环境变量 |
| `chatroom-backend/server/service/ChatService.cc`（13 路由） | `server/business/account/*`、`friend/*` | **重写为 Service** | 只搬业务规则（验证码策略、CAS 登录位、双向好友状态机），不搬分发与网络 |
| `chatroom-backend/server/http/controller/*` + `Router`/`Auth`/`main_http.cc` | `server/app/handlers/*` | **重写** | 参数映射规则逐行移植，形态改为 JSON→Service→JSON |
| `chatroom-backend/server/reactor|net|model/service` | — | **退役** | P7 网络与在线位不进入新链路；SQL 语义并入 DAO |
| `chatroom-backend/web`/`transit`/`client`(TUI) | — | **退役** | Web/WS/TUI 被 Windows App 替代 |
| 根 `server/tcp`、`identity`、`cache`、`app`、`shared/protocol` | 保留 | **整改** | 修编译错误、并发缺陷、未接线模块 |
| `agent/`（Python） | 保留，入口改造 | **改造** | S2 起改为命名管道；业务数据经 App 的 TCP 桥 |

## 3. 关键协调点

### 3.1 身份（三套 ID 合并）

- 运行期唯一键：`user_key CHAR(26)`（ULID），来自 `002_identity.sql`。
- `account_alias(account_id, alias_type, normalized_value)` 承载：`login:<规范化邮箱/手机>`、`legacy_p7:<user.id>`、`legacy_p8:<agent_uid>`。
- 握手成功只绑定 `ConnContext.user_key`；body 内 `user_id`/`user_key` 一律 `Erase` 并计 `identity_spoof_attempt`。
- 迁移生成 `user_key` 必须**确定性可重跑**（固定种子/映射表），禁止每次随机导致重复账号。

### 3.2 餐记录与幂等（避免双写）

- `meal_records`：`PK (id,date)`、`idx_uid_date_id(user_key,date,id)`、软删 `deleted_at`、`version` 乐观锁；按月 `RANGE(TO_DAYS(date))`。
- `idempotency_records`：`UK (user_key, idem_key, created_day)`——分区表唯一键必须含分区列，这是已登记的偏离（`migrations/add/README.md:21-28`）。
- 旧 P8 行迁移：`agent_uid` → `user_key` 通过 `account_alias` 解析；`created_at` 保留，`created_day` 为生成列。

### 3.3 好友（P7 双行 → 新表）

- `friend_relation(owner_key, peer_key, status, blocked_by)`，`UK(owner_key,peer_key)`。
- P7 迁移方向：`friend(user_id,friend_id,status)` 双行 → 两行 `friend_relation`；`status=1`（待对方验证）按 `0` 归并并记录审计。
- 运行期写操作：`friend.request/accept/remove/block` 同事务写双行；事件写 `event_outbox`，`sync.pull` 按 `seq` 补拉；**不使用 Redis 队列/pubsub**（P7 的 `offline_request:<uid>` 退役）。

### 3.4 验证码与登录

- P7 验证码在进程内存（`ChatService.cc` `m_codes`），单实例可接受但重启丢失。
- 合并后：验证码存 Redis（key `eat:{env}:code:{type}:{normalized_login}`，TTL 300s，60s 频控，错 5 次锁 5min）——仍是"服务端存储后端"，符合约束。
- 登录态：P7 `user.state` 在线位废弃；以 `SessionRegistry`（内存）+ `session` 表（持久）为准。

### 3.5 配置统一

- `server.toml` 增 `mysql_password`/`redis_password`/`mysql_socket`，但**优先读环境变量**，仓库只留 `.example`。
- 现状 `Config` 约 25 个字段无消费者（`epoll_batch`、`read_buffer_limit`、`global_inflight_limit`、`rate_per_user_*`、`cache_*`、`admin_port` 等）。裁决：**要么接线，要么删除**，不允许"配置存在但不生效"（见 `03-refactor-cleanup.md`）。
- 去掉 `glog` 依赖，统一用 `server/app/Log`（JSON 行 + 脱敏）。

## 4. 目标目录

```
CMakeLists.txt
cmake/                                # 依赖查找（Muduo/OpenSSL/MySQL/hiredis）
shared/protocol/                      # 唯一协议实现（server/client/bench 同源）
third_party/muduo/                    # vendored + patches/
server/
  tcp/                                # Muduo 接入、帧循环、连接治理、main.cc
  identity/                           # PasswordHasher/TokenVerifier/IdentityService/SessionRegistry
  cache/                              # CacheClient/键/TTL/单飞/失效清单/降级 WAL
  database/                           # MySQLPool、RedisClient（仅连接与命令层）
  business/
    account/ friend/ profile/ meal/ stats/ food/ ranking/
  app/                                # Config/Log/Metrics/BusinessExecutor/ServiceRegistry/handlers/HttpAdmin
  http/                               # 仅过渡期保留，S2 后删除
migrations/
  base/001_p8_business.sql foods_seed.sql
  add/002..006 + rollback
  oneoff/p7_to_v1.sql                 # 一次性、可重跑、双向校验
client/
  app/ transport/ ipc/ agentbridge/ keyvault/ llm/ resources/web/
agent/                                # Python Agent（能力本体保留）
deploy/ tests/ tools/ docs/
```

## 5. 过渡期兼容

- S1 完成前：保留 `server_http`（P8）与 `ChatService`（P7）可运行，作为对照与回滚基线。
- S1 期间：Agent 仍可走 `:8080`（`ServiceClient`），App 尚未交付；**两套并存仅限过渡期，不得对外宣称已合并**。
- S2 完成后：删除 `server/http`、`chatroom-backend/`、`transit`、`web`；Agent 改走 App 桥。

## 6. 退役与删除清单（需确认后执行）

| 对象 | 体积/理由 | 处置 |
|---|---|---|
| `chatroom-backend/`（291 文件） | 两套网络栈 + 历史 evidence 日志 | 业务迁完后整目录删除（Git 历史保留）；保留 104 断言脚本作为回归参考前先迁入 `tests/legacy/` |
| 根 `server/http`（过渡容器） | 合并完成后无调用方 | S2 后删除 |
| `chatroom-backend/web/httplib.h` | 仅 P8/Web 使用 | 随目录删除 |
| `transit/`、`chatroom-backend/web`、TUI `client/` | 被 App 替代，且规格禁 WebSocket | 删除 |

## 7. 验收判据（S1）

1. `eat-server` 在 Linux 构建成功，`ldd` 出现 `libssl`，不出现 `glog`。
2. 握手接 `TokenVerifier`：未握手业务帧 → `2001` 并断连；有效 token → `handshake_resp`。
3. `profile.get`/`meal.create`/`stats.weekly`/`food.menu`/`ranking.friends` 五条链路返回 `ok=true`，字段与 P8 对照一致。
4. 写方法缺 `idem_key` → `3001`；同键异 payload → `3003`；重发同 payload → DB 增量 1。
5. 缓存：命中路径 MySQL `Com_select` 零增长；写后立即读为新值；停 Redis 只读仍成功。
6. 迁移：`001→006` 可重跑、可成对回滚；oneoff 迁移行数与抽样哈希一致。
7. 日志零 token/密码/Key/body 原文。
