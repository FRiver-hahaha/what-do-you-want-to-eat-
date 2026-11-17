# 总方案：后端合并 · Windows App · 仓库整改（S0–S4）

> 依据：`.codeartsdoer/specs/app_transport_scale/{spec,design,tasks}.md`（R6）、`AGENT.md`、`AGENT_Linux.md`、`docs/architecture-status.md`、`docs/protocol/v1.md`。
> 本文件只定义阶段、裁决与门禁，不承诺工期；未实测的能力一律标注【未验证】或【阻断】。

## 1. 背景：仓库里现在有三套东西

| 代号 | 位置 | 形态 | 实际能力 |
|---|---|---|---|
| ① | 根目录 `index.html`/`app.js`/`agent-*.js/css` + `agent/` | 浏览器页面 + Python Agent（HTTP/SSE，`:8787`）+ SQLite | 功能最完整；本机 51 项单测实测通过 |
| ② | `chatroom-backend/` | P7 TCP（`\n` 分帧，`:8000`）+ P8 HTTP（cpp-httplib，`:8080`）+ TUI/WebSocket/Web | 可运行；P7 8 脚本 104 断言、P8 S1–S5 证据齐全 |
| ③ | 根 `shared/protocol`、`server/`、`client/`、`migrations/add`、`deploy` | Muduo + 40B 帧 + JSON 的新架构 | 纯逻辑可测；业务链路未打通、`eat-server` 不可构建 |

三套并存导致：两套网络栈、两套身份、两套幂等、三份相似的协议实现、两份交接文档、大量死配置与死入口。

## 2. 目标形态（三句话）

1. **服务端**：一个 Linux 单机进程 `eat-server`（Muduo 主从 Reactor + 40B 帧 + JSON），承载 P7 的账号/好友语义与 P8 的档案/餐记录/统计/菜单/排行语义，唯一 schema（`002`–`006` 演进 P8 `001`），MySQL/Redis 只由它连接。
2. **客户端**：一个 Windows App（Win32 + WebView2 壳，页面资源随包），内部带 Python Agent 子进程；App 与 Agent 之间走命名管道，Agent 的业务数据只能经 App 的唯一 TCP 长连接获取；Key 只存客户端 DPAPI。
3. **仓库**：单一构建入口（根 `CMakeLists.txt`）、单一测试树（`tests/`）、单一文档集（`docs/`）；退役代码删除或归档，不进构建、不进 CI。

## 3. 五条不可放宽的约束

1. 服务端**零 LLM**：无模型域名出网、无 Key 读取、无代理/中转接口、无 `llm_proxy`/`llm_stream_*` 帧。
2. Redis **单实例、仅服务端连接、禁 pub/sub**；不新增第二套缓存或队列中间件。
3. 序列化**只有 JSON**（`shared/protocol` 一套实现，server/client/loadtest 同源）；禁 protobuf/MessagePack/CBOR。
4. 客户端**只有一条业务 TCP**；无 `:8000`/`:8080`/`:8787` 直连；模型出网不算业务连接且不承载业务数据。
5. 未实测不得宣称容量/性能；阶段门未过不得进入下一阶段。

## 4. 阶段划分与门禁

| 阶段 | 内容 | 门禁（全过才算完成） | 回滚 |
|---|---|---|---|
| **S0 基线修复** | 修 CMake/测试缺陷，Linux 上 ctest 全绿；补 `<netdb.h>`、测试工作目录、`OfflineQueue.cc`、`keyvault_test` 平台注册；修 `TcpServerApp` 编译错误 | `cmake -S . -B build && cmake --build build -j && ctest --output-on-failure` 全绿；无 `eat-server` 时也必须有明确 `message(STATUS)` | 回滚该批提交 |
| **S1 后端合并** | P8 `service/`+`dao/` 迁入新树去 httplib；P7 账号/好友语义迁入 Service；单进程注册全部 method 与握手；MySQLPool/CacheClient 接入；迁移 001→006 + oneoff 数据迁移 | 真实链路：注册→登录→握手→`profile.get`/`meal.create`/`stats.weekly`；P8 语义对照一致；未握手 `2001`、缺 `idem_key` `3001` 可复现 | 保留 P8 进程与旧表只读；回滚迁移脚本成对 |
| **S2 Windows App** | Win32+WebView2 壳、WebView2 消息桥、Agent 子进程+命名管道、单实例/托盘/离线、MSIX 签名 | 断网冷启动可用；仅 1 条业务 TCP；无 `:8787` 监听；Key 三路径；功能对等清单差异 0 | 灰度回滚上一签名包；Web 入口保留至 S4 |
| **S3 重构与去冗余** | 四技能整改：死码清除、小步重构、模式应用、风险复审 | 每步前后测试全绿；每步 ≤3 文件 / ≤2000 行（超出先确认）；删除清单经确认 | 按提交粒度回退 |
| **S4 集成与收尾** | 迁移演练、故障注入、bench_tcp 冒烟、文档收敛（AGENT.md/AGENT_Linux.md 合并）、Web 退场 | 证据落 `evidence/`；阻断项状态与证据一致；无"理论可达"充当结论 | 恢复静态页 + 上一版本包 |

