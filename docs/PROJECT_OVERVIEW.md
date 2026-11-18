# 今天你吃了吗（Eat App）· 项目详细介绍

> 文档版本：2026-11-18
> 对应分支：`feat/app-transport-scale`
> 对应提交：`23c9a97`（Release `v0.3.0` 之后仅增加文档提交）
> 阅读顺序建议：本文 → [`README.md`](../README.md) → [`DEV_PROCESS.md`](DEV_PROCESS.md) → [`architecture.md`](architecture.md)

---

## 1. 项目是什么

「今天你吃了吗」是一个围绕日常饮食决策与健康记录的跨端应用。它要解决的不是「查一道菜的做法」，而是三个连续的问题：

1. **今天吃什么**：结合身体状态、口味、过敏原、预算和当前时段给出可执行的建议；
2. **吃了什么、吃得怎么样**：记录三餐，计算蛋白质、膳食纤维、蔬菜、饮水量与健康分；
3. **长期怎么调整**：按周看营养缺口，和好友比较坚持程度，形成可反馈的习惯。

项目不是单一 Web 页面，而是三端协作：

| 端 | 形态 | 职责 |
|---|---|---|
| **Windows 客户端** | Win32 + WebView2 桌面壳，页面资源随包 | 用户唯一入口：窗口、托盘、单实例、登录态、本地页面渲染 |
| **Python Agent** | 本地进程（开发期 `:8787`，App 内命名管道） | 「饭饭」人格、LLM 对话、RAG、MCP、Skill、工具编排、业务方法调用 |
| **Linux 服务端** | C++17 + Muduo 主从 Reactor，单进程 `eat-server` | 账号、会话、档案、餐记录、统计、菜单、好友、排行；唯一能访问 MySQL / Redis 的角色 |

设计上刻意保持一条边界：**服务端零 LLM**。模型 Key 只在客户端侧保管，LLM 流量不经过服务端；服务端只处理确定性的业务数据。

---

## 2. 产品定位与目标用户

### 2.1 目标用户

- 想控制体重、改善饮食结构，但不想学习复杂营养学的人；
- 上班族、学生等「选择困难 + 时间碎片化」人群；
- 需要记录过敏原、忌口、预算区间，希望建议能落地的用户；
- 愿意用对话方式获得建议，而不是填一堆表单的人。

### 2.2 产品原则

1. **先能用，再智能**：断网、无模型 Key 时，Agent 仍以本地规则引擎运行，菜单、记录、统计不受影响。
2. **建议必须可执行**：给出菜名、分量、时段与预算，而不是「多吃蔬菜」这类空话。
3. **数据归用户**：客户端可导出/清理本地会话；服务端只存业务必需字段。
4. **能力边界说清楚**：未在目标平台验证的功能不宣称完成；未压测的容量不宣称达标。

---

## 3. 核心使用场景

### 场景 A：首次进入

用户双击桌面快捷方式，看到「饭饭」的自我介绍。首次流程引导填写身高、体重、年龄、性别、目标、活动量、过敏原、忌口、预算区间。表单提交走 `profile.patch`，数据落到 MySQL 的 `health_profile`。

### 场景 B：今天吃什么

用户在对话框问「中午吃什么，20 块左右，不要辣」。Agent 解析意图与约束，调用 `food.menu` / `food.search` 获取服务端真实菜单，再结合档案中的过敏原、预算与营养缺口排序，输出推荐并说明理由。

### 场景 C：记录一餐

用户说「午饭吃了鸡胸肉沙拉和一杯无糖茶」。Agent 抽取食物项、估算营养，调用 `meal.create` 写入服务端；写方法带 `idem_key`，重复提交不会产生重复记录。页面上的手动记录入口走同一套方法。

### 场景 D：看今天/本周表现

`stats.nutrition_gap` 返回当日蛋白质、纤维、蔬菜、饮水的缺口；`stats.weekly` 返回按周聚合的趋势。页面用这些数据画健康分与周报。

### 场景 E：好友与排行

`friend.request` / `friend.accept` / `friend.list` 维护双向好友关系；`ranking.friends` 按坚持度或健康分排序，仅返回好友范围内的数据。

