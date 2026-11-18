# 代码生成、重构、排错过程记录

> 文档版本：2026-11-18
> 对应分支：`feat/app-transport-scale`
> 记录范围：从 `origin/main`（`fc34b0f`）到 `23c9a97` 的实际开发过程
> 证据来源：Git 提交历史、`docs/` 下的交接与设计文档、`.codeartsdoer/specs/`、测试脚本与实测输出
> 记录原则：只写发生过的事；已验证与未验证分开标注；不把「代码写完」写成「功能可用」

---

## 0. 先说明这份记录怎么读

本文不是「最佳实践教程」，而是这个项目真实的施工日志。它回答三个问题：

1. **代码是怎么生成的**：需求如何变成规格、任务和具体文件；
2. **结构是怎么改的**：三套并存的代码如何收敛成一套；
3. **问题是怎麼定位和修的**：每个故障的现象、根因、修复与验证。

文中的编号用于交叉引用：

- `G-xx`：代码生成条目；
- `R-xx`：重构条目；
- `T-xx`：排错条目。

每条尽量给出可核查的文件路径或提交。个别早期问题只有交接文档记录、没有保留原始终端输出，这类会标注「据交接文档」。

---

## 1. 时间线与阶段

| 阶段 | 时间 | 主要工作 | 结果 |
|---|---|---|---|
| **需求审查** | 2026-11-17 前 | 审查 PR #2（P7/P8 后端）是否可跑通、是否支撑百万并发 | 结论：功能有证据，百万并发不成立，不合并 |
| **口径确认 R2–R6** | 2026-11-17 | 客户端改 App、服务端 C++/Muduo、单机、JSON、Cache-Aside、Key 位置 | 形成 `app_transport_scale/spec.md`（约 2986 行） |
| **方案设计** | 2026-11-17 | 单进程服务端、40B 帧、统一身份、分区表、IPC、打包 | 形成 `design.md` 与 `docs/design/00–04` |
| **任务规划** | 2026-11-17 | 13 个任务组、102 个子任务、11 个阶段门 | 形成 `tasks.md` |
| **S0 基线修复** | 2026-11-17 | 修 CMake、协议、连接治理、线程池，让 `eat-server` 可构建、`ctest` 全绿 | 12/12 通过 |
| **S1 业务合并** | 2026-11-17 | 恢复 P8 业务语义，接 MySQL、Redis、身份、15 个方法 | 真实 TCP 15 方法通过 |
| **S2 客户端/传输** | 2026-11-17 | `bridge.js` 抽象、IPC、Win32+WebView2、Agent TCP | IPC smoke 通过；GUI 待 Windows 验证 |
| **S3 重构与清理** | 2026-11-17 | 删 `chatroom-backend/`、统一身份、修死代码与契约漂移 | 测试保持全绿 |
| **路线 A** | 2026-11-17 | 保留 Agent 三层，业务全部走 TCP，GUI 桥接 | 服务端 15 方法 + 缓存 + Agent TCP 实测 |
| **v0.3.0 交付** | 2026-11-17/18 | 验证码、安装包、登录重设计、真实数据引导 | Release `v0.3.0` |
| **Windows 交接** | 2026-11-18 | Web/GUI/源码三条测试路径、UI 自迭代闭环 | 服务端冻结，队友可独立改 UI |

阶段推进有一条硬规则：**上一阶段的验证不通过，不进入下一阶段**。例如 S0 的 `ctest` 不全绿之前，不开始 S1 的业务迁移；S1 的真实请求没跑通之前，不宣称「服务端可用」。

---

## 2. 代码生成记录

### G-01 协议层：40B 帧 + CRC32C + 严格 JSON

**目标**：三端（C++ 服务端、C++ 客户端、Python Agent）共用同一套帧语义，避免「服务端能收、客户端发不出」。

**产出**：

| 文件 | 作用 |
|---|---|
| `shared/protocol/protocol.h` | magic、版本、帧类型、错误码、最大帧长 |
| `shared/protocol/FrameCodec.{h,cc}` | 粘包/半包/拆包、超长前置拒绝、CRC 校验 |
| `shared/protocol/Crc32c.{h,cc}` | CRC32C 查表实现，标准向量 `0xE3069283` |
| `shared/protocol/JsonValue.{h,cc}` | 轻量 JSON 值模型 |
| `shared/protocol/StrictJson.{h,cc}` | 重复键、深度、尾随、BOM、NaN、大整数转字符串 |
| `agent/transport/tcp_frame.py` | Python 侧同构编解码 |

**关键决策**：

