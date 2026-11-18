# 后续实现路线（ROADMAP）

> 更新时间：2026-11-18
> 状态：规划稿，按当前代码事实编写；本文只描述"还没做完的事"与验收口径，已完成能力见 `docs/architecture.md`、`AGENT.md`。
> 约束：不修改 C++ 服务端既有协议语义；Agent 仍只通过 `ServiceClient` 访问业务数据；MySQL/Redis 只允许服务端连接。

## 0. 总览

| 编号 | 事项 | 现状 | 目标形态 | 主要落点 |
|---|---|---|---|---|
| R1 | 食物菜单真实数据 | 仅模拟数据 | 按地域/学校/商圈实时获取并带来源与时效 | `agent/data/service_client.py`、`agent/tools/menu_tools.py`、`server/` |
| R2 | 模型切换与自定义 API | 服务端预置模型，用户只能选"饭饭" | 用户可选模型、可填自定义 OpenAI 兼容端点 | `agent/config/models.json`、`agent/llm/gateway.py`、`agent/server.py` |
| R3 | 独立 App 封装 | Web 端可用；Win32+WebView2 壳已编译，IPC 业务桥接未接完 | 双击即用的独立桌面软件，不依赖浏览器和 8787 端口 | `client/app/win32/`、`agent/ipc_server.py`、`bridge.js`、`packaging/` |
| R4 | 服务端高并发演进 | 单机 Muduo epoll + 线程池 | 多机分布式 + io_uring 可选 I/O 后端 | `server/`、`deploy/`、`migrations/` |

优先级建议：R2 → R1 → R3 → R4。R2 改动面最小且能立刻验证；R1 依赖外部数据源与合规确认；R3 依赖打包链路稳定；R4 需要压测基线与容量目标先落地。

---

## R1 食物菜单真实数据接入

### 现状

- Agent 侧菜单来自 `ServiceClient`：
  - 服务端可用时走 `food.menu`（TCP 帧）与 `food.search`。
  - 未配置服务端时回退 `MOCK_FOODS` 模拟数据（`agent/data/service_client.py:71`），共 16 道菜。
- 菜单相关工具已存在：`get_menu`、`import_menu`、`pick_random_food`、`update_spend_level`（`agent/tools/menu_tools.py:143`）。
- 痛点：数据是静态模拟/种子，没有"哪家店、哪个校区、几点供应、现在是否售罄、价格是否变化"这些实时维度。

### 目标

1. 菜单按**地域**组织：至少支持"学校内（校区/食堂/档口）"与"城市周边（商圈/街道）"两类范围。
2. 每条菜单带**来源、采集时间、有效期、可信度**，过期数据自动降级而不是继续冒充实时。
3. Agent 回答时能说明"数据来自哪里、什么时候更新"，前端能区分实时/快照/模拟三种来源。

### 实施要点

1. 数据模型（服务端 MySQL，新增迁移 `migrations/add/`）：
   - `menu_source`：来源类型（官方接口/商家合作/公开页面/用户上报）、授权状态、频控配置。
   - `menu_snapshot`：地域键（城市/校区/商圈）、餐次、抓取时间、有效期、版本号。
   - `menu_item`：快照 ID、菜名、价格、供应时段、售罄状态、图片、标签、过敏原。
   - 唯一键建议 `(snapshot_id, item_key)`；抓取幂等键 `(source_id, region_key, meal_type, fetch_day)`。
2. 采集层（服务端或独立采集进程，禁止 Agent 直连外部数据库）：
   - 优先接开放平台/校方接口；没有接口时只做**有授权**的页面采集，遵守 robots、频控与缓存。
   - 采集结果先写快照，再原子切换"当前有效快照"，避免用户读到半成品。
3. 接口层：在现有 40B 帧协议上扩展只读方法（例如 `food.menu` 增加 `region_key`/`lat`/`lng`/`radius`），保持旧字段兼容，旧客户端不带新参数时回退默认地域。
4. Agent 侧：
   - `ServiceClient.get_menu`/`search_foods` 透传地域参数并保留 `source` 字段（`service`/`snapshot`/`mock`）。
   - `get_menu` 工具描述补充"地域、时效、来源"语义，回答中必须带上快照时间。
   - 无实时数据时明确标注"当前为最近一次快照/模拟数据"，不得让模型表述成实时。
5. 前端：菜单卡展示来源标签与"更新于 HH:mm"；过期快照置灰并提示刷新。