### 场景 F：断网或没有模型 Key

Agent 检测到 `DEEPSEEK_API_KEY` 缺失或模型不可达，降级到 `rule-fallback` 本地规则引擎。对话能力变弱，但菜单、记录、统计、档案仍可用；页面会明确显示当前模式。

---

## 4. 功能清单与实现状态

| 功能域 | 能力 | 协议方法 / 入口 | 状态 |
|---|---|---|---|
| 账号 | 注册、登录、刷新、登出、改密、撤销全部会话 | 握手、`auth.*` | 已实现，Linux 实测 |
| 会话 | 设备顶替、3 设备上限、令牌哈希存储 | `session` 表 + `SessionRegistry` | 已实现，单测覆盖 |
| 档案 | 读取、字段白名单更新、BMI 现算、幂等 | `profile.get` / `profile.patch` | 已实现，真实 TCP 实测 |
| 餐记录 | 新增、最近列表、修改、删除 | `meal.create/recent/patch/delete` | 已实现，真实 TCP 实测 |
| 统计 | 当日营养缺口、周报 | `stats.nutrition_gap` / `stats.weekly` | 已实现，真实 TCP 实测 |
| 菜单 | 按时段取菜单、搜索 | `food.menu` / `food.search` | 已实现，真实 TCP 实测 |
| 好友 | 列表、申请、接受、删除、拉黑 | `friend.list/request/accept/remove/block` | 已实现，真实 TCP 实测 |
| 排行 | 好友范围排行 | `ranking.friends` | 已实现，真实 TCP 实测 |
| 缓存 | Cache-Aside、负缓存、TTL 抖动、单飞、写后 DEL、熔断降级 | `server/cache/*` | 已接线；写后失效、停机降级、恢复回填已实测 |
| AI 对话 | 「饭饭」人格、意图路由、流式输出 | Agent `/api/chat/stream` | 已实现；真实模型需 `DEEPSEEK_API_KEY` |
| RAG | 本地知识库检索、哈希嵌入 | `agent/rag/*` | 已实现，SQLite 存储 |
| MCP | 外部工具进程管理 | `agent/config/mcp_servers.json` | 已实现，可配置 |
| Skill | 技能加载、市场、导入 | `agent/skills/*` | 已实现 |
| 工具编排 | 读写工具、权限分级、确认 | `agent/tools/*`、`agent/core/*` | 已实现，51 项单测 |
| Windows GUI | 安装、托盘、单实例、本地页面、会话持久化 | `client/app/win32` | Release `v0.3.0` 可用 |
| 单 exe | 资源嵌入 + Agent 内置 | — | **未完成** |
| 100 万在线 | 压测上限 | — | **阻断，无实测数据** |

---

## 5. 总体架构

```text
┌──────────────────────────── Windows 10/11 ────────────────────────────┐
│  Eat App（Win32 + WebView2，单实例，托盘）                            │
│    ├─ web/ 本地页面：index.html / app.js / bridge.js / CSS / assets   │
│    ├─ bridge.js：window.eatTransport                                   │
│    │    ├─ 浏览器模式：fetch + SSE → 127.0.0.1:8787                    │
│    │    └─ App 模式：postMessage → 壳                                  │
│    └─ 壳：会话、登录、验证码、业务请求转发                             │
│         └─ 命名管道（4B LE + JSON）── eat-agent.exe                    │
│              ├─ LLM 直连（HTTPS/SSE，Key 只在客户端）                  │
│              ├─ RAG / MCP / Skill / 工具编排                           │
│              └─ ServiceClient ── 40B TCP 帧 ──┐                        │
└───────────────────────────────────────────────┼───────────────────────┘
                                                │ 明文 TCP :9443（可信局域网）
┌───────────────────────────────────────────────▼───────────────────────┐
│  eat-server（Linux 单机，C++17 + Muduo）                              │
│    ├─ 主 Reactor：accept、连接管理、定时器                            │
│    ├─ 16 个 sub Reactor：读事件、分帧、CRC 校验                       │
│    ├─ BusinessExecutor：有界线程池，JSON → Service → JSON             │
│    ├─ ServiceRegistry：握手、认证、15 个业务方法                      │
│    ├─ MySQLPool：连接池 + 超时 + 事务                                 │
│    ├─ RedisClient：RESP 白名单，Cache-Aside                           │
│    └─ 回环 admin：healthz / metrics（规划中）                         │
│         ├── MySQL 8.4：账号、会话、档案、餐记录、好友、幂等账本       │
│         └── Redis 7：缓存（单实例，仅服务端连接，禁 pub/sub）         │
└───────────────────────────────────────────────────────────────────────┘
```