- 头固定 40 字节，不用变长头，便于 Python/C++ 对齐；
- body 唯一 JSON，不做 Protobuf/MessagePack 的可配置编码，避免两端实现分叉；
- 超长帧在只读到头部时就拒绝，避免攻击者用小包骗出大缓冲；
- 严格 JSON 拒绝重复键，防止「同名键后值覆盖」带来的鉴权绕过。

**验证**：`frame_codec_test`（往返、粘包、逐字节半包、超长、magic、CRC 位翻转、零 body）、`strict_json_test`（重复键、深度、尾随、NaN、BOM、大整数、Unicode）。

### G-02 连接治理：状态机、心跳、限流、背压

**目标**：一个连接从建立到关闭的每个异常路径都有明确归宿，不靠「应该不会」。

**产出**：

| 文件 | 作用 |
|---|---|
| `server/tcp/ConnContext.{h,cc}` | 连接状态机、幂等关闭 |
| `server/tcp/HeartbeatMonitor.{h,cc}` | 最小堆心跳，只检查堆顶 |
| `server/tcp/RateLimiter.{h,cc}` | 令牌桶：连接/用户/全局/握手 IP |
| `server/tcp/Backpressure.{h,cc}` | 每连接与全局发送水位 |
| `server/tcp/FrameDispatcher.{h,cc}` | 未握手拒绝、写方法强制幂等键、身份伪证丢弃 |

**关键决策**：

- 心跳帧不落库、不查 MySQL/Redis，避免 100 万连接每 60s 打爆存储；
- 未握手连接发业务帧直接返回 `2001`，不进入业务线程池；
- 请求体里的 `user_key` 一律丢弃并计数，身份只认连接上下文，防止越权；
- 写方法缺 `idem_key` 在分发层拒绝，不依赖业务层自觉。

**验证**：`connection_governance_test`、`frame_dispatcher_test`。

### G-03 业务线程池：ThreadPool + BusinessExecutor

**目标**：把阻塞 SQL 从 Muduo IO 线程挪走，并保证过载时快速失败。

**产出**：

| 文件 | 作用 |
|---|---|
| `server/app/ThreadPool.{h,cc}` | 有界队列、非阻塞提交、deadline、异常隔离、Stop 丢弃计数 |
| `server/app/BusinessExecutor.{h,cc}` | 业务门面：默认超时策略 + 指标 |

**关键决策**：

- 队列有界（默认 8192），满了立即拒绝，不无限堆积；
- 任务抛异常只增加 `failed` 计数，不终止进程；
- `Stop()` 后丢弃未执行任务并计数，便于优雅关闭时观察损失；
- `BusinessExecutor` 只做门面，不重复实现线程池，避免两套并发逻辑。

**验证**：`thread_pool_test`（提交、拒绝、超时、异常、Stop）。

### G-04 身份：scrypt、令牌哈希、会话上限、验证码

**目标**：统一身份，废弃 P7 整型 `id` 与 P8 `agent_uid` 两套旧标识。

**产出**：

| 文件 | 作用 |
|---|---|
| `server/identity/PasswordHasher.{h,cc}` | OpenSSL `EVP_PBE_scrypt`，参数可升级 |
| `server/identity/TokenVerifier.{h,cc}` | 令牌 SHA-256 摘要、60s LRU、过期/吊销 |
| `server/identity/SessionRegistry.{h,cc}` | 设备顶替、3 设备上限、事件序号 |
| `server/identity/CaptchaStore.{h,cc}` | SVG 验证码、TTL、一次性消费 |
| `server/identity/IdentityService.{h,cc}` | 注册/登录/改密/撤销 |
| `migrations/add/002–003*.sql` | `account` / `account_alias` / `session` |

**关键决策**：

- `user_key` 用 26 位 ULID，不再用自增整型做主身份；
- 令牌只存摘要，数据库泄露不等于令牌泄露；
- 登录失败 5 次锁 5 分钟，刷新令牌复用直接拒绝；
- 验证码默认开启，避免脚本暴力注册；联调可通过配置临时关闭，但正式环境保持开启。

**验证**：`identity_test`、`token_verifier_test`，以及真实握手。

### G-05 缓存：Cache-Aside 全套组件

**目标**：在不引入第二套中间件、不用 pub/sub 的前提下，把读多写少的业务挡住。

**产出**：

