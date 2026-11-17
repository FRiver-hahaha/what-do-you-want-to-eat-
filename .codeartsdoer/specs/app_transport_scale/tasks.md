# Windows App · 自研 TCP / C++(Muduo) 单机服务端 · 编码任务规划（tasks.md）

> **依据**：`.codeartsdoer/specs/app_transport_scale/spec.md`（R6，3157 行）与 `design.md`（1834 行），全文已读取；`.codeartsdoer/specs/p8_backend_check/spec.md`（864 行）全文已读取。
> **代码基线**：`origin/main`（`fc34b0f`）；PR #2 `origin/feat/backend-p8`（`ab248f3`，base `b5887a2`）仅作**只读参照**；`D:\gitclone\p8-verify` **只读**。
> **本阶段边界**：只产出任务清单。**不修改任何源码、不创建提交、不合并 PR、不压测、不改 `p8-verify`**。
> **口径纪律**：capacity / 性能 / 可靠性结论只能落在【已验证事实】【理论推导】【未验证】【阻断】四类；**未实测不得勾选完成**。本文件不承诺工期。
> **判定规则**：任务勾选 = 验收判据全部满足且证据已归档；仅有代码不算完成；压测类任务必须附原始日志。

## 0. 任务组织与图例

### 0.1 处置图例（按任务主处置唯一标注）

| 标注 | 含义 | 本轮典型对象 |
|---|---|---|
| **【复用】** | 语义与实现可沿用，只做调用适配 | P8 `service/` + `dao/`、`ServiceResult`、`HealthScore`、`foods` 种子、`001_p8_business.sql`、Agent 能力本体、`gateway.py` 出网骨架、P7 `Redis` 连接池骨架、P7 `Connection` fd 生命周期骨架（约 10–15%） |
| **【改造】** | 保留主体、改接口/形态/参数 | `MySQLPool`（`borrow_for(200ms)`）、`Redis`（扩命令层）、`MealService`（加 patch/delete）、`FoodService`（去 `defaultUserId`）、`gateway.py`（三层超时/重试/取消/脱敏）、Agent 入口（改命名管道） |
| **【重写】** | 语义或形态不兼容，推倒重做 | P8 `main_http.cc`/`Router`/`Auth`/6 个 Controller/`DbConn`；P7 `\n` 分帧；P7 自研 Reactor 与 `ChatService` 路由 |
| **【新建】** | 存量为零 | `server/tcp/*`、`identity/*`、`cache/*`、`app/*`、`shared/protocol`、`client/*`、`deploy/*`、压测与数据集、迁移 `002`–`006` |

### 0.2 验证方式图例

- **U**＝单元测试（gtest / pytest / Catch2）；**I**＝集成测试（进程内/本机多进程）；**R**＝真实请求（真客户端或真协议帧）；**P**＝压测；**D**＝演练（故障注入/重启/回滚）；**S**＝静态检索/构建期扫描/评审。

### 0.3 任务统计

- 任务组（主任务）：**13** 组（T0–T12）。
- 子任务：**102** 个（T0 8 + T1 9 + T2 10 + T3 7 + T4 6 + T5 8 + T6 10 + T7 8 + T8 10 + T9 8 + T10 6 + T11 5 + T12 7）。
- 需求覆盖：R6 第 4 章 REQ-PERF-01…13 / REL-01…10 / SEC-01…10 / MNT-01…06 / COMP-01…06 / STATE-01…02 全部映射（见 §14 覆盖矩阵）。

### 0.4 阶段与依赖总览

| 阶段 | 任务组 | 产出 | 阶段门 |
|---|---|---|---|
| A 骨架与协议 | T0、T1、T2 | Linux 可构建的 Muduo 服务端 + 完整帧协议 + 连接治理 | G1、G2 |
| B 身份·数据·缓存 | T3、T4、T5 | 统一身份 + 可迁移数据模型 + Cache-Aside（写后 DEL/负缓存/降级） | G3、G4、G5 |
| C 业务·客户端 | T6、T7、T8 | P8 业务迁移到帧 + 客户端 Key/LLM + Windows App/Agent | G6、G7、G8 |
| D 运维·容量 | T9、T10、T11 | 可部署可观测 + 压测体系 + 100 万在线实测 | G9、G10 |
| E 交付 | T12 | Web 退场、契约/交付文档、证据与回滚 | G11 |

**不可跨越约束（来自 design 2.6.3）**：① T1.1 协议冻结前 T8.2 不得开工；② T3 未完成前真实用户数据不得进入新链路；③ T5 不得早于 T3（缓存键含 `user_key`）；④ 任何阶段不得用 Redis pub/sub 替代推送/失效；⑤ T11 的容量结论不得倒推为日常承诺；⑥ T12.3 下线 Web 前必须有 App 灰度覆盖证据。

---

## 1. T0 基础设施与工程骨架

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T0.1 | 【新建】冻结基线并建立实现分支：从 `origin/main fc34b0f` 拉 `feat/app-transport-scale`，登记 PR #2 `ab248f3` 为只读参照，记录 spec/design 哈希 | `.git`、`.codeartsdoer/specs/app_transport_scale/` | 无 | 分支基于 `fc34b0f`；PR 未合并；三份文档哈希入库；`p8-verify` 工作区干净 | S：`git merge-base` / `git log` / `git status` | 删除分支（main 不动） | 是（全部任务） |
| - [ ] | T0.2 | 【新建】引入 vendored Muduo v2.0.x（含 `epoll_batch` 可配的最小 patch）与 OpenSSL 3.0，建立 Linux 构建 | `third_party/muduo/`、`third_party/muduo/patches/`、`CMakeLists.txt`、`cmake/` | T0.1 | Linux 构建成功；Muduo 版本与 patch changelog 入库；`ldd` 出现 libssl/libcrypto；线程名可区分主/子 Reactor | S/U：`cmake -S -B && cmake --build`；`ldd`；启动日志 | 移除 `third_party/muduo` 与新 target | 是（T1、T2、T8.2） |
| - [ ] | T0.3 | 【新建】建立 `shared/protocol`：40B 帧头常量、魔数 `EAT1`、帧类型 0x01–0x09、错误码表、method 常量、能力位、协议版本 | `shared/protocol/protocol.h`、`shared/protocol/errors.h`、`shared/protocol/methods.h` | T0.1 | 偏移/宽度/端序与 design 2.2.2.1 逐项一致；`request_id` 在头内；**无编码类型位**；三端（server/client/loadtest）同源编译 | U/S：`static_assert(sizeof(Header)==40)`、`offsetof` 断言；`git grep` 魔数仅一处 | 删除目录与引用 | 是（T1、T8.2、T10.1） |
| - [ ] | T0.4 | 【新建】建立目标目录与构建目标：`server/tcp`、`identity`、`cache`、`app`、`client/*`、`tests/{protocol,load,cache}`、`deploy/`、`migrations/add` | 根 `CMakeLists.txt`、上述目录 | T0.2 | 空目标可构建；新目标**不链接** `httplib`、P7 `reactor/net`、`transit` | S：构建 + `nm`/链接清单检查 | 删除目录与 target | 否（可与 T0.5 并行） |
| - [ ] | T0.5 | 【复用】将 P8 `service/`（6 个）+ `dao/`（7 个）+ `MySQLPool` + `001_p8_business.sql` + `foods_seed.sql` 纳入新树，**语义零改动**；记录保留边界 | `server/http/service/*`、`server/http/dao/*`、`server/database/MySQLPool.*`、`migrations/keep/` | T0.4 | `grep -l httplib service/ dao/` 零命中；文件清单与 design 1.2.1 一致；P8 既有契约回归基线已归档 | S/U：编译 + P8 回归脚本 + `grep` | 还原目录到 main 状态 | 是（T6） |
| - [ ] | T0.6 | 【新建】建立仓库门禁：禁 `.proto`/protobuf/MessagePack/CBOR、禁 WebSocket 客户端/服务端实现、禁 `PUBLISH/SUBSCRIBE`、禁真实 Key/明文密钥/编码开关 | `.githooks/pre-commit`、`tools/scan_repo.*`、CI 配置 | T0.3 | 故意注入 5 类违规样例均被拦截；仓库零 `.proto`、零编码开关；扫描报告入库 | S：运行扫描 + `git grep` + 注入样例 | 禁用 hook（脚本保留） | 否（G5/G7 依赖） |
| - [ ] | T0.7 | 【新建】配置/日志/指标骨架：`server.toml` 分节解析、JSON 行日志、`request_id` 贯通、脱敏字段、原子计数 Metrics | `server/app/Config.*`、`server/app/Log.*`、`server/app/Metrics.*`、`deploy/server.toml.example` | T0.2 | 启动校验四条生效：`max_connections ≤ nofile/1.2`、`listen_backlog ≥ somaxconn`、`idle ≥ 2.5×heartbeat`、`heartbeat==60 或已登记偏离`；日志无 token/Key/body 原文 | U/S：配置单测 + 样例日志扫描 | 删除模块 | 是（T2、T9） |
| - [ ] | T0.8 | 【新建】P7 只读化：CI 检测 `server/reactor`、`net`、`service`、`model` 与 P7 `init.sql` 的 `user/friend` diff；回归脚本常跑 | CI、`tests/p7/` | T0.1 | P7 目录 diff 为空；13 条路由回归全绿；P7 进程未参与新链路 | S/D：`git diff` + 回归 | 无（规则本身） | 否（但 G11 依赖） |

---

## 2. T1 TCP 协议（定长头 + 长度字段 + JSON body）

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T1.1 | 【新建】冻结协议文档 v1：40B 头字段、大端序、CRC32C 范围、9 类帧、错误码 0/1001–1009/2001–2008/3001–3005/4001–4005/5001–5004/9001–9004、关闭原因、能力位 | `docs/protocol/v1.md`、`shared/protocol/` | T0.3 | 字段/偏移/非法处理逐项可查；`request_id` 必须在头内；**无 `llm_proxy`/`llm_stream_*`**；无编码类型位；能力位含 `push/pull_sync/meal_edit/friend_write/idem_v1/tls_resume` | S：评审 checklist 逐项签字 | 文档版本回退 | 是（T1.2、T8.2） |
| - [ ] | T1.2 | 【新建】`FrameCodec`：先读满头→校验魔数/版本/类型/标志/头长/`body_len`→**超限在读 body 前拒绝**→读满 body→CRC，读缓冲按已消费偏移滑动 | `server/tcp/FrameCodec.{h,cc}`、`tests/protocol/frame_codec_test.cc` | T1.1 | 2 帧粘包逐帧分发；1 字节切片半包正确；`body_len>max` 时 RSS 不随声明长度增长；错位流断连不跳字节；缓冲不 O(n²) | U/P：单测 + fuzz + RSS 采样 | 删除模块（T2 回退到 echo） | 是（T2.3、T8.2） |
| - [ ] | T1.3 | 【新建】CRC32C 校验：`header[0..35] ‖ body`，不含自身字段；失败返回 1007 | `server/tcp/Crc32c.*`、`FrameCodec` | T1.2 | 单比特翻转必失败；正常帧零误判；失败计数可观测 | U：位翻转用例 | 删除模块 | 是（G1） |
| - [ ] | T1.4 | 【新建】`StrictJson`：UTF-8/无 BOM、重复键拒绝、深度 ≤16、仅有限数、尾随拒绝、`>2^53-1` 转字符串、解析失败原子回滚、六类 `reason` | `server/tcp/StrictJson.{h,cc}`、`tests/protocol/json_test.cc` | T0.3 | 六类 reason 各有用例；body 不可解析时 `request_id` 仍正确回带；失败不产生任何 DB/Redis 写入；日志零 body 原文 | U/P：用例 + fuzz | 删除模块 | 是（T6） |
| - [ ] | T1.5 | 【新建】`FrameDispatcher`：帧类型→处理器表；`method` 白名单；未握手发业务帧→2001 并断连；未协商能力位拒绝 | `server/tcp/FrameDispatcher.{h,cc}`、`shared/protocol/methods.h` | T1.2、T1.4 | 未握手业务帧断连且错误码可检索；未知 method → 3001/1003；握手期不读写 Redis；业务帧 `user_id` 忽略并计 `identity_spoof_attempt` | I：协议集成用例 | 删除模块 | 是（T6） |
| - [ ] | T1.6 | 【新建】协议异常用例集与回放工具（BLK-14）：半包/粘包/1000 帧一次/超长/零长度/非法魔数/位翻转/半关闭/RST；JSON 六类 | `tests/protocol/*`、`tools/frame_replay.*` | T1.2–T1.5 | S-10、S-11 可自动断言；回放可还原 request_id/帧类型/耗时/错误码；输出不含敏感载荷 | U/I：全部用例 | 删除用例 | 否（G1 依赖） |
| - [ ] | T1.7 | 【新建】四项上限配置与握手下发：`max_frame_size=65536`（硬顶 262144）、每连接读缓冲上限、每连接发送高水位、全局发送高水位；握手下发 `max_frame_size` | `server/tcp/ConnContext.*`、`server/app/Config.*` | T1.2、T0.7 | 四项不改代码可调；客户端按下发值拒绝超限；超限帧按 1006 处理 | U/I：配置矩阵 + 抓包 | 恢复默认配置 | 是（T2.7） |
| - [ ] | T1.8 | 【新建】JSON 唯一与体积红线门禁：禁 base64 内嵌图片/附件；超大对象走分页/游标；`body_len` 体积直方图与超阈值告警 | `tools/scan_repo.*`、`server/app/Metrics.*`、用例 | T1.4、T0.6 | 注入 base64 图片被拒且错误可读；整月统计/全量好友用例分页成功；仓库零 `.proto`、零编码开关 | S/I：扫描 + 用例 | 关闭埋点（门禁不撤） | 否（G5 依赖） |
| - [ ] | T1.9 | 【新建】`bench-tcp` 最小版：复用 `shared/protocol`，支持建连/握手/心跳/echo/延迟统计 | `tests/load/bench_tcp/` | T0.3、T1.2 | 单机 1,000 连接冒烟成功；输出 P50/P99 与错误率 | P：冒烟 | 删除目录 | 否（T10.1 依赖） |

