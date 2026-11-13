# 今天你吃了吗 · Agent 项目说明

## 1. 项目概述

### 1.1 产品定位

“今天你吃了吗”是一款面向电脑端的饮食健康 Agent 产品。它围绕“用户今天实际吃了什么”展开，提供饮食记录、健康分析、健康画像猜餐、每周健康积分、好友健康均分排行榜和背景装扮等功能。

健康报告、猜餐结果与配餐建议仅用于日常饮食管理参考，不构成医疗诊断或治疗建议。

项目仓库：https://github.com/FRiver-hahaha/what-do-you-want-to-eat-

### 1.2 仓库组成与三层边界

| 部分 | 状态 | 职责 |
| --- | --- | --- |
| 客户端前端 | 已同步上游并接入 Agent | 页面与交互、`agent-adapter.js` 适配层、离线回退本地规则 |
| Agent 服务 | 本项目实现 | 模型接入、工具、MCP、Skill、RAG、上下文与记忆 |
| C++ 服务端 | 由其他成员实现 | epoll、主从 Reactor、MySQL、Redis、账号与好友关系 |

边界约束：

- 客户端通过 HTTP/SSE 访问 Agent 服务。
- Agent 服务只通过 `ServiceClient` 访问 C++ 服务端。
- 只有 C++ 服务端可以连接 MySQL 与 Redis。
- Agent 服务不引入 MySQL/Redis 客户端，不占用 Redis，不使用 Redis 订阅发布。
- Agent 自有数据使用 SQLite。

### 1.3 当前交付状态

| 能力 | 状态 |
| --- | --- |
| 双模式问答 / 工作 | 已实现 |
| 单模型图文输入（`deepseek-flash`） | 已实现并实测 |
| 语气人格（标准 / 温和 / 效率） | 已实现并实测 |
| 专家组合（多选、输入框标识） | 已实现 |
| 健康档案全字段写入 | 已实现 |
| Skill 市场（搜索、安装、导入、卸载） | 已实现 |
| 上下文 1MB 窗口与摘要压缩 | 已实现 |
| 图片粘贴上传（最多 10 张） | 已实现 |
| 前端授权改动 | 已落地 |
| C++ 服务端联调 | 由其他成员负责，Agent 侧已留接口 |

## 2. 产品目标与范围

### 2.1 目标

- 降低用户记录三餐的成本，支持自然语言与食物卡片两种入口。
- 把单次饮食记录放进近 7 天饮食节奏、个人健康档案和每周健康目标中理解。
- 用积分与好友排名鼓励持续记录，而不是评判某一口食物。
- 让 Agent 具备两种明确的工作方式：问答模式负责理解与建议，工作模式负责菜单配餐与本地操作。
- 让 Skill、Tool、MCP、模型与 RAG 成为可配置、可扩展、可隔离的能力，而不是写死在代码里的分支。
- 在没有服务端联调条件时，Agent 服务仍能靠 mock 数据与本地规则模型完整运行。

### 2.2 边界与不做的事

- 不修改 C++ 服务端；Agent 侧只按约定发起 HTTP/JSON 请求。
- 不连接 MySQL、Redis；不实现账号体系与好友关系存储。
- 不自行改写上游前端的主流程；前端改动以用户明确点名的范围为准。
- 不做医疗诊断、用药建议或化验单解读。
- 不把模型 API Key、服务端令牌写入仓库。

## 3. 系统架构

### 3.1 三层架构

```
客户端 UI（index.html / styles.css / features.css / app.js）
        │  HTTP + SSE（agent-adapter.js 接入，离线回退本地规则）
        ▼
Agent 服务（Python）
  ├─ 接入层：HTTP 路由、SSE、CORS、请求限长
  ├─ 编排层：Super-agent Router / Worker、ReAct、双模式、Skill 注入、降级
  ├─ 模型层：LLMGateway、模型注册、用户选择、规则兜底
  ├─ 能力层：Tools / MCP / Skills / RAG / Memory
  └─ 数据适配层：ServiceClient（唯一数据出口）
        │  HTTP/JSON
        ▼
MCP Server 子进程（stdio，JSON-RPC 2.0）
        │
        ▼
C++ 服务端（epoll + 主从 Reactor）
        ├─ MySQL（仅服务端连接）
        └─ Redis（仅服务端连接，单实例，不用发布订阅）
```

### 3.2 Agent 内部模块

```
AgentApp
  ├─ SuperAgent ── Router / WorkerPool / EventBus / Watchdog
  ├─ LLMGateway ── rule-fallback / deepseek-flash（饭饭）
  ├─ Planner ── Plan-and-Solve 计划、失败重规划
  ├─ TraceRecorder ── trace / span / token 可观测性
  ├─ Safety ── 注入检测、图片校验、输出过滤
  ├─ ToolRegistry ── 只读、写入、本地与 MCP 工具
  ├─ SkillsRegistry ── 内置技能、用户技能、专家组合、权限过滤
  ├─ SkillMarket ── 发布、安装、版本、下载量、好友分享、社区精选
  ├─ KnowledgeBase ── 文档切片、BM25 + 哈希向量混合检索
  ├─ ContextManager ── 追加式历史、全量摘要压缩、前缀稳定
  ├─ MemoryStore ── 长期偏好、过敏、忌口、目标
  ├─ MCPManager ── 子进程生命周期、工具发现、故障隔离
  └─ AgentRuntime ── Worker 内 ReAct：计划 → 流式 → 工具 → 重规划 → 回答
```

### 3.3 与服务端架构的对应

| 服务端能力 | Agent 侧对应 |
| --- | --- |
| epoll I/O 多路复用 | `ThreadingHTTPServer` 承载并发请求，SSE 独立线程 |
| 主从 Reactor | 主线程分发请求，工具线程池与 MCP 子进程承担阻塞工作 |
| MySQL 只由服务端连接 | Agent 只持有 `ServiceClient`，不持有数据库驱动 |
| Redis 只由服务端连接 | Agent 使用自有 SQLite，不占用 Redis |
| JSON 序列化 | 服务端接口、MCP、SSE、工具参数全部使用 JSON |
| 高并发客户端 | 无全局可变业务状态；SQLite 使用 WAL 与独立连接 |

## 4. 当前能力

### 4.1 客户端

上游基线为 `origin/main c83068c`，其后已合并页面动效与猜菜卡视觉迭代。客户端能力包括：

- 饮食记录与健康分析：早餐、午餐、下午茶、晚餐或加餐的自然语言记录，更新今日餐次、营养进度、健康评分、近 7 天趋势与历史记录。
- 本地健康画像猜餐：结合时段、目标、活动量、年龄、睡眠、近 7 天健康分与当前饮水营养进度；支持“再猜一次”和“猜对了，记录这道”。
- 食物菜单：16 道菜、分页、搜索、随机选菜、5 列瀑布流与拖拽展开。
- 每周积分：签到、三餐上报、三餐齐报奖励、Agent 互动与健康奖励。
- 好友排行榜与周奖励：本周与今日榜切换，周日按均分结算。
- 积分商城与背景装扮：四套背景，兑换后立即应用，跨周保留所有权。
- 布局：左侧食物与记录、中央对话与报告、右侧冠军封面与排行榜；侧栏可收起。
- 视觉：浅冷底、毛玻璃面板、珊瑚粉强调；食物图片是主要色彩来源。