### 5.1 三条运行路径

| 路径 | 传输 | 适用 |
|---|---|---|
| 浏览器开发 | 页面 → `fetch/SSE` → Agent `:8787` → Agent TCP → 服务端 | 前端快速调试 |
| Windows GUI | 页面 → `postMessage` → 壳 → 命名管道 → Agent → TCP → 服务端 | 最终用户 |
| 控制台验收 | `client_smoke.exe` → TCP → 服务端 | 协议与业务回归，不依赖 GUI |

GUI 不监听任何 HTTP 端口，因此不会与浏览器调试用的 `:8787` 冲突。

---

## 6. 服务端设计

### 6.1 进程与线程模型

- **主 Reactor**：负责 `accept`、连接建立/关闭、全局 1s 定时巡检（心跳、背压、连接上限）。
- **sub Reactor**：默认 16 个，负责已建立连接的读写事件。每个连接归属固定 sub loop，避免跨线程操作。
- **业务线程池**：`ThreadPool` + `BusinessExecutor` 门面。有界队列（默认 8192）、非阻塞提交、队列满快速拒绝、任务 deadline、异常隔离、`Stop` 丢弃计数。
- **数据库池**：`MySQLPool` 默认 32、上限 64，借出超时 200ms，语句超时 2500ms。
- **Redis 客户端**：自研最小 RESP 客户端 + 有界连接池，默认 8 连接、20ms IO 超时；只允许 GET / SETEX / DEL / MGET / EXISTS / PING。

为什么单独引入线程池：Muduo 的 IO 线程不应执行阻塞 SQL。把业务放到有界池后，慢查询只会耗尽业务池并触发 `4002 overloaded`，不会拖垮网络层。该实现参考了用户本机的 `lab_threadPool` 练习代码。

### 6.2 帧协议

- 定长 40 字节大端头 + JSON body（UTF-8，无 BOM）。
- magic `EAT1`，版本 1.0，帧类型 0x01–0x09。
- CRC32C 覆盖 `header[0..35] || body`，不包含 checksum 字段自身。
- 默认最大帧 64 KiB，硬顶 256 KiB；超长帧在读头阶段直接拒绝，不分配大缓冲。
- `request_id` 由客户端单调递增，服务端原样回带；`seq` 用于推送/事件。
- 粘包、半包、逐字节拆包由 `FrameCodec` 处理；一帧未读满不会解析下一帧。

帧类型：

| 值 | 类型 | 说明 |
|---|---|---|
| 0x01 | HandshakeReq | 握手请求 |
| 0x02 | HandshakeResp | 握手响应 |
| 0x03 | Request | 业务请求 |
| 0x04 | Response | 业务响应 |
| 0x05 | Heartbeat | 心跳 |
| 0x06 | HeartbeatAck | 心跳应答 |
| 0x07 | Push | 服务端推送/事件 |
| 0x08 | Close | 关闭 |
| 0x09 | Error | 协议错误 |

握手支持三条路径：`access_token` 校验、`login_id/password` 登录、`action=register` 注册后登录。成功后返回 `user_key`、`session_id`、tokens、心跳参数与能力位。

### 6.3 业务方法

共 15 个业务方法 + 认证方法：

```text
auth.register / auth.login / auth.refresh / auth.logout
auth.revoke_all / auth.change_password

profile.get / profile.patch

meal.create / meal.recent / meal.patch / meal.delete
stats.nutrition_gap / stats.weekly
food.menu / food.search
friend.list / friend.request / friend.accept / friend.remove / friend.block
ranking.friends
sync.pull（预留）
```

写方法强制要求 `idem_key`：`auth.register`、`meal.create/patch/delete`、`profile.patch`、`friend.request/accept/remove/block`。缺失返回 `3001 invalid_params`。