---

## 3. T2 Muduo 接入与连接治理（含高风险项）

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T2.1 | 【新建】`TcpServerApp`：`Acceptor` + `EventLoopThreadPool` 主从 Reactor；主 loop 只 accept，子 loop 读写；轮询分配；`sub_reactors=min(16,max(4,核/2))` 可配；**P7 复用边界**：仅参考搬移 `TcpServer` 的 `SO_REUSEADDR/TCP_NODELAY/非阻塞`（约 20–30 行）与 `Connection` fd 生命周期（约 25–40 行），**不复用** P7 `Reactor` 的 `epoll_wait/MAX_EVENTS=1024/扫描式心跳** | `server/tcp/main.cc`、`server/tcp/TcpServerApp.{h,cc}` | T0.2、T1.2 | 线程名可证主/子 Reactor 归属；连接在各 loop 可观测且分配均匀；子 Reactor 数可配；`epoll_batch=2048` 生效（patch） | I/S：启动 + `/proc` + 线程日志 | 停 `eat-server`，回到无新链路状态 | 是（全部服务端） |
| - [ ] | T2.2 | 【新建】`ConnContext` 连接状态机与统一关闭：`Connected→Handshaking→Authenticated→Replaced/Draining→Closed`；`close(reason)` 幂等、先摘 `SessionRegistry` 再释放缓冲 | `server/tcp/ConnContext.{h,cc}` | T2.1 | 状态迁移用例全过；关闭后 fd 与连接计数一致回收；不误删新连接映射；无"半包永久悬挂"连接 | U/I：状态单测 + fd 采样 | 删除模块 | 是（T3.5、T6） |
| - [ ] | T2.3 | 【改造】帧循环接入 IO 线程：头/长度/CRC 在 IO；JSON 严格解析在业务池；每次 read 最多处理 `MAX_FRAMES_PER_READ=64` 帧后让出；半包 5s 超时清理 | `server/tcp/TcpServerApp.cc`、`FrameCodec` | T1.2、T2.1 | 半包不阻塞其它连接；1000 帧粘包不饿死同 loop；未读满不解析；超时返回 1009 并断连 | P：S-10 | 降级为逐帧阻塞版（仅调试） | 是（T2.5） |
| - [ ] | T2.4 | 【新建】`HeartbeatMonitor`：60s 心跳就地回执、`last_recv_mono` 单调时钟、165s 空闲（可配 150–180）、每子 loop `runEvery(1s)` 最小堆只查堆顶 | `server/tcp/HeartbeatMonitor.{h,cc}` | T2.1、T2.2 | **纯心跳期间 MySQL `Questions` 与 Redis `commandstats` 零增长**；心跳不进业务队列；100 万连接心跳 QPS 可观测；心跳 P99 可测 | P：S-02 子集 | 删除模块（退化为无心跳，不可上线） | 是（G2） |
| - [ ] | T2.5 | 【新建】`BusinessExecutor` 有界工作池：16 线程（8–64）、队列 8192、单任务 3s、全局在途 `threads×64`；满即 4002 | `server/app/BusinessExecutor.{h,cc}` | T2.1、T0.7 | 队列满快速拒绝不排队到超时；30 分钟高压 RSS 不单调上升；IO 线程零 SQL/Redis | U/P：过载用例 + RSS 采样 | 删除模块 | 是（T6） |
| - [ ] | T2.6 | 【新建】`RateLimiter` 三级令牌桶 + 握手限速：每连接 60/s（突发 120）、每用户 120/s（240）、全局 5000/s、握手 5/s/IP、全局 3000/s、登录 5/min 锁 5min | `server/tcp/RateLimiter.{h,cc}` | T2.1 | 单用户超频 → 4001+`retry_after_ms`；握手失败 3 次 → 2007 冷却 60s；全局过载新连接快速拒绝 | U/I：限流用例 | 删除模块 | 否 |
| - [ ] | T2.7 | 【新建】`Backpressure` 三级水位：每连接发送 256KB/回落 64KB、读缓冲 `max_frame_size+40`、全局 512MB、慢客户端 30s 关闭 | `server/tcp/Backpressure.{h,cc}` | T1.7、T2.1 | 慢读客户端只影响自身；发送队列不无界；全局水位触顶拒绝新任务；超水位 `disableRead` | P：慢客户端用例 | 删除模块 | 是（T11.2） |
| - [ ] | T2.8 | 【新建】优雅关闭：SIGTERM 停 accept → 全连接 `close(9001,retry_after)` → 等在途 ≤5s → drain ≤5s → `shutdown` → 10s 退出 | `server/tcp/main.cc`、`TcpServerApp` | T2.2 | 10s 内退出；客户端收到关闭帧；无数据半写；`TimeoutStopSec=20s` 有余量 | D：SIGTERM 演练 | 恢复默认信号处理 | 否（G11 依赖） |
| - [ ] | T2.9 | 【新建·高风险】fd/内核参数与 `max_connections` 联动：`nofile≥1.5M`、`fs.nr_open≥2M`、`file-max≥4.19M`、`somaxconn=65535`、`tcp_max_syn_backlog=262144`、`netdev_max_backlog=262144`、`tcp_rmem/wmem`、`tcp_mem`、`syncookies=1`；启动断言 | `deploy/sysctl/eat-server.conf`、`deploy/checklist.md`、启动校验 | T0.7 | 目标值可设置成功；启动断言失败即退出；`ListenOverflows=0`；达上限快速 close 并计数；**不达标登记 BLK-1，阻断 1M 宣称** | D/P：`sysctl`/`prlimit`/启动用例/阶梯压测 | 以变更前 sysctl 快照还原 | 是（T11.2；失败即阻断） |
| - [ ] | T2.10 | 【新建·高风险】Muduo 阻塞边界门禁：`server/tcp/` 符号黑名单（`mysql_*`、`mysql_query`、`CacheClient`、文件 IO、`sleep`、同步 HTTP）；CI + 评审规则 | CI、`tools/check_io_thread.*`、评审清单 | T2.1、T2.5 | 故意注入违规样例被 CI 拦截；评审确认 IO 线程无同步 DB/Redis；心跳 P99 不随业务压力线性恶化 | S/P：CI + 长尾观测 | 放宽规则须走评审（不得静默） | 是（不可跨越；R-04 阻断条件） |
---

## 4. T3 统一身份与会话（新建，不复用 P7/P8）

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T3.1 | 【新建】迁移 `002_identity.sql` + `003_session.sql` 及成对 rollback：`account`（`user_key CHAR(26)` ULID、`login_id` 唯一、`password_digest` 含 `scrypt$N$r$p$salt$digest`、`status`、`revoke_epoch`、`failed_attempts`、`locked_until`）、`account_alias`、`session`（`session_hash`/`refresh_hash` 唯一、`device_id`、`status`、`replaced_by`） | `migrations/add/002_identity.sql`、`003_session.sql`、`*_rollback.sql` | T0.5、T4.3 | 重复执行不报错；回滚只删本次新增对象；表注释前缀可识别；schema guard 覆盖新表 | D/I：迁移演练 + 幂等重跑 | 执行 `_rollback.sql` | 是（T3.2–T3.7、T6） |
| - [ ] | T3.2 | 【新建】`PasswordHasher`：OpenSSL `EVP_PBE_scrypt`（N=2^15,r=8,p=1，16B salt，64B 输出，参数入编码串）；`needsRehash` 支持登录惰性重哈希 | `server/identity/PasswordHasher.{h,cc}` | T3.1 | 同密码验证通过；参数变更后旧摘要仍可验证并重哈希；登录 P99 在目标 CPU 有实测值；禁用无盐 SHA256 | U/P：单测 + 微基准 | 删除模块 | 是（T3.3） |
| - [ ] | T3.3 | 【新建】`IdentityService`：注册/登录/刷新/登出/全量撤销/改密/禁用；登录标识 NFKC→去空白→全半角→小写→折叠空白；失败 5 次锁 5min；access 30min、refresh 30d 一次性轮换、重用撤销整链 | `server/identity/IdentityService.{h,cc}` | T3.1、T3.2 | `A@x.com`/`a@x.com`/含空格/全角判为同一账号；吊销后下一次请求立即失败；refresh 重用 → 2008 且整链撤销；禁用用户在线连接立即关闭 | U/I：身份矩阵用例 | 关闭新身份入口（不恢复 P8 静态 Token 对外） | 是（T3.4、T3.5） |
| - [ ] | T3.4 | 【新建】`TokenVerifier`：32B CSPRNG 不透明令牌→base64url；库中只存 SHA-256；内存 LRU 60s 优先，未命中点查 `session` | `server/identity/TokenVerifier.{h,cc}` | T3.1、T3.3 | 握手/业务帧不因验签查库（除首登/续期）；吊销即时生效；日志只记 `session_id` 摘要前 8 位，零令牌明文 | U/I：用例 + `Com_select` 计数 | 删除模块 | 是（T3.7） |
| - [ ] | T3.5 | 【新建】`SessionRegistry`：`user_key→{conn_id,device_id,since}` 纯内存分片哈希（64 桶）；同 `device_id` 新连接顶替旧连接并 close(replaced)；同账号最多 3 台活跃设备；每用户事件序号分配 | `server/identity/SessionRegistry.{h,cc}` | T2.2、T3.4 | 同 device 旧连接收 `close(9002)` 且原因可查；1 小时内无同用户多活（白名单除外）；Redis 清空不影响在线判定；在线状态不写 MySQL | I/D：多连接/清 Redis 用例 | 删除模块 | 是（T6.9、T8.2） |
| - [ ] | T3.6 | 【新建·高风险 TLS】Muduo SSL 模式：OpenSSL 3.0、TLS 1.2 最低优先 1.3、服务端证书、客户端 SPKI 指纹固定、证书校验强制开启、**构建期断言无 skip-verify**、私钥 0400、SIGHUP 重载 | `server/tcp/TlsConfig.*`、`deploy/tls/`、`client/keyvault/` 证书校验 | T2.1、T3.1 | 抓包无明文业务字段；篡改/替换证书客户端拒绝并给可读原因；配置无"跳过校验"；证书轮换新连接生效且旧连接不中断；握手成本有实测（未测则标【未验证】） | D/S：抓包 + 证书异常 + 轮换演练 | 回滚到上一证书；关闭 TLS 仅限隔离测试构建（不得发布） | 是（G3；REQ-SEC-01） |
| - [ ] | T3.7 | 【新建】握手帧序与身份不可自证：首帧必须 `handshake_req`（10s 超时）、握手期禁 Redis、握手响应下发 `heartbeat=60/idle=165/max_frame_size/版本/能力`、业务帧身份只取 `ConnContext.user_key` | `server/tcp/FrameDispatcher.cc`、`IdentityService` | T3.4、T1.5 | 未握手业务帧 → 2001 断连；篡改 body 内 `user_id` 不影响判定并计 `identity_spoof_attempt`；版本过旧 → 1002+"需升级"；越权访问他人资源全失败 | I：协议 + 越权用例 | 删除握手分支（不可上线） | 是（G3、T8.2） |

---

## 5. T4 数据模型、迁移与索引

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T4.1 | 【新建】`004_friend_relation.sql`：双行边 `owner_key/peer_key/status(0,2,3)/blocked_by`；UK `(owner_key,peer_key)`；`idx_owner_status`、`idx_peer_status` | `migrations/add/004_friend_relation.sql` + rollback | T3.1 | 双向好友查询与反查均走索引；拉黑可过滤；P7 `friend` 表零改动 | I：EXPLAIN + 迁移幂等 | `_rollback.sql` | 是（T6.9） |
| - [ ] | T4.2 | 【新建】`005_meal_partition.sql`：`meal_records` 增 `user_key/version/updated_at/deleted_at`、PK `(id,date)`、`idx_uid_date_id`、按月 `RANGE(TO_DAYS(date))`；`idempotency_records` 增 `user_key`、按月 `RANGE(TO_DAYS(created_at))`、UK `(user_key,idem_key)` | `migrations/add/005_meal_partition.sql` + rollback | T3.1、T0.5 | 分区裁剪生效；7 类查询中餐记录范围扫无 filesort；迁移幂等；回滚只删新增分区/列 | I：EXPLAIN + `information_schema.partitions` | `_rollback.sql` | 是（T6.5、T11.5） |
| - [ ] | T4.3 | 【新建】`006_event_outbox.sql`（PK `(user_key,seq)`、`event_type/ref_kind/ref_id`、`idx_created`）+ `schema_migration` 表；启动校验缺失即 fail-closed；同名来源不明表中止 | `migrations/add/006_event_outbox.sql`、`server/app/SchemaGuard.*` | T3.1 | 未迁移库拒绝启动且零写入；迁移记录与关键表/列校验生效；不 DROP 重建 | D：schema guard 用例 | `_rollback.sql` | 是（T6.9） |
| - [ ] | T4.4 | 【改造】P7→新表一次性迁移（`friend` 单向边扩为双行、`user` 账号可选映射）：独立脚本、可重跑、双向校验、P7 只读 | `migrations/oneoff/p7_to_v1.*` | T4.1、T4.3 | 行数与抽样哈希校验一致；重跑幂等；P7 目录零 diff；映射关系显式登记（无隐式混用） | D：迁移 + 校验 SQL | 清空新表数据（不动 P7） | 否（灰度期可选） |
| - [ ] | T4.5 | 【改造】DAO 适配：`user_key` 字符串键、`version/updated_at` 条件更新、软删条件 `deleted_at IS NULL`、游标分页（`(date,id)`/`(seq)`）；DAO 内**禁止缓存** | `server/http/dao/*` | T4.1–T4.3 | 7 类查询 `EXPLAIN` 均非 `ALL`；条件更新冲突返回 3005；深 offset 不存在 | I：EXPLAIN + 单测 | 恢复旧 DAO 签名（T6 未上线时） | 是（T6） |
| - [ ] | T4.6 | 【新建】百万级数据集生成器：≥100 万用户、每人 ≥100 条餐记录、幂等账本与会话同量级；分布说明与行数校验 | `tests/load/gen_dataset/` | T4.2 | 可生成 ≥1 亿行；生成耗时/磁盘预算记录；行数与分布校验 SQL 通过；**空库结论不得用于容量验收** | D：生成 + 校验 | 删除测试库数据集 | 是（T11.5、T12.6） |

---

## 6. T5 Cache-Aside（写后删除 + 负缓存 + 降级）

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T5.1 | 【改造】Redis 命令层与 `CacheClient`：`GET/SETEX/DEL/MGET/EXISTS/PING`；连接 50ms、命令 20ms；错误分类；熔断连续 3 次失败或 1s 失败率 >50% → open 5s；**禁 PUBLISH/SUBSCRIBE/SCAN/KEYS/Lua**；单实例 | `server/database/Redis.*`、`server/cache/CacheClient.{h,cc}` | T0.5、T0.7 | `MONITOR` 采样 10 分钟零订阅发布；部署仅 1 个 `redis-server`；禁用命令静态检索零命中；熔断与半开可观测 | S/I：`MONITOR` + 单测 + 故障注入 | 关闭缓存开关（直查 MySQL） | 是（T5.2–T5.8、T6） |
| - [ ] | T5.2 | 【新建】`CacheKeyBuilder`：`eat:{env}:{gen}:{domain}:{params}`；参数排序/去空白/小写/日期 `YYYY-MM-DD`/分页夹取/集合排序/空值 `-`；自由文本 `sha256[:16]`；键 ≤200B；**正负同键** | `server/cache/CacheKeyBuilder.{h,cc}` | T3.1、T5.1 | 5 组语义等价变体生成同一 key；语义不同生成不同 key；键不含未转义自由文本；抽样 key 可人工还原语义 | U：表驱动单测 | 删除模块 | 是（T5.3–T5.7） |
| - [ ] | T5.3 | 【新建】缓存值结构 v1：`{"v":1,"gen":N,"empty":bool,"ts":ms,"data":...}`；`empty=true` 时 `data=null`；版本不符/坏 JSON → 按未命中并 DEL 坏值 | `server/cache/CacheValue.h` | T5.1 | 抽样值可被标准 JSON 解析器独立解析；坏字节不导致 5xx；缓存值与帧 body 字段名/语义一致 | U：单测 + 抽样 | 删除结构（等同无缓存） | 是（T5.5–T5.6） |
| - [ ] | T5.4 | 【新建】`TtlPolicy` 与抖动：prof 60s、meal 30s、gap 30s、week 60s、menu 300s、search 120s、friend 60s；负缓存 15s/30s/60s；正缓存 ±10%、负缓存 ±20%；**ranking 不缓存** | `server/cache/TtlPolicy.h`、配置 | T5.2 | 每域 TTL 可配且不改代码；负缓存 TTL 严格小于正缓存；过期时间分散无尖峰；无 TTL 业务缓存为零 | U/P：配置单测 + 过期分布观测 | 恢复默认 TTL | 是（T5.5） |
| - [ ] | T5.5 | 【新建】读路径 Cache-Aside + `SingleFlight`：GET→命中返回（`source=cache`）→未命中同键单飞回源→MySQL `borrow_for(200ms)`→SETEX→返回（`source=db`）；follower 等待 200ms 超时直查；禁分布式锁 | `server/cache/SingleFlight.{h,cc}`、`server/app/handlers/` | T5.1–T5.4、T2.5 | 命中路径 `Com_select` 零增长；同键 TTL 内回源 1 次；单飞等待/超时计数；回写失败不影响本次返回 | P：S-06 | 关闭缓存开关 | 是（T6.4–T6.9、G5） |
| - [ ] | T5.6 | 【新建】负缓存与正负同键联动：空结果写 `empty=true` 短 TTL；错误/超时/越权**不写**；新增数据由写路径 DEL 同键 | `server/app/handlers/*`、`InvalidationSet` | T5.5 | **S-08：负缓存命中 → 写入 → 立即读 = 新值**；负缓存值不含存在性敏感字段；TTL 到期后数据出现可读到 | P：S-08 + 越权用例 | 关闭负缓存（穿透风险，不得上线） | 是（G5、REQ-COMP-05） |
| - [ ] | T5.7 | 【新建】`InvalidationSet` 与写后 DEL：事务提交成功后 pipeline 多键 DEL；失败重试 3 次（10/50/200ms）+ 计数告警 + WAL；**禁 SET 新值、禁先删后写、禁 KEYS/SCAN**；键归属脚本保证零孤儿键 | `server/cache/InvalidationSet.{h,cc}`、`PendingInvalidationWal.*`、`tools/check_key_owner.*` | T5.2、T5.5 | S-07：写后立即读 1,000 次全部新值；失效清单逐键可枚举；孤儿键 0；静态检索写路径零 SET；窗口实测 ≤ 声明 | S/P：检索 + S-07 + 归属脚本 | 关闭缓存（直查 MySQL） | 是（G5、REQ-REL-10） |
| - [ ] | T5.8 | 【新建】Redis 降级/恢复门/整代作废：熔断直查 MySQL、跳过缓存读写与负缓存写入；降级期写操作追加 `pending_invalidate.log`（fsync everysec）；恢复门批量 DEL（1,000/批）或 `INCR cache_gen`；恢复门期间禁缓存读 | `server/cache/CacheClient.*`、`PendingInvalidationWal.*`、`server/cache/CacheGen.*` | T5.7 | S-14：停 Redis 5 分钟只读成功率 100%；连接下降 ≤1%、心跳正常；**"故障中写入→恢复→立即读"= 新值**；恢复门时长可测；全程无 pub/sub | D/P：停/恢复 Redis 演练 | 重启服务回到正常缓存路径 | 是（G4、REQ-REL-07） |

---

## 7. T6 业务模块迁移（复用 P8 service/dao，重写 HTTP 语义层）

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T6.1 | 【改造】`ServiceResult` 扩展 `RateLimited/Overloaded/NotFound`（不改既有 5 个语义），建立业务异常→帧错误码映射：Conflict(幂等)=3003、Conflict(版本)=3005、Unavailable=5001、RateLimited=4001、Overloaded=4002、NotFound=3004；帧头与 body 双写 | `server/http/service/ServiceResult.h`、`server/app/ErrorMap.*` | T0.5、T1.1 | 8 类映射表与实现一一对应；越权与不存在均返回 3002/3004 不泄露存在性；响应必带 `request_id` | U：单测表驱动 | 删除新增枚举（T6 未上线） | 是（T6.3–T6.10） |
| - [ ] | T6.2 | 【改造·高风险 单机 MySQL】`MySQLPool` 默认 `borrow_for(200ms)`、`PoolStats`（借出/等待/超时）、池 ≤64（默认 32）、`statement_timeout=2500ms`；连接数**不随客户端连接数增长** | `server/database/MySQLPool.{h,cc}`、`server/app/ServiceRegistry.*` | T0.5 | 池超时返回 5001；`Threads_connected` 峰值 ≤ 预算；停库连接与心跳不受影响；无无限等待 `borrow()` 调用 | D/P：池耗尽 + 停库 + 连接计数采样 | 恢复旧池参数（不回退无限等待） | 是（T6.3–T6.10、T11.5） |
| - [ ] | T6.3 | 【新建】薄 Handler 框架 + `ServiceRegistry`：JSON→Service→JSON；身份只取 `ConnContext.user_key`；Handler 不依赖 `httplib`、不直接依赖帧类型；**P8 HTTP 语义层重写范围**：`main_http.cc`、`Router.*`、`Auth.*`、6 个 `*Controller.*`、`DbConn.h` 由 `tcp/` + `app/handlers/` 替代，逐行移植字段/校验/截断/默认值，不移植 HTTP 形态；保留 `DateUtil/Utf8Util/JsonUtil` 与 JSON 信封语义 | `server/app/handlers/*`、`server/app/ServiceRegistry.{h,cc}` | T1.5、T2.5、T3.5、T5.1、T6.1 | `grep httplib` 在新 Handler 零命中；Handler 单测可注入假 Service；请求 ID 贯穿帧→业务→缓存→DB | S/U：编译 + 单测 | 删除 Handler（协议层保留） | 是（T6.4–T6.10） |
| - [ ] | T6.4 | 【复用+改造】`profile.get` / `profile.patch`：复用 `ProfileService`；缓存键 `prof:{user_key}`；乐观并发 `version`；写后 DEL + `prof_ver` 派生失效（gap/week/search） | `server/app/handlers/ProfileHandler.*`、`ProfileService` | T6.3、T5.5–T5.7 | 四态（正/负/未命中/降级）逐字段一致；BMI 服务端现算；写后立即读新值；未提交字段不变 | I/P：S-06/S-07 + 对照用例 | 关闭缓存开关 | 否 |
| - [ ] | T6.5 | 【复用】`meal.create` / `meal.recent`：复用 `MealService::recordMeal/recentMeals`；幂等键进 `params`；餐记录+幂等账本同事务；`recent` 分页游标 ≤50、≤31 天；写后 DEL meal/gap/week 多键 | `MealHandler.*`、`MealService`、`InvalidationSet` | T4.2、T6.3、T5.7 | 幂等三场景（重发/双连接/重启）DB 增量 1；同键异内容 3003；不同键同内容 2 条；整月/整年不分页单帧被拒 | I/P：P8 幂等回归 + S-07 | 关闭 `meal.create` 能力位 | 是（T6.6、T6.7） |
| - [ ] | T6.6 | 【改造】`meal.patch` / `meal.delete`：`MealService` 新增方法，复用 `MealDao`；软删 `deleted_at`、`version` 乐观并发；统计条件统一 `deleted_at IS NULL`；写后 DEL 扩展 | `MealService.{h,cc}`、`MealDao.*`、`MealHandler.*` | T6.5、T4.2 | 删改落库并可回读；周统计/缺口随删改正确变化；版本冲突 3005；BLK-10 闭环 | I：删改对照用例 + 统计复算 | 关闭 `meal_edit` 能力位，保留只读 | 是（G6） |
| - [ ] | T6.7 | 【复用】`stats.nutrition_gap` / `stats.weekly`：复用 `StatsService`（缺口恒等式、健康分算法、服务端时区）；缓存 + 写后失效 | `StatsHandler.*`、`StatsService` | T6.5、T6.6、T5.7 | 缺口 `max(0,target-intake)`；周统计与独立复算逐值相等；跨日期不计入今日；缓存四态语义一致 | I：P8 统计回归 + 复算 | 关闭缓存开关 | 否 |
| - [ ] | T6.8 | 【复用+改造】`food.menu` / `food.search`：复用 `FoodService`；**下线 `defaultUserId`**，过敏/忌口只来自会话 `user_key`；`q` 哈希进 key；分页 ≤20；菜单长 TTL | `FoodHandler.*`、`FoodService`、`FoodDao` | T6.3、T5.2 | 无 uid 调用路径为零；16 道种子/展示 14 道/70–30 规则不破；过敏"花生"过滤生效；命中数排序正确 | I：P8 菜品用例 + `grep defaultUserId` | 恢复旧构造参数（不得保留白名单） | 否 |
| - [ ] | T6.9 | 【复用+新建】`friend.list/request/accept/remove/block` + 推送与拉取：复用 `FriendService`；新增写方法 + `friend_relation`；`event_outbox` 事件序号；`push` 帧 ≤1KB；`sync.pull(since_seq,limit≤100)`；超窗 `resync_required` | `FriendHandler.*`、`FriendService`、`EventOutboxDao.*`、`PushService.*` | T4.1、T4.3、T6.3、T3.5 | 好友双向/拉黑过滤正确；事件 ≤2s 到达；断线后按序号补齐；超窗返回全量同步；离线队列/outbox 不依赖 pub/sub | I/P：好友用例 + 推送延迟 + 断线补拉 | 关闭 `friend_write` 能力位 | 否（G6） |
| - [ ] | T6.10 | 【复用】`ranking.friends`：复用 `RankingService`；含本人、仅好友、名次连续；好友 ≤500（超出分页）；**不缓存**；索引覆盖禁全表扫 | `RankingHandler.*`、`RankingService`、`RankingDao` | T4.2、T6.3 | 100 万用户数据集 `EXPLAIN` 非 `ALL`；P95 达标（实测替换推导）；非好友不出现；本人分数与周统计一致 | I/P：S-21 + 用例 | 关闭排行能力位 | 否（G10） |
---

## 8. T7 客户端 LLM 直连与 Key 安全（服务端零 LLM）

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T7.1 | 【新建】`KeyVault`（Windows DPAPI `CryptProtectData(CurrentUser)`）：`store/load/revoke/rotate/describe`；落点 `%LOCALAPPDATA%\EatApp\secrets.dat`；来源 `platform/self`；指纹 `SHA-256[:8]`；明文用后 `memset_s` 清零；解密失败分类 `vault`；**UI 只显示"已配置/来源/指纹"** | `client/keyvault/*` | T0.4 | Key 仅存加密存储/凭据库；明文配置/`localStorage`/命令行/安装包资源零 Key；删除后下次请求立即失败且不从缓存/日志恢复；轮换后下一次生效、旧 Key 零残留；审计零明文 | D/S：凭据库检查 + 撤销/轮换演练 + 扫描 | 删除 blob 并清内存（回到"未配置"） | 是（T7.2–T7.7、G7） |
| - [ ] | T7.2 | 【新建】三条 Key 路径与本地准入：平台 Key（出厂占位、激活时写入）、用户自带 Key（优先、可删）、无 Key → `rule-fallback`；无对外"模型代理"监听端口；IPC 绑定 T8.4 ACL；本地限额并发 4（1–8 可配）、超限 `llm_quota_local` 快速拒绝；声明"本地限额可被绕过，不作计费承诺" | `client/app/settings/*`、`client/agentbridge/*` | T7.1、T8.4 | 三路径各有可执行用例；界面可区分"未配置/失效/网络不可达"；同机非授权进程不能借用 Key；安装包零真实 Key；无监听模型代理端口 | D/S：端口 + 双用户 IPC + 安装包扫描 | 删除自带 Key 回退平台 Key/`rule-fallback` | 是（G7） |
| - [ ] | T7.3 | 【改造】`LlmEgress`：域名白名单（域名级）；连接 5s、首帧 20s、空闲 30s 三层超时；重试 ≤2（500ms/2s+抖动）仅限连接失败/首字节前超时/5xx；**流开始后不重试**；取消 ≤500ms 关闭连接并释放槽位；并发槽位可配；增量缓冲 256KB；证书强制校验、**无 skip-verify** | `client/llm/*`、`agent/llm/gateway.py` | T7.1、T0.4 | 代理三档（自定义/系统/直连）可分类；DNS/证书/连接失败可区分；三层超时各注入一次并观测槽位释放；重试次数/退避可出示；流开始后零自动重试 | D：故障注入 + 日志曲线 | 回退到单值超时版本（不得发布） | 是（T7.4、T7.5、G8） |
| - [ ] | T7.4 | 【改造】客户端 SSE 语义：仅以 `[DONE]`/`finish_reason` 判定正常结束；UTF-8 流式解码（1 字节切片不得乱码）；未完成不得渲染完整回答；本地背压（缓冲上限暂停读取或终止并提示） | `agent/llm/streaming.*`、`client/llm/*` | T7.3 | 多字节字符与 emoji 零乱码；1 字节切片内容无损；断流/缺 `[DONE]` 判"未完成"；慢 UI 内存有界；WebSocket 零使用 | U/I：切片 + 断流 + 慢 UI 用例 | 回退到旧行解析（仅调试） | 是（G8） |
| - [ ] | T7.5 | 【改造】错误分类与脱敏：`llm_unavailable(proxy/dns/tls/vault)`、`llm_upstream_error`（固定文案、按状态码分类）、`llm_quota_exceeded`、`llm_timeout(connect/first_byte/idle)`、`llm_canceled`、`auth`（401/403 引导换 Key）；**禁透传上游原始报文**；日志默认不记 `Authorization`/prompt/回答全文 | `agent/llm/errors.*`、`client/llm/*`、日志配置 | T7.3 | 注入 401/403/429/5xx/断流/证书错误，界面与日志零 Key 片段；错误 message 为固定文案；调试开关显式、默认关、有保留期 | D/S：注入用例 + 日志扫描 | 恢复默认错误映射 | 是（G7） |
| - [ ] | T7.6 | 【新建】工具白名单与防注入：仅 11 个业务工具（`get_profile/update_profile/get_recent_meals/record_meal/get_nutrition_gap/get_weekly_stats/search_foods/get_menu/get_friends/get_friend_ranking/pull_events`）；**禁文件读写、环境变量、系统命令、任意 URL、凭据库读取**；prompt 不含 Key/路径/env/内网地址 | `agent/tools/*`、`agent/core/*` | T7.1、T8.5 | 注入"输出 API Key/环境变量/凭据库内容"全部失败；白名单外工具被拒并计数；系统提示零 Key | D/S：注入用例集 + 工具清单评审 | 回退工具清单（不得放开禁用项） | 是（G7） |
| - [ ] | T7.7 | 【新建】客户端模型可观测与 Key 审计：请求数、成败分类（五类+auth）、并发/排队、TTFT 分位、响应时长分位、取消数/原因、三层超时分别计数、上游状态码分布、限额拒绝、Key 来源；撤销/轮换审计只记不可逆指纹 | `client/llm/metrics.*`、`client/keyvault/audit.*` | T7.3、T7.5 | REQ-MNT-06 指标齐全；同一时间窗口可回答"慢在 DNS/代理/上游/首帧/限额"；审计零明文；**服务端指标零模型项** | S/I：看板 + 归因演练 | 关闭指标采集 | 否（G8 依赖） |
| - [ ] | T7.8 | 【新建·不可跨越】服务端零 LLM 负向门禁：服务端零模型域名出网、零模型 SDK、零 Key 读取、零代理/中转接口；出网清单（`ss/lsof`）零模型域名；`metrics` 零模型指标 | CI、`tools/check_server_no_llm.*`、出网清单 | T0.6、T2.1 | 静态检索与运行时出网双侧为零；出现"prompt 交服务端出网"接口即否决；评审记录入库 | S/D：`grep` + `ss` + 评审 | 无（红线不可回滚） | 是（不可跨越；违者 G8 否决） |

---

## 9. T8 Windows App 壳与客户端 Agent 运行时

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T8.1 | 【新建】`AppShell`：Windows 10 1809+/11 x64；页面资源随包（无远程首屏）；托盘常驻、关窗不断连、退出即断连；单实例 `Local\EatApp.Instance`（重复启动激活已有窗口）；开机自启可选且默认关；签名安装包与卸载清理 | `client/app/*`、`client/installer/*` | T0.4 | 断网冷启动可操作并提示离线；重复启动无双托盘/双连接；退出后服务端释放连接；卸载无残留服务/自启项；签名验证通过 | D：安装矩阵 + 场景录屏 | 回滚上一签名版本 | 是（T8.2–T8.10、G8） |
| - [ ] | T8.2 | 【新建】`TcpTransport`：复用 `shared/protocol`；**仅 1 条 App→服务端业务 TCP**（+可选升级检查）；握手携带 access/refresh（经 KeyVault）；60s 心跳；连续 2 次未收 ack 立即重连；退避 1s→2s→…→30s ±20% 抖动；单调时钟；请求 ID 单连接唯一；写操作幂等键 | `client/transport/*`、`shared/protocol` | T1.1、T3.7、T8.1 | 抓包仅 1 条业务 TCP；无 `:8000/:8080/:8787` 直连；断网 30s/休眠 5min/服务端重启后自动恢复；心跳失败到重连 ≤3 个周期；1 小时内无同用户多活 | D/P：S-13 + 抓包 + 日志曲线 | 回滚上一客户端版本 | 是（T8.3、T8.5、G8） |
| - [ ] | T8.3 | 【新建】`OfflineQueue` 有界队列：幂等键 + 载荷 + 创建时间 + 重试次数 + 最后错误；同幂等键仅一条；超限提示并停止入队；恢复后按幂等键补发 | `client/transport/offline_queue.*` | T8.2 | 离线写显示"待同步"；恢复后 DB 增量 1（不重复）；超限停止入队并有可读提示；重试上限后转"需人工处理" | I/D：弱网用例 + DB 计数 | 清空队列（丢弃前提示） | 是（G8） |
| - [ ] | T8.4 | 【新建·高风险 命名管道多用户隔离】Agent IPC：`\\.\pipe\EatAppAgent-{userSidHash}-{appInstance}`；DACL 仅当前用户 SID；消息 4B LE 长度 + JSON ≤1MB；工具调用 15s 超时；`hello` 版本自检；`ping/pong` 5s；拒绝非授权进程；**不对其它设备开放** | `client/ipc/*`、`client/agentbridge/*` | T8.1、T0.4 | 同机其它用户/非授权进程连接被拒；版本不匹配拒绝进入可用态；超时可控；无 TCP 监听端口；快速用户切换/RDP 后 ACL 仍正确 | D：双用户会话 + ACL 检查 + 版本矩阵 | 删除管道端点（Agent 不可用但业务可用） | 是（T7.2、T8.5、G7/G8；ACL 不可隔离即阻断） |
| - [ ] | T8.5 | 【新建】`AgentBridge`：`tool_call(JSON)→业务请求帧→tool_result`；线程/队列与 LLM 完全隔离；`hops` 上限 3、超限 `hops_exceeded`；**强制校验服务端 `ok`，失败不得显示成功**（BLK-11）；删除 mock 冒充路径；`request_id` 贯通六步链路 | `client/agentbridge/*`、`agent/` 工具调用层 | T8.2、T8.4、T6.3 | 一次 Agent 请求六步日志可串成同一 `request_id`；服务端零 Agent 调用；Agent 零 `:8080`；跳数链可观测；注入服务端失败时 UI 显示失败 | I/D：失败注入 + 链路日志 | 回退旧 HTTP 适配（仅调试，不得发布） | 是（T8.7、G6） |
| - [ ] | T8.6 | 【改造】Agent 运行时随包分发：内置子进程（默认）；入口改命名管道，**不再监听 `:8787`**；开发者回环模式默认关闭；崩溃检测并重启 ≤3 次；重启期间 App 自身直接走 TCP 帧 | `client/agent-runtime/*`、`agent/server.py`、`agent/__main__.py` | T8.4、T8.5 | `:8787` 零监听；Agent 未启动/崩溃时业务功能不受影响、长连接不断；重启次数与间隔可观测；断网 Agent 仍能启动到本地降级态 | D/S：端口扫描 + kill 测试 + 冷启动 | 停止子进程（业务仍可用） | 是（G8） |
| - [ ] | T8.7 | 【重写】`ServiceClient` → IPC 帧：移除对 `:8080` 的 10 个 HTTP 方法；改为 10 个业务工具 → `tool_call` → TCP 帧；保留 `source` 语义（`service/cache/db_degraded/local`）；**禁止 mock 冒充真实数据** | `agent/data/service_client.py`、`client/agentbridge/*` | T8.5 | 客户端产物与 Agent 代码零 `:8080`/`:8000`/`:8787`；工具返回 `source` 正确；未配置地址时不再回退 mock 业务数据；参数/字段与 T6 method 一致 | S/I：`grep` + 契约测试 | 回退旧适配（仅隔离环境） | 是（G6） |
| - [ ] | T8.8 | 【复用】Agent 能力保留回归：`llm/`、`rag/`、`mcp/`、`skills/market.py`、`tools/`、本地 SQLite；Skill 市场列表/装载/执行；RAG 问答；SQLite 损坏可重建且不影响 MySQL 业务；本地不落 Key/`Authorization`/服务端配置 | `agent/rag/*`、`agent/mcp/*`、`agent/skills/*`、`agent/data/db.py` | T8.6 | 与改造前能力清单差异为零（除登记移除项）；SQLite 损坏可重建；本地库非业务真相；零 Key 落盘 | I/D：能力回归 + 重建演练 | 删除本地库并重建 | 否 |
| - [ ] | T8.9 | 【改造】功能对等清单：逐项"Web 有/App 有/差异"为零，覆盖 `origin/main` 4 项新功能（菜单定价分组、餐记录编辑、消费层级同步、头像/聊天 UI） | `docs/functional-parity.md`、`client/app/*` | T8.5、T6.4–T6.10 | 清单差异项为 0；4 项新功能各有 App 用例与录屏；旧 Web 冻结快照对照结果一致 | D：逐项用例 + 双端对照 | 保留 Web 入口（T12.3 前） | 是（T12.3、G11） |
| - [ ] | T8.10 | 【新建】模型故障与 TCP 隔离验证：注入断网/DNS/代理/证书/429/断流/401；核心业务成功率 100%、心跳零中断、连接不重连、业务 P99 波动在声明阈值；服务端指标零模型项 | `tests/load/client_llm_fault/`、`client/llm/*` | T7.3、T7.5、T8.2 | S-16/S-17：分类正确；TTFT 分位（并发 1/4/8/上限）；取消 ≤500ms；核心业务 100%；TCP 零重连；REQ-PERF-13 解耦对照通过 | D/P：故障注入 + Windows 目标机压测 | 关闭注入工具 | 是（G8、REQ-PERF-13） |

---

## 10. T9 部署与运维

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T9.1 | 【新建】systemd 单元 `eat-server.service`：`LimitNOFILE=1500000`、`LimitNPROC=65535`、`Restart=on-failure/2s`、`StartLimitBurst=5/60s`、`TimeoutStopSec=20s`、`NoNewPrivileges/ProtectSystem=strict/ProtectHome=true`、`StateDirectory=eat`、`ExecStart=... --config` | `deploy/systemd/eat-server.service` | T2.1、T2.8 | 启停成功；崩溃自愈且不形成重启风暴；优雅关闭 ≤10s；配置/迁移缺失退出码非 0；仅 `/var/lib/eat`、`/var/log/eat` 可写 | D：systemd 启停/崩溃演练 | `systemctl disable` + 停进程 | 否（G11） |
| - [ ] | T9.2 | 【新建·高风险】部署检查单与自动化：`sysctl` 基线（与 T2.9 同源）、`prlimit` 校验、`somaxconn/backlog` 对齐、`nofile` 余量 ≥50%、启动断言、变更前快照 | `deploy/sysctl/*`、`deploy/checklist.md`、`deploy/preflight.sh` | T2.9 | 检查单每项有实测值与命令；preflight 失败即中止部署；快照可一键还原；`ListenOverflows=0` 记录 | D：preflight + 还原演练 | 用快照还原内核参数 | 是（T11.2） |
| - [ ] | T9.3 | 【新建】配置模板与热更：`server.toml` 全量键；SIGHUP 仅白名单键生效；非热更键变更告警"需重启"；运行时校验（连接/backlog/idle/heartbeat） | `deploy/server.toml.example`、`server/app/Config.*` | T0.7 | 热更键立即生效；非热更键不静默忽略；非法值启动失败；配置与 design 2.4.3 键表一致 | U/I：配置矩阵 | 恢复上一配置 | 否 |
| - [ ] | T9.4 | 【新建】TLS 运维：证书/私钥权限 `0400 eat:eat`；SIGHUP 重载；轮换不中断已有连接；证书到期告警 | `deploy/tls/*`、`deploy/scripts/rotate_tls.sh` | T3.6、T9.1 | 轮换后新连接用新证书、旧连接不中断；抓包无明文；到期前告警可触发 | D：轮换演练 + 抓包 | 回滚上一证书 | 否 |
| - [ ] | T9.5 | 【新建】`healthz` 与 `/metrics`：仅回环 `127.0.0.1:9080`；`healthz` 不查业务表；报告 db/redis/connections/queue_depth/inflight/fd_usage/cache_gen/uptime；`metrics` 文本 Prometheus 兼容 | `server/app/HttpAdmin.{h,cc}`、`Metrics.*` | T2.1、T5.1 | 停库报 `database=unavailable` 但进程/连接/心跳正常；指标覆盖 REQ-MNT-05 全项；零模型指标；10s 采集可用 | I/D：探测 + 指标抓取 | 关闭 admin 端口 | 否（G10） |
| - [ ] | T9.6 | 【新建】日志与脱敏：JSON 行字段（`ts/level/module/request_id/conn_id/user_key_hash[:8]/frame_type/method/err_code/reason/latency_us/source/reactor_index`）；禁 token/密码/Key/`Authorization`/body 原文/健康档案明文；IP 哈希；512MB×14 天；慢请求 100% 采样 | `server/app/Log.*`、`deploy/logrotate/*` | T0.7 | 敏感扫描零命中；抽样日志可还原一次完整交互；慢请求与过载 100% 记录；轮转可用 | S/D：扫描 + 日志还原样例 | 恢复默认日志级别 | 否（G10） |
| - [ ] | T9.7 | 【新建】归档任务 `eat-archive.timer`：02:00–05:00；`EXCHANGE PARTITION` 月归档；批量 ≤5,000 + sleep 50ms；失败重试 3 次告警；归档前后行数/抽样哈希校验 | `deploy/systemd/eat-archive.*`、`tools/archive/*` | T4.2、T9.1 | S-20：在线业务无感；行数校验一致；失败可回滚；归档期间业务 P99 不超阈值；不触碰 P7 表 | D：归档演练 | 停止 timer + 恢复分区 | 否（G10） |
| - [ ] | T9.8 | 【新建】备份与恢复：`xtrabackup` 周全量 + binlog（ROW）+ 每日归档；保留 4 周/14 天；季度恢复演练到独立实例并跑 20 条 SQL + 5 个业务读；RPO≤5min、RTO≤60min（实测替换） | `deploy/backup/*`、`docs/runbook-backup.md` | T9.1 | 恢复后业务自检通过；RPO/RTO 有实测记录；备份校验（`--prepare` + 行数/校验和）；不得把备份等同容灾 | D：恢复演练 | 使用备份回滚（注明数据丢失窗口） | 否（G10） |
---

## 11. T10 压测基础设施与场景矩阵

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T10.1 | 【新建】`bench-tcp` 完整版（C++17，复用 `shared/protocol`，epoll ET 多 loop）：连接数、建连速率、心跳周期（固定 60s）、业务 QPS、读写比、TLS 开/关、会话复用、请求大小分布、错误率、P50/P95/P99、每连接 RTT | `tests/load/bench_tcp/*` | T1.9、T0.3 | 参数化一键复跑；输出原始日志与分位值；协议帧与服务端同源；压测端自身资源可记录 | P：单机 100K 冒烟 | 删除目录 | 是（T11） |
| - [ ] | T10.2 | 【新建】压测拓扑与多机编排：**3 台压测机**（每台 ≤40 万连接、独立网卡/端口段）、10GbE、被测端单机 32C/64G/NVMe；采集端 `/proc`、`ss`、`pidstat`、`perf`、MySQL/Redis 计数 | `tests/load/topology/*`、`tests/load/orchestrate.*` | T10.1 | 拓扑文档与启动脚本入库；压测端资源不先饱和（有基线证明）；被测端始终单机；时钟同步 | D：联调 | 停止压测机进程 | 是（T11.2） |
| - [ ] | T10.3 | 【新建】场景矩阵落地 S-01…S-21：每场景一个脚本（判据、参数、证据目录 `bench-<场景>-<时间戳>`）与报告模板（双档分离、三态判定、拐点归因、原始日志） | `tests/load/scenarios/*`、`docs/bench-report-template.md` | T10.1 | 21 个场景脚本齐全且可执行；每场景含"达标/未达标/未实测"三态；报告首页含硬件/DB/内核/压测端快照 | P：逐场景 dry-run | 删除脚本（不影响服务） | 否（G9/G10） |
| - [ ] | T10.4 | 【新建】协议/缓存/JSON 专项：S-10（帧六类）、S-11（JSON 六类）、S-06（缓存三态）、S-07（写后删）、S-08（负缓存联动）、S-09（并发回填） | `tests/load/scenarios/s06…s11` | T1.6、T5.5–T5.8 | 每场景判据与 R6 一致；S-08 必须"负缓存→写入→立即读=新值"；S-09 陈旧时长 ≤ 声明窗口并登记风险 | P：执行 | 无（只读压测） | 否（G5） |
| - [ ] | T10.5 | 【新建】故障注入 S-12/S-13/S-14/S-15/S-16：kill -9 重启、弱网/休眠/VPN/时间跳变、Redis 停/恢复、MySQL 停/恢复、客户端模型故障 | `tests/load/scenarios/s12…s16`、`tools/fault_inject/*` | T9.1、T8.10 | 每类"预期 vs 实测"对照；失败项登记阻断；停 MySQL 连接下降 ≤1%；停 Redis 只读 100%；模型故障 TCP 零中断 | D/P：执行 | 恢复注入项 | 否（G10） |
| - [ ] | T10.6 | 【新建】长稳与泄漏 S-19：≥1 小时（建议 24h）稳态；RSS/fd/队列/连接无单调上升；GC/碎片在阈值内 | `tests/load/scenarios/s19` | T10.3、T9.5 | RSS 与 fd 波动在阈值内、无单调上升；队列深度稳定；连接数无漂移；期间 P99 可解释 | P：长稳 | 停止压测 | 否（G10） |

---

## 12. T11 100 万同时在线容量验收（单独验收任务）

> **口径**：1,000,000 同时在线**仅指压测上限**，不是日常运营承诺；日常容量另按 `C_daily = 注册用户 × 月活率 × 日活率 × 峰时在线率` 建模（参数未确认前【未验证】）。
> **完成规则**：未实测不得勾选；未达标必须写"未达标"并登记 BLK-1/BLK-3，不得写"理论可达"充当结论。

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T11.1 | 【新建】压测前基线快照：CPU 32C/64G/NVMe、内核版本、MySQL/Redis 版本与关键参数、`max_connections` 实测、`sysctl` 全量快照、构建镜像/二进制哈希、压测端 3 台资源 | `evidence/bench-baseline-<ts>/` | T10.2、T9.2 | 快照字段齐全可复现；`max_connections` 为实测值；压测端与测端网络拓扑记录；构建哈希一致 | S/D：采集脚本 | 无（只读） | 是（T11.2–T11.5） |
| - [ ] | T11.2 | 【新建·关键】连接阶梯 S-01：1K/10K/100K/500K/**1M**；每档稳态 ≥10 分钟；记录建连成功率、稳态 RSS、`ss -s` 总连接、进程 fd、空载 CPU、心跳 CPU | `tests/load/scenarios/s01`、`evidence/bench-s01-<ts>/` | T11.1、T2.9、T2.7、T10.1 | 建连成功率 ≥99.9%；**95% 以上连接在目标档位保持 ≥10min 不掉线**；RSS/fd 曲线无泄漏；1M 档每连接应用态内存有实测上界；报告首页标注"1M 为压测上限" | P：3 台压测机执行 + `/proc`、`ss`、`pidstat` 采样 | 停止压测、释放连接 | 是（G9；不达标=BLK-1） |
| - [ ] | T11.3 | 【新建·关键】心跳 QPS 与零落库 S-02：1M 空载 + 60s 心跳挂载；记录单向/双向 QPS、心跳 CPU、MySQL `Questions`/`Com_select` 差值、Redis `INFO commandstats` 差值 | `tests/load/scenarios/s02` | T11.2、T2.4 | 单向 ≈16,667/s、双向 ≈33,333/s（与 `C÷60` 一致）；**纯心跳期间 MySQL 与 Redis 计数零增长**；心跳 CPU ≤单核 25% 或给出可接受理由并实测；心跳 P99 不随业务压力线性恶化 | P：执行 + 计数快照 | 停止心跳挂载 | 是（G2、G9） |
| - [ ] | T11.4 | 【新建·关键】新建连接速率 S-03：稳态 50/s；恢复 3,000–5,000/s；极端 16,667/s 红线（不作为默认目标）；记录成功率、P99 握手、`ListenOverflows/ListenDrops`、握手期 `Com_select` | `tests/load/scenarios/s03` | T11.2、T2.6、T3.7 | `ListenOverflows=0`；恢复档 P99 握手达标；**握手期 `Com_select` 零增长（除首登/续期）**；3k–5k/s 未达则登记 BLK-3；极端档仅作红线记录 | P：执行 | 限速至稳态 | 是（G9） |
| - [ ] | T11.5 | 【新建·关键】业务 QPS 与延迟 S-04/S-05：读梯度 500/1,000/2,000/5,000；写 100/300/500/1,000（含幂等）；记录业务 QPS、P50/P95/P99、错误率、CPU/RSS/fd、MySQL TPS/连接数、Redis ops、分域缓存命中率、拐点归因 | `tests/load/scenarios/s04,s05`、`evidence/bench-s04-s05-<ts>/` | T11.2、T4.6、T5.5–T5.8、T6.2 | 业务 QPS ≤ 单机 MySQL 实测读写能力 ×0.7；错误率与 P99 达标（阈值在报告中声明）；≥6 档梯度可定位拐点；缓存命中/未命中/负缓存分域统计；幂等重放零重复行；**不得用空库结论** | P：数据集 ≥100 万用户 + 每人 ≥100 条记录 | 降低 QPS 至稳态 | 是（G10） |

---

## 13. T12 文档收尾与 Web 退场

| 状态 | ID | 任务目标（处置） | 涉及文件/目录 | 前置 | 验收判据 | 验证 | 回滚 | 阻塞后续 |
|---|---|---|---|---|---|---|---|---|
| - [ ] | T12.1 | 【改造】三端契约文档：method/字段/错误码/端口（9443/9080/3306/6379）/协议版本/能力位/启动方式；修正 `AGENT.md` 缺口与 `HANDOFF.md` 端口错误 | `AGENT.md`、`HANDOFF.md`、`docs/protocol/v1.md` | T6.10、T8.7 | 文档与实现逐项一致（自动比对零差异）；无 `:8080` 对外入口描述；无 P7 端口混用 | S：比对脚本 + 评审 | 回滚文档版本 | 否（G11） |
| - [ ] | T12.2 | 【改造】交付文档：`README`/`HANDOFF`/`KNOWN-ISSUES`/`DELIVERY`/`TEST-REPORT` 写入本轮架构位置、启动、结论、阻断清单；替换 P7 最终版内容 | 根文档 5 份 | T11.5、T12.1 | 文档含本轮章节；阻断项状态与证据一致；结论四类标注齐全；无"日常百万在线"表述 | S：检索 + 评审 | 回滚文档 | 是（G11） |
| - [ ] | T12.3 | 【改造】Web 灰度下线：灰度比例 → 全量 → 停 `:4173` → 回滚方式；旧地址返回"请使用 Windows 客户端"静态页；前置 App 覆盖证据 | `start-all.bat`、`docs/rollout/*`、静态提示页 | T8.9、T12.2 | REQ-COMP-02：`:4173` 不再提供业务页；回滚步骤执行后浏览器端限时恢复；P7 13 路由回归全绿；无 WebView 远程首屏 | D：下线/回滚演练 | 恢复 `:4173` 静态托管（限时）+公告 | 是（G11） |
| - [ ] | T12.4 | 【新建】兼容性与版本矩阵：当前版/前一版/过旧版；未知字段/未知枚举/大整数；Win10 21H2、Win11 22H2、Win11 最新安装启动；DPI/多显示器/睡眠唤醒 | `tests/compat/*`、`docs/compat-matrix.md` | T8.1、T1.4 | 三档版本行为符合预期（过旧拒绝+可读升级提示）；旧端忽略未知字段且行为不变；大整数以字符串传输；安装矩阵通过 | I/D：矩阵执行 | 回滚上一版本 | 否（G11） |
| - [ ] | T12.5 | 【新建·高风险 Key 风险】安全评审与签收：STRIDE/等价威胁建模；协议模糊测试；重放防护；Key 固有风险登记 + 产品侧签收；泄露事件响应（撤销/轮换/指纹审计/声明无法集中追溯）；对外文案禁"Key 已安全/不可提取" | `docs/security-review.md`、`docs/risk-register.md`、`docs/key-incident-runbook.md` | T7.1–T7.7、T1.6 | 评审结论与模糊测试报告入库；重放用例失败；风险登记含签收人；事件流程可执行；全文检索无违规宣称 | S/D：评审 + 注入 + 检索 | 无（红线） | 是（G7、G11） |
| - [ ] | T12.6 | 【新建】证据归档与结论标注核查：`evidence/bench-*` 原始日志/配置/曲线；四类标注；压测上限与日常容量双档分离；全文检索"百万并发/支持百万/日常百万在线"仅出现在目标定义且相邻有标注 | `evidence/*`、`docs/claim-audit.md` | T11.5、T10.6 | 每项结论可追溯到证据编号；未实测项一律"未实测"；双档报告独立；无未标注容量数字 | S：脚本检索 + 人工复核 | 无 | 是（G11） |
| - [ ] | T12.7 | 【新建】回滚总演练：服务端版本回滚、迁移 `_rollback.sql`、客户端回上一签名版本、`sysctl` 快照还原、恢复 `:4173`；回滚后 P7/P8 基线不变 | `deploy/rollback/*`、`docs/runbook-rollback.md` | T9.1–T9.8、T12.3 | 一键回滚脚本可执行并留痕；回滚后 P7 回归全绿、P8 基线未变；数据回滚窗口与丢失范围已声明 | D：全链路回滚演练 | 前滚到当前版本 | 是（G11） |
---

## 14. 需求覆盖矩阵（R6 → 任务）

### 14.1 第 4 章 DFX 需求

| R6 需求 | 承接任务 | 验收证据/阶段门 |
|---|---|---|
| REQ-PERF-01 六口径分离 | T10.3、T11.1、T12.6 | 六口径表 + 场景编号；G9/G10 |
| REQ-PERF-02 连接承载（压测上限 1M） | T11.2 | 阶梯报告 + RSS/fd 曲线 + 内核快照；G9 |
| REQ-PERF-03 心跳 60s 零落库 | T2.4、T11.3 | S-02 + MySQL/Redis 计数差值；G2 |
| REQ-PERF-04 新建连接速率 | T2.6、T11.4 | S-03 + `ListenOverflows`；G9 |
| REQ-PERF-05 业务 QPS 与 MySQL 边界 | T6.2、T11.5 | S-04/S-05 + `sysbench` + 硬件快照；G10 |
| REQ-PERF-06 过载保护 | T2.5、T2.7 | 梯度曲线 + RSS + 4002；G1/G10 |
| REQ-PERF-07 五段耗时可定位 | T7.7、T9.5、T9.6 | 分段埋点 + 慢请求全采样；G10 |
| REQ-PERF-08 双档容量 | T11.1、T11.5、T12.6 | 压测上限档与 `C_daily` 独立报告；G9/G10 |
| REQ-PERF-09 缓存读路径三态 | T5.5、T10.4 | S-06 + 分域命中率；G5 |
| REQ-PERF-10 帧解析开销 | T1.2、T1.6、T10.4 | 半包/粘包/超长用例 + RSS；G1 |
| REQ-PERF-11 客户端并发与 TTFT | T7.3、T7.7、T8.10 | 并发 1/4/8/上限 + TTFT 分位 + 心跳对照；G8 |
| REQ-PERF-12 JSON 开销与体积分布 | T1.4、T1.8、T10.4 | 1KB/8KB/64KB/上限四档 + 直方图；G1/G5 |
| REQ-PERF-13 服务端与 LLM 解耦 | T7.8、T8.10 | 出网清单零模型域名 + 并发注入对照；G8 |
| REQ-REL-01 重连自愈 | T8.2、T8.3 | S-13 + 幂等补发计数；G8 |
| REQ-REL-02 重启风暴恢复 | T2.6、T10.5、T11.4 | S-12 + 退避曲线 + MySQL 峰值连接；G9 |
| REQ-REL-03 失败必须可见 | T6.1、T8.5 | 失败注入 + UI 断言（BLK-11 闭环）；G6 |
| REQ-REL-04 降级透明（两类不可达） | T7.5、T8.5、T8.10 | `data_unavailable`/`ai_degraded` 标记；G8 |
| REQ-REL-05 优雅关闭 | T2.8、T9.1 | SIGTERM ≤10s + close 帧；G11 |
| REQ-REL-06 单点故障语义 | T9.8、T12.2 | 语义表 + 演练记录；G11 |
| REQ-REL-07 缓存不可用降级 | T5.8、T10.5 | S-14 + 待失效清单 + 恢复门；G4/G5 |
| REQ-REL-08 帧异常不崩溃 | T1.2、T1.6 | S-10 + fuzz + fd 回收；G1 |
| REQ-REL-09 客户端出网故障/流中断 | T7.3、T7.5、T7.4、T8.10 | S-16 + 分类 + 取消时延；G8 |
| REQ-REL-10 写后删除与窗口 | T5.6、T5.7、T10.4 | S-07/S-08/S-09 + 窗口实测；G5 |
| REQ-SEC-01 传输加密 | T3.6、T9.4 | 抓包 + 证书异常 + 无 skip-verify；G3 |
| REQ-SEC-02 握手鉴权 | T2.6、T3.7 | 2001 断连 + 失败 3 次冷却；G3 |
| REQ-SEC-03 凭证/Key 安全存储 | T7.1、T8.4 | DPAPI + 卸载清除 + IPC ACL；G7 |
| REQ-SEC-04 身份不可自证 | T3.7、T6.3 | 越权全失败 + 产物无硬编码 Token；G3/G6 |
| REQ-SEC-05 协议安全评审 | T1.6、T12.5 | STRIDE + 模糊测试 + 重放用例；G11 |
| REQ-SEC-06 敏感信息零落盘 | T7.5、T9.6 | 日志/转储扫描零命中；G7/G10 |
| REQ-SEC-07 Key 只由客户端保管 | T7.1、T7.2、T12.5 | 仓库/产物/帧/日志零 Key + 风险签收；G7 |
| REQ-SEC-08 负缓存不泄露存在性 | T5.6、T6.1 | 越权返回 3002/3004 且不写负缓存；G5 |
| REQ-SEC-09 三条 Key 路径与本地准入 | T7.2、T8.4 | 三路径用例 + 非授权进程被拒；G7 |
| REQ-SEC-10 错误脱敏与防注入 | T7.5、T7.6 | 注入用例 + 固定文案 + 白名单；G7 |
| REQ-MNT-01 版本协商 | T1.1、T1.5、T3.7 | 三档版本矩阵 + 可读升级提示；G3 |
| REQ-MNT-02 故障可观测 | T9.5、T9.6 | 六类故障字段 + 指标；G10 |
| REQ-MNT-03 分发可复现 | T8.1、T9.1、T12.7 | 产物哈希 + 安装/卸载/回滚；G11 |
| REQ-MNT-04 协议可调试 | T1.6 | 解码/回放工具 + 交互还原样例；G1 |
| REQ-MNT-05 帧与缓存可观测 | T5.1、T5.7、T9.5 | 指标清单 + 一次故障归因；G10 |
| REQ-MNT-06 客户端模型与 Key 可观测 | T7.7 | 指标 + 审计指纹（零明文）；G8 |
| REQ-COMP-01 业务语义不变 | T6.4–T6.10、T12.4 | 双端逐字段对照；G6 |
| REQ-COMP-02 Web 退场 | T12.3 | 灰度/下线/回滚演练；G11 |
| REQ-COMP-03 P7 冻结 | T0.8、T12.7 | P7 diff 为空 + 13 路由回归；G11 |
| REQ-COMP-04 Windows 兼容 | T8.1、T12.4 | 安装矩阵 + DPI/多显示器/唤醒；G11 |
| REQ-COMP-05 缓存不改变业务语义 | T5.5、T5.6、T10.4 | 四态逐字段一致 + 负缓存联动；G5 |
| REQ-COMP-06 JSON 版本兼容 | T1.4、T1.8、T12.4 | 未知字段/枚举 + 大整数 + 非法 JSON；G1/G11 |
| REQ-STATE-01 单机可用性声明 | T9.8、T12.2 | SLA 声明 + 演练；G11 |
| REQ-STATE-02 存储故障不断连 | T5.8、T6.2、T10.5 | 停库/停 Redis 连接下降 ≤1%；G10 |

### 14.2 附录 C 的 16 条硬约束

| # | 硬约束 | 承接任务 | 阶段门 |
|---|---|---|---|
| 1 | Windows App 完全替代 Web | T8.1、T8.9、T12.3 | G8/G11 |
| 2 | 100 万注册 + 100 万同时在线分别建模 | T11.1–T11.5、T12.6 | G9/G10 |
| 3 | 服务端 Linux 单机 | T0.1、T9.1、T9.2 | G9 |
| 4 | 自研 TCP + Muduo 主从 Reactor + 60s 心跳 | T0.2、T1、T2 | G1/G2 |
| 5 | 服务端唯一 C++ | T0.2、T0.4 | G1 |
| 6 | MySQL/Redis 仅服务端；JSON 序列化 | T5.1、T7.8、T8.7 | G5/G7 |
| 7 | Redis 禁 pub/sub、禁客户端直连、禁多实例 | T0.6、T5.1、T9.5 | G5 |
| 8 | 新建统一身份（不复用 P7/P8） | T3.1–T3.7 | G3 |
| 9 | App 后台常驻长连接 | T8.1、T8.2 | G8 |
| 10 | 禁 WebSocket 推送/状态同步 | T0.6、T7.4、T8.2 | G8 |
| 11 | 自研 TCP 真实完整（定长头+长度+body；六类异常；心跳零落库） | T1、T2.4、T2.7 | G1/G2 |
| 12 | Cache-Aside（负缓存/写后 DEL/降级） | T5 | G4/G5 |
| 13 | Agent 链路重排（客户端先处理→TCP→服务端→回客户端） | T8.4–T8.7 | G6 |
| 14 | 帧 body 唯一 JSON（无 Protobuf 双分支） | T0.3、T1.4、T1.8 | G1/G5 |
| 15 | 写后删除 + 可接受不一致窗口 | T5.7、T5.8、T10.4 | G5 |
| 16 | Key 在客户端（DPAPI）+ 客户端直连模型（HTTPS/SSE）+ 服务端零 LLM | T7.1–T7.8、T8.10 | G7/G8 |

---

## 15. 关键依赖摘要与执行顺序

### 15.1 主关键路径

`T0.2 → T0.3 → T1.1 → T1.2 → T2.1 → T2.4 → T3.1 → T3.4 → T3.5 → T6.3 → T6.5 → T8.2 → T8.5 → T10.1 → T11.2 → T11.5 → T12.2/12.3`

### 15.2 硬前置（不满足即不得开工/不得宣称）

| 前置 | 被阻塞任务 | 阻断条件 |
|---|---|---|
| T1.1 协议冻结 | T8.2 客户端帧循环 | 协议字段未冻结即开工 → 帧语义漂移，G1 否决 |
| T2.9 fd/内核参数实测 | T11.2 1M 阶梯 | `nofile/fs.nr_open` 不达标 → 登记 BLK-1，不得宣称 1M |
| T2.10 Muduo 阻塞边界 | G1/G9 | IO 线程内出现 SQL/Redis/文件 IO → 阻断合并 |
| T3.1–T3.7 统一身份 | T6 全部、T8.2 | 身份不可自证 → 真实用户数据不得进入新链路 |
| T4.1–T4.5 表与索引 | T6.5/T6.6/T6.9/T6.10 | 7 类查询未过 `EXPLAIN` → G4 否决 |
| T4.6 百万数据集 | T11.5 业务 QPS | 空库压测结论无效（降级为【理论推导】） |
| T5.1–T5.8 缓存 | T6.4–T6.9、G5 | 写路径 SET / 正负分键 / 孤儿键 → 阻断上线 |
| T8.4 命名管道 ACL | T7.2 本地准入、T8.5 | ACL 无法隔离同机进程 → 阻断（BLK-23/26） |
| T8.9 功能对等 | T12.3 Web 下线 | 差异项非零 → 不得停 `:4173` |
| T9.2 部署检查单 | T11.2 | 内核参数未按检查单落地 → 压测结论不可用 |

### 15.3 可并行项（在依赖满足后）

- T0.5（P8 service/dao 入库）可与 T0.2–T0.4 并行；T0.6/T0.8 可在 T0.1 后立即并行。
- T1.3/T1.4 可在 T1.2 完成后与 T1.5 并行；T1.6/T1.9 在 T1.2 后可并行。
- T4.1–T4.3 与 T3.2–T3.5 可并行（都依赖 T3.1）；T4.6 可与 T4.5 并行。
- T5.2–T5.4 在 T5.1 后可并行；T5.5/T5.6/T5.7 需 T5.2–T5.4。
- T7.1/T7.3（库层 KeyVault/LlmEgress）可与 T6 并行，但 T7.2 依赖 T8.4。
- T9.1/T9.3/T9.5/T9.6 可与 T8 并行；T9.7 依赖 T4.2。
- T10.1/T10.2/T10.3 可与 T9 并行；T11 只能在 T9.2 + T10.1/T10.2 + T4.6 完成后执行。

### 15.4 高风险任务与控制点

| 高风险项 | 任务 | 控制点 | 阻断条件 |
|---|---|---|---|
| fd/内核参数 | T2.9、T9.2 | 启动断言 + preflight + 快照 | 目标值设置失败或阶梯未过 → BLK-1 |
| Muduo 阻塞边界 | T2.10、T2.3 | CI 符号黑名单 + 心跳 P99 | IO 线程同步 DB/Redis → 阻断合并 |
| 单机 MySQL 容量 | T6.2、T11.5 | 池 ≤64 + `borrow_for` + 实测 ×0.7 | 目标 QPS > 实测 ×0.7 → 不得宣称达标 |
| TLS 握手/内存 | T3.6、T9.4、T11.2 | TLS1.3 + ticket + 内存预算 | 恢复期建连未达或 RSS 超预算 → 记录未达标 |
| 命名管道多用户隔离 | T8.4 | DACL 仅当前 SID + 双用户测试 | ACL 不可隔离 → 阻断 G7/G8 |
| 缓存并发回填 | T5.7、T10.4（S-09） | 回填二次校验 + `dirty` 标记 + 短 TTL | 陈旧 > 声明窗口 → 阻断；标记"已解决" → 阻断 |
| Web 下线 | T12.3 | App 灰度覆盖证据 + 回滚 | 覆盖不足 → 保留双端 |

---

## 16. 阶段门（G1–G11）与完成定义（DoD）

### 16.1 阶段门判据

| 门 | 判据（全部满足才通过） | 证据 | 对应任务 |
|---|---|---|---|
| **G1 协议完整性** | S-10 + S-11 全过；40B 帧头字段/错误码与实现一一对应；超长帧读前拒绝；回放工具可用；仓库零 `.proto`/编码开关 | 协议报告 + fuzz 日志 + 回放样例 | T1.1–T1.8 |
| **G2 心跳零落库** | 纯心跳期间 MySQL `Questions`/`Com_select` 与 Redis `commandstats` 零增长；60s 周期可观测；空闲 165s；心跳不进业务队列 | S-02 子集 + 计数快照 | T2.4、T11.3 |
| **G3 身份与 TLS** | 越权用例全失败；吊销即时生效；refresh 重用撤销整链；抓包无明文；篡改证书被拒；无 skip-verify；握手失败 3 次冷却 | 身份矩阵 + 抓包 + 证书用例 | T3.1–T3.7 |
| **G4 数据模型** | 7 类查询 `EXPLAIN` 非 `ALL`；迁移幂等且可回滚；schema guard fail-closed；分区裁剪生效 | EXPLAIN 报告 + 迁移/回滚日志 | T4.1–T4.5 |
| **G5 缓存一致性** | S-06/S-07/S-08/S-09/S-14 全过；命中路径零 MySQL；写路径零 SET；孤儿键 0；`MONITOR` 零 pub/sub；停 Redis 只读 100% | 缓存报告 + 原始日志 + 键归属脚本 | T5.1–T5.8 |
| **G6 业务语义与 Agent 链路** | P8 字段/错误码逐项对照一致；餐记录删改落库且统计联动；好友双向/拉黑/推送正确；六步链路同 `request_id`；服务端零 Agent 调用；Agent 零 `:8080`；`ok` 校验生效 | 对照用例 + 链路日志 + 静态检索 | T6.1–T6.10、T8.5、T8.7 |
| **G7 客户端 Key 与安全** | 仓库/产物/帧/日志零真实 Key；Key 位于 DPAPI；三路径可用；撤销/轮换即生效；注入零外泄；非授权进程不能借用 Key；风险已签收 | 扫描报告 + 凭据库检查 + 演练 + 签收 | T7.1–T7.7、T8.4、T12.5 |
| **G8 Windows App 与模型隔离** | 单入口（仅 1 条业务 TCP）；断网冷启动；功能对等差异 0；S-16/S-17/S-18 通过；模型故障 TCP 零重连；服务端指标零模型项 | 抓包 + 安装矩阵 + 故障注入报告 | T8.1–T8.10 |
| **G9 连接容量** | T11.2–T11.4 有实测：1M 档 95% 连接 ≥10min、心跳 QPS、恢复建连速率、`ListenOverflows=0`；未达标必须写"未达标"+BLK-1/BLK-3 | 阶梯报告 + 内核/资源快照 + 原始日志 | T2.9、T11.1–T11.4 |
| **G10 业务容量与运维** | T11.5 业务 QPS ≤ MySQL 实测 ×0.7；S-19 长稳无泄漏；S-20 归档演练；备份恢复演练；healthz/metrics 语义正确；systemd 启停/优雅关闭 | S-04/S-05/S-19/S-20 报告 + 运维证据 | T9.1–T9.8、T11.5、T10.6 |
| **G11 退场与交付** | 灰度→全量→停 `:4173`→回滚演练通过；P7 13 路由回归全绿；契约/交付文档与实现一致；证据四类标注齐全；回滚总演练通过 | 下线/回滚日志 + 文档比对 + 证据索引 | T12.1–T12.7 |

### 16.2 各阶段完成定义（DoD）

**阶段 A（T0–T2）DoD**
1. Linux 单机可构建、可启动 `eat-server`，Muduo 主从 Reactor 线程归属可证；
2. 40B 帧协议 + 粘包/半包/拆包/超长/非法/连接中断六类用例全过；
3. 严格 JSON 六类 reason + 原子回滚 + 请求 ID 回带；
4. 纯心跳零 MySQL/Redis；心跳 60s、空闲 165s；
5. IO 线程零 SQL/Redis/文件 IO（CI + 评审双证）；
6. fd/内核参数预检通过（不通过则登记 BLK-1，阶段 A 仍可完成但 G9 阻断）；
7. 优雅关闭 ≤10s；SIGTERM 演练记录。

**阶段 B（T3–T5）DoD**
1. 统一身份闭环（注册/登录/刷新/登出/撤销/改密/禁用），越权全失败；
2. TLS 抓包无明文、SPKI 固定、无 skip-verify；
3. `002`–`006` 迁移幂等、回滚可执行、schema guard fail-closed；
4. 7 类查询索引证据（百万级数据集）；
5. 缓存 S-06/S-07/S-08 通过；正负同键；写路径零 SET；孤儿键 0；
6. 停 Redis 只读成功率 100%、心跳/连接不受影响、"故障中写入→恢复→立即读"=新值；
7. 零 pub/sub、单 Redis 实例、仅服务端连接。

**阶段 C（T6–T8）DoD**
1. P8 6 个业务域全部帧化，字段/错误码/幂等语义与 P8 逐项一致；
2. 餐记录 patch/delete 落库，统计联动正确（BLK-10 闭环）；
3. 好友双向/拉黑/推送/拉取可用，断线可按序号补齐；
4. Windows App 单入口、断网冷启动、托盘常驻、单实例；
5. 命名管道 IPC ACL 通过双用户测试；Agent 不再监听 `:8787`；Agent 零 `:8080`；
6. KeyVault DPAPI + 三路径 + 撤销/轮换 + 零回显；
7. LlmEgress 三层超时/有界重试/取消/分类/脱敏；SSE 未完成判定与本地背压；
8. 模型故障期间核心业务 100%、TCP 零重连（S-16/S-17）；
9. 功能对等清单差异为 0。

**阶段 D（T9–T11）DoD**
1. systemd 可启停、崩溃自愈、优雅关闭；preflight/检查单可执行；
2. healthz/metrics 语义正确，日志脱敏与慢请求全采样；
3. 归档与备份恢复演练通过；
4. `bench-tcp` 参数化可复跑，多机拓扑可用；
5. 1M 连接阶梯、心跳 QPS、新建连接速率有实测（未过写"未达标"并登记 BLK-1/3）；
6. 业务 QPS ≤ 单机 MySQL 实测 ×0.7，P50/P95/P99/错误率/拐点齐全；
7. 长稳 ≥1h（建议 24h）无 RSS/fd/队列单调上升。

**阶段 E（T12）DoD**
1. Web 灰度→全量→停 `:4173`→回滚演练完成；旧地址仅返回静态提示页；
2. P7 13 路由回归全绿、P7 diff 为空；
3. 三端契约文档与实现自动比对零差异；交付文档含本轮结论与阻断清单；
4. 证据归档齐全，四类标注与双档容量口径核查通过；
5. Key 风险登记已签收、泄露事件响应流程可用；
6. 回滚总演练通过（服务端/迁移/客户端/sysctl/Web）。

---

## 17. 阻断项闭环映射与"不得宣称"清单

### 17.1 阻断项 → 闭环任务

| 阻断项 | 闭环任务 | 闭环证据 |
|---|---|---|
| BLK-1 100 万连接无实测 | T2.9、T11.2 | 阶梯报告 + fd/RSS 曲线 + 内核快照 |
| BLK-2 单机 MySQL 无实测 | T11.5、T4.6 | `sysbench` + 业务压测 + 硬件快照 |
| BLK-3 新建连接速率未测 | T11.4 | `ListenOverflows` + P99 握手 |
| BLK-4 无统一身份 | T3.1–T3.7 | 越权/吊销/refresh 用例 |
| BLK-5 无 TLS/协议评审 | T3.6、T12.5 | 抓包 + fuzz + STRIDE |
| BLK-6 无 App 产物 | T8.1、T8.9 | 安装包/签名/功能对等清单 |
| BLK-7 无压测工具 | T10.1、T10.2 | 脚本 + 原始日志 |
| BLK-8 无归档/分区 | T4.2、T9.7 | S-20 演练 |
| BLK-10 餐记录删改缺口 | T6.6 | 落库 + 统计联动用例 |
| BLK-11 Agent 不校验 `ok` | T8.5 | 失败注入用例 |
| BLK-12 业务阻塞 IO 未验证 | T2.10、T2.3 | CI + 心跳 P99 |
| BLK-13 Web 下线中断风险 | T12.3 | 灰度/回滚演练 |
| BLK-14 无协议调试/回放 | T1.6 | 回放样例 |
| BLK-15 Muduo 未引入 | T0.2 | 构建日志 + 线程归属 |
| BLK-16 服务端→Agent 回路 | T7.8、T8.5 | 服务端零 Agent 调用 + 零出站 |
| BLK-17 双档容量无实测 | T11.1–T11.5 | 两档独立报告 |
| BLK-18 P8 传输层不兼容 | T6.2、T6.3 | 重写层证据 + 无 httplib 依赖 |
| BLK-19 帧协议未实现 | T1.1–T1.8 | S-10/S-11 |
| BLK-20 缓存层零实现 | T5.1–T5.8 | S-06/07/08 |
| BLK-21 客户端 Agent 运行时未落地 | T8.4–T8.7 | 单入口 + IPC ACL + 跳数 |
| BLK-22 帧/缓存实测缺口 | T10.4、T10.5 | 场景矩阵全量 |
| BLK-23 Key 与出网前置未落地 | T7.1–T7.5 | S-16/S-17/S-18 |
| BLK-24 写后删除/负缓存联动零实现 | T5.6–T5.8 | S-07/S-08/S-14 |
| BLK-25 JSON 严格校验与体积红线 | T1.4、T1.8 | S-11 + 体积直方图 |
| BLK-26 Key 风险签收与仓库门禁 | T0.6、T7.1、T12.5 | 扫描报告 + 签收记录 |
| BLK-27 客户端模型调用实测缺口 | T7.7、T8.10 | Windows 目标机报告 |

### 17.2 未闭环前"不得宣称"清单

1. 不得宣称"支持百万在线/百万并发/日常可承载 100 万"——1M 仅为压测上限，日常容量参数未确认前为【未验证】；
2. 不得宣称"缓存已加速/写后一致已达标"——未过 S-06/S-07/S-08/S-14 前不成立；
3. 不得宣称"Agent 链路已重排"——未过 G6 前不成立；
4. 不得宣称"Key 已安全保管/不可提取"——必须显式声明客户端 Key 可被逆向/内存提取的风险，且风险由产品侧签收；
5. 不得宣称"LLM 已可靠可用"——未过 S-16/S-17 前不成立；
6. 不得宣称"服务端已承载 LLM/可代理模型"——R6 明令禁止；出现即否决；
7. 不得把"单机压测结论"外推为"多机结论"，不得用平均值代替 P99，不得用空库结论充当验收。

---

## 18. 本阶段边界与未决参数

1. **未修改任何源码**：本文件只做任务规划；未创建提交、未合并 PR、未改动 `D:\gitclone\p8-verify`、未执行压测。
2. **任务勾选规则**：代码完成不等于勾选；必须满足验收判据 + 证据归档。压测类任务必须附原始日志与快照；未实测只能标"未实测/未达标"。
3. **待产品/运维确认（不阻塞可确定任务，但阻塞对应门的最终判定）**：
   - 日常容量在线率三参数（`T11.1`/`T12.6`）；
   - 各域 TTL 的具体数值（`T5.4`，当前取 design 裁决值，需产品确认业务陈旧窗口）；
   - `max_frame_size` 最终值（`T1.7`，当前 64KB，需体积直方图）；
   - scrypt 参数（`T3.2`，需目标 CPU 实测）；
   - 限流速率与全局 QPS（`T2.6`，需压测修订）；
   - 模型域名白名单、平台 Key 发放与费用归属、用户自带 Key 文案、本地限额数值（`T7.2`/`T7.3`）；
   - 归档保留月数与备份保留策略（`T9.7`/`T9.8`）。
4. **任务总数**：13 个任务组、102 个子任务、11 个阶段门（G1–G11）。
5. **执行权归属**：本文件确认后由实现代理执行编码；本代理不进入编码阶段。