### 4.2 Agent 服务

- 双模式：问答模式只读，工作模式可执行菜单查询、写入与技能管理。
- 单 Agent 双层架构：常驻 Router 负责分派，Worker 隔离执行；同一会话后续事件直通原 Worker。
- 规划：Plan-and-Solve 生成计划，ReAct 调工具，失败自动重规划。
- 流式：OpenAI 兼容接口使用 SSE，token 逐字到达前端。
- 排版：简单问题轻量段落，复杂问题才使用标题、列表与引用。
- 图文输入：`deepseek-flash` 原生支持视觉，文字与图片可混合输入。
- 上下文：追加式历史，全量摘要压缩，1MB 产品窗口，上限拦截。
- 可观测：每次请求生成 trace_id，记录 Router、规划、LLM、工具 span 与 token。
- Watchdog：长时间无事件的 Worker 发出 `watchdog` 事件并标记 stalled。
- 安全：注入检测、图片校验、输出密钥与手机号过滤、写操作确认。
- 模型降级：无 Key 或网络失败时自动切换 `rule-fallback`，并发出提示。
- 工具隔离：按模式、类别、来源三重过滤；写工具必须确认。
- MCP：Agent 作为 Client，饮食数据服务作为 Server，stdio + JSON-RPC 2.0。
- Skill：内置技能、语气技能、专家组合、市场安装与导入。
- RAG：内置知识文档，BM25 + 哈希向量混合检索，回答带引用事件。
- 记忆：自动识别过敏、忌口、偏好与目标，可查询、可删除。
- 审计：每次工具调用写入 `tool_audit`。
- 前端接入：SSE、Markdown、逐字流式、图片输入、确认弹窗、市场、专家、权限抽屉与离线降级。

### 4.3 模型与人格

- 对外只有一个用户可选模型「饭饭」，底层 `deepseek-flash`，同时支持文字与图片。
- `rule-fallback` 为内部兜底，不出现在用户列表。
- 三档语气人格：标准（默认）、温和、效率，可在客户端设置切换。
- 专家组合：医学专家、健身专家等可作为可多选的专家标识。

### 4.4 技能与专家

- 内置专家：健康、饮食、营养、配餐、猜餐、页面设计、医学、健身。
- 语气技能：标准、温和、效率，只控制表达，不参与主技能匹配。
- Skill 市场：社区精选、搜索、分类、安装、卸载、导入 SKILL.md。
- 手动导入不需要工作模式；通过 Agent 命令安装或从市场下载需要工作模式。

### 4.5 上下文、图片与权限

- 上下文窗口 1,000,000（产品口径 1MB），五段明细：系统提示词、工具、对话消息、MCP、技能。
- 压缩把全部未压缩对话写成简略摘要，压缩后对话消息段归零。
- 达到 1MB 时直接返回 `context_limit`，不执行本轮工具与档案查询，并提示新开对话。
- 图片最多 10 张，单张原图 ≤2MB，支持剪贴板粘贴；同一会话可在指代时复用上一轮图片。
- 工作权限两档：手动审批与完全访问；记录早、中、晚饭始终需要确认。

## 5. 技术栈

| 分类 | 技术 |
| --- | --- |
| Agent 语言 | Python 3.10+，无第三方依赖 |
| HTTP / SSE | 标准库 `http.server.ThreadingHTTPServer` |
| 数据存储 | SQLite（WAL） |
| 模型接入 | OpenAI 兼容 Chat Completions |
| MCP | stdio + JSON-RPC 2.0 |
| 检索 | BM25 + 哈希嵌入余弦相似度 |
| 客户端 | 原生 HTML / CSS / JavaScript |
| 测试 | `unittest` 进程内路由与事件生成器 |
| 前端语法检查 | Node（本机使用 IDE 内置 Node v22.21.1） |

## 6. 文件结构

```
.
├─ index.html                 客户端页面
├─ styles.css                 上游主样式
├─ features.css               上游动效与猜菜卡样式
├─ app.js                     上游主逻辑
├─ agent.css                  Agent 适配层样式
├─ agent-adapter.js           Agent 适配层
├─ AGENT.md                   本说明
├─ .env.example               环境变量示例
├─ agent/
│  ├─ app.py                  应用组装与对外方法
│  ├─ server.py               HTTP 路由与 SSE
│  ├─ config.py               配置加载
│  ├─ config/
│  │  ├─ models.json          模型注册
│  │  ├─ modes.json           模式与工具白名单
│  │  └─ mcp_servers.json     MCP 服务配置
│  ├─ core/
│  │  ├─ super_agent.py       Router / Worker / EventBus / Watchdog
│  │  ├─ runtime.py           ReAct 执行器
│  │  ├─ planner.py           计划与重规划
│  │  ├─ prompts.py           系统提示词
│  │  ├─ context.py           会话历史与压缩
│  │  ├─ memory.py            长期记忆
│  │  ├─ frontend.py          前端快照校验与档案合并
│  │  ├─ safety.py            注入、图片、输出校验
│  │  └─ trace.py             trace / span
│  ├─ llm/gateway.py          模型网关与降级
│  ├─ tools/                  工具注册与实现
│  ├─ skills/
│  │  ├─ registry.py          加载、匹配、启用、卸载、专家组合
│  │  ├─ market.py            市场、社区精选、发布、安装、分享
│  │  ├─ loader.py            SKILL.md 解析与校验
│  │  └─ builtin/             内置技能目录
│  ├─ rag/                    知识库与检索
│  ├─ mcp/                    MCP Client、Server 与协议
│  ├─ data/
│  │  ├─ db.py                SQLite 初始化
│  │  ├─ schema.sql           表结构
│  │  └─ service_client.py    服务端适配与 mock
│  ├─ scripts/                联调与诊断脚本
│  └─ tests/test_smoke.py     冒烟测试
└─ data/agent/                SQLite 数据目录（运行时生成）
```

内置技能目录当前为 11 个：`diet-expert`、`fitness-expert`、`health-expert`、`meal-guess`、`meal-planning`、`medical-expert`、`nutritionist`、`page-designer`、`tone-efficient`、`tone-gentle`、`tone-normal`。

## 7. 运行与配置

### 7.1 启动

```bash
python -m agent --host 127.0.0.1 --port 8787
```

前端静态服务：

```bash
python -m http.server 4173 --bind 127.0.0.1
```

打开 `http://127.0.0.1:4173`。适配层默认访问 `http://127.0.0.1:8787`，可通过 `localStorage["eat-agent-base"]` 覆盖。

### 7.2 环境变量

