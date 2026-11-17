# 路线 A 实现交接：保留 Agent 的完整客户端 + 服务端

> 决策日期：2026-11-17
> 分支：`feat/app-transport-scale`
> 当前提交：`1a54ee6`
> 状态：**仅记录方案，尚未修改业务代码**。等待 Linux + MySQL/Redis 环境后继续。

## 1. 路线决策

采用 **路线 A：保留 Python Agent 的三层架构**。

```text
Windows GUI（Win32 + WebView2）
  │  本地页面：index.html / app.js / bridge.js / agent-adapter.js
  │  postMessage + 命名管道（4B LE + JSON）
  ▼
Python Agent（eat-agent.exe）
  │  LLM / RAG / MCP / Skill / 工具编排
  │  ServiceClient → 自研 TCP 帧
  ▼
C++ 服务端（Linux + Muduo + MySQL/Redis）
```

明确不采用路线 B（去掉 Agent 的纯 C++ 客户端），因为会丢失 `AGENT.md` 定义的对话、RAG、MCP、Skill 与工具编排能力。

## 2. 目标交付定义

“全部实现”指以下四件事同时成立：

1. **客户端可用**
   - 用户只拿到一个 `eat-app.exe`（允许首次启动释放内部资源）；
   - 双击后能注册/登录、维护档案、记录三餐、查看统计、菜单、好友、排行；
   - AI 对话、Skill 市场、RAG、MCP 仍可用；
   - 不要求用户安装 Python、Node 或手动配置 `web/`。
2. **服务端可用**
   - Linux 单机 Docker 或裸机启动；
   - 认证、档案、餐记录、统计、菜单、好友、排行全部走 TCP 帧；
   - MySQL 迁移完整；Redis Cache-Aside 可用；
   - 真实请求跑通。
3. **传输符合既有设计**
   - 40B 帧 + CRC32C + JSON body；
   - 60s 心跳；禁 WebSocket 推送；禁 Redis pub/sub；
   - 客户端不直连 MySQL/Redis；服务端零 LLM 流量。
4. **发布可用**
   - README 重写；
   - GitHub Release 提供 Windows 客户端包与校验说明；
   - 交付证据包含启动截图、服务端日志、真实请求记录。

## 3. 当前代码事实（2026-11-17）

| 模块 | 现状 | 缺口 |
|---|---|---|
| `server/business/` | 只有 `profile/` | `meal/stats/food/friend/ranking` 未迁入 |
| `server/app/ServiceRegistry.cc` | 只注册握手、`auth.*`、`profile.*` | 缺 6 类业务 Handler |
| `server/cache/` | 有 `CacheKeyBuilder/CacheValue/InvalidationSet/SingleFlight/TtlPolicy` | 缺 `RedisClient`/`CacheClient`，未接线 |
| `server/database/` | 只有 `MySQLPool`/`DbConn` | 无 Redis 连接层 |
| `agent/data/service_client.py` | 仍是 HTTP，指向旧 `:8080`，未配置时 mock | 需改 TCP 帧 |
| `agent/ipc_server.py` | 只处理 `hello/ping/agent.chat/shutdown` | 不处理 `service.request` |
| `bridge.js` | App 模式发 `agent.chat` 与 `service.request` | 壳未接 TCP |
| `client/app/win32/main.cc` | WebView2 壳 + IPC，未实例化 `TcpTransport` | 无业务桥接、无登录/服务器配置 |
| `client/transport/TcpTransport.*` | 已有帧循环、心跳、重连骨架 | 需接入壳与 Agent |
| `dist/eat-app/` | `eat-app.exe + WebView2Loader.dll + web/` | 不是单 exe |
| Agent 打包 | 无 `eat-agent.exe` | 需 PyInstaller/运行时释放 |

`chatroom-backend/` 已在 `1a54ee6` 删除；业务代码只能从 Git 历史恢复：

```bash
git show 1a54ee6^:chatroom-backend/server/http/service/MealService.cc
git show 1a54ee6^:chatroom-backend/server/http/dao/MealDao.cc
```

## 4. 实施清单

### 4.1 服务端业务恢复

来源：`1a54ee6^:chatroom-backend/server/http/{service,dao}`。

迁移目标：

```text
server/business/meal/
server/business/stats/
server/business/food/
server/business/friend/
server/business/ranking/
```

必须保留 P8 业务语义：

- 餐记录幂等：同键同内容返回首次结果，同键异内容冲突；
- 营养缺口 `gap = max(0, target - intake)`；
- 周统计、健康分算法、服务端时区；
- 菜单 16 道种子、展示 14 道、过敏/忌口过滤；
- 好友仅 `status=2`，正反双向去重；
- 排行含本人、仅好友、名次连续。

必须修正：

- `agent_uid` → `user_key`；
- `FoodService::defaultUserId` 下线，身份只来自握手 `ConnContext.user_key`；
- `MySQLPool::borrow()` → `borrow_for(200ms)`；
- `ServiceResult` 扩展 `RateLimited/Overloaded/NotFound`；
- 日志不得输出密码、token、body 原文。

### 4.2 TCP Handler

在 `ServiceRegistry` 注册：

```text
meal.create
meal.recent
meal.patch
meal.delete
stats.nutrition_gap
stats.weekly
food.menu
food.search
friend.list
friend.request
friend.accept
friend.remove
friend.block
ranking.friends
```

Handler 只做 JSON → Service → JSON，不依赖 HTTP，不直接读 `httplib`。

### 4.3 Cache-Aside

补 `server/database/RedisClient` 与 `server/cache/CacheClient`：