### 6.4 错误码分层

| 区间 | 含义 | 示例 |
|---|---|---|
| 1001–1009 | 帧/协议错误 | `1007 checksum_failed`、`1008 invalid_json` |
| 2001–2008 | 身份/会话 | `2001 handshake_required`、`2002 unauthorized`、`2003 token_expired` |
| 3001–3005 | 参数/业务 | `3001 invalid_params`、`3003 idempotency_conflict` |
| 4001–4005 | 限流/过载 | `4001 rate_limited`、`4002 overloaded` |
| 5001–5004 | 服务依赖 | `5001 service_unavailable`、`5002 cache_unavailable` |
| 9001–9004 | 关闭/超时 | `9001 server_shutdown`、`9003 idle_timeout` |

### 6.5 连接治理

- 心跳：默认 60s，空闲 165s 判定超时；心跳帧不落库、不查 MySQL/Redis。
- 限流：每连接令牌桶（60/s，突发 120）、每用户 120/s、全局 5000/s、握手每 IP 5/s。
- 背压：每连接发送水位 256 KiB / 128 KiB，全局 512 MiB；慢消费者超时 30s。
- 最大连接：配置默认 100 万，但这是**压测上限**，没有实测数据。

### 6.6 配置

配置采用 TOML 风格键值，示例见 `deploy/server.toml.example`。所有敏感项支持环境变量覆盖：

```text
EAT_MYSQL_HOST / EAT_MYSQL_PORT / EAT_MYSQL_DB / EAT_MYSQL_USER
EAT_MYSQL_PASSWORD / EAT_MYSQL_SOCKET
EAT_REDIS_HOST / EAT_REDIS_PORT / EAT_REDIS_PASSWORD
EAT_TCP_PORT
```

密码只从环境变量或本地被忽略的配置文件读取，**不得写入仓库、日志、命令行历史**。

---

## 7. 数据模型

### 7.1 表清单

| 表 | 作用 | 关键约束 |
|---|---|---|
| `account` | 统一账号 | `user_key` 唯一；密码 scrypt 哈希；状态、失败计数 |
| `account_alias` | 旧身份迁移映射 | P7 `id` / P8 `agent_uid` → `user_key` |
| `session` | 登录会话 | 设备顶替、3 设备上限、令牌只存 SHA-256 |
| `health_profile` | 健康档案 | 按 `user_key` 一行；字段白名单更新 |
| `meal_records` | 餐记录 | 按 `created_day` 月分区；`(id, created_day)` 主键 |
| `idempotency_records` | 幂等账本 | `(id, created_day)` 主键；同日幂等、跨日可复用 |
| `foods` | 菜谱种子 | utf8mb4；按时段、价格、标签 |
| `friend_relation` | 双向好友关系 | 状态机：申请、接受、删除、拉黑 |
| `event_outbox` | 事件外发队列 | 会话替换、好友事件等 |
| `schema_migration` | 迁移记录 | 防止重复执行 |

### 7.2 分区与迁移注意

- 餐记录与幂等账本按月 `RANGE` 分区，初始 25 个分区。
- MySQL 要求分区表的所有唯一键包含分区列，因此主键为 `(id, created_day)`；这是设计评审后修正的点（详见 `DEV_PROCESS.md` T-01）。
- 迁移文件包含 CRLF 与 `--注释` 缺空格的历史格式，直接导入会触发 1064；打包脚本会先 `tr -d '\r'`、再给 `--` 补空格。
- 导入必须显式 `--default-character-set=utf8mb4`，否则中文菜名会被二次编码。

---

## 8. 缓存设计（Cache-Aside）

```text
读：key → Redis GET
      ├─ 命中 → 返回
      ├─ 未命中 → SingleFlight 查 MySQL → 回写 Redis（含负缓存短 TTL）→ 返回
      └─ Redis 错误/熔断 → 直接查 MySQL（fail-open）

写：MySQL 提交成功 → 同步 DEL 相关键（可多键）→ 返回
```