| 文件 | 作用 |
|---|---|
| `server/cache/CacheKeyBuilder.{h,cc}` | 键规范化（大小写、空白、参数顺序） |
| `server/cache/CacheValue.{h,cc}` | JSON 序列化/反序列化、版本字段 |
| `server/cache/TtlPolicy.{h,cc}` | 正缓存/负缓存 TTL + 抖动 |
| `server/cache/SingleFlight.{h,cc}` | 同键并发重建只放行一个 |
| `server/cache/InvalidationSet.{h,cc}` | 写后 DEL 清单 |
| `server/cache/CacheClient.{h,cc}` | 读路径、回填、熔断、降级 |
| `server/database/RedisClient.{h,cc}` | 最小 RESP 客户端 + 有界连接池 |

**关键决策**：

- 写路径只 DEL、不更新，避免并发写把旧值写回；
- 负缓存短 TTL，防止空查询穿透；
- 熔断连续 3 次失败打开 5s，期间直接查 MySQL；
- Redis 命令白名单只含 GET/SETEX/DEL/MGET/EXISTS/PING，`PUBLISH/SUBSCRIBE/SCAN/KEYS` 在接口上就不存在；
- 连接池与 IO 超时都很短（20ms），宁可降级也不拖慢请求。

**验证**：`cache_test`；真实环境验证写后失效、停 Redis 业务仍成功、恢复后回填。负缓存命中路径未单独覆盖，已在状态文档中标注。

### G-06 业务方法：从 P8 语义恢复到 15 个方法

**目标**：不丢 P8 已定的字段、校验、截断、默认值与错误映射，但把 HTTP 语义层换成 TCP Handler。

**产出**：

| 目录 | 内容 |
|---|---|
| `server/business/profile/` | `ProfileDao`、`ProfileService` |
| `server/business/meal/` | `MealDao`、`IdempotencyDao`、`MealService` |
| `server/business/stats/` | `StatsService`（营养缺口、周报） |
| `server/business/food/` | `FoodDao`、`FoodService`（菜单、搜索） |
| `server/business/friend/` | `FriendDao`、`FriendService`（双向关系状态机） |
| `server/business/ranking/` | `RankingDao`、`RankingService`（好友范围） |
| `server/app/ServiceRegistry.cc` | 统一注册 15 个方法 + 认证方法 |

**关键决策**：

- 构造注入 `user_key`、`MySQLPool`、`CacheClient`，Service 不感知网络；
- DAO 用条件更新（`version` / `updated_at`）与软删条件，避免并发覆盖；
- 写方法在分发层已强制幂等键，业务层再做冲突检测；
- `chatroom-backend/` 删除前先从 Git 历史恢复语义，不做「凭记忆重写」。

**验证**：`tests/integration/service_tcp_test.py` 覆盖 15 个方法；真实 TCP + MySQL + Redis 跑通。

### G-07 Agent：HTTP → TCP 数据通道

**目标**：Agent 不再直连旧 P8 `:8080`，业务数据统一走 `ServiceClient` → TCP。

**产出**：

| 文件 | 作用 |
|---|---|
| `agent/data/service_client.py` | 业务方法封装、幂等键生成、mock 回退控制 |
| `agent/transport/tcp_frame.py` | 40B 帧编码/解码、握手、请求超时 |
| `agent/ipc_server.py` | 命名管道服务：`hello/ping/agent.chat/service.request/service.configure/shutdown` |
| `agent/config.py` | `EAT_TCP_HOST/PORT`、`EAT_SERVICE_REQUIRED` 等配置 |

**关键决策**：

- `EAT_SERVICE_REQUIRED=1` 时禁止回退 mock，避免验收时看到假数据；
- 未配置 `EAT_TCP_HOST` 时 `/api/service/status` 明确返回 `mode=mock`，不静默；
- 幂等键由请求内容哈希生成，重试不会重复写；
- IPC 用 4B 小端长度 + JSON，上限 1 MiB，避免大对象撑爆管道。

**验证**：`tests/integration/agent_service_client_test.py`、`ipc_smoke.py`（122 个事件）。

### G-08 客户端：TCP 传输、离线队列、KeyVault、Win32 壳

**产出**：

| 文件 | 作用 |
|---|---|
| `client/transport/TcpTransport.{h,cc}` | 帧循环、握手、心跳、退避重连、请求超时 |
| `client/transport/OfflineQueue.{h,cc}` | 幂等去重、有界、失败转人工 |
| `client/ipc/IpcProtocol.{h,cc}` | 与 Agent 同构的 4B LE + JSON |
| `client/keyvault/KeyVault.{h,cc}` | Windows DPAPI 加密、撤销、轮换、指纹 |
| `client/app/win32/main.cc` | 单实例、托盘、WebView2、本地资源、桥接 |

**关键决策**：

- GUI 不监听 HTTP 端口，页面只通过 `postMessage` 与壳通信；
- 页面资源随包，禁止远程首屏；
- Key 只在客户端，服务端零 LLM；
- Linux 上只验证 IPC 协议与前端语法，Windows 相关结论明确标注未验证。