- 命令白名单：`GET/SETEX/DEL/MGET/EXISTS/PING`；
- 禁 `PUBLISH/SUBSCRIBE/SCAN/KEYS`；
- 读：Redis → 未命中 MySQL → 回写；
- 写：MySQL 提交成功后同步 DEL 多键；
- 空结果写负缓存短 TTL；
- Redis 故障降级直查 MySQL。

### 4.4 Agent 数据通道

把 `agent/data/service_client.py` 从 HTTP 改为 TCP 帧：

- 握手参数来自客户端配置；
- 方法名与 `ServiceRegistry` 一致；
- 保留 `source=service/mock` 语义，但**生产模式禁止 mock 冒充真实数据**；
- 超时、错误码、request_id 贯通。

### 4.5 GUI 桥接

在 `client/app/win32/main.cc`：

- 实例化 `TcpTransport`；
- 处理页面 `service.request`：
  - 把 `path` 映射到帧 `method`；
  - 把 `init.body` 映射到 `params`；
  - 把响应回注 WebView2；
- 增加服务器地址、登录/注册、令牌本地保存；
- 保留托盘、单实例、WebView2 页面；
- Agent 仍通过命名管道处理 `agent.chat`。

### 4.6 单 exe

目标：用户只拿到 `eat-app.exe`。

方案：

1. `WebView2LoaderStatic.lib` 静态链接，去掉外置 `WebView2Loader.dll`；
2. `web/` 资源嵌入 exe（RCDATA 或 zip），首次启动释放到 `%LOCALAPPDATA%\EatApp\web`；
3. `eat-agent.exe` 由 PyInstaller 打包，作为资源嵌入，首次启动释放到 `%LOCALAPPDATA%\EatApp\agent`；
4. 壳启动时拉起释放后的 Agent，命名管道名保持 `\\.\pipe\EatAppAgent-{sid}-{instance}`；
5. 目标机仍需 WebView2 Runtime（Win11 通常自带；Win10 需安装 Evergreen Runtime）。

### 4.7 验证与发布

Linux：

```bash
docker compose -f deploy/docker/docker-compose.yml up -d --build
docker compose -f deploy/docker/docker-compose.yml logs -f eat-server
sudo ufw allow 9443/tcp
ss -ltn | grep 9443
```

Windows：

- 双击 `eat-app.exe`；
- 注册/登录；
- 记录一餐并刷新统计；
- 查看菜单、好友、排行；
- AI 对话正常流式返回；
- 断网/重连、幂等重发。

发布：

- README 更新为单 exe 使用方式；
- Release 资产：`eat-app-windows-x64.zip`（如仍需外置资源则改为安装包）；
- 发布说明写明已验证项与未验证项。

## 5. Linux 环境准备

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev libmysqlclient-dev \
  default-mysql-client mysql-server redis-server docker.io docker-compose-plugin
```

Muduo：

```bash
git clone --depth 1 --branch v2.0.2 https://github.com/chenshuo/muduo.git ../muduo
cmake -S ../muduo -B ../muduo/build -DCMAKE_BUILD_TYPE=Release
cmake --build ../muduo/build -j"$(nproc)"
```

服务端：

```bash
git pull
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEAT_MUDUO_ROOT=../muduo
cmake --build build -j"$(nproc)" --target eat-server client_smoke
```

数据库迁移顺序：

```text
migrations/base/000_init.sql
migrations/base/001_p8_business.sql
migrations/base/foods_seed.sql
migrations/add/002_identity.sql
migrations/add/003_session.sql
migrations/add/004_friend_relation.sql
migrations/add/005_meal_partition.sql
migrations/add/006_event_outbox.sql
```

## 6. 验收清单

- [ ] Linux `ss -ltn` 显示 `0.0.0.0:9443`
- [ ] 注册 / 登录 / 握手成功
- [ ] `profile.get` / `profile.patch` 真实落库
- [ ] `meal.create` 幂等：重发同键同内容只增 1 条
- [ ] `meal.patch` / `meal.delete` 后统计正确变化
- [ ] `stats.nutrition_gap` 满足缺口恒等式
- [ ] `stats.weekly` 与独立复算一致
- [ ] `food.menu` / `food.search` 过敏忌口过滤正确
- [ ] `friend.list` 双向去重
- [ ] `ranking.friends` 含本人、仅好友、名次连续
- [ ] Redis 写后 DEL、负缓存、故障降级
- [ ] GUI 内完成上述业务，不出现“本地模式”
- [ ] Agent 对话、Skill、RAG 正常
- [ ] 单 exe 冷启动，目标机只放一个 exe
- [ ] Release 资产可下载并运行

## 7. 风险与边界

- **Python 版本**：建议 PyInstaller 使用 Python 3.11/3.12；3.14 兼容性未验证。
- **单 exe 体积**：嵌入 Agent 运行时后体积会显著增大；WebView2 Runtime 仍依赖系统。
- **业务代码恢复**：P8 代码来自 Git 历史，删除后无源码树；恢复后必须重新编译与回归。
- **数据库**：无 MySQL 时不能宣称业务可用；不能用 mock 冒充。
- **Agent 工具**：MCP 子进程、Skill 资源、配置需要一起打包，不能只打包 `ipc_server.py`。
- **验证纪律**：只有真实请求 + 截图/日志才算完成；编译通过不算。

## 8. 交接状态

- 本文档记录路线 A 与实施清单。
- 本轮**没有修改** `server/business`、`agent/data/service_client.py`、`client/app/win32/main.cc` 的业务逻辑。
- 下一轮在 Linux 上继续：先恢复 `chatroom-backend` 的 Service/DAO，再补 Handler 与 CacheClient，随后做 GUI/Agent 桥接和单 exe。
- 开始前先 `git pull --ff-only`，确认远端无新提交。
