# 码道（CodeArts）在“泛饭”项目中的运用场景总结

> 文档版本：2026-11-18
> 对应仓库：`FRiver-hahaha/what-do-you-want-to-eat-`，分支 `feat/app-transport-scale`，本稿对应 HEAD `78bd815`
> 证据来源：Git 提交历史、`docs/DEV_PROCESS.md`、`docs/DELIVERY-STATUS.md`、`.codeartsdoer/specs/`、`tests/`、`README.md`
> 口径：只写项目里真实发生过的用法；“已验证”和“未验证”分开写，不把“代码写完”当成“功能可用”。

---

## 0. 一句话结论

在这个项目里，码道不是“代码补全器”，而是**从需求裁决到可验证交付的工程副驾驶**：它参与规格编写、架构收敛、跨端实现、安全审查、故障定位、打包部署、文档交接和经验沉淀。它最大的价值不是“写得多”，而是把 C++、Python、JavaScript 三类代码约束在同一套架构规则下，并把“看起来能跑”持续压到“真实请求跑通”。

---

## 1. 项目速览：为什么这个项目适合这样用码道

“泛饭”是一个面向日常饮食的多端应用：PC 客户端里有 AI 饮食助手“饭饭”，负责建档、配餐、记录、营养缺口和好友排行榜。

| 维度 | 实际情况 |
|---|---|
| 端 | Windows 客户端（Win32 + WebView2）、Web 端（原生 HTML/CSS/JS）、Python Agent、C++ 服务端 |
| 语言 | C++17、Python 3.11+、JavaScript |
| 服务端 | `eat-server`，Muduo epoll 主从 Reactor + 业务线程池 + 自定义 TCP 帧 |
| 数据 | MySQL 8.x 为权威数据，Redis 仅服务端缓存 |
| 协议 | 40 字节帧头 + CRC32C + 严格 JSON，方法白名单与错误码 |
| 复杂度 | 协议编解码、连接治理、身份鉴权、幂等、缓存降级、IPC、跨端传输、打包安装 |
| 约束 | 客户端不直连数据库/Redis；Agent 只经 TCP 访问业务；Windows 产物只能在 Windows 生成 |

单人或小团队要在这种跨度上推进，最容易出现三类问题：协议两端不一致、文档与运行态漂移、功能“写完了但没在目标环境跑过”。码道的使用场景基本都围绕这三类问题展开。

---

## 2. 码道参与的整体链路

```text
需求审查/可行性裁决
        ↓
规格 spec（可验收的“必须满足什么”）
        ↓
设计 design（架构、协议、数据、部署）
        ↓
任务 tasks（阶段、依赖、门禁）
        ↓
代码生成 → 重构 → 代码审查 → 死代码清理
        ↓
单元/集成/负向/真实局域网验证
        ↓
排错（日志、请求、配置、产物四类证据）
        ↓
打包、部署、交接文档
        ↓
经验与记忆沉淀（下一轮会话直接复用）
```

这条链路在本项目不是口号，而是有产物的：

- `.codeartsdoer/specs/app_transport_scale/spec.md`（3156 行）、`design.md`（1833 行）、`tasks.md`（483 行）；
- `.codeartsdoer/specs/p8_backend_check/spec.md`（863 行，用于审查旧 P8 后端）；
- `docs/DEV_PROCESS.md` 按 `G-xx`（生成）、`R-xx`（重构）、`T-xx`（排错）记录了 9 + 9 + 22 条真实过程；
- `docs/DELIVERY-STATUS.md` 把“已完成 / 未验证 / 阻断 / 待确认的破坏性操作”分开列出。

---

## 3. 场景清单（按能力归类）