### G-09 打包与部署

**产出**：

| 文件 | 作用 |
|---|---|
| `deploy/docker/Dockerfile` | 多阶段：编译 Muduo + 服务端，运行阶段只留二进制与依赖 |
| `deploy/docker/docker-compose.yml` | MySQL 8.4 + eat-server，健康检查后启动 |
| `deploy/docker/entrypoint.sh` | 等待 MySQL、执行迁移、启动服务 |
| `scripts/build-linux.sh` | Linux 构建 + ctest + Agent 单测 + 前端语法 |
| `scripts/package-linux.sh` | 生成服务端 tar.gz 与客户端源码 zip |
| `scripts/build-windows-oneclick.ps1` | Windows 一键：vcpkg OpenSSL、WebView2、eat-app、eat-agent、web、zip |
| `scripts/build-installer.ps1` | Inno Setup 安装包与桌面快捷方式 |
| `scripts/install-deps.sh` | Linux 依赖一键安装 + Muduo 构建 |

**关键决策**：

- 二进制与安装包不进 Git，走 Releases；
- 迁移在打包时清洗换行与注释，避免目标机导入失败；
- Windows 产物必须在 Windows 构建，Linux 构建机只提供源码与脚本。

---

## 3. 重构记录

### R-01 三线合并：从三套并存到一套骨架

**问题**：仓库里同时存在

1. 根目录 Web + Python Agent（功能最全）；
2. `chatroom-backend/`（P7 自研 Reactor + P8 cpp-httplib，两套传输）；
3. 根 `server/` + `shared/protocol`（Muduo + 40B 帧，业务未打通）。

三套导致两套网络栈、两套身份、两套幂等、三份相似协议实现、两份交接文档。

**决策**：

- 以 ③ 为唯一服务端骨架；
- ② 只保留业务语义、SQL 与测试用例作为迁移参考，然后删除；
- ① 改为 Windows App，页面 100% 复用。

**执行**：

- 先从 Git 历史恢复 P8 的 Meal/Stats/Food/Friend/Ranking Service/DAO；
- 重写为 TCP Handler，注册到 `ServiceRegistry`；
- 删除 `chatroom-backend/`，SQL 基线迁入 `migrations/base/`；
- 统一构建入口为根 `CMakeLists.txt`。

**结果**：单进程 `eat-server` 承载全部业务；`ctest` 保持 12/12。

### R-02 身份统一：`user_key` 成为唯一标识

**问题**：P7 用自增整型 `id`，P8 用字符串 `agent_uid`，新架构用 `user_key`，三套 ID 并存会让好友、会话、幂等全部对不上。

**做法**：

- 新建 `account` + `account_alias`，`user_key` 为 26 位 ULID；
- 旧 ID 只作为 `account_alias` 的迁移来源；
- 业务表统一按 `user_key` 读写；
- 协议层丢弃请求体里的 `user_key`，只认连接上下文。

**遗留**：`health_profile.agent_uid` 列名尚未改成 `user_key`（007 迁移未执行），当前以新值写入旧列名。这是已知差异，不是完成状态。

### R-03 协议统一：只保留 40B 帧

**问题**：P7 用 `\n` 分帧，P8 用 HTTP，新架构用 40B 帧。三套协议意味着客户端要写三套解析。

**做法**：

- `shared/protocol` 为唯一实现；
- 客户端 `TcpTransport`、Agent `tcp_frame.py`、压测工具 `bench_tcp` 全部复用同一套常量与编解码；
- 删除 P7 行协议与 P8 httplib 入口；
- 序列化唯一 JSON，不做可配置编码。

### R-04 缓存统一：只保留服务端 Cache-Aside

**问题**：P7 的 Redis 用于离线消息，P8 没有缓存，新架构要 Cache-Aside。如果保留多套用法，会出现「同一个 Redis 既做队列又做缓存」。

**做法**：

- Redis 只作为服务端缓存；
- 禁 pub/sub、禁客户端直连、禁多实例；
- `RedisClient` 命令白名单化；
- 业务读路径统一走 `CacheClient`，写路径统一 DEL。

**纪律**：客户端与 Agent 都不允许直连 Redis；Agent 只能经 TCP 调服务端。

### R-05 线程池门面化

**问题**：早期 `BusinessExecutor` 自己管线程与队列，后来又引入参考 `lab_threadPool` 的 `ThreadPool`，两套并发逻辑会漂移。

**做法**：