| 变量 | 默认 | 说明 |
| --- | --- | --- |
| `DEEPSEEK_API_KEY` | 空 | DeepSeek Key，优先环境变量，其次 `.env` |
| `EAT_AGENT_PORT` | `8787` | Agent 端口 |
| `EAT_AGENT_DATA_DIR` | `data/agent` | Agent 数据目录 |
| `EAT_AGENT_DB` | `data/agent/agent.db` | Agent SQLite |
| `EAT_AGENT_RAG_DB` | `data/agent/knowledge.db` | 知识库 SQLite |
| `EAT_SERVER_BASE_URL` | 空 | C++ 服务端地址；空时使用 mock |
| `EAT_SERVER_TOKEN` | 空 | 服务端令牌 |
| `EAT_SERVICE_TIMEOUT` | `5` | 服务端请求超时 |
| `EAT_DEFAULT_USER_ID` | `u_demo` | 默认用户 |
| `EAT_MAX_TOOL_STEPS` | `6` | 最大工具步数 |
| `EAT_MAX_CONTEXT_MESSAGES` | `18` | 历史消息上限 |
| `EAT_MAX_CONTEXT_CHARS` | `1000000` | 上下文硬上限字符 |
| `EAT_MAX_TOOL_RESULT_CHARS` | `6000` | 单次工具结果截断 |
| `EAT_RAG_TOP_K` | `4` | 检索返回条数 |
| `EAT_MCP_DISABLE` | `0` | 设为 1 时关闭 MCP |
| `EAT_MCP_START_TIMEOUT` | `12` | MCP 启动超时 |
| `EAT_LLM_PROXY` | 空 | 显式模型代理 |
| `EAT_LLM_USE_SYSTEM_PROXY` | `0` | 设为 1 时复用系统代理 |
| `EAT_ENV_FILE` | `.env` | 指定环境变量文件 |
| `EAT_DISABLE_DOTENV` | `0` | 设为 1 时不加载 `.env` |

### 7.3 模型代理与网络

模型请求默认**直连**，不读取系统代理。Windows 上 `urllib` 默认读取注册表代理，容易被受管环境拦截；需要代理时显式设置 `EAT_LLM_PROXY`，或设 `EAT_LLM_USE_SYSTEM_PROXY=1`。

诊断入口：`GET /api/agent/meta` 的 `llm` 字段返回 `proxy_mode`、`proxy_env`、`last_error`。调用失败时服务端 stderr 输出 `[agent-llm] 模型请求失败：...`。

### 7.4 数据存储

- Agent 自有数据：SQLite，WAL 模式。
- 会话、消息、记忆、市场、安装状态、审计、RAG、trace 与 span 全部在 Agent SQLite。
- 服务端数据：只经 `ServiceClient` 访问；未配置 `EAT_SERVER_BASE_URL` 时使用 mock。
- mock 下 `update_health_profile` 与 `record_meal` 的写入保存在进程内存或 mock 存储，重启后回到默认值。
- 图片不落库：历史只保存文本与图片数量，缓存仅在进程内。

## 8. Agent 服务设计

### 8.1 双模式与权限

| 能力 | 问答 `qa` | 工作 `work` |
| --- | --- | --- |
| 健康档案、饮食记录、营养缺口、周统计、好友排名 | 允许 | 允许 |
| 知识库检索与引用 | 允许 | 允许 |
| 实时菜单 `get_menu` | 禁止 | 允许 |
| 写入饮食 `record_meal` | 禁止 | 允许，需确认 |
| 更新健康档案 `update_health_profile` | 禁止 | 允许，需确认 |
| 导入 SKILL.md（Agent 命令） | 禁止 | 允许，需确认 |
| 安装市场技能 | 禁止 | 允许，需确认 |
| 启用/停用技能 | 禁止 | 允许 |
| 最大工具步数 | 4 | 6 |

工作模式两级访问权限：

| `access_level` | 行为 |
| --- | --- |
| `manual`（默认） | 所有写操作都需要用户确认 |
| `full` | 除 `record_meal` 外，写操作直接执行；记录三餐始终需要确认 |

- 签到没有对应工具，Agent 不能自动签到。
- 系统提示词明确禁止自动上报早、中、晚饭。
- `record_meal` 在 `ALWAYS_CONFIRM_TOOLS` 中，即使 `full` 也返回 `confirm_required`。
- 客户端在切到工作模式或再次点击「工作」时弹出权限抽屉，选择结果存 `localStorage["eat-agent-access"]`。

权限三处同时生效：

1. `modes.json` 定义模式工具白名单。
2. `ToolRegistry.specs_for_mode()` 只向模型暴露允许的工具。
3. `ToolRegistry.call()` 执行前再次校验模式、类别与本地开关，防止伪造调用。

Skill 再收窄一次：技能只能使用 `allowed-tools` 与模式工具集的交集；越权工具不注入上下文，也无法执行。

### 8.2 模型接入

`agent/config/models.json` 注册模型，`LLMGateway` 负责解析、调用与降级。

| 模型 ID | 显示名 | 提供方 | 真实模型 | 能力 | 用途 |
| --- | --- | --- | --- | --- | --- |
| `deepseek-flash` | 饭饭 | OpenAI 兼容 | `deepseek-flash` | chat、tools、stream、vision | 唯一对外模型，图文统一入口 |
| `rule-fallback` | 本地规则引擎 | 本地规则 | - | chat、tools | 离线兜底，`hidden: true` |

- `/api/models` 只返回一个用户可选模型。
- 前端显示「饭饭」，副标题展示真实模型名 `deepseek-flash`。
- 请求直接使用 `model=deepseek-flash`。
- 该 API 当前可用模型为 `deepseek-flash` 与 `deepseek-v4-pro`；`deepseek-v4.1-*` 会被拒绝。
- 两个模型中 `deepseek-flash` 已验证支持图片输入。
- 配置优先级：真实环境变量 > `.env` > `models.json`。
- Key 不可用时 `default_model_id()` 回退到 `rule-fallback`，服务仍可启动。
- 调用失败时发出 `notice`，不会静默降级。
- 规则模型可完成意图识别、工具规划、结果汇总与降级回答。

路由预留：`routing.intent`、`routing.chat`、`routing.summary`、`routing.embedding`。

### 8.3 工具与隔离