| # | 运用场景 | 在项目中的实际做法 | 代表产出或证据 |
|---|---|---|---|
| 1 | 需求审查与可行性裁决 | 审查 PR #2 的 P7/P8 后端：功能有证据，但“百万并发”不成立，结论是不合并 | `p8_backend_check/spec.md`、`DEV_PROCESS.md` 时间线 |
| 2 | 规格驱动开发 | 先把口径写成可验收的 spec，再写 design，再拆 tasks；上一阶段验证不通过不进入下一阶段 | `app_transport_scale/` 三件套、S0–S3 阶段门 |
| 3 | 架构收敛 | 把三套并存后端收敛为单一 TCP 骨架，旧代码只保留业务语义 | `server/`、`shared/protocol/`、删除 `chatroom-backend/` |
| 4 | 底层基础设施生成 | 生成帧编解码、CRC32C、严格 JSON、连接状态机、心跳最小堆、令牌桶、背压、线程池、Cache-Aside | `shared/protocol/`、`server/tcp/`、`server/cache/`、`server/app/` |
| 5 | 业务迁移 | 把 Profile/Meal/Stats/Food/Friend/Ranking 恢复为 Service/DAO，并注册为 15 个 TCP 方法 | `server/business/`、`ServiceRegistry`、`service_tcp_test.py` |
| 6 | 身份统一 | `user_key`（26 位 ULID）成为唯一标识；scrypt 密码、令牌摘要、会话上限、SVG 验证码 | `server/identity/`、`migrations/add/002–003` |
| 7 | 跨端改造 | 前端 `eatTransport` 抽象：浏览器走 `fetch + SSE`，App 走 `postMessage → IPC → Agent` | `bridge.js`、`client/ipc/`、`agent/ipc_server.py` |
| 8 | 客户端壳与本地安全 | Win32 + WebView2 壳、单实例、托盘、DPAPI KeyVault、离线幂等队列 | `client/app/win32/`、`client/keyvault/`、`client/transport/` |
| 9 | 代码审查 | 复审连接计数下溢、`confirmed="false"` 真值绕过、跨用户越权、密钥输出风险 | `code-reviewer`；`DEV_PROCESS.md` T-06、T-21 |
| 10 | 设计模式与重构 | 传输层三态耦合落成 Adapter + Strategy；服务端分层的 `ServiceRegistry` 门面；线程池门面化 | `design-pattern-applier`、`code-refactoring`、R-01–R-09 |
| 11 | 死代码与契约清理 | 全仓 grep 确认无调用方后删除死代码；把 `delete_meal/update_meal` 标为 `persisted=false` | `dead-code-eliminator`、R-08 |
| 12 | 前端视觉重制 | 用 `unslop` 先定方向再改间距、配色、图标、文案，重做登录页与主页面 | `login.css`、`redesign.css`、`index.html` |
| 13 | 测试与验收 | C++ 单元测试、Agent 单测、IPC smoke、15 方法集成、前端语法、真实局域网请求 | `ctest` 12/12、Agent 51/51、`S1_INTEGRATION_OK`、`IPC_SMOKE_OK` |
| 14 | 故障定位 | 按“服务端日志 / Agent 日志 / 浏览器 Network / 运行配置”四类证据排查 | `DEV_PROCESS.md` T-01–T-22 |
| 15 | 交付与文档 | 三件套文档、RUNBOOK、Windows 交接、Docker、tar.gz、安装包 | `README.md`、`docs/PROJECT_OVERVIEW.md`、`docs/DEV_PROCESS.md`、`docs/RUNBOOK.md` |
| 16 | 经验与记忆沉淀 | 把非代码事实写入记忆：帧上限、迁移清单 6 处、Agent 重启清空 `user_key`、三层生效边界 | 跨会话复用，减少重复踩坑 |
| 17 | 子代理交叉验证 | 重构、审查、死代码清理、UI 改造分别交给专门子代理，互相验证改动 | 用户明确的工作方式 |
| 18 | 交付边界管理 | 明确区分“已验证 / 未验证 / 阻断”；破坏性操作先确认 | `DELIVERY-STATUS.md`、`README.md` 第 5 节 |

---

## 4. 典型场景详解

### 4.1 需求审查：先判断“该不该做”

项目早期同时存在三套后端：根目录 Web + Python Agent、`chatroom-backend/`（P7 自研 Reactor + P8 cpp-httplib）、根 `server/`（Muduo + 40B 帧）。

码道在这里没有直接“接着写”，而是先做可行性审查：

- P8 的业务语义、SQL、测试用例有价值；
- “支撑百万并发”的说法没有压测数据支撑，不能作为合并理由；
- 三套网络栈、三套身份、三套幂等会持续制造分叉。

结论是**不整体合并**，只迁移业务语义，再删除旧实现。这个判断避免了把三套复杂度带进新架构，也确立了后续所有工作的基线。

