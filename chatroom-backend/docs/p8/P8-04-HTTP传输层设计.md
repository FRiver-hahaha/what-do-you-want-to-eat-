# P8-04 HTTP 传输层设计（P8-2 修订版）

> 选型与全部边界条件均基于现有代码事实：web/httplib.h 已在仓库（cpp-httplib，web 目标已用其托管静态页）。

## 1. 选型：cpp-httplib（复用仓库现有 web/httplib.h）

| 备选 | 结论 | 理由 |
|---|---|---|
| **cpp-httplib（采纳）** | ✅ | 零新依赖；header-only；自带请求解析/Keep-Alive/Content-Length/线程池/超时；学长仓库已在用，风格一致 |
| 手写 HTTP 解析挂进 Reactor | ❌ | 重造轮子；动现有稳定网络层；HTTP 解析状态机复杂度高 |
| 引入 Crow/Pistache 等 | ❌ | 新增外部依赖；与"最小改动"原则冲突 |

## 2. 服务器骨架（main_http.cc 设计）

```cpp
httplib::Server svr;
svr.set_read_timeout(5, 0);            // 读超时 5s（对齐 Agent 客户端超时）
svr.set_write_timeout(5, 0);
svr.set_keep_alive_timeout(10);         // Keep-Alive
svr.set_keep_alive_max_count(100);
svr.set_payload_max_length(64 * 1024);  // 请求体上限 64KB（业务 JSON 远小于此）
svr.set_default_headers({{"Server", "EatBackend/1.0"}});
svr.set_exception_handler(...);         // 统一 500 → 2xx 错误体约定
// 路由注册（见 P8-06）
svr.listen("127.0.0.1", 8080);
```

绑定地址经 `CHATROOM_HTTP_BIND`（默认 127.0.0.1）与端口 `CHATROOM_HTTP_PORT`（默认 8080）环境变量控制（复用 server/common/Config.h 的 envOr/envInt）。

## 3. 各边界条件处理

| 需求 | httplib 现成能力 | 设计值 |
|---|---|---|
| HTTP 请求解析 | 内置（RFC 7230 级别） | — |
| 响应序列化 | set_content(body, "application/json; charset=utf-8") 自动带 Content-Length | 所有响应统一 JSON |
| Keep-Alive | 内置 | 开（Agent urllib 每次新建连接，Keep-Alive 对其无收益但无害） |
| 请求体上限 | set_payload_max_length | 64KB（record_meal 含 items[] 文本，64KB 富余 100 倍） |
| JSON 解析 | nlohmann::json::parse（try/catch） | 解析失败 → 错误体 invalid_params |
| 超时 | read/write timeout | 5s/5s |
| 并发连接 | httplib 动态线程池：基础 `max(8, cores-1)`，峰值 ×4，空闲 3s 回收（httplib.h:161-173 原文） | 见 P8-08 §1.1 |
| 错误响应 | 统一 2xx + {ok:false,error,message} | 见 P8-02 §5 |
| CORS | 不需要 | 调用方是 Agent（Python 服务器到服务器），浏览器从不直连 8080（app.js/agent-adapter.js 全部指向 :8787）；为防未来直连误用，显式**不加** CORS 头 |
| TLS | 暂不启用 | 本机回环/内网部署；生产由 nginx 反代终止 TLS（部署文档注明） |

## 4. 请求/响应生命周期

```
httplib worker 线程接受完整请求（含 body）
  → Router（前缀 + 参数提取，uid 路由统一正则 **`[A-Za-z0-9_-]{1,64}`**（R15：允许连字符）；任何解析失败 → invalid_params，**不进入 Auth 之后任何层**）
  → Auth 中间件（Bearer 校验）
  → Controller（json::parse(body) → 校验 → Service → DAO）
  → 组装响应 JSON → set_content → 线程归还池
```

- 全过程无全局锁（除 MySQLPool 借还的短临界区）。
- 单请求 SQL 总时长设计目标 < 4s（预留 1s 余量）——全部为单表/双表短查询，**理论预期 ms 级（未实测，实测回填 P8-08 §3，未测前不写耗时结论）**。
- **超时语义边界（R8）**：HTTP read/write timeout 与 borrow_for 都**不是 SQL 执行超时**；客户端超时/断开后服务端 SQL 可能继续执行并提交（P8-08 §2.3 五问裁定）。
- 慢查询防护：所有 DAO 查询走索引（P8-07），无全表扫描路径。

## 5. 路由表（与 P8-02 一一对应）

| 方法+路径 | Controller |
|---|---|
| GET /api/v1/users/{uid}/health-profile | ProfileController::get |
| PATCH /api/v1/users/{uid}/health-profile | ProfileController::update |
| GET /api/v1/users/{uid}/meals | MealController::list |
| POST /api/v1/users/{uid}/meals | MealController::create |
| GET /api/v1/users/{uid}/nutrition-gap | StatsController::gap |
| GET /api/v1/users/{uid}/weekly-stats | StatsController::weekly |
| GET /api/v1/users/{uid}/ranking | StatsController::ranking |
| GET /api/v1/users/{uid}/friends | StatsController::friends |
| GET /api/v1/foods/search | FoodController::search |
| GET /api/v1/menu | FoodController::menu |
| GET /health | 健康检查（{ok:true,version,db:bool}） |

未匹配路径 → 200 + {ok:false,error:"not_found"}（保持 Agent 可解析语义，与 2xx 约定一致）。

## 6. 时区与时间（可执行配置方案，P8-2 修订）

| 项 | 配置（必须落地，不是口头约定） |
|---|---|
| 容器部署 | Dockerfile/env：`TZ=Asia/Shanghai`；镜像内 tzdata 已装 |
| systemd 部署 | unit 文件 `Environment=TZ=Asia/Shanghai`；或 `/etc/localtime -> /usr/share/zoneinfo/Asia/Shanghai` |
| 进程自检（S1 编码） | main_http 启动时 `localtime` 计算当前 UTC 偏移，非 +08:00 → **WARNING 日志**（可配置 FATAL：CHATROOM_TZ_STRICT=1） |
| 语义 | "今天" = localtime 的 YYYY-MM-DD；周起点 = 本地时间的周一（对齐 app.js getWeekKey 周一定义：`(getDay()+6)%7` 回溯） |
| 禁止 | UTC 日期计算（G-7）；DB 连接不设 time_zone 转换（DATE 无时区，写入前已按本地时区格式化） |
| 验证 | S1 用例：进程日志偏移 +0800；跨日测试（23:59 写入 → 次日查询边界） |

## 7. 日志

- 复用 glog：每请求一行 INFO（method/path/uid/耗时 ms/结果码）。
- 鉴权失败、SQL 异常 ERROR 级（含上下文，不含 token 明文/用户敏感数据）。