- `ThreadPool` 只负责有界队列、worker、deadline、异常隔离；
- `BusinessExecutor` 变成门面，只保留业务超时策略与指标；
- 测试直接覆盖 `ThreadPool`，门面不重复测。

### R-06 前端传输抽象：`eatTransport`

**问题**：页面原来直接 `fetch('127.0.0.1:8787')`，放进 WebView2 后没有 HTTP 端口，页面会全部失效。

**做法**：

- `bridge.js` 暴露 `window.eatTransport`；
- 浏览器模式走 `fetch + SSE`；
- App 模式走 `postMessage` → 壳 → IPC → Agent；
- 页面业务代码只调用 `eatTransport.request`，不感知运行环境。

**结果**：同一套 DOM/CSS/JS 在浏览器与 App 内都能跑；GUI 不需要重写 UI。

### R-07 前端重设计：登录、引导、真实数据

**问题**：早期登录靠手改 `login.json`，首次使用没有引导，页面数据可能来自 mock。

**做法**：

- 新增页面内登录表单：服务端地址、账号、密码、图形验证码；
- 新增首次引导：身高、体重、年龄、目标、过敏原、忌口、预算；
- 登录成功后把会话写入壳，再通知 Agent；
- 增加 `redesign.css` / `login.css`，统一视觉；
- 明确标注哪些数据来自服务端、哪些来自 mock。

**注意**：UI 修改与服务端解耦。队友可以无限次改页面、重建客户端，服务端不需要改参数或重启，只要仍调用既有 TCP 方法。

### R-08 死代码与契约漂移清理

**清理项**：

- Agent：删除无调用方的 `ContextManager.history()`、`Planner.render_plan_prompt()`、`EventBus.drain()`、`_ensure_simulated`；
- 前端：清理不存在元素的事件绑定、未使用的 `updateModelLabel`、未绑定的 `shareSkill`；
- 契约：图片上限统一为 10、随机比例统一 70/30、选择器改为真实 DOM；
- 语义：`delete_meal` / `update_meal` 标记 `persisted=false`，不再暗示已落库；
- 安全：`/api/tools/call` 的 `confirmed` 改为严格布尔。

**纪律**：删除前必须全仓 grep 确认无调用方；删除与功能变更分开提交；每步保持测试全绿。

### R-09 Windows 交付形态调整

**原目标**：单 exe（资源嵌入 + Agent 内置）。

**实际**：先交付「exe + `web/` + `eat-agent.exe` + DLL」目录形态，再用 Inno Setup 打成安装包。单 exe 仍未完成。

**原因**：资源嵌入与 Agent 释放需要额外工程；先把可用形态交付，避免为了单文件拖住验收。

---

## 4. 排错记录

### T-01 MySQL 1503：分区表唯一键必须包含分区列

**现象**：执行 `005_meal_partition.sql` 报 `ERROR 1503 (HY000): A UNIQUE INDEX must include all columns in the table's partitioning function`。

**定位**：`idempotency_records` 设计主键为 `(user_key, idem_key)`，但表按 `created_day` 分区。

**根因**：MySQL 要求分区表的所有唯一键都包含分区列。

**修复**：主键改为 `(id, created_day)`，并保留 `(user_key, idem_key, created_day)` 唯一约束。语义变为「同日幂等、跨日可复用」，仍在幂等窗口内能拦截重复。

**验证**：迁移在 MySQL 8.4 上完整执行，两张分区表各 25 个分区。

**经验**：分区键不是普通索引列，设计唯一约束时必须先看分区函数。

### T-02 CRLF 与 `--注释` 缺空格导致 1064

**现象**：导入 `001_p8_business.sql` / `foods_seed.sql` 报 `ERROR 1064`，指向看似正常的注释行。

**定位**：文件来自 Windows，含 CRLF；部分注释写成 `--注释`，MySQL 8.4 要求 `--` 后至少一个空格。

**修复**：

- 打包与 Docker 构建时先 `tr -d '\r'`；
- 用 `sed 's/^--\([^ ]\)/-- \1/'` 补空格；
- 排除 `*_rollback.sql`，避免正向迁移后误执行回滚。

**验证**：Docker 容器首次启动自动迁移成功；tarball 内 `00_all.sql` 可直接导入。

**经验**：SQL 文件跨平台传递前先统一换行与注释格式，不要把「本机能导入」当成「目标机能导入」。

### T-03 utf8mb4 导入导致中文菜名乱码

**现象**：菜单与搜索返回空或乱码，但数据库里能查到行。

**定位**：导入时未指定客户端字符集，中文被二次编码。

**修复**：导入命令显式加 `--default-character-set=utf8mb4`；连接串与表统一 `utf8mb4_unicode_ci`。