### 4.2 规格驱动：把“要什么”和“怎么证明”先写死

这个项目的规格不是形式文档，而是可执行的约束：

- `spec.md` 写清客户端形态、服务端技术栈、单机部署、JSON 序列化、Cache-Aside、Key 位置等口径；
- `design.md` 写清单进程服务端、帧格式、统一身份、分区表、IPC、打包；
- `tasks.md` 拆成 13 个任务组、102 个子任务、11 个阶段门。

执行时有一条硬规则：**上一阶段的验证不通过，不进入下一阶段**。例如 S0 的 `ctest` 不全绿，不开始 S1 业务迁移；S1 的真实请求没跑通，不宣称“服务端可用”。这条规则后来直接减少了“边写边改需求”的返工。

### 4.3 三线合并：从三套并存到一套骨架

这是项目里最大的一次结构改造，也是码道参与最深的一次：

- 以根 `server/` + `shared/protocol` 为唯一服务端骨架；
- 先从 Git 历史恢复 P8 的 Service/DAO 语义，不凭记忆重写；
- 重写为 TCP Handler，注册到 `ServiceRegistry`；
- 删除 `chatroom-backend/`，SQL 基线迁入 `migrations/base/`；
- 统一构建入口为根 `CMakeLists.txt`。

结果不是“代码变少了”这么简单，而是：一个 `eat-server` 进程承载全部业务，客户端、Agent、测试工具共用同一套帧常量，后续新增方法只需要在 Service/DAO 和注册表里各加一处。

### 4.4 协议与并发底座：把异常路径写成代码

这一层的产出几乎都是“平时看不见、出事时救命”的代码：

- 40 字节固定帧头，Python 与 C++ 严格对齐；超长帧在只读到头部时就拒绝；
- CRC32C 查表实现，标准向量校验；
- 严格 JSON：拒绝重复键、过深嵌套、尾随内容、BOM、NaN，防止“同名键后值覆盖”造成鉴权绕过；
- 连接状态机保证关闭幂等，修掉活跃连接计数下溢；
- 心跳用最小堆只检查堆顶，避免连接数上升后 `OnTimer` 线性扫描；
- 令牌桶限流、每连接与全局背压、未握手拒绝、写方法强制幂等键；
- 有界线程池，队列满立即拒绝，任务异常只计数不杀进程；
- Redis 命令白名单只保留 GET/SETEX/DEL/MGET/EXISTS/PING，`PUBLISH/SUBSCRIBE/SCAN/KEYS` 在接口上就不存在。

对应测试：`frame_codec_test`、`strict_json_test`、`connection_governance_test`、`frame_dispatcher_test`、`thread_pool_test`、`cache_test`。

### 4.5 业务迁移与身份统一

业务迁移不是“把 HTTP 处理函数换成 TCP 函数”，而是保留 P8 已定的字段、校验、截断、默认值和错误映射：

- `Profile / Meal / Stats / Food / Friend / Ranking` 各自拆成 Dao + Service；
- Service 构造注入 `user_key`、`MySQLPool`、`CacheClient`，不感知网络；
- DAO 用 `version` / `updated_at` 条件更新与软删条件，避免并发覆盖；
- 写方法在分发层已强制幂等键，业务层再做冲突检测；
- 15 个方法统一注册到 `ServiceRegistry`。

身份侧把 P7 的自增整型 `id`、P8 的 `agent_uid`、新架构的 `user_key` 收敛为唯一标识：

- `user_key` 用 26 位 ULID；
- 密码用 OpenSSL `EVP_PBE_scrypt`；
- 令牌只存 SHA-256 摘要，数据库泄露不等于令牌泄露；
- 登录失败 5 次锁 5 分钟，刷新令牌复用直接拒绝；
- 验证码默认开启，正式环境不关闭；
- 请求体里的 `user_key` 一律丢弃，身份只认连接上下文。

### 4.6 跨端传输抽象：同一套页面跑在浏览器和 App 里

页面原来直接 `fetch('127.0.0.1:8787')`，放进 WebView2 后没有 HTTP 端口，会全部失效。

码道把这个问题落成 Adapter + Strategy：

