# 重构与去冗余方案（四技能协同）

> 纪律来源：`code-refactoring`（行为不变、小步、先测后改）、`design-pattern-applier`（先诊断后开方、最小侵入）、`dead-code-eliminator`（扫描→确认→删除）、`code-reviewer`（风险复审）。

## 1. 前置条件（不可跳过）

1. 先建立可重复的测试基线：`ctest`（C++）、`python -m unittest agent.tests.test_smoke`、`node --check`（前端）。
2. 基线未绿不得重构；先修 S0 的构建/测试缺陷。
3. 每步 ≤3 文件或 ≤2000 行；超出时列清单与风险，征得确认后分批。
4. 一步一提交；失败立即回退，不在失败状态叠加改动。
5. 重构与功能变更分离：合并/新增功能在 S1/S2 完成，S3 只做结构整改。

## 2. 冗余清单（扫描结果，删除需确认）

| 分类 | 对象 | 证据 | 处置 |
|---|---|---|---|
| 整目录 | `chatroom-backend/`（除迁移/测试参考） | 两套网络栈、TUI/WS/Web、history evidence | S2 后删除（Git 保留） |
| 整目录 | `transit/`、`chatroom-backend/web`、TUI `chatroom-backend/client` | 规格禁 WebSocket；App 替代 TUI | 删除 |
| 重复协议实现 | `chatroom-backend` 的 `\n` 分帧与根 `shared/protocol` | 两套帧语义 | 只留 `shared/protocol` |
| 死配置 | `Config.h` 中 20+ 无消费者字段 | `config_` 仅在 `TcpServerApp` 6 处使用 | 接线或删除，禁止"配置存在但不生效" |
| 死代码（C++） | `BusinessExecutor` 无异常隔离/无超时回执；`HeartbeatMonitor` 每帧入堆；`TcpServerApp::OnTimer` O(n) 扫描；`Backpressure::OnDrained` 非原子 | 并发与容量缺陷 | S0/S1 修复（属缺陷，不是重构） |
| 死代码（Agent） | `ContextManager.history()`、`Planner.render_plan_prompt()`、`EventBus.drain()`（无调用）、`_ensure_simulated` | 全仓 grep 无调用方 | 删除或接线（`watchdog` 需要接线到 SSE） |
| 死代码（前端） | `#agentExpertButton`/`openExpertMenu`、`#agentAccessDialog`/`openAccessSheet`、`updateModelLabel`、`shareSkill` 未绑定、`#agentExpertMenu` | 元素不存在或事件未绑定 | 删除或补齐入口（二选一，S2 定） |
| 契约漂移 | 图片上限 4 vs 10；随机比例 60/40 vs 70/30；`#profileForm`/`#reportSummary`/`#scoreValue` 不存在 | `agent/core/frontend.py` vs `safety.py`/`menu.py`/HTML | 以实现为准修正契约端点 |
| 测试冗余 | `chatroom-backend/tests`（P7 8 脚本 + P8 25 文件）与根 `tests/` | 对象退役后失效 | P8 用例迁入 `tests/integration/p8/`，P7 脚本归档 |
| 文档冗余 | `AGENT.md`（1101 行，Web 时代）与 `AGENT_Linux.md`（495 行，交接） | 大量重叠、口径不一 | 合并为 `docs/architecture.md` + `docs/handoff.md` |
| 目录冗余 | 根目录前端资源与 App 资源 | App 需要同一份页面 | 移入 `client/resources/web/`，根目录不保留副本 |
| 依赖冗余 | `glog`（仅 chatroom-backend） | 新服务端自有 `Log` | 合并后移除 |

## 3. 四技能执行顺序

| 顺序 | 技能 | 输入 | 产出 | 门禁 |
|---|---|---|---|---|
| 1 | **dead-code-eliminator** | 全仓（排除 `.git`/`build`/`evidence`） | 未引用类/函数/变量/配置/测试清单 | 只出报告，不删除 |
| 2 | **code-refactoring** | 已确认的删除清单 + 高复杂度模块 | 小步重构（提取方法/拆类/消重/参数对象） | 每步测试全绿 |
| 3 | **design-pattern-applier** | 长 if/else、重复创建、难测模块 | 模式改造（见第 4 节） | 行为不变 + 新扩展点可单测 |
| 4 | **code-reviewer** | 合并后的 `server/`、`client/`、`agent/` | 风险清单（并发/内存/注入/资源/信息泄露） | 高危项清零或登记 |

