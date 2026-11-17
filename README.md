# 今天你吃了吗（Eat App）

跨端饮食助手：**Linux 服务端**（C++17 + Muduo + MySQL/Redis）+ **Windows 客户端**（Win32 + WebView2）+ **Python Agent**（LLM / RAG / MCP / Skill）。

> 状态（2026-11-17）：三线后端已合并，服务端在 Linux 上完成真实 TCP 验证；Windows GUI 尚未在目标机编译验证。
> 唯一权威现状见 [`docs/architecture.md`](docs/architecture.md)，交付与阻断见 [`docs/DELIVERY-STATUS.md`](docs/DELIVERY-STATUS.md)。

## 功能状态

| 能力 | 状态 |
|---|---|
| 注册 / 登录 / 握手鉴权（scrypt + 令牌哈希） | ✅ 已实现并实测 |
| `profile.get` / `profile.patch`（MySQL 落库、幂等键） | ✅ 已实现并实测 |
| 餐记录 / 统计 / 菜单 / 好友 / 排行 帧化 | ⛔ 未迁移（仍在 `chatroom-backend/`） |
| Redis 缓存（单飞、写后失效、熔断降级） | ⛔ 未接线 |
| Windows WebView2 GUI | ⚠️ 代码已落盘，未在 Windows 编译验证 |
| Agent（RAG / MCP / Skill / 规则降级） | ✅ 51 项单测通过 |
| 100 万在线 | ⛔ 阻断，无压测数据 |

## 架构

```
页面（本地资源）── bridge.js ──┬─ 浏览器 fetch/SSE（仅开发）
                              └─ WebView2 postMessage
                                        │ 命名管道（4B LE + JSON）
                                  eat-agent.exe（Python）
                                        │ LLM HTTPS/SSE
TcpTransport ── 1 条业务 TCP ──▶ eat-server（Muduo + MySQL/Redis）
```

## 快速开始

### 服务端（Docker，推荐）

```bash
export EAT_MYSQL_ROOT_PASSWORD='换成强密码'
docker compose -f deploy/docker/docker-compose.yml up -d --build
docker compose -f deploy/docker/docker-compose.yml logs -f eat-server
```

监听 `0.0.0.0:9443`，首次启动自动迁移；详见 [`docs/RUNBOOK.md`](docs/RUNBOOK.md)。

### 服务端（裸机）

```bash
sudo apt install -y build-essential cmake libssl-dev libmysqlclient-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEAT_MUDUO_ROOT=/path/to/muduo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
mysql -h 127.0.0.1 -uroot -p < migrations/00_all.sql   # 或使用 dist 包内脚本
EAT_MYSQL_PASSWORD='...' ./build/eat-server --config deploy/server.toml.example
```

### Windows 客户端

- 控制台真实请求客户端：解压 `dist/eat-client-windows.zip` → `build-client.ps1` → `client_smoke.exe`。
- GUI（WebView2）：按 [`AGENT_win.md`](AGENT_win.md) 在 Windows 上构建 `eat-app.exe`。

## 仓库结构

```
shared/protocol/     帧协议 / CRC32C / 严格 JSON（server、client、bench 同源）
server/              tcp、identity、app、business、database、cache
client/              Win32+WebView2 壳、TCP 传输、IPC、KeyVault
agent/               Python Agent（HTTP 开发入口 + IPC App 入口）
migrations/          002–006 增量迁移（001 基线仍随旧后端目录）
deploy/              Docker、systemd、sysctl、preflight
tests/               protocol / unit / integration / load
docs/                架构、运行手册、设计、协议
chatroom-backend/    P7/P8 历史基线（不参与构建，待退役）
```

## 测试

```bash
ctest --test-dir build --output-on-failure     # C++：12/12
python -m unittest agent.tests.test_smoke      # Agent：51/51
python tests/integration/ipc_smoke.py          # IPC
bash tests/integration/s1_smoke.sh             # 真实 TCP（需 MySQL）
```

## 贡献与安全

- 提交规范、分支与 PR 门禁见 [`CONTRIBUTING.md`](CONTRIBUTING.md)。
- 漏洞与密钥处理见 [`SECURITY.md`](SECURITY.md)。
- **禁止**提交 `.env`、Key、数据库密码、`dist/` 与构建产物。

## 许可

当前未声明开源许可证，默认保留所有权利。如需开源，请先在仓库补充 `LICENSE` 并经维护者确认。