**验证**：`food.menu?meal_type=午餐` 返回真实中文菜名。

**经验**：这类问题容易被误判成代码 bug，实际重导一次即可；排查时先看 `HEX()` 与客户端字符集。

### T-04 FrameCodec 头文件与命名空间错误

**现象**：`eat-server` 编译失败，报 `FrameCodec` 未声明或命名空间不匹配。

**根因**：协议重构后头文件路径与 `eat::protocol` 命名空间没有同步。

**修复**：统一 include 路径为 `shared/protocol/FrameCodec.h`，统一使用 `eat::protocol`。

**验证**：`eat-server` 构建通过，`frame_codec_test` 通过。

### T-05 `Buffer::peek` 类型转换错误

**现象**：编译告警/错误，Muduo `Buffer::peek()` 返回类型与代码假设不一致。

**修复**：显式按 Muduo 的实际返回类型取指针与长度，避免隐式转换。

**验证**：构建通过，帧解析测试通过。

### T-06 连接计数下溢

**现象**：连接反复建立/关闭后，活跃连接数出现极大值或负数。

**根因**：关闭路径可能重复执行，`active_connections_` 被减两次。

**修复**：连接状态机保证关闭幂等；计数只在状态从「已建立」转「已关闭」时减一次。

**验证**：`connection_governance_test` 覆盖重复关闭。

### T-07 一次读取 64 帧后停止处理

**现象**：压测或批量请求时，单次 `OnMessage` 只处理固定数量的帧，后续帧留在缓冲里不被处理。

**根因**：循环上限写死，且没有在达到上限后继续调度。

**修复**：按缓冲实际可解析帧数循环，直到半包或空；不再用固定 64 截断。

**验证**：`frame_codec_test` 的粘包用例与集成请求。

### T-08 `OnTimer` O(n) 扫描

**现象**：连接数上升后定时器开销线性增长。

**根因**：每个 tick 遍历全部连接做心跳/背压检查。

**修复**：心跳改用最小堆只查堆顶；其他检查按连接状态与水位短路。

**验证**：`connection_governance_test`；但 100 万连接下的实际开销**未压测**，不能宣称已解决容量问题。

### T-09 信号处理里调用 `Stop()` 导致关闭卡死

**现象**：Ctrl+C 后进程不退出或退出很慢。

**根因**：信号 handler 中执行阻塞关闭，与事件循环互相等待。

**修复**：handler 只调用 `loop->quit()`，由主循环有序停止线程池、连接与数据库池。

**验证**：本地启动/停止多次，端口正常释放。

### T-10 写方法缺幂等键返回 3001

**现象**：`profile.patch` 偶发 `patch_error code=3001`。

**根因**：客户端某条路径没有生成 `idem_key`；分发层按设计拒绝。

**修复**：客户端与 Agent 统一用请求内容哈希生成幂等键；测试脚本提供 `--omit-idem` 专门验证负向用例。

**验证**：正常路径成功；`--omit-idem` 稳定返回 3001。

### T-11 未握手直接发业务帧返回 2001

**现象**：自研客户端/脚本直接发 `profile.get` 得到 `pre_handshake_error code=2001`。

**根因**：这是设计行为，不是故障。

**处理**：在交接文档中把它写成负向验收用例；客户端必须先完成握手。

**验证**：`--skip-handshake` 稳定返回 2001。

### T-12 Agent 未配置服务端地址时返回 mock

**现象**：前端看到菜单、档案都有数据，但和服务端数据库对不上。

**定位**：Agent 启动时 `EAT_TCP_HOST` 为空，`/api/service/status` 返回 `mode=mock`。

**修复**：启动脚本显式设置 `EAT_TCP_HOST=127.0.0.1 EAT_TCP_PORT=9443`；已运行实例若 `tcp_host` 为空则自动重启；验收时设置 `EAT_SERVICE_REQUIRED=1` 禁止回退。

**验证**：`curl /api/service/status` 显示真实地址；业务接口返回 `source=service`。

**经验**：最危险的故障不是报错，而是「看起来正常但是假数据」。状态接口必须能区分 mock 与真实。

### T-13 Agent 默认只绑 127.0.0.1，局域网连不上

**现象**：本机能访问 `:8787`，队友机器访问失败。

**根因**：`ThreadingHTTPServer` 默认绑定回环地址。

**修复**：启动参数 `--host 0.0.0.0`，并在启动脚本中固定。

**验证**：`ss -ltn` 显示 `0.0.0.0:8787`；局域网 `curl` 成功。

### T-14 工具会话结束后 Agent 进程被回收

