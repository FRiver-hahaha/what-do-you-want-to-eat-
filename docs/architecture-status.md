# Windows App + 自研 TCP 架构落地状态

## 1. 分支与基线

- 本地工作分支：`feat/app-transport-scale`（未 push、未建 PR、未合并）。
- 基线：`origin/main`（`fc34b0f`）+ 引入 `origin/feat/backend-p8` 的 `chatroom-backend/` 作为 P8 复用基线。
- 规格与设计：`.codeartsdoer/specs/app_transport_scale/`（R6 `spec.md`、`design.md`、`tasks.md`）。

## 2. 已落地代码

| 任务 | 文件 | 状态 | 验证 |
|---|---|---|---|
| T0.3 协议常量 | `shared/protocol/protocol.h` | 完成 | 与 design 40B 头逐项对齐 |
| T1.1 协议文档 | `docs/protocol/v1.md` | 完成 | 帧类型、错误码、握手、心跳、粘包规则 |
| T1.2 FrameCodec | `shared/protocol/FrameCodec.{h,cc}` | 完成 | 粘包、1 字节半包、超长前置拒绝、CRC |
| T1.3 CRC32C | `shared/protocol/Crc32c.{h,cc}` | 完成 | 标准向量 `0xE3069283` |
| T1.4 StrictJson | `shared/protocol/JsonValue.{h,cc}`、`StrictJson.{h,cc}` | 完成 | 重复键、深度、尾随、BOM、NaN、大整数转字符串 |
| T1.5 帧分发 | `server/tcp/FrameDispatcher.{h,cc}`、`shared/protocol/methods.h` | 完成 | 未握手拒绝、写方法强制 `idem_key`、`user_key` 丢弃并计数 |
| T1.9 bench-tcp 冒烟 | `tests/load/bench_tcp/bench_tcp.cc` | 冒烟版完成 | 可编译、`--help` 可运行 |
| T2.2 连接状态 | `server/tcp/ConnContext.{h,cc}` | 完成 | 状态机与幂等关闭 |
| T2.4 心跳 | `server/tcp/HeartbeatMonitor.{h,cc}` | 完成 | 165s 空闲、最小堆只查堆顶 |
| T2.5 业务池 | `server/app/BusinessExecutor.{h,cc}` | 完成 | 有界队列、拒绝、超时计数 |
| T2.6 限流 | `server/tcp/RateLimiter.{h,cc}` | 完成 | 令牌桶 |
| T2.7 背压 | `server/tcp/Backpressure.{h,cc}` | 完成 | 每连接/全局水位 |
| T2.1/T2.3/T2.8 Muduo 骨架 | `server/tcp/TcpServerApp.{h,cc}`、`server/tcp/main.cc` | 代码已落地，未构建 | 本机无 Linux/Muduo |
| T0.7 配置/日志/指标 | `server/app/Config.{h,cc}`、`Log.{h,cc}`、`Metrics.{h,cc}` | 完成 | 配置校验、JSON 行日志、Prometheus 文本 |
| T3.1–T3.2 身份表与 scrypt | `migrations/add/002–006*.sql`、`server/identity/PasswordHasher.{h,cc}` | 完成 | 迁移幂等/回滚成对；scrypt 哈希验证 |
| T3.4 TokenVerifier | `server/identity/TokenVerifier.{h,cc}` | 完成 | SHA-256 摘要、60s LRU、过期/吊销 |
| T3.5 SessionRegistry | `server/identity/SessionRegistry.{h,cc}` | 完成 | 设备顶替、3 设备上限、事件序号 |
| T4.1–T4.3 迁移 | `migrations/add/004_friend_relation.sql`、`005_meal_partition.sql`、`006_event_outbox.sql` | 完成 | 分区与唯一键约束已处理 |
| T5.2–T5.7 缓存 | `server/cache/CacheKeyBuilder.{h,cc}`、`CacheValue.{h,cc}`、`TtlPolicy.{h,cc}`、`SingleFlight.{h,cc}`、`InvalidationSet.{h,cc}` | 完成 | 键规范化、TTL、负缓存、单飞、写后 DEL 清单 |
| T7.1 KeyVault | `client/keyvault/KeyVault.{h,cc}` | 完成 | Windows DPAPI 加密、撤销、轮换、指纹 |
| T8.2 TCP 客户端 | `client/transport/TcpTransport.{h,cc}` | 代码已落地，未连真实服务端 | 帧循环、握手、心跳、退避重连、请求超时 |
| T8.3 离线队列 | `client/transport/OfflineQueue.{h,cc}` | 完成 | 幂等键去重、有界、失败转人工 |
| T9.1–T9.2 部署 | `deploy/systemd/eat-server.service`、`deploy/sysctl/eat-server.conf`、`deploy/preflight.sh` | 完成 | systemd/sysctl/preflight |
| T0.6 仓库门禁 | `tools/scan_repo.py` | 完成 | 当前 `scan-ok` |