- `bridge.js` 暴露 `window.eatTransport`；
- 浏览器模式：`fetch + SSE`；
- App 模式：`postMessage → 壳 → IPC → Agent`；
- 页面业务代码只调用 `eatTransport.request`，不感知运行环境。

结果是同一套 DOM/CSS/JS 在两种运行形态下都能跑，GUI 不需要重写 UI；同时 GUI 不监听 HTTP 端口，避免和 Web 端抢 8787。

### 4.7 安全与代码审查：专挑“能跑但不对”的地方

审查类子代理在本项目抓到的典型问题：

- 关闭路径重复执行导致活跃连接数下溢；
- `/api/tools/call` 把字符串 `"false"` 当成真值，未确认的写操作被执行；
- 业务写入不认连接身份，前端可伪造 `user_key` 越权；
- 扫描工具把 `.env` 内容原样打印到终端，密钥泄露风险。

修复方式不是“加个判断”，而是把约束前移：关闭幂等由状态机保证，`confirmed` 只接受严格布尔，身份只认连接上下文，扫描工具不得原样输出敏感文件。

### 4.8 死代码与契约漂移清理

清理前必须全仓 grep 确认无调用方，删除与功能变更分开提交：

- Agent：`ContextManager.history()`、`Planner.render_plan_prompt()`、`EventBus.drain()`、`_ensure_simulated`；
- 前端：不存在元素的事件绑定、未使用的 `updateModelLabel`、未绑定的 `shareSkill`；
- 契约：图片上限统一为 10、随机比例统一 70/30、选择器改为真实 DOM；
- 语义：`delete_meal` / `update_meal` 标记 `persisted=false`，不再暗示已落库。

这类清理的价值在于消除“文档说支持、代码其实没接”的假象。

### 4.9 前端视觉重制：先定方向，再改细节

`unslop` 的用法不是“换一套渐变”，而是先找产品自身的视觉方向，再按间距、色彩、图标、文案、布局、动效的顺序改：

- 重做登录页：服务端地址、账号、密码、图形验证码；
- 重做首次引导：身高、体重、年龄、目标、过敏原、忌口、预算；
- 新增 `login.css` / `redesign.css`，统一视觉；
- 明确标注哪些数据来自服务端、哪些来自 mock；
- UI 与服务端解耦：队友可以无限次改页面、重建客户端，服务端不需要改参数或重启。

### 4.10 排错：从“改了还不行”到证据链

项目里 22 条排错记录大致分为五类，码道的用法是**先定位证据，再改代码**：

| 类别 | 代表问题 | 定位方式 |
|---|---|---|
| 数据库迁移 | MySQL 1503 分区键、CRLF/`--注释` 导致 1064、utf8mb4 二次编码 | 看报错行、`HEX()`、客户端字符集 |
| 协议与并发 | FrameCodec 命名空间、`Buffer::peek` 类型、连接计数下溢、一次只处理 64 帧、`OnTimer` O(n) | 单测 + 粘包/重复关闭用例 |
| 环境与网络 | Agent 未配地址返回 mock、只绑 `127.0.0.1`、会话结束进程被回收、端口 19443/9443 漂移 | `ss -ltn`、`/api/service/status`、运行配置 |
| 产物与交付 | `dist/eat-web` 是旧产物、验证码让 `client_smoke` 返回 3001、GUI `Failed to fetch`、`token_expired` | 浏览器 Network、资源 404、请求响应 |
| 缓存与安全 | Redis 停机业务报错、`confirmed="false"` 被当真 | 停 Redis 复测、严格布尔单测 |

其中最重要的一条经验是：**最危险的故障不是报错，而是“看起来正常但是假数据”**。所以 Agent 的 `/api/service/status` 必须显式返回 `mode=mock` 或真实地址，验收时用 `EAT_SERVICE_REQUIRED=1` 禁止回退。

### 4.11 测试与验收：正反向都要有

已实测的证据包括：

- C++ 单元/协议测试 `ctest` 12/12；
- Agent 单测 51/51；
- IPC `IPC_SMOKE_OK`；
- S1 集成 `S1_INTEGRATION_OK`；
- 15 个业务方法 TCP 集成通过；
- 前端 `node --check app.js bridge.js agent-adapter.js`；
- 真实局域网注册/登录/档案/菜单/统计/好友/排行。