### 验收标准

- `GET /api/menu?mode=work&region_key=<校区>` 返回的每道菜都有 `source`、`snapshot_at`、`stale=false|true`。
- 连续两次抓取同一来源，快照版本递增且不出现重复菜品（幂等键生效）。
- 断开外部数据源后：接口返回最近快照并标记 `stale=true`，不返回 500。
- 完全没有地域数据时回退 mock，且 `source=mock` 在响应与前端标签上可见。
- 过敏/忌口过滤仍然生效（复用 `search_foods` 现有过滤逻辑）。

### 风险与边界

- 合规：未获授权的平台数据不采集；不在仓库保存任何第三方密钥。
- 频控：同一来源 QPS 上限写入配置，失败指数退避。
- 数据质量：用户上报需去重与置信度评分，低置信度不进入推荐池。

---

## R2 用户切换模型与自定义 API

### 现状

- 模型清单由 `agent/config/models.json` 决定，`/api/models` 与 `/api/agent/meta` 对外暴露（`agent/server.py:168`、`agent/app.py:89`）。
- 当前产品口径是"对外只有一个可选模型「饭饭」，底层 `deepseek-flash`，`rule-fallback` 仅内部兜底"（`AGENT.md:158`）。
- `AgentRuntime` 已支持请求携带 `model_id`（`agent/core/runtime.py:100`），`LLMGateway` 已支持 OpenAI 兼容流式与本地规则降级（`agent/llm/gateway.py`）。
- 痛点：用户不能增删模型，也不能填自己的 base_url / Key；切换模型实际受服务端配置限制。

### 目标

1. 用户可在客户端选择模型（至少：内置模型 + 自己添加的模型）。
2. 用户可添加自定义 OpenAI 兼容端点（base_url + model 名 + Key），并在本地安全保存。
3. 切换后本轮请求真实走该模型；失败按既有策略降级并明确提示，不静默改用其他模型。

### 实施要点

1. 模型配置分层：
   - 内置层：`agent/config/models.json`，只读。
   - 用户层：SQLite 新表 `user_models`（user_id、display_name、provider、base_url、model、caps、context、enabled、created_at），**不存明文 Key**。
   - Key 存储：Windows 走 DPAPI/KeyVault（参考 `client/app` 现有 KeyVault 分支），Linux 开发环境走受限权限文件；Key 只在进程内解密使用。
2. 网关改造（`agent/llm/gateway.py`）：
   - `LLMGateway` 增加"按 model_id 解析 provider 配置"的入口，支持运行时注入用户配置。
   - 统一能力声明：`caps` 至少区分 `chat`/`tools`/`vision`；不支持的能力在发请求前拦截并提示。
   - base_url 校验：只允许 `http(s)`；禁止回环/内网地址（防 SSRF）；限制重定向次数与响应体大小。
   - 超时、重试、代理配置按模型独立设置，沿用 `EAT_LLM_PROXY` 语义。
3. 接口：
   - `GET /api/models` 合并内置与用户模型，返回 `available`、`caps`、`source`。
   - 新增用户模型 CRUD（创建/更新/删除/测试连通），写操作要求工作模式；删除时清理本地 Key。
   - `POST /api/models/test` 做一次最小 chat 探测，返回延迟与错误摘要，不返回 Key。
4. 前端：
   - 模型下拉区分"内置/我的模型"，提供"添加模型"表单与"测试连接"按钮。
   - `meta.model` 与实际使用模型不一致时（降级）必须显示提示，保持现有 `notice` 语义。
5. 降级策略：
   - 用户模型失败 → 明确提示失败原因 → 询问是否切回内置模型或 rule-fallback；默认行为沿用现有"自动降级 + 提示"，但需在 `done` 中带 `requested_model` 与 `used_model`。

### 验收标准

- 添加一个 OpenAI 兼容模型后，`/api/models` 可见且 `available=true`；发消息时 `done.used_model` 等于所选模型。
- 填错 Key：返回可读错误，`degraded=true` 且有 `notice`，不泄露 Key 片段。
- 自定义 base_url 指向 `127.0.0.1`/内网地址被拒绝，返回明确的校验错误。
- 删除用户模型后，列表不再出现，Key 文件/凭据条目被清除。
- 重启 Agent 后用户模型仍在（SQLite 持久化），Key 仍可解密（平台凭据存储可用时）。

### 风险与边界