- **只删除、不更新**：避免并发写把旧值写回缓存。
- **负缓存**：空结果写短 TTL，防穿透；新增数据时联动失效。
- **TTL 抖动**：避免同一时刻批量过期造成雪崩。
- **单飞**：同一键的并发重建只放行一个请求。
- **熔断**：连续 3 次失败打开 5s，期间直接走 MySQL；恢复后自动回填。
- **Redis 约束**：单实例、仅服务端连接、禁 pub/sub、禁 SCAN/KEYS、禁第二套缓存中间件。
- **序列化**：JSON；键由 `CacheKeyBuilder` 规范化，避免大小写/空白导致命中率下降。

---

## 9. 身份与安全

### 9.1 服务端

- 密码使用 `scrypt`（OpenSSL `EVP_PBE_scrypt`），参数与盐随哈希存储，支持参数升级后重新哈希。
- 访问令牌与刷新令牌只存 SHA-256 摘要，不存明文。
- 登录失败 5 次锁定 5 分钟；刷新令牌复用返回 `2008 refresh_reused`。
- 设备顶替与 3 设备上限由 `SessionRegistry` 管理。
- 图形验证码由 `CaptchaStore` 生成 SVG，默认 `captcha_required = true`，TTL 120s。
- 服务端不读取模型 Key，不代理 LLM，不承担任何模型出网流量。

### 9.2 客户端

- 会话写入 `%LOCALAPPDATA%\EatApp\session.json`，包含服务端地址与令牌。
- Agent Key 只存客户端本地；Windows 侧通过 DPAPI 封装（`client/keyvault`）。
- 页面不接触明文 Key；Key 不进 TCP 帧、不进日志、不进崩溃转储。
- `login.json`（批量部署用）被 `.gitignore` 忽略，不入库。

### 9.3 传输

当前业务链路是**明文 TCP**，只适合可信局域网。公网部署前必须加 TLS/SPKI；在此之前不要把 9443 直接暴露到互联网。

---

## 10. Agent 设计

### 10.1 内部模块

| 模块 | 职责 |
|---|---|
| `agent/app.py` | 应用装配：模型、工具、技能、RAG、MCP、运行时 |
| `agent/core/` | 上下文、记忆、规划、提示词、安全、运行时、超级 Agent、追踪 |
| `agent/llm/` | OpenAI 兼容网关；支持流式、工具调用、视觉输入；无 Key 时降级 |
| `agent/rag/` | 知识库检索、哈希嵌入、SQLite 存储 |
| `agent/mcp/` | MCP 客户端、进程管理、协议实现 |
| `agent/skills/` | 技能加载、注册、市场 |
| `agent/tools/` | 饮食、菜单、报告、系统工具；读写分级 |
| `agent/data/` | SQLite 本地库、`ServiceClient`（TCP 业务通道） |
| `agent/transport/` | 40B 帧编码/解码，与 C++ 协议同源 |
| `agent/server.py` | 开发期 HTTP + SSE 服务（`:8787`） |
| `agent/ipc_server.py` | App 内命名管道服务（4B LE + JSON，≤1 MiB） |

### 10.2 人格与交互

Agent 的固定人设是「饭饭」：首次进入时自我介绍，语气温和引导；主动引导用户完成身高、体重、健康状况等设置。人格与规则集中在 `agent/core/prompts.py`，不在业务代码里拼接字符串。

### 10.3 双模式

| 模式 | 工具权限 | 适用 |
|---|---|---|
| `qa` 问答模式 | 只读，最多 4 步工具 | 咨询、解释、查询 |
| `work` 工作模式 | 读写 + 本地工具 + Skill 安装，最多 6 步 | 配餐、记录、导入技能 |

写操作必须经用户确认；`/api/tools/call` 的 `confirmed` 按严格布尔解析，`"false"` 不得被当作 true。

### 10.4 降级策略

1. 没有 `DEEPSEEK_API_KEY` → 使用 `rule-fallback` 本地规则引擎。
2. 模型超时/不可达 → 有界重试后降级，页面显示当前模式。
3. 服务端不可达 → 明确返回 `source=service_error`，不以假数据冒充真实数据。
4. `EAT_SERVICE_REQUIRED=1` 时禁止回退 mock，便于验收。

---

## 11. 客户端设计

### 11.1 三种形态