| 工具 | 类别 | 模式 | 说明 |
| --- | --- | --- | --- |
| `get_health_profile` | read | 两种 | 档案、目标、活动量、过敏与忌口 |
| `get_recent_meals` | read | 两种 | 近 N 天饮食记录 |
| `get_nutrition_gap` | read | 两种 | 蛋白质、纤维、蔬菜、饮水缺口 |
| `search_foods` | read | 两种 | 关键词、标签、餐次检索，自动过滤忌口 |
| `get_weekly_stats` | read | 两种 | 本周健康分、记录天数、积分、连续天数 |
| `get_friend_ranking` | read | 两种 | 好友健康均分排行榜 |
| `get_menu` | local | work | 实时可售菜单 |
| `record_meal` | write | work | 写入饮食记录，需确认 |
| `update_health_profile` | write | work | 称呼、性别、身高、体重、年龄、目标、活动量、饮水、睡眠、饮食风格、过敏、忌口、备注，需确认 |
| `list_skills` | read | 两种 | 列出可用技能 |
| `enable_skill` | local | work | 启用或停用技能 |
| `install_skill` | local | work | 安装市场技能，需确认 |
| `import_skill` | local | work | 导入 SKILL.md，需确认 |
| `get_agent_status` | read | 两种 | 模式、服务状态、工具与技能数量 |
| `mcp_diet_data_*` | read | 两种 | MCP Server 暴露的只读工具 |

隔离机制：

- 模式隔离：问答模式看不到写工具与本地菜单工具。
- 技能隔离：`allowed-tools` 再收窄。
- 执行隔离：每个工具在独立线程池任务中执行，超时转结构化失败。
- 结果隔离：按 `MAX_TOOL_RESULT_CHARS` 截断，避免原始 JSON 撑爆上下文。
- 身份隔离：模型不能传 `user_id` / `session_id`，注册中心强制注入当前用户。
- 审计隔离：每次调用写入 `tool_audit`，包含工具名、参数、成功标记与摘要。

### 8.4 Skill 与专家

内置技能：

| 技能 | 类别 | 作用 |
| --- | --- | --- |
| `health-expert` | health | 生活方式解读与今天可执行的小目标 |
| `diet-expert` | diet | 饮食记录查询与复盘 |
| `nutritionist` | nutrition | 蛋白质、纤维、蔬菜、饮水的配餐增强 |
| `meal-planning` | planning | 按实时菜单与缺口安排下一餐 |
| `meal-guess` | guess | 结合时段、记录、缺口与上下文的猜测 |
| `page-designer` | design | 模块布局、信息层级与视觉建议 |
| `medical-expert` | health | 体检指标相关的饮食方向，不做诊断 |
| `fitness-expert` | fitness | 训练日蛋白、碳水与加餐时机 |
| `tone-normal` | tone | 标准语气 |
| `tone-gentle` | tone | 温和陪伴 |
| `tone-efficient` | tone | 效率优先 |

语气规则：

| tone | 名称 | 规则 |
| --- | --- | --- |
| `normal`（默认） | 标准语气 | 先结论后依据，1–2 条依据 + 一个动作，中性陈述 |
| `gentle` | 温和陪伴 | 先共情后建议，不用命令式，≤120 字、最多 3 句 |
| `efficient` | 效率优先 | 结论先行，不铺垫不客套，≤60 字、最多 2 句 |

- 语气存 `localStorage["eat-agent-tone"]`，请求携带 `tone`。
- `match()` 排除 `category=tone`，语气技能不抢占主技能。
- 语气是提示词层的人格控制，不是模型权重微调。
- `GET /api/agent/meta` 返回 `tones` 与 `default_tone`。

专家组合：

- 请求携带 `skill_ids` 可组合多个专家。
- `resolve_experts()` 按顺序去重，过滤语气技能与未启用技能。
- 多个专家正文依次注入 `<skill>`。
- 工具权限取各专家 `allowed-tools` 与模式工具集的并集；任一专家未限制工具时按全部可用处理。
- 未传 `skill_ids` 时回退单个 `skill_id`，再回退关键词自动匹配。
- 前端多选后存 JSON 数组，并在输入框上方显示可移除的专家标识 chip。
- 请求同时携带 `skill_ids` 与首个 `skill_id`。

加载与匹配：

- 启动时扫描 `skills/builtin/*/SKILL.md` 与用户技能目录。
- 显式 `skill_id` 优先。
- 未指定时按 `triggers`、标签、名称与类别打分，同分按 `priority` 排序。
- 命中技能正文注入系统提示词，并限制可用工具。
- 技能正文用 `<skill>` 包裹，明确是参考资料而不是系统指令。

### 8.5 MCP

协议：stdio + JSON-RPC 2.0。Agent 为 Client，饮食数据服务为 Server。

握手顺序：

```
Client → initialize
Server → protocolVersion / capabilities / serverInfo
Client → notifications/initialized
Client → tools/list
Server → tools[]
Client → tools/call
Server → content + structuredContent
```

进程模型：

```
Agent 父进程
  ├─ stdin  ── 父进程写，子进程读 ──▶ MCP Server 子进程
  ├─ stdout ◀── 子进程写，父进程读 ──┘
  └─ stderr ◀── 子进程写，父进程读 ── 日志，不进入协议
```

配置在 `agent/config/mcp_servers.json`，支持 `allow` / `deny` 工具白名单、启动超时与日志尾部。子进程崩溃不影响主服务，状态可从 `GET /api/mcp/status` 查询。

### 8.6 RAG

定位：知识库只装公共知识，用户健康数据永远走结构化工具查询。

内置知识：

| 文档 | 内容 |
| --- | --- |
| `protein-basics.md` | 蛋白质目标、每餐分配、常见食物含量、减脂提醒 |
| `fiber-vegetables.md` | 蔬菜摄入量、纤维目标、外食补蔬菜 |
| `meal-distribution.md` | 三餐能量分配、早餐模板、外卖策略、记录粒度 |
| `allergy-and-avoidance.md` | 过敏与忌口的区别、常见过敏原、安全边界 |

检索流程：

1. 按 `##` 小节与段落切片。
2. 中文按字符 bigram、英文按单词分词。
3. BM25 计算关键词相关度。
4. 哈希嵌入计算余弦相似度。
5. 归一化后按 `0.72 × BM25 + 0.28 × 余弦` 排序。
6. 返回标题、片段、来源与分数。

触发：命中营养、蛋白质、纤维、减脂、外卖等关键词，或命中营养、健康、配餐、猜餐类技能；请求可用 `use_rag` 覆盖。命中结果注入 `<knowledge>` 并逐条发出 `citation` 事件。

### 8.7 上下文、压缩与上限

| 层 | 内容 | 存储 | 策略 |
| --- | --- | --- | --- |
| 请求级 | 本轮工具调用与结果 | 内存 | `summary + data`，超长截断 |
| 会话级 | 未压缩消息 + 一次性摘要 | Agent SQLite | 只追加；压缩后推进 `compacted_until_id` |
| 长期记忆 | 过敏、忌口、偏好、目标 | Agent SQLite | 规则抽取，可删除 |

产品口径：