**现象**：日志已打印启动成功，但端口随后消失。

**根因**：进程挂在临时会话下，会话结束被回收。

**修复**：用 `setsid </dev/null` 启动，脱离控制终端；日志重定向到文件。

**验证**：多次会话切换后端口仍在。

### T-15 端口漂移：配置 19443，文档与客户端 9443

**现象**：`ss -ltn` 看不到 9443。

**根因**：`.dev/route-a.conf` 里的 `tcp_port` 曾写成 19443，与文档/客户端约定不一致。

**修复**：统一为 9443；排查命令先读配置实际值，不只看文档。

**经验**：端口、路径、版本这类「约定值」必须以运行配置为准；文档漂移会浪费大量排查时间。

### T-16 `dist/eat-web/` 是旧产物

**现象**：页面样式异常、资源 404，但源码看起来正常。

**根因**：静态目录长期未同步，`index.html` 引用缺失的 `login.css`，也不含 `redesign.css`。

**修复**：改前端后重新同步根目录的 HTML/JS/CSS/assets 到 `dist/eat-web/`；不要假设 8000 端口一定是最新页面。

**验证**：浏览器无 404，样式与源码一致。

### T-17 验证码导致 `client_smoke` 返回 3001

**现象**：新增验证码后，`client_smoke` 握手失败 `code=3001`。

**根因**：`client_smoke` 不支持验证码；默认 `captcha_required=true`。

**修复**：

- 正式环境保持验证码开启；
- 快速联调可在本地配置临时 `captcha_required=0`；
- 或用 `POST /api/service/captcha` 取 SVG，提取 `<text>` 字符后带 `captcha_id/captcha_code` 登录。

**验证**：两种路径都跑通；文档明确说明 `client_smoke` 的边界。

### T-18 GUI 提示 `Failed to fetch`

**现象**：Windows 页面登录/请求失败。

**根因**：页面仍指向 `127.0.0.1` 或旧地址，没有指向服务端局域网地址。

**修复**：登录表单显式填写 `10.101.17.32:8000`（Web）或 `10.101.17.32:9443`（GUI 业务）；GUI 业务不占 8787。

**验证**：队友机器完成注册、登录、档案、菜单、记录、统计、好友、排行。

### T-19 `token_expired` 后页面不恢复

**现象**：长时间放置后请求失败，重开页面才恢复。

**根因**：会话过期后前端没有重新登录。

**修复**：重新登录即可恢复；文档写明这是会话边界，不是服务端故障。

**验证**：过期后重登，接口恢复。

### T-20 Redis 停机导致业务报错

**现象**：停掉 Redis 后，部分读接口失败。

**根因**：早期缓存调用没有降级。

**修复**：`CacheClient` 熔断 fail-open，Redis 错误直接查 MySQL；恢复后自动回填。

**验证**：停 Redis 业务仍成功；启动后缓存命中恢复。

### T-21 `/api/tools/call` 的 `confirmed="false"` 被当成 true

**现象**：写操作在未确认时被执行。

**根因**：Python 真值判断把非空字符串 `"false"` 视为真。

**修复**：严格布尔解析，只接受 `true/false` 布尔值或明确的字符串映射，其余拒绝。

**验证**：Agent 单测覆盖 `"false"`、`"0"`、`None` 等输入。

### T-22 管理端口与文档不一致

**现象**：健康检查/指标访问不到。

**根因**：配置 `admin_port` 在不同环境写成 9080/19080，且该功能尚未完整接线。

**处理**：当前不以 healthz/metrics 作为验收依据；文档标注「规划中」，避免队友误判。

---

## 5. 验证与证据

### 5.1 已实测（Linux）

| 项目 | 命令 | 结果 |
|---|---|---|
| C++ 单元/协议测试 | `ctest --test-dir build --output-on-failure` | 12/12 通过 |
| Agent 单测 | `python -m unittest agent.tests.test_smoke` | 51/51 通过 |
| IPC | `python tests/integration/ipc_smoke.py` | `IPC_SMOKE_OK` |
| S1 集成 | `bash tests/integration/s1_smoke.sh` | `S1_INTEGRATION_OK` |
| 15 个业务方法 | `python tests/integration/service_tcp_test.py` | 通过 |
| Agent TCP | `python tests/integration/agent_service_client_test.py` | 通过 |
| 前端语法 | `node --check app.js bridge.js agent-adapter.js` | 通过 |
| 真实局域网 | `10.101.17.32:9443` 注册/登录/档案/菜单 | 通过 |

### 5.2 已验证的部署形态