## 5. 关键裁决（ADR 摘要）

| ID | 裁决 | 理由 | 详见 |
|---|---|---|---|
| ADR-1 | 以 ③ 为唯一服务端骨架，② 拆解为"业务语义 + SQL + 测试用例"输入 | ③ 已满足帧协议/缓存/身份设计，② 的 Reactor/HTTP 双栈与新约束冲突 | `01-backend-merge.md` |
| ADR-2 | 身份统一为 `user_key CHAR(26)`；P7 `id`、P8 `agent_uid` 仅作 `account_alias` 迁移源 | 避免三套 ID 并存；握手只认 `ConnContext.user_key` | `01` |
| ADR-3 | 餐记录/幂等直接演进 P8 表（改列名 + 分区），不建第二套同义表 | 数据只有一份真相；避免双写 | `01` |
| ADR-4 | P7 网络层/`ChatService`/P8 HTTP 语义层退役；业务规则逐条移植 | 传输形态不兼容，但字段/校验/默认值必须逐行对齐 | `01` |
| ADR-5 | Windows App = Win32 + WebView2（本地资源），不做远程首屏 | 页面 100% 复用现有前端，改动最小；符合 Win10 1809+ | `02-windows-app.md` |
| ADR-6 | 前端抽象 `Transport` 接口：浏览器实现 fetch、App 实现 WebView2 桥 | 让同一套页面同时服务开发调试与交付，避免一次性重写 | `02` |
| ADR-7 | Agent 保留 Python 能力本体，仅换入口（命名管道）；Key 只在客户端 | 规格 R6 明确；Agent 的 rag/mcp/skills/tools 已验证 | `02` |
| ADR-8 | 删除前先出清单、经确认；重构前先建测试基线 | 四技能的删除/重构纪律，防止误删与回归 | `03-refactor-cleanup.md` |

## 6. 交付物

- 代码：`server/`、`shared/protocol`、`client/`、`agent/`、`migrations/`、`deploy/`、`tests/`。
- 文档：本目录 5 份 + `docs/protocol/v1.md` + 合并后的 `docs/architecture.md`/`docs/handoff.md`。
- 证据：`evidence/<stage>/` 下原始命令输出、迁移前后行数/哈希、测试日志、抓包摘要（不含敏感内容）。
- 明确不交付：`chatroom-backend/` 的 TUI/WebSocket/Web 前端、`transit`、任何 `.env` 内容、任何真实 Key。

## 7. 当前阻断（必须先解决）

| 编号 | 阻断 | 解除条件 |
|---|---|---|
| B-1 | 本机无 Muduo，`eat-server` 不生成；且 `TcpServerApp` 存在两处编译错误 | 源码构建 Muduo + 修 include/命名空间 |
| B-2 | 服务端未注册任何 method、未接 TokenVerifier | S1 完成握手与最小业务 method |
| B-3 | 无 MySQL/Redis 客户端实现（根 CMake 只链 `Threads`/`crypto`） | 接入 MySQLPool + CacheClient |
| B-4 | Windows App 无法在 Linux 编译/验证（WebView2、DPAPI、MSIX） | 需 Windows 目标机或 Windows CI |
| B-5 | 100 万在线无任何实测 | 3 台压测机 + 32C/64G 被测机；未测前保持【阻断】 |