- 窗口 `1,000,000`（1MB），面板显示「1M」。
- 五段明细：系统提示词、工具、对话消息、MCP、技能。
- 系统提示词包含稳定规则、档案、记忆与摘要。
- 工具按当前模式的实际 JSON Schema 估算。
- 技能只统计必注入的语气技能与会话命中的专家，不统计全部技能。
- `POST /api/context/compact` 调用 `compact_all()`：把全部未压缩对话写成简略摘要，每条 80 字、总长上限 2400 字，推进 `compacted_until_id`，压缩后对话消息段归零。
- 达到 1MB 上限时，`chat_events` 在进入工具与档案查询前直接返回 `context_limit` 并提示新开对话，不执行本轮业务查询。
- DeepSeek 单次请求仍受 64K 限制：Runtime 在历史 token 超过 60K 的 80% 时自动调用 `compact_all()`，避免模型 400；压缩后仍超限才由 1MB guard 拦截。
- 持久化摘要不调用模型，使用确定性规则压缩，避免额外 token 与延迟。

前缀稳定：

- 历史只追加，不每轮滑动裁剪。
- 系统提示词先放稳定规则，再放动态块（档案、记忆、摘要、计划、技能、知识、图片说明）。
- 稳定规则位于前缀，便于提供方命中前缀缓存。

### 8.8 记忆

抽取规则：

- “我对 X 过敏” → 过敏。
- “我不吃 X / 我讨厌 X” → 忌口。
- “我喜欢 X / 我爱吃 X” → 偏好。
- “我在减脂 / 我的目标是 X” → 目标。

去重后写入，回答前注入 `<memory>`。可通过 `GET /api/memory` 查看，`DELETE /api/memory/{id}` 删除。

### 8.9 图片输入与缓存

- 请求 `images` 支持 dataURL 与 http(s) URL。
- 最多 10 张，单张原图 ≤2MB（dataURL ≤350 万字符）。
- 请求体上限 36MB。
- 前端支持输入框粘贴与拍照按钮。
- Agent 构造 OpenAI 多模态 `image_url`。
- `deepseek-flash` 声明 `vision`，可直接识别；未声明视觉能力的模型会收到提示。
- 图片缓存按 `user_id + session_id` 隔离，每会话最多 4 张，最多 50 个会话，LRU 淘汰。
- 后续请求不带图时，消息包含“这张图 / 这图 / 刚才那张 / 图中”等指代，或显式传 `use_cached_image: true`，会自动复用上一轮图片，`meta.image_source` 为 `cache`。
- 无指代且未显式开启时不会复用，避免每轮重复传图。
- 缓存仅在当前进程有效，重启失效；图片不落库。

### 8.10 安全与审计

- 注入检测：命中指令覆盖、越权查询等标记时提高风险等级并提示。
- 图片校验：数量、类型、大小与 dataURL 前缀。
- 输出过滤：密钥与手机号在返回前替换。
- 写操作确认：`record_meal`、`update_health_profile`、`install_skill`、`import_skill` 必须二次确认。
- 身份隔离：用户与会话由注册中心注入。
- 审计：`tool_audit` 记录工具名、参数、结果与时间。
- 密钥：只放环境变量或被忽略的 `.env`，不写入仓库、不回显明文。

### 8.11 Super-agent

单进程、单机实现：

- Router 常驻，负责接收事件、维护会话、判断任务类型、分派 Worker、汇总结果。
- Worker 隔离执行，挂载工具与独立上下文，不直接对用户回复。
- 同一 Thread（`session_id`）后续事件直通原 Worker；新 Thread 冷启动并建立订阅。
- Worker 内部 Plan-and-Solve + ReAct，工具失败自动重规划。
- `WorkerPool` 用 `BoundedSemaphore(4)` 限制并发。
- `EventBus` 保存每个 Thread 最近事件，供 Watchdog 与状态查询。
- Watchdog 检测长时间无事件的 Worker。
- 对外仍是一个 Agent、一个入口、一套 SSE 事件。

已实现：Router/Worker 分层、事件驱动、订阅直通、规划、Trace、Watchdog、上下文前缀稳定。

架构预留：存算分离、跨设备 daemon、A2A、真正的异步 `start/stop` 工具。

## 9. 接口说明