负向用例同样重要：未握手返回 `2001`、缺幂等键返回 `3001`、错误令牌返回 `2002`、`--omit-idem` 稳定失败。这些比正向用例更能证明协议真的生效。

### 4.12 交付与文档：让下一个人能接着做

码道在这个项目里产出的文档不是“说明文”，而是可执行的交接材料：

- `README.md`：依赖矩阵、一键安装、三套部署、验收命令、局域网排障、开源组件、码道总结；
- `docs/PROJECT_OVERVIEW.md`：定位、用户、场景、架构、线程模型、协议、数据模型、缓存、身份、Agent、客户端、部署；
- `docs/DEV_PROCESS.md`：G/R/T 编号的施工日志，含现象、根因、修复、验证、经验；
- `docs/RUNBOOK.md`、`docs/WINDOWS_CLIENT_LAN_RUNBOOK.md`、`docs/WINDOWS_TEAMMATE_TEST_GUIDE.md`：按角色和平台拆分的运行手册；
- Windows：`build-windows-oneclick.ps1` 一键构建、`build-installer.ps1` 生成安装包；
- Linux：`package-linux.sh` 生成 tar.gz，Docker 多阶段镜像自动迁移。

文档同时承担“边界声明”的职责：哪些接口未帧化、哪些平台未验证、哪些只是设计目标。

### 4.13 经验与记忆沉淀

这个项目沉淀下来的非代码事实，后来直接影响了后续会话的判断：

- 帧大小硬上限 64 KiB，图片不能硬塞，改走 `profile_media` + 版本号按需拉取；
- 新增迁移要同步 6 处清单，否则打包产物或 Docker 镜像缺表；
- 重启 Agent 会清空内存里的 `user_key`，前端必须重登；
- 纯前端改动只需同步 `dist/eat-web` + 强刷，不需要重启服务端；
- `dist/` 不受 git 跟踪，源码同步不等于生效；
- 三层生效边界：`server/` 重编重启、`agent/` 重启重登、静态页只强刷；
- 验证聊天回答只能取 token 事件，不能把 citation 原文当回答。

这些内容不是读代码能得到的，写进记忆后避免了重复试错。

---

## 5. 子代理协同：让不同角色互相验证

项目内固化了四个代码类技能和一个 UI 技能，用法不是“都跑一遍”，而是按阶段分工：

| 技能 | 在项目中的职责 | 典型产出 |
|---|---|---|
| `design-pattern-applier` | 识别耦合，落成 Adapter / Strategy / 门面 | `eatTransport`、`ServiceRegistry`、`BusinessExecutor` 门面 |
| `code-refactoring` | 拆分超大文件、统一命名、保持行为不变 | R-01–R-09 的重构条目 |
| `code-reviewer` | 复审并发、越权、注入、密钥输出、资源管理 | 连接计数下溢、`confirmed` 绕过、身份伪造 |
| `dead-code-eliminator` | 全仓扫描无调用方代码与契约漂移 | Agent/前端死代码、`persisted=false` |
| `unslop` | 重做视觉方向与细节 | 登录页、主页面、`redesign.css` |

协同顺序通常是：**先重构或应用模式 → 再审查 → 再清理死代码 → 最后跑测试**。用户会刻意让不同子代理互相验证，避免“自己改、自己说没问题”。

---

## 6. 量化结果（只列可核对的事实）

| 维度 | 事实 |
|---|---|
| 技术栈 | 1 套 C++17 服务端、1 个 Python Agent、1 套原生 Web、1 个 Win32 + WebView2 客户端 |
| 规格规模 | `spec.md` 3156 行、`design.md` 1833 行、`tasks.md` 483 行；另有 P8 审查规格 863 行 |
| 业务方法 | 15 个 TCP 方法 + 认证/握手 |
| 数据库 | 基线 + 迁移 002–007，分区表各 25 个分区 |
| 测试 | `ctest` 12/12、Agent 51/51、IPC smoke、S1 集成、15 方法集成 |
| 过程记录 | 9 条代码生成（G）、9 条重构（R）、22 条排错（T） |
| 交付 | Linux tar.gz、Docker 镜像、Windows 源码包与安装包（v0.3.0）、Web 静态页 |
| 文档 | README + 项目概览 + 施工日志 + RUNBOOK + Windows 交接 + 协议/架构/设计文档 |

