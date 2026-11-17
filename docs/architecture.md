# Eat 架构说明（合并后 · 2026-11-17）

> 本文是当前代码事实的权威入口。历史文档：`AGENT.md`（Agent 能力）、`AGENT_Linux.md`（上一轮交接）、`chatroom-backend/*.md`（P7/P8 基线，已退役）。

## 1. 三线合并结果

| 原线 | 现状 |
|---|---|
| ① 浏览器 + Python Agent | 页面与 Agent 能力保留；页面改为经 `bridge.js` 传输抽象运行，App 内走 IPC，浏览器仅开发调试 |
| ② `chatroom-backend/` P7/P8 | 业务语义、SQL、测试用例作为迁移源；网络层与 HTTP 语义层退役（目录暂留，未参与构建） |
| ③ 根 `server/` + `shared/protocol` | 唯一服务端实现：Muduo 主从 Reactor + 40B 帧 + JSON + MySQL + 通用线程池 |

## 2. 运行形态

```
Windows App (Win32 + WebView2, 本地页面)
  ├─ bridge.js  ── postMessage ──▶ 壳
  ├─ TcpTransport ── 1 条业务 TCP ──▶ eat-server (Linux, :9443)
  └─ 命名管道 ──▶ eat-agent.exe (Python, 能力本体)
                      └─ LLM HTTPS/SSE（不承载业务数据）

eat-server ── MySQL（账号/档案/餐记录/好友/事件）
           ── Redis（缓存，仅服务端；当前 CacheClient 未接线）
           ── healthz/metrics（回环，规划中）
```

## 3. 已打通的业务链路（S1）

协议方法（`server/app/ServiceRegistry.cc`）：

| method | 行为 |
|---|---|
| 握手 | `access_token` 校验；或 `login_id/password` 登录；或 `action=register` 注册后登录；返回 `user_key`/`session_id`/tokens/心跳参数/能力位 |
| `auth.login` / `auth.register` | 已握手连接上的登录与注册 |
| `profile.get` | 按 `ConnContext.user_key` 返回档案；`source=service` |
| `profile.patch` | 字段白名单/截断/BMI 现算；写 `health_profile`；写方法必须有 `idem_key` |

身份：`account`/`session`（迁移 002/003），scrypt 密码，令牌只存 SHA-256；登录失败 5 次锁 5 分钟。

## 4. 构建与运行（Linux）

```bash
# 依赖
sudo apt install -y build-essential cmake libssl-dev libmysqlclient-dev libhiredis-dev

# Muduo（已有源码树时直接指定）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
      -DEAT_MUDUO_ROOT=/path/to/muduo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

# 数据库（示例为用户态/容器 MySQL）
mysql < migrations/base/001_p8_business.sql   # 先 init.sql + 001
for f in migrations/add/00{2,3,4,5,6}_*.sql; do mysql < "$f"; done

# 启动（密码只走环境变量）
EAT_MYSQL_PASSWORD=*** ./build/eat-server --config deploy/server.toml.example

# 真实链路
./build/client_smoke --host 127.0.0.1 --port 9443 \
  --login u@test.local --password pass-123456 --register --patch-weight 68
```

环境变量覆盖：`EAT_MYSQL_HOST/PORT/DB/USER/PASSWORD/SOCKET`、`EAT_REDIS_*`。

## 5. Windows App（S2，未在 Windows 验证）

- 壳：`client/app/win32/main.cc`（Win32 + WebView2；单实例；托盘；关窗进托盘；`file:///.../web/index.html`）。
- 页面：根目录资源在构建时复制到安装目录 `web/`，禁止远程首屏。
- 传输：`bridge.js` 暴露 `window.eatTransport`；App 模式 `postMessage`，浏览器模式 `fetch + SSE`。
- Agent：`agent/ipc_server.py`，4B LE + JSON，≤1MB；Windows 命名管道 `\\.\pipe\EatAppAgent-{sid}-{instance}`，POSIX 用 `AF_UNIX` 便于 CI。
- 构建：`client/app/CMakeLists.txt`，需 WebView2 SDK（`WEBVIEW2_ROOT`）；`eat-agent.exe` 需 PyInstaller/embeddable 打包；MSIX 签名待发布环境。

## 6. 测试现状（本机实测）

| 项 | 结果 |
|---|---|
| C++ `ctest` | **12/12 通过**（协议、JSON、连接治理、线程池、IPC、配置、缓存、身份、令牌、离线队列、KeyVault 平台分支） |
| `eat-server` 构建 | 通过（Muduo + MySQL 客户端） |
| S1 集成 `tests/integration/s1_smoke.sh` | **通过**：注册→登录→握手→`profile.get`→`profile.patch`(68)→读回；缺 `idem_key`→3001；未握手→2001；无效 token→2002 |
| Agent 单测 | **51/51 通过** |
| IPC smoke `tests/integration/ipc_smoke.py` | **通过**（hello/ping/agent.chat，122 事件） |
| 前端语法 | `node --check` app.js / bridge.js / agent-adapter.js 通过 |
| Windows App | **未验证**（当前 Linux 无 WebView2/MSVC/DPAPI） |

## 7. 已修正的合并缺陷

1. `005_meal_partition.sql`：幂等账本主键改为 `(id, created_day)`，满足分区唯一键必须包含分区列（MySQL 1503）。
2. `001_p8_business.sql`/`foods_seed.sql`：CRLF 与 `--注释` 缺空格导致 MySQL 8.4 1064；迁移执行前需统一换行与注释格式（已登记，脚本暂用流式清洗）。
3. `TcpServerApp`：`FrameCodec` 头文件/命名空间、`Buffer::peek` 类型转换、连接计数下溢、64 帧后续读、`OnTimer` O(n) 扫描。
4. 信号处理不再在 handler 内执行 `Stop()`，改为 `loop->quit()` 后有序关闭。
5. 配置新增 `mysql_password/mysql_socket/redis_password`，支持环境变量覆盖；禁止把密码写入仓库配置。

## 8. 未完成与阻断

| 项 | 状态 |
|---|---|
| Redis `CacheClient`（GET/SETEX/DEL/SingleFlight 接线、写后 DEL、熔断） | 未接线（T5.1/T5.8） |
| `meal.create/recent`、`stats.*`、`food.*`、`friend.*`、`ranking.*` 帧化 | 未迁移（P8 Service/DAO 仍在 `chatroom-backend`） |
| `meal.patch/delete` 落库 | 未实现；Agent 侧明确标记 `persisted=false` |
| `health_profile.agent_uid` → `user_key` 列名迁移（007） | 未执行；当前以 `user_key` 值写入旧列名 |
| TLS/SPKI、Windows 安装包签名 | 未实现 |
| 100 万在线压测 | **阻断**，无实测评测数据 |
| Web 退场 | 未执行（App 功能对等未验收） |

## 9. 下一步顺序

1. 完成 `chatroom-backend` 的 Service/DAO 迁移并注册 `meal.*`/`stats.*`/`food.*`/`friend.*`/`ranking.*`。
2. 接 Redis CacheClient（写后 DEL、负缓存、熔断降级）并跑 S-06/07/08 用例。
3. 在 Windows 目标机编译运行 App，打通 WebView2 → IPC → Agent → TCP。
4. 执行 007 列名迁移，删除 `chatroom-backend/`，合并 AGENT.md/AGENT_Linux.md。
5. 只有完成上述并留下证据，才可讨论容量宣称与 Web 下线。