### 9.1 REST

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/` | 服务说明 |
| GET | `/health` | 健康检查与服务端连接状态 |
| GET | `/api/agent/meta` | 模式、模型、工具、技能、MCP、RAG、语气汇总 |
| GET | `/api/frontend/contract` | 前端 DOM 映射、已对齐字段与缺口 |
| GET | `/api/super/status` | Router、Worker、线程池、Watchdog 与事件总线状态 |
| GET | `/api/traces` | 最近 trace 列表 |
| GET | `/api/traces/{id}` | 单次 trace 与全部 span |
| GET | `/api/models` | 模型列表与可用性 |
| GET | `/api/context?session_id=&mode=&access_level=&tone=&model_id=` | 上下文使用率与分段统计 |
| POST | `/api/context/compact` | 压缩全部历史为摘要，返回前后 token |
| POST | `/api/chat` | 非流式对话，返回完整事件与答案 |
| POST | `/api/chat/stream` | SSE 流式对话 |
| GET | `/api/sessions/{id}/messages` | 会话消息 |
| DELETE | `/api/sessions/{id}` | 清空会话 |
| GET | `/api/skills?mode=&user_id=` | 技能列表与安全摘要 |
| POST | `/api/skills/enable` | 启用/停用技能，需工作模式 |
| POST | `/api/skills/install` | 手动导入 SKILL.md，问答与工作模式均可 |
| GET | `/api/skills/market` | 市场列表，支持 `search` 与 `category` |
| POST | `/api/skills/market/publish` | 发布技能 |
| POST | `/api/skills/market/install` | 安装市场技能，需工作模式 |
| GET | `/api/skills/market/installed?user_id=` | 已安装技能 |
| DELETE | `/api/skills/market/{id}` | 删除自己发布的市场技能 |
| DELETE | `/api/skills/{id}` | 卸载已安装技能，需工作模式 |
| POST | `/api/skills/market/share` | 分享给好友，校验好友关系 |
| GET | `/api/skills/market/shared?user_id=` | 收到的分享 |
| GET | `/api/memory?user_id=` | 长期记忆 |
| DELETE | `/api/memory/{id}?user_id=` | 删除记忆 |
| POST | `/api/rag/search` | 知识检索 |
| POST | `/api/rag/ingest` | 文本入库 |
| GET | `/api/rag/stats` | 知识库统计 |
| GET | `/api/mcp/status` | MCP 运行状态 |
| GET | `/api/tools?mode=` | 当前模式工具列表 |
| POST | `/api/tools/call` | 调试工具调用，仍受模式权限约束 |
| POST | `/api/feedback` | 用户反馈写入长期记忆 |
| GET | `/api/service/status` | 服务端连接状态 |

前端实际使用：`/api/agent/meta`、`/api/models`、`/api/context`、`/api/context/compact`、`/api/chat/stream`、`/api/skills`、`/api/skills/install`、市场列表/安装/已安装/卸载。

保留接口但暂无界面：`/api/sessions`、`/api/rag/*`、`/api/memory`、`/api/tools` 调试接口。

### 9.2 对话请求体

```json
{
  "user_id": "u_demo",
  "session_id": "s_001",
  "message": "今天该吃什么",
  "mode": "qa",
  "model_id": "deepseek-flash",
  "tone": "normal",
  "access_level": "manual",
  "skill_id": "meal-planning",
  "skill_ids": ["medical-expert", "fitness-expert"],
  "confirmed_tools": [],
  "use_rag": null,
  "use_cached_image": false,
  "images": [],
  "client_context": {
    "profile": {
      "name": "我",
      "gender": "女",
      "diet_style": "普通饮食",
      "height": 170,
      "weight": 60,
      "age": 28,
      "goal": "均衡饮食",
      "activity": "久坐",
      "waterTarget": 1800,
      "sleep": 7,
      "note": "下午容易口渴"
    },
    "meals": [
      {
        "type": "早餐",
        "time": "08:20",
        "text": "燕麦酸奶、蓝莓、水煮蛋",
        "protein": 22,
        "fiber": 6,
        "veggies": 0,
        "water": 300
      }
    ],
    "today": {
      "score": 82,
      "status": "整体均衡",
      "nutrition": {"protein": 60, "fiber": 15, "veggies": 320, "water": 950}
    }
  },
  "stream": false
}
```

字段说明：

- `client_context` 是前端 localStorage 的只读快照，经过字段白名单与长度校验。
- mock 模式下用于对齐页面显示；真实服务端可用时以服务端数据为准。
- Agent 通过 `update_health_profile` 写入的档案字段会标记为 overridden，后续旧快照不覆盖这些字段。
- 未写入过的字段仍以前端快照为准。
- `images` 上限见 8.9。
- `skill_ids` 为空时回退 `skill_id` 与自动匹配。

### 9.3 SSE 事件

`Content-Type: text/event-stream`：

```
event: token
data: {"text":"今天"}
```

| 事件 | 载荷 | 说明 |
| --- | --- | --- |
| `meta` | session_id、trace_id、mode、model、skill、skills、tone、access_level、plan、images、image_source、new_memories、service、client_context | 本轮开始 |
| `plan` | id、task_type、complexity、steps、needs_confirmation | 执行计划 |
| `replan` | plan、failed_tool、error | 工具失败后的重规划 |
| `worker_start` | worker_id、thread_id、created、trigger_id | Router 已分派 Worker |
| `worker_done` | worker_id、thread_id、status | Worker 本轮结束 |
| `watchdog` | level、worker_id、thread_id、idle_seconds | Worker 长时间无事件 |
| `skill` | id、display_name、category | 命中的专家，可多个 |
| `citation` | id、doc_id、title、text、score | 知识库引用 |
| `tool_call` | id、name、arguments | 即将执行工具 |
| `tool_result` | id、name、ok、need_confirm、summary、error | 工具结果 |
| `token` | text | 逐字增量 |
| `notice` | level、message | 降级、图片能力、敏感信息等提示 |
| `confirm_required` | message、mode、confirmed_tools | 等待写操作确认 |
| `done` | session_id、trace_id、model、degraded、finish_reason、usage、tokens、plan、citations | 本轮结束 |
| `error` | code、message | 参数或执行错误 |

### 9.4 写操作确认

1. 模型调用写工具。
2. 注册中心发现未确认，返回 `need_confirm=true`。
3. 服务端发出 `confirm_required` 并结束本轮。
4. 前端弹出短确认弹窗。
5. 用户确认后带 `confirmed_tools` 重发同一请求。
6. 工具执行并返回结果。

完全访问下，除 `record_meal` 外的写工具自动确认；`record_meal` 始终走上述流程。

### 9.5 错误与降级

| 场景 | 行为 |
| --- | --- |
| 参数缺失 | `error` 事件，code 为具体原因 |
| 模式越权 | 工具不暴露；强制调用返回 `permission_denied` |
| 写操作未确认 | `confirm_required`，本轮结束 |
| 模型无 Key / 网络失败 | 切换 `rule-fallback`，发 `notice`，`done.degraded = true` |
| 上下文达 1MB | `notice` + `error(context_limit)` + `done`，提示新开对话 |
| MCP 子进程异常 | 工具不可用，主服务继续运行 |

## 10. 提示词与输出

系统提示词由 `agent/core/prompts.py` 组装，顺序：

1. 基础规则
2. 模式规则
3. 权限规则
4. 排版规则
5. 工具规则
6. 安全规则
7. Super-agent 规则
8. 档案
9. 前端快照
10. 记忆
11. 历史摘要
12. 计划
13. 工具清单
14. 技能正文
15. 语气
16. 知识
17. 图片说明

输出约束：

- 先给结论，再给 1–2 条依据，最后给一个可执行动作。
- 简单问题 1–3 句；复杂问题才用标题与列表。
- 数字必须来自工具结果，不凭印象编造。
- 不展示内部计划、工具原始 JSON 或思考链。
- 不自动签到、不自动上报三餐。
- 更新档案前先读取现有档案，只改用户明确要求的字段，更新后复述新值。
- 不确定时明说，不硬答。
- 涉及严重过敏、持续呕吐、呼吸困难时建议就医。

## 11. 数据结构

### 11.1 客户端 localStorage

| 键 | 内容 |
| --- | --- |
| `eat-health-profile-v2` | 健康档案 |
| `eat-health-meals-v2` | 今日餐次与营养估算 |
| `eat-health-history-v2` | 历史饮食与健康分 |
| `eat-health-points-v1` | 周积分、签到、互动、奖励、消费与背景所有权 |
| `eat-agent-motion` | 背景动效开关 |
| `eat-champion-cover-v1` | 冠军封面 dataURL |
| `eat-agent-mode` | 问答 / 工作 |
| `eat-agent-model` | 模型 ID |
| `eat-agent-tone` | 语气 |
| `eat-agent-access` | 手动审批 / 完全访问 |
| `eat-agent-expert` | 专家 ID 数组 |
| `eat-agent-session` | 会话 ID |
| `eat-agent-user` | 用户 ID |
| `eat-agent-base` | Agent 服务地址 |

档案字段：`name, gender, diet_style, height, weight, age, goal, activity, waterTarget, sleep, note`。

餐次字段：`type, time, text, protein, fiber, veggies, water, penalty`。

积分字段：`week, baseEarned, spent, checkins, interactions, healthAwards, owned, selected`。

### 11.2 Agent SQLite

| 表 | 内容 |
| --- | --- |
| `sessions` | 会话、模式、模型、技能、摘要、`compacted_until_id` |
| `messages` | user / assistant / tool 消息、工具名、调用 ID |
| `memories` | 过敏、忌口、偏好、目标 |
| `market_skills` | 市场技能、作者、版本、下载量 |
| `market_versions` | 技能版本历史 |
| `skill_shares` | 好友分享与分享码 |
| `installed_skills` | 安装状态与启用开关 |
| `tool_audit` | 工具调用审计 |
| `rag_docs` / `rag_chunks` | 知识文档、切片、token、嵌入 |
| `traces` | 请求 trace、模型、token、状态 |
| `spans` | LLM / 工具 / Router span 的耗时与详情 |

### 11.3 服务端 JSON 约定

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/v1/users/{id}/health-profile` | 健康档案 |
| PATCH | `/api/v1/users/{id}/health-profile` | 更新档案，Agent 已预留 |
| GET | `/api/v1/users/{id}/meals?days=` | 饮食记录 |
| GET | `/api/v1/users/{id}/nutrition-gap?date=` | 营养缺口 |
| GET | `/api/v1/users/{id}/weekly-stats` | 周统计 |
| GET | `/api/v1/users/{id}/ranking?scope=` | 好友排名 |
| GET | `/api/v1/users/{id}/friends` | 好友列表 |
| GET | `/api/v1/foods/search?q=&meal_type=&limit=` | 菜品检索 |
| GET | `/api/v1/menu?meal_type=` | 实时菜单 |
| POST | `/api/v1/users/{id}/meals` | 写入饮食，带幂等键 |
| GET | `/api/v1/skills/market` | 市场列表，Agent 接入后优先请求 |

所有请求与响应均为 JSON。写入请求包含 `idempotency_key`，避免重试造成重复记录。服务端未配置时自动使用 mock，并在元信息中标记 `source: mock`。

字段对齐：

- 菜品：`id, name, category, description, calories, protein, fiber, veggies, water, nature, keywords, image`；Agent 额外返回 `match`（20–98）。
- `nature`：`recommend` / `indulge`，对应“推荐菜 / 放纵菜”。
- 档案：前端使用 `waterTarget` 与数字 `sleep`，Agent 内部兼容 `water_target` 与 `sleep_hours`。
- 餐次：`type, time, text, protein, fiber, veggies, water, penalty`。
- 健康目标：均衡饮食、控制体重、增加蛋白、减少添加糖。
- 活动量：久坐、适中、较高。
- 匹配度算法与 `app.js getFoodMatch` 同源：基础 54，按缺口加权，热量档位，放纵 −14，目标修正，区间 20–98。

## 12. 前端适配契约

### 12.1 文件与挂载点

新增两个独立文件：

- `agent-adapter.js`：读取 localStorage、组装 `client_context`、请求 SSE、解析事件、渲染消息、确认弹窗、模型与模式、权限抽屉、专家、市场、上下文面板、连接状态与降级。
- `agent.css`：适配层样式，沿用珊瑚粉、毛玻璃与圆角体系。

`index.html` 引入 `agent.css` 与 `agent-adapter.js`，并包含顶部 Skill 市场入口、输入框字数计数、文案与 tooltip 文案调整。

`app.js` 挂载点：

1. `sendPrompt()` 开头调用 `window.eatAgent.intercept(rawText)`，返回 true 时由适配层接管，并清空输入。
2. 文件末尾导出 `window.eatLocal = { sendPrompt, recordMeal, getHealthAnalysis, showToast }`。

### 12.2 授权改动

| 文件 | 改动 |
| --- | --- |
| `index.html` | 输入框 `maxlength="2000"` 与计数；顶部导航新增 Skill 市场按钮；设置/我的 tooltip 改名；输入框下方改为“内容由 AI 生成，仅作日常饮食管理参考” |
| `app.js` | Agent 接管分支发送后清空输入；输入框自适应高度上限 112px → 220px |
| `agent-adapter.js` | 图片入口合并到拍照按钮；粘贴上传与 10 张上限；模型菜单；语气设置；权限抽屉；上下文面板；专家多选与标识；Skill 市场增强；短确认弹窗；模式轻 tooltip；档案写回同步 |
| `agent.css` | 长文本、计数、模型副标题、语气、权限抽屉、上下文面板、专家菜单与标识、市场、短弹窗、模式 hover 与 tooltip |
| `styles.css` | 顶部 tooltip 层级与 overflow |

### 12.3 已对齐字段

| 项 | 对齐方式 |
| --- | --- |
| 菜品数量与字段 | 16 道菜，字段与 `app.js foods` 一致 |
| 推荐 / 放纵分组 | `search_foods` 支持 `nature` |
| 匹配度 | 与 `getFoodMatch` 同源，区间 20–98 |
| 档案字段 | 兼容 `waterTarget`、数字 `sleep`，保留服务端别名 |
| 餐次字段 | 支持 `text` 原文写入 |
| 今日营养 | `today.nutrition` 可作为缺口计算输入，蔬菜目标 500g |
| 健康目标 | 与前端取值一致 |
| 输入入口 | `#chatForm` + `#chatInput` |
| 消息 DOM | `.message.agent-message`、`.agent-avatar`、`.message-meta`、`.message-bubble` |
| 问答顺序 | 先用户消息，再 Agent 回答 |
| Markdown | 简单问题轻量段落；复杂问题支持标题、列表、加粗、行内代码与引用 |
| 逐字流式 | token 逐字追加后重新渲染 |
| 图片输入 | 拍照按钮 + 粘贴，最多 10 张，请求携带 `images` |
| 档案写回 | 成功后同步 localStorage 并提示刷新档案页 |
| 工作权限 | 底部抽屉选择，存 `eat-agent-access`，请求携带 `access_level` |
| 上下文面板 | 五段明细与压缩，`meta` / `done` 后刷新 |
| 专家入口 | 多选与输入框标识，请求携带 `skill_ids` |
| 市场入口 | 顶部导航积分商城右侧，搜索、分类、安装、卸载、导入 |
| 确认弹窗 | 380px 短弹窗 |
| 模式提示 | 白色轻 tooltip，无底部黑框 |

契约可通过 `GET /api/frontend/contract` 获取。

### 12.4 DOM 映射

| 用途 | 选择器 |
| --- | --- |
| 输入表单 / 输入框 | `#chatForm` / `#chatInput` |
| 对话容器 | `#conversation` |
| Agent 消息 | `.message.agent-message` |
| Agent 头像 | `.agent-avatar` |
| 消息元信息 | `.message-meta` |
| 消息气泡 | `.message-bubble` |
| 输入中占位 | `#typingMessage` |
| 猜餐名称 / 理由 / 匹配度 / 进度 | `#guessName` / `#guessReason` / `#guessMatch` / `#guessBar` |
| 猜餐接受 / 再猜 | `#acceptGuess` / `#nextGuess` |
| 菜品搜索 / 结果 | `#foodSearchInput` / `#foodSearchResults` |
| 左侧食物 / 中央瀑布流 | `#sidebarFoodList` / `#expandedMenuList` |
| 随机选菜层 | `#randomPicker` |
| 档案表单 | `#profileForm`、`[data-option-group]`、`#waterRange`、`#sleepRange` |
| 报告 / 历史 | `#reportSummary` / `#scoreValue` / `#mealTimeline` / `#historyList` |
| 排行榜 | `#leaderboardList` / `#championName` |

### 12.5 缺口

- 前端没有自动化 DOM 测试环境；当前只做 Node 语法检查与浏览器加载日志确认。
- 市场「发布」与自己发布条目的删除只在 API 层，没有界面入口。
- 上下文 token 为估算值，不是模型 tokenizer 的精确计数；MCP 只按工具名估算。
- 1MB 是产品层窗口；模型单次真实窗口仍为 64K，依靠自动压缩衔接。
- 图片只用于当前轮，历史不存图片；缓存重启失效。
- 服务端 `PATCH /health-profile` 尚未实现，mock 写入重启后丢失。
- 前端档案页保存后需要刷新才能看到 Agent 写入的新值。

## 13. 验证与验收

### 13.1 单测

```bash
python -m unittest agent.tests.test_smoke
```

31 项全部通过，覆盖：

| 分组 | 覆盖内容 |
| --- | --- |
| HTTP | 健康检查、元信息、前端契约、非流式对话、SSE、越权工具、问答模式手动导入 |
| MCP | 子进程启动、协议版本、工具列表、真实调用、别名 |
| Market | 发布、列表、安装、好友分享、陌生人拒绝 |
| Memory | 抽取、查询、删除 |
| RAG | 中文检索、入库、统计 |
| Runtime | 工具调用、记录查询、猜餐、工作模式菜单、RAG 引用、模式隔离、client_context |
| Skills | 内置技能、匹配、工具范围、语气、专家组合 |
| Tools | 模式隔离、读工具、菜品字段、写入确认、前端契约 |
| SuperAgent | Router / Worker 事件、状态 |
| Planner | 复杂度判断、重规划 |
| Context | 追加、全量摘要压缩、摘要字段 |
| Safety | 注入检测、密钥过滤 |
| Images | 图片进入 meta、多模态参数 |
| Trace | trace_id、span 查询 |

### 13.2 真实请求

2026-11-13 实测：

| 场景 | 结果 |
| --- | --- |
| 模型与图文 | `deepseek-flash`，`degraded: false`，图片颜色识别正确 |
| 语气 | 温和 75 字共情、效率 78 字结论先行、标准 264 字完整结构 |
| 档案写入 | 称呼/性别/饮食风格/身高/体重/备注写入成功，BMI 重算 |
| 档案锁定 | 带旧身高 170 的 `client_context` 不覆盖 Agent 写入的 190 |
| 专家组合 | `meta.skills` 与 `skill` 事件均包含医学与健身专家 |
| 图片缓存 | 上传识别 → 指代追问命中 `image_source: cache` → 无关问题不复用 |
| 上下文 | 窗口 1000000；压缩后消息段 0；达上限返回 `context_limit` |
| 权限 | `full` 下更新档案自动执行；`full` 下记录午餐仍需确认；`manual` 下更新需确认 |
| 市场 | 4 个社区精选；安装、已安装、卸载通过 |
| 导入 | 问答模式手动导入 200；市场安装问答模式 403 |

### 13.3 环境限制

- 真实模型需要 Agent 进程能访问外网。受管沙箱可能拦截出站 443，此时自动降级并提示。
- 部分环境限制回环 TCP，单测在进程内直接调用路由与事件生成器，不占真实端口。
- 本机无系统 Node，前端语法检查使用 IDE 内置 Node v22.21.1。
- Windows 系统代理可能干扰模型请求，服务默认直连，可用 `EAT_LLM_PROXY` 显式配置。

### 13.4 回归清单

- 问答模式看不到 `get_menu`、`record_meal`、`update_health_profile`。
- 工作模式写操作必须确认；`full` 下 `record_meal` 仍确认。
- 语气切换后回答风格变化。
- 专家多选后 `skill_ids` 生效，输入框显示标识。
- 图片最多 10 张，粘贴可用，追问可复用。
- 上下文面板五段数据与压缩按钮可用。
- 市场安装、卸载、导入可用；导入不要求工作模式。
- 档案写入后跨轮读取一致，旧快照不覆盖。
- 模型失败时降级并提示，不静默。
- 前端 `node --check` 通过，页面加载触发 `meta`、`models`、`skills`、`context` 请求。

## 14. 安全与协作约定

### 14.1 密钥

- API Key 只放环境变量或被忽略的 `.env`。
- 不提交 `.env`，不在日志、文档、回答中回显明文。
- 服务端令牌同理。

### 14.2 边界

- 不修改 C++ 服务端代码与数据库；Agent 只通过 `ServiceClient` 调用。
- 不引入 MySQL/Redis 驱动。
- 前端改动以用户明确点名范围为准，改前先确认远端与本地状态。
- 健康建议不做诊断、不替代医生。

### 14.3 提交

- 提交前先 `git fetch` 检查远端；有新提交时先 `stash -u` → `pull --ff-only` → `stash pop`，在最新代码上重新验证后再提交。
- 提交前运行 `python -m unittest agent.tests.test_smoke` 与前端 `node --check`。
- 密钥文件不进入暂存区。
- `.codeartsdoer/package.json` 等 IDE 环境文件默认不随功能改动提交。

### 14.4 文档

- AGENT.md 只写当前事实，不写历史迭代流水账。
- 接口、字段、环境变量、权限与验证结果必须与代码一致。
- 新增或变更接口时同步更新第 9 章与第 12 章。
- 缺口单列，不把未实现能力写成已完成。

## 15. 附录

### 15.1 术语

| 术语 | 含义 |
| --- | --- |
| 问答模式 `qa` | 只读对话与建议 |
| 工作模式 `work` | 可查询菜单、写入记录、管理技能 |
| `manual` | 所有写操作需确认 |
| `full` | 除记录三餐外自动执行写操作 |
| 专家 | 可选择的 Skill，可多选组合 |
| 语气 | 只控制表达风格的 Skill |
| 社区精选 | 启动时写入市场的模拟技能 |
| `compacted_until_id` | 已压缩到的消息 ID |
| `overridden_fields` | Agent 已写入、需锁定防止前端旧快照覆盖的档案字段 |

### 15.2 排查

| 现象 | 排查 |
| --- | --- |
| 一直降级到本地规则 | 看 stderr 的 `[agent-llm]` 或 `meta.llm.last_error`；确认 Key、代理与出站网络 |
| 出现 `hit restricted [127.0.0.1:7897]` | 请求走了系统代理；确认 `proxy_mode=direct`，或显式设置 `EAT_LLM_PROXY` |
| 图片无法识别 | 确认模型声明 `vision`；检查 `notice` 与 `image_source` |
| 写操作不执行 | 确认模式为 `work`、已带 `confirmed_tools`；`record_meal` 在 `full` 下仍需确认 |
| 上下文百分比不下降 | 压缩后消息段归零，但摘要计入系统段；长对话下降明显，短对话不明显 |
| 档案写入后又变回旧值 | 检查是否被 `client_context` 覆盖；Agent 已写字段应被锁定 |
| 前端没有工具栏 | 检查 `agent-adapter.js` 语法与浏览器控制台；确认 Agent 服务在线 |
| 市场为空 | 确认 SQLite 已初始化且 `SkillMarket.seed()` 执行；重启服务 |