这里不写“效率提升百分之多少”，因为项目没有做前后对比度量；能证明的是**返工次数、假完成和重复踩坑的减少**，而不是一个虚构的百分比。

---

## 7. 可复用的“码道使用法”

1. **规格先行**：先把“必须满足什么、怎么证明”写成可验收的 spec，再写 design，再拆 tasks。
2. **先绿基线再重构**：每个阶段开始前先让现有测试全绿，不在红色基线上动结构。
3. **小步提交**：一次提交只做一类事，失败可回退。
4. **证据分级**：`已验证 / 理论推导 / 未验证 / 阻断` 四档，写文档时强制标注。
5. **负向用例**：未握手 2001、缺幂等 3001、错误令牌 2002，比正向用例更能证明协议生效。
6. **脚本化交付**：构建、打包、启动都封装成脚本，避免让用户逐行粘贴多行命令（尤其 fish shell）。
7. **状态可观测**：`mode=mock` 必须显式暴露，防止假数据看起来正常。
8. **约束写成硬规则**：Redis 只允许服务端连接、单实例、禁 pub/sub、禁客户端直连——这些要落到接口和代码里，而不是文档里。
9. **子代理交叉审查**：重构、审查、死代码清理分开做，不同角色互相验证。
10. **把非代码事实写进记忆**：帧上限、迁移清单、重启影响、产物漂移，避免下一轮重复试错。

---

## 8. 边界与需要人工把关的地方

- **不能替代真实环境验证**：Windows GUI、DPAPI、托盘、单实例、断网冷启动、百万并发、TLS 公网，都必须在目标环境实测；Linux 上只能验证 IPC 协议与前端语法。
- **破坏性操作必须先确认**：删库、删文件、清缓存、强推、执行迁移，不能由 AI 自行决定。
- **密钥与隐私**：扫描工具不得原样打印 `.env`；任何出现在终端或日志里的 Key 都应视为已泄露并轮换。
- **文档会漂移**：例如 `CHANGELOG.md` 的 Known limitations 仍停留在早期状态（写着 `meal/stats/food/friend/ranking` 未帧化、Redis 未接线，而当前已打通）；架构图里的帧头长度也曾与代码不一致。这类差异要以代码和运行配置为准，并定期收敛。
- **跨平台产物不可代生成**：Windows exe 只能在 Windows + MSVC + WebView2 SDK 上构建；Linux 构建机只能提供源码与脚本。
- **不虚标完成**：“代码写完”“编译通过”“单测绿”都不等于“功能可用”；完成定义必须包含目标平台验证。

---

## 9. 结论

这个项目对码道的使用，已经从“让它写一段代码”推进到“让它参与一条工程流水线”：

- 需要判断时，它做需求审查与可行性裁决；
- 需要动手时，它按规格生成协议、并发、业务、跨端与前端代码；
- 需要质量时，它用审查、重构、死代码清理和测试把风险前移；
- 需要交付时，它把运行步骤、验证证据、能力边界写成可执行文档；
- 需要延续时，它把非代码事实沉淀成记忆和施工日志。

最适合复用到其他项目的，不是某个具体文件，而是这套**“规格 → 设计 → 实现 → 审查 → 验证 → 沉淀”的闭环**，以及一条底线：**只把真实跑通的东西写成“已完成”**。

---

## 10. 相关文档

| 文档 | 内容 |
|---|---|
| [`README.md`](../README.md) | 依赖、部署、验收、开源组件、码道总结简版 |
| [`DEV_PROCESS.md`](DEV_PROCESS.md) | G/R/T 编号的完整施工日志 |
| [`PROJECT_OVERVIEW.md`](PROJECT_OVERVIEW.md) | 项目定位、架构、协议、数据模型与部署 |
| [`DELIVERY-STATUS.md`](DELIVERY-STATUS.md) | 交付物、验证证据、未完成项 |
| [`RUNBOOK.md`](RUNBOOK.md) | Linux 服务端运行与排障 |
| [`WINDOWS_CLIENT_LAN_RUNBOOK.md`](WINDOWS_CLIENT_LAN_RUNBOOK.md) | Windows 客户端与局域网联调 |