- Docker 多阶段镜像构建成功；容器内自动迁移 + 启动 + 真实请求通过。
- `eat-server-linux-x86_64.tar.gz` 解包后可启动，真实请求通过。
- Windows `EatAppSetup-x64.exe`（`v0.3.0`）安装、桌面快捷方式、托盘、会话持久化可用。
- Web 路径 `http://10.101.17.32:8000` 可完成注册、登录、菜单、档案、统计、好友、排行。

### 5.3 明确未验证

| 项目 | 原因 |
|---|---|
| 单 exe | 未实现 |
| TLS / 公网 | 未实现 |
| 100 万在线 | 无压测机与数据，阻断 |
| MSIX 签名 | 未实现 |
| 断网冷启动 | 未在目标机系统验证 |
| 多用户并发 | 未做规模化测试 |
| 负缓存命中路径 | 未单独覆盖 |
| `health_profile` 列名迁移 007 | 未执行 |

---

## 6. 未完成与后续建议

按优先级：

1. **Windows 单 exe**：资源嵌入、Agent 释放、启动自检；完成后才能说「双击即用」。
2. **TLS**：公网前的硬前置；当前明文只限可信局域网。
3. **压测**：先用 `bench_tcp` 做基线，再逐步放大；没有数据前不宣称容量。
4. **007 迁移**：`health_profile.agent_uid` → `user_key`，清理历史列名。
5. **负缓存与熔断的完整用例**：补齐命中、过期、恢复的自动化。
6. **healthz/metrics 接线**：把「规划中」变成可观测。
7. **文档收敛**：`AGENT.md` / `AGENT_Linux.md` 与当前架构存在历史差异，需合并成单一事实源。

---

## 7. 过程复盘

### 7.1 有效的做法

1. **规格先行**：先把「必须满足什么、怎么证明」写成 spec，再写 design，再拆 tasks。避免边写边改需求。
2. **测试基线**：每个阶段开始前先让现有测试全绿；不在红色基线上重构。
3. **小步提交**：一次提交只做一类事，失败可回退。
4. **证据分级**：`已验证 / 理论推导 / 未验证 / 阻断` 四档，写文档时强制标注。
5. **脚本化交付**：构建、打包、启动都封装成脚本，避免让用户逐行粘贴命令。
6. **负向用例**：未握手 2001、缺幂等 3001、错误令牌 2002，这些比正向用例更能证明协议真的生效。
7. **状态可观测**：Agent 的 `mode=mock` 必须显式暴露，防止「假数据看起来正常」。

### 7.2 踩过的坑

1. **环境漂移**：系统 MySQL 受 AppArmor 限制、Muduo 不在系统包内、端口配置与文档不一致。结论：先读运行配置与端口，再信文档。
2. **产物漂移**：`dist/` 与源码根目录不同步，页面 404。结论：改前端必须同步产物，或走构建脚本。
3. **跨平台假设**：Linux 构建机不能产出 Windows exe；GUI 结论必须标注「未在 Windows 验证」。
4. **数据污染**：端到端测试会写入真实业务表，清理不彻底会残留。结论：用独立测试库，测试标识可追踪。
5. **密钥泄露风险**：门禁扫描曾把 `.env` 内容输出到终端。结论：扫描工具不得原样打印敏感文件；任何出现在终端/日志的 Key 都应视为已泄露并轮换。
6. **「代码写完」≠「功能可用」**：Windows 桥接代码补完但未编译验证时，不能写「已完成」。结论：完成定义必须包含目标平台验证。

### 7.3 如果重来一次

- 更早把「运行配置」与「文档约定」做成单一来源（例如由配置生成文档表格），减少端口/地址漂移。
- 更早建立独立测试库与一键清理脚本，避免在开发库上跑 e2e。
- 更早把 Windows 构建放进 CI（哪怕非阻塞），让「未验证」窗口更短。
- 单 exe 若短期做不完，应在第一版就明确「目录形态 + 安装包」是交付形态，而不是把它写成临时妥协。

---

## 8. 相关文档

| 文档 | 内容 |
|---|---|
| [`README.md`](../README.md) | 依赖、一键安装、部署运行、开源组件、码道应用总结 |
| [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) | 项目详细介绍 |
| [`architecture.md`](architecture.md) | 合并后架构事实 |
| [`architecture-status.md`](architecture-status.md) | 任务级落地状态 |
| [`DELIVERY-STATUS.md`](DELIVERY-STATUS.md) | 交付状态 |
| [`design/03-refactor-cleanup.md`](design/03-refactor-cleanup.md) | 重构与去冗余方案 |
| [`protocol/v1.md`](protocol/v1.md) | 帧协议规范 |