| 形态 | 产物 | 说明 |
|---|---|---|
| Web | `dist/eat-web/` 静态页 | 零安装验收，浏览器访问 `:8000` |
| GUI 安装包 | `EatAppSetup-x64.exe` | 桌面快捷方式，最终用户形态 |
| 源码构建 | `eat-app.exe` + `eat-agent.exe` | 可改 UI、可重新打包 |

### 11.2 壳的职责

- 单实例：重复启动唤起已有窗口。
- 托盘：关窗不退出，右键「退出」才结束进程。
- 本地页面：`file://` 加载安装目录 `web/`，禁止远程首屏。
- 登录：接收页面 `service.login`，完成 TCP 握手，写入会话文件，再通过 IPC 通知 Agent。
- 验证码：接收 `service.captcha`，把 SVG 返回页面。
- 业务转发：页面 `service.request` → 壳 → TCP；Agent 的 `service.request` 走同一套方法。

### 11.3 前端

- 纯原生 HTML/CSS/JS，无框架、无 npm、无 CDN、无外部字体。
- `bridge.js` 暴露 `window.eatTransport`，页面不直接碰端口。
- App 模式用 `postMessage`，浏览器模式用 `fetch + SSE`，同一套 DOM/CSS/JS。
- 视觉规范集中在 `styles.css` / `features.css` / `agent.css` / `redesign.css` / `login.css`。
- 页面资源在仓库根目录，构建时复制到安装目录 `web/`，浏览器开发不需要构建。

---

## 12. 部署拓扑

### 12.1 最小联调环境

```text
一台 Linux（服务端 + MySQL + Redis + 静态页）
        ↑ 局域网
一台 Windows（浏览器或 GUI）
```

端口：

| 端口 | 进程 | 绑定 | 用途 |
|---|---|---|---|
| 9443 | eat-server | 0.0.0.0 | 业务 TCP |
| 8787 | Agent（开发期） | 0.0.0.0 | 页面 HTTP/SSE |
| 8000 | 静态页 | 0.0.0.0 | Web 验收 |
| 3306 | MySQL | 127.0.0.1 或容器网络 | 仅服务端 |
| 6379 | Redis | 127.0.0.1 | 仅服务端 |

### 12.2 交付形态

| 产物 | 内容 | 验证状态 |
|---|---|---|
| `eat-server-linux-x86_64.tar.gz` | 服务端二进制、`client_smoke`、`run.sh`、全量迁移 | 解包后真实请求通过 |
| `eat-client-windows.zip` | 客户端源码、构建脚本、Agent、页面 | Windows 上构建 `client_smoke.exe` |
| `EatAppSetup-x64.exe` | 安装包、桌面快捷方式 | Release `v0.3.0` |
| Docker 镜像 | 多阶段构建、自动迁移 | 镜像构建与容器内真实请求通过 |

---

## 13. 仓库结构

```text
shared/protocol/        帧协议、CRC32C、严格 JSON（三端同源）
server/
  app/                  Config、Log、Metrics、ThreadPool、BusinessExecutor、ServiceRegistry
  tcp/                  TcpServerApp、ConnContext、FrameDispatcher、心跳、限流、背压
  identity/             PasswordHasher、TokenVerifier、SessionRegistry、CaptchaStore
  business/             profile、meal、stats、food、friend、ranking、user
  database/             MySQLPool、DbConn、RedisClient
  cache/                CacheKeyBuilder、CacheValue、TtlPolicy、SingleFlight、InvalidationSet
client/
  app/win32/            Win32 + WebView2 壳
  transport/            TcpTransport、OfflineQueue
  ipc/                  IpcProtocol（4B LE + JSON）
  keyvault/             Windows DPAPI
agent/                  Python Agent（HTTP、IPC、TCP、LLM、RAG、MCP、Skill、工具）
migrations/base/        000_init、001_p8_business、foods_seed
migrations/add/         002–006 增量迁移与回滚
deploy/                 Docker、systemd、sysctl、preflight、配置模板
tests/                  protocol、unit、integration、load
docs/                   架构、协议、运行手册、设计、交接文档
scripts/                构建、打包、安装依赖
packaging/              服务端/客户端发布说明、Inno Setup 脚本
```