## 3. 已验证测试

以下测试均在本机 MSYS2 g++ 16.1.0 编译并运行通过：

1. `frame_codec_test`：帧往返、粘包、逐字节半包、超长前置拒绝、magic、CRC 位翻转、零 body。
2. `strict_json_test`：重复键、深度、尾随、NaN、BOM、无穷、大整数、Unicode、往返。
3. `connection_governance_test`：心跳、令牌桶、业务池拒绝、背压、连接状态。
4. `config_test`：示例配置、心跳/空闲/帧长校验。
5. `frame_dispatcher_test`：未握手、握手成功、未知方法、写方法 `idem_key`、身份伪证、心跳。
6. `cache_test`：SHA-256 键、菜单规范化、TTL、缓存值、单飞、失效清单。
7. `identity_test`：scrypt 哈希/验证/重哈希、设备顶替与上限、事件序号。
8. `token_verifier_test`：有效、吊销代际、过期、非活跃。
9. `offline_queue_test`：幂等去重、容量、失败转人工。
10. `keyvault_test`：DPAPI 存储/读取/轮换/撤销/指纹。

## 4. 未完成与阻断

| 任务 | 状态 | 原因/解除条件 |
|---|---|---|
| T0.2 vendored Muduo + OpenSSL | 未完成 | 本机无 WSL 发行版、无 cmake；需 Linux 环境拉取 Muduo v2.0.x 并打 patch |
| T2.1/T2.3/T2.8 真实构建与线程归属验证 | 阻断 | 同上；代码已写但未在 Linux 编译 |
| T2.9 fd/内核参数实测 | 阻断 | 需 Linux root 与压测机 |
| T2.10 IO 阻塞门禁 CI | 部分 | 脚本未接入 CI |
| T3.3 IdentityService（DB） | 未完成 | 需 MySQL 与真实会话/账号 DAO |
| T4.5/T4.6 DAO 改造与百万数据集 | 未完成 | 需 MySQL 8 与执行环境 |
| T5.1/T5.8 Redis 客户端与熔断/WAL | 未完成 | 需 hiredis/Redis 单实例 |
| T6 业务 Handler 全部帧化 | 未完成 | 依赖 T3/T5 与 MySQL |
| T7.2–T7.8 LlmEgress/SSE/指标 | 未完成 | 需 Windows 目标机与模型出网 |
| T8.1/T8.4–T8.10 Windows App/命名管道/Agent 运行时 | 未完成 | 需 Windows App 工程与双用户测试 |
| T10/T11 压测与 100 万在线验收 | 阻断 | 需 3 台压测机 + 32C/64G Linux 被测机；未实测不得宣称 |
| T12 文档收尾与 Web 退场 | 部分 | 需 App 功能对等证据 |

## 5. 环境限制

- 本机为 Windows；WSL 未安装发行版，无法构建/运行 Muduo 服务端。
- 无 `cmake`、无 `mysql`/`redis-server` 可执行文件。
- 仅有 MSYS2 g++ 16.1.0 与 OpenSSL 开发库，因此协议、缓存、身份、客户端 DPAPI 等纯逻辑可验证；Muduo、MySQL、Redis、百万压测不可验证。
- 100 万同时在线仅为压测上限，当前没有任何实测数据，不得宣称达标。

## 6. 安全事项

- `.env` 被 `.gitignore` 忽略，未被 Git 跟踪；仓库只保留 `.env.example`。
- 本次门禁扫描曾原样输出 `.env` 中的真实 Key。该 Key 未进入 Git，也未提交，但已出现在本地终端输出中，**建议立即轮换**。
- 扫描器已修正为跳过 `.env`、掩码输出 secret、跳过旧 `chatroom-backend` 与 `__pycache__`。

## 7. 下一步顺序

1. 在 Linux 环境安装 cmake、MySQL 8、Redis 单实例，拉取并 vendored Muduo。
2. 完成 T3.3 IdentityService 与 T6 业务 Handler，跑通一次真实注册→登录→握手→档案/餐记录→统计→缓存失效。
3. 完成 T7 LlmEgress/SSE 与 T8 Windows App/命名管道，验证模型故障不触发 TCP 重连。
4. 完成 T9 部署与 T10/T11 压测；未实测前保持“未验证/阻断”标注。
5. 最后做 T12 文档收尾与 Web 退场；App 功能对等差异为 0 前不得停 `:4173`。