- 安全：自定义端点属于用户自带凭据，禁止写入日志、trace、错误消息。
- 兼容：不同厂商 tool calling 格式有差异，MVP 只承诺 OpenAI 兼容子集；不兼容时明确报错。
- 口径：若产品仍要求"单一模型"，需先改 `AGENT.md` 的产品描述，避免文档与实现冲突。

---

## R3 独立 App 封装

### 现状

- 已有 Win32 + WebView2 壳：`client/app/win32/main.cc`，单实例、托盘、关窗进托盘、加载本地页面。
- 壳会拉起 Agent：`client/app/win32/main.cc:656` 执行 `eat-agent.exe --ipc`，通过命名管道通信。
- Agent IPC 服务已实现：`agent/ipc_server.py:128`，4 字节小端长度 + JSON、单帧 ≤1MiB；Windows 命名管道 DACL 限制当前用户，POSIX 用 `AF_UNIX`。
- 页面传输抽象已就绪：`bridge.js:161` 的 `window.eatTransport`，App 模式 `postMessage`，浏览器模式 `fetch + SSE`。
- 未完成（与 `docs/architecture.md:66-72` 一致）：WebView2 → IPC → Agent → TCP 的业务桥接未全部接线；安装包签名与自动更新未做；当前仍有 Web 端并行存在。

### 目标

1. 用户下载安装包后**双击即用**，不打开浏览器、不手动填地址、不占用 8787。
2. 未联网时可启动并给出离线规则回答；联网后自动连上服务端。
3. 关闭窗口进托盘，托盘可退出；单实例，重复启动只唤起已有窗口。

### 实施要点

1. 进程生命周期：
   - 壳启动顺序：创建命名管道客户端 → 拉起 `eat-agent.exe --ipc` → `hello` 握手 → 加载页面。
   - Agent 退出/崩溃：壳负责重启并重建连接；重启次数上限与退避写入配置。
   - 退出：托盘退出时先发 `shutdown`，超时后终止子进程，保证不留孤儿进程。
2. IPC 业务桥接：
   - `bridge.js` 的 `request/chatStream/login/captcha/logout` 全部映射到 IPC 消息；页面不得回退到 `fetch :8787`（除开发模式）。
   - `service.configure` 在登录/自动恢复后下发 host/port/user_key/access_token，Agent 侧走 `ServiceClient._ensure_tcp`。
   - 错误码统一：IPC 层只传结构化 `{code, message}`，页面不解析英文异常字符串。
3. 打包：
   - Agent 用 PyInstaller 产出单文件或目录形态 `eat-agent.exe`，冷启动优化：MCP 默认懒启动、知识库延迟加载、避免 onefile 解包黑框。
   - 壳 + WebView2 Runtime 检测：缺失时引导安装；页面资源随安装包落盘，禁止远程首屏。
   - 安装包（Inno Setup/MSIX）：桌面快捷方式、开始菜单、卸载清理管道与本地数据目录；代码签名待发布环境具备证书后执行。
4. 数据目录与升级：
   - 用户数据固定到 `%LOCALAPPDATA%\Eat\`，升级不覆盖；Agent SQLite 与技能目录版本化迁移。
   - 自动更新只做"检查 + 提示下载"，不在 MVP 阶段做静默热更新。
5. 保留 Web 端作为开发调试入口，但正式发布文档只讲 App 路径。

### 验收标准

- 干净 Windows 机器：安装 → 双击 → 出现登录页 → 登录 → 对话流式输出，全程不需要浏览器、不需要手动改配置。
- 断网启动：App 能打开，对话返回"离线规则回答"提示，不出现白屏或连接超时死等。
- 任务管理器只有一个 `eat-agent.exe`；关窗后进程仍在托盘，托盘退出后进程消失。
- 重复双击快捷方式不会产生第二个实例。
- 卸载后：安装目录、快捷方式、命名管道、开机自启项清理干净，用户数据按用户选择保留或删除。

### 风险与边界

- WebView2 Runtime 依赖需在安装包内检测，不能假设用户已装。
- 杀软误报：PyInstaller onefile 容易被误判，优先目录形态或签名。
- 不在 Linux 上承诺 GUI 产物；Linux 仅保留 Agent/IPC 的可测试路径。

---

## R4 服务端高并发演进

### 现状

- 服务端为 Muduo 主从 Reactor + 40B 帧 + JSON + MySQL + 通用线程池（`server/`），单机部署。
- Agent 侧并发模型：`ThreadingHTTPServer`（`agent/server.py:350`）+ `SuperAgent` WorkerPool 4 并发（`agent/core/super_agent.py:112`）+ 工具线程池 8 线程（`agent/tools/registry.py:19`）+ MCP 子进程隔离。
- 当前没有容量基线：百万在线压测在 `docs/architecture.md:103` 标记为阻断项。

### 目标

1. 先有可复现的压测基线与容量数字，再谈架构升级；没有数据的"高并发"改造不做。
2. 水平扩展：多机部署下会话、幂等、好友关系、排行榜结果一致。
3. I/O 后端可插拔：默认 epoll 稳定可用，io_uring 作为可选后端，按内核能力自动回退。

### 实施要点

1. 基线（前置条件）：
   - 压测工具与场景脚本入库（登录、profile.get/patch、meal.create/recent、food.menu、stats.*）。
   - 指标：QPS、P50/P99、错误率、连接数、MySQL 慢查询、内存曲线；明确目标值后再排期。
2. 分布式：
   - 接入层：四层负载均衡 + 连接数均衡；40B 帧协议保持无状态，避免粘性依赖。
   - 会话与令牌：登录态放服务端共享存储（Redis 仅服务端使用），令牌校验不依赖单机内存。
   - 幂等：`idem_key` 落 MySQL 唯一约束，跨机重复请求返回同一结果。
   - 数据层：先读写分离，再按 user_key 分片；好友/排行榜属跨分片查询，需单独设计聚合或异步物化。
   - 缓存：写后 DEL、负缓存、熔断降级；禁止 Redis 订阅发布（与既有约束一致）。
3. io_uring：
   - 在 Muduo 的 `Poller/Channel` 抽象上增加 `IoUringPoller` 实现，事件循环接口不变。
   - 启动探测内核版本与 `io_uring_setup` 可用性（含容器 seccomp 限制），不可用自动回退 epoll。
   - 先做读路径与 accept/read/write 批处理，再评估定时器与跨线程唤醒。
   - 保留 epoll 构建开关，CI 两套后端都要跑通协议与连接治理测试。
4. 观测：
   - `healthz`/`metrics` 回环暴露（QPS、延迟、连接、错误码分布），便于多机对比。
   - 日志带 `user_key` 前缀哈希，禁止明文令牌。

### 验收标准

- 压测报告可复现：同一脚本、同一机器规格、同一数据集，指标波动在约定范围内。
- 双机部署：同一用户跨机请求结果一致；重复 `meal.create` 只落一条记录。
- 杀掉任一节点：客户端在超时内重连成功，无数据错乱。
- io_uring 构建：内核支持时生效，不支持时日志明确回退 epoll，功能测试与 epoll 构建结果一致。
- 任何容量结论必须附带原始数据与测试环境说明。

### 风险与边界

- io_uring 在部分容器/旧内核不可用，不能作为唯一路径。
- 分布式会放大一致性问题：先做幂等与只读缓存，再碰跨分片聚合。
- 不引入 Redis 发布订阅、不新增 Redis 实例；Agent 仍不直连 Redis/MySQL。

---

## 建议里程碑

| 里程碑 | 内容 | 退出条件 |
|---|---|---|
| M1 | R2 用户模型与自定义端点 | 添加/切换/删除/测试四项接口与前端可用，并有失败降级证据 |
| M2 | R1 菜单快照链路 | 至少一个真实来源跑通抓取→快照→接口→前端标签全链路 |
| M3 | R3 App 闭环 | 干净 Windows 机器双击安装即可完成登录与对话 |
| M4 | R4 基线 + 分布式 | 压测报告落库，双机部署通过一致性用例 |
| M5 | R4 io_uring | 可选后端构建通过，自动回退验证完成 |

## 非目标

- 不做医疗诊断、用药建议、化验单解读。
- 不在仓库保存第三方 API Key、服务端令牌、数据库密码。
- 不为了"看起来高并发"而提前引入消息队列、微服务拆分；先有指标再决定。
- 不在 Linux 上交付 GUI 客户端。

## 与其他文档的关系

- 当前架构事实：`docs/architecture.md`
- 交付物与验证证据：`docs/DELIVERY-STATUS.md`
- 运行与排查：`docs/RUNBOOK.md`
- 协议字段与错误码：`docs/protocol/v1.md`

本文每完成一项，应同步更新对应实现文档与验收证据，避免再次出现"文档说未完成、代码已完成"或相反的情况。