代码规模（不含生成物）：

| 模块 | 行数（近似） |
|---|---|
| `server/` | 7,200+ |
| `shared/` | 1,100+ |
| `client/` | 1,800+ |
| `agent/` | 9,900+ |
| `tests/` | 1,900+ |
| 前端 JS/CSS/HTML | 4,700+ |

---

## 14. 版本与状态

| 版本 | 内容 |
|---|---|
| `v0.1.0` | 协议、连接治理、缓存/身份基础、迁移、客户端骨架 |
| `v0.2.0` | 业务 TCP 恢复、Redis Cache-Aside、Agent 数据通道 |
| `v0.3.0` | 验证码登录、Windows 安装包、登录流程重设计、真实数据引导 |

当前状态（2026-11-18）：

- Linux 服务端常驻 `10.112.151.15`，9443 / 8787 / 8000 可用；
- `ctest` 12/12、Agent 单测 51/51、IPC smoke、S1 集成脚本通过；
- 15 个业务方法经真实 TCP + MySQL + Redis 验证；
- Windows GUI 业务链路经页面 → 壳 → Agent → TCP，已在队友机器上跑通注册、登录、档案、菜单、记录、统计、好友、排行；
- **未完成**：单 exe、TLS、公网部署、100 万在线压测；
- **未在 Windows 验证**：MSIX 签名、断网冷启动、多用户并发。

---

## 15. 能力边界（必须显式声明）

1. **100 万同时在线是压测上限，不是承诺**。当前没有任何压测数据，不得对外宣称达标。
2. **明文 TCP**。只适合可信局域网；公网必须先接 TLS。
3. **Windows 侧产物需在 Windows 构建**。Linux 构建机不能产出 `eat-app.exe` / `client_smoke.exe`。
4. **页面「点选记录」与「删除/修改记录」的历史入口**不写服务端，不能用它们判断服务端故障；以协议方法调用为准。
5. **无模型 Key 时对话降级到规则引擎**，能力变弱但业务数据仍真实。
6. **Redis 不可用时缓存 fail-open**，业务继续但性能下降，不应误判为数据错误。
7. **未实现多机集群/跨机容灾/零停机发布**；当前是 Linux 单机。

---

## 16. 术语表

| 术语 | 含义 |
|---|---|
| **饭饭** | Agent 的固定人格名称 |
| **user_key** | 统一用户标识（26 位 ULID） |
| **idem_key** | 幂等键，写方法必填 |
| **Cache-Aside** | 旁路缓存：读先查缓存，写后删除缓存 |
| **负缓存** | 对空结果写短 TTL，防止缓存穿透 |
| **单飞（SingleFlight）** | 同一键并发重建只放行一个请求 |
| **40B 帧** | 固定 40 字节头 + JSON body 的应用层协议 |
| **sub Reactor** | Muduo 从事件循环，负责已连接 socket 的读写 |
| **BusinessExecutor** | 业务线程池门面，隔离阻塞 SQL 与 IO 线程 |
| **IPC** | App 与 Agent 之间的命名管道，4B LE 长度 + JSON |
| **eatTransport** | 前端传输抽象，屏蔽浏览器/App 两种环境 |
| **v0.3.0** | 当前 Windows 安装包对应的 Release |

---

## 17. 相关文档

| 文档 | 内容 |
|---|---|
| [`README.md`](../README.md) | 依赖、一键安装、部署运行、开源组件、码道应用总结 |
| [`DEV_PROCESS.md`](DEV_PROCESS.md) | 代码生成、重构、排错过程记录 |
| [`architecture.md`](architecture.md) | 合并后架构事实 |
| [`protocol/v1.md`](protocol/v1.md) | 帧协议规范 |
| [`RUNBOOK.md`](RUNBOOK.md) | 服务端/客户端运行手册 |
| [`WINDOWS_CLIENT_TEST_HANDOFF.md`](WINDOWS_CLIENT_TEST_HANDOFF.md) | Windows 测试交接 |
| [`design/00-master-plan.md`](design/00-master-plan.md) | S0–S4 总方案 |
| [`ROUTE-A-HANDOFF.md`](ROUTE-A-HANDOFF.md) | 路线 A 实施交接 |