## 4. 重构与模式映射（诊断 → 手法）

| 目标 | 现状问题 | 手法 | 预期收益 |
|---|---|---|---|
| `FrameDispatcher` 帧/方法分发 | `Handle()` 内按 frame_type 长 if + method 字符串比较 | **Command + Registry**：`MethodHandler{method, is_write, handle}` 注册表 | 新增 method 只注册，不改分发器 |
| 业务 Handler 层 | P8 六个 Controller 各自解析/校验/渲染 | **Template Method**：`HandlerBase`（解析→鉴权→Service→信封） | 消除 6 份重复胶水 |
| 缓存降级 | 无 CacheClient，降级逻辑散落 | **Strategy + Null Object**：`CacheMode{normal, degraded, disabled}` | 停 Redis 行为可替换、可测 |
| DAO | 手写 SQL + 三态返回，散落各 Service | **Repository**：统一 `Lookup{Found,NotFound,Unavailable}` 与条件更新 | DAO 可替换、Service 不碰 SQL |
| 配置 | 长 if 链、未知键即失败、死字段 | **Builder + 显式 Schema 表**：键→类型→校验→是否热更 | 死配置可被静态发现 |
| 登录/鉴权 | 顺序校验散落 | **Chain of Responsibility**：token→UID 形状→白名单→业务 | 顺序不可被绕过，易加审计 |
| 前端传输 | 页面直连 fetch | **Adapter/Strategy**：`eatTransport` 接口 | 浏览器开发与 App 交付共用一套页面 |
| Agent Worker | 单例 Runtime 冒充隔离 | **先登记风险，S3 只做接口收敛**；真正隔离属功能变更 | 避免把架构改造混进重构 |

## 5. 删除审批流程

1. `dead-code-eliminator` 输出清单（路径、符号、引用计数、证据）。
2. 逐项标注：删除 / 接线 / 保留（附理由）。
3. 用户确认后执行；每批 ≤3 目录或 ≤2000 行。
4. 删除后跑全量测试；失败回退该批。
5. 证据写入 `evidence/s3/`。

## 6. 重点审查对象（code-reviewer）

| 对象 | 已发现风险 |
|---|---|
| `server/tcp/TcpServerApp.cc` | `active_connections_` 双重递减导致下溢；`ConnContext` 跨线程无锁；64 帧后不续读；`Stop` 不停 `server_`/`loop_`；信号处理调用非 async-signal-safe 代码 |
| `server/tcp/HeartbeatMonitor.cc` | 每帧入堆 + 无锁读写 + `Remove` 不清理堆 |
| `server/cache/SingleFlight.cc` | loader 抛异常导致状态泄漏；超时后 stampede |
| `server/cache/CacheKeyBuilder.cc` | cursor/idem_key 被小写化，分页/幂等键冲突 |
| `agent/app.py` | `/api/tools/call` 的 `confirmed` 用 `bool("false")==True` 绕过确认；可任意指定 `user_id` |
| `agent/data/service_client.py` | 服务端失败静默降级 mock，前端无法区分 |
| `agent/tools/diet_tools.py` | `delete_meal`/`update_meal` 只回 `meal_action`，不落库 |
| `client/transport/TcpTransport.cc` | 首连握手先于读线程；`send` 无 `MSG_NOSIGNAL`；心跳只发不收 |
| `tools/scan_repo.py` | 排除 `chatroom-backend`，合并后应重新纳入扫描 |

## 7. 完成判据

- 全仓无未引用符号（脚本输出可复现）。
- 单函数 ≤50 行（新代码；存量按阶段收敛）。
- 无"配置存在但无消费者"。
- 无重复协议/重复网络栈/重复测试树。
- 文档与实现一致（契约端点、DOM、上限、比例）。
- `ctest` + `pytest` + `node --check` 全绿，且新增测试覆盖修复过的缺陷。