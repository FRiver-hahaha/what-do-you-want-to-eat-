# 今天你吃了吗（Eat App）

跨端饮食助手：**Linux 服务端**（C++17 + Muduo + MySQL）+ **Windows 客户端**（Win32 + WebView2，复用现有 Web 页面）+ **Python Agent**（LLM / RAG / MCP / Skill）。

> 状态（2026-11-17）：三线后端已合并；服务端认证与档案读写已在 Linux 通过真实 TCP 验证；Windows GUI 已在本机完成编译并验证页面加载；`meal/stats/food/friend/ranking` 仍未帧化，GUI 与业务 TCP 的桥接尚未接线。

## 功能状态

| 能力 | 状态 |
|---|---|
| 注册 / 登录 / 握手鉴权（scrypt + 令牌哈希） | 已实现并实测 |
| `profile.get` / `profile.patch`（MySQL 落库、幂等键） | 已实现并实测 |
| 控制台客户端 `client_smoke.exe` | 已生成并验证完整握手/档案流程 |
| Windows GUI `eat-app.exe` | 已编译并验证启动、窗口与页面渲染 |
| GUI 业务数据 / Agent↔TCP 桥接 | 未接线（页面当前显示本地模式） |
| 餐记录 / 统计 / 菜单 / 好友 / 排行 帧化 | 未迁移（P8 历史代码已删除） |
| Redis 缓存 | 未接线 |
| 100 万在线 | 阻断，无压测数据 |

## 架构

```
Windows App (Win32 + WebView2)
  ├─ web/index.html、app.js、bridge.js（与浏览器同一套页面资源）
  ├─ bridge.js ── postMessage ──▶ 壳
  └─ 命名管道 ──▶ eat-agent.exe（Python，LLM/RAG/MCP/Skill）

client_smoke.exe ── 1 条业务 TCP ──▶ eat-server (Linux :9443)
eat-server ── MySQL（账号/档案/事件）
           ── Redis（规划中，当前 CacheClient 未接线）
```

## 从零启动

### 0. 前置条件

| 端 | 要求 |
|---|---|
| 服务端 | Linux x86_64；Docker + Docker Compose（推荐），或 CMake 3.16+、C++17、OpenSSL、MySQL 8、Muduo |
| 客户端 | Windows 10/11 x64；GUI 需要 WebView2 Runtime（Win11 通常自带）；控制台客户端无额外依赖 |
| 网络 | 两台机器在同一局域网，服务端 TCP `9443` 可达 |

### 1. 获取代码

```bash
git clone https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
git checkout feat/app-transport-scale
```

### 2. 启动服务端（Linux）

#### 方案 A：Docker（推荐，自动迁移）

```bash
export EAT_MYSQL_ROOT_PASSWORD='换成强密码'
docker compose -f deploy/docker/docker-compose.yml up -d --build
docker compose -f deploy/docker/docker-compose.yml logs -f eat-server
```

- 首次构建会从 GitHub 拉取并编译 Muduo，需要外网。
- 容器启动时自动执行 `migrations/base/000_init.sql`、`001_p8_business.sql`、`foods_seed.sql` 与 `migrations/add/002→006`。
- 服务端监听 `0.0.0.0:9443`。

#### 方案 B：裸机

安装依赖（Ubuntu 示例）：

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev libmysqlclient-dev \
  default-mysql-client mysql-server
```

构建 Muduo 与服务端：

```bash
git clone --depth 1 --branch v2.0.2 https://github.com/chenshuo/muduo.git ../muduo
cmake -S ../muduo -B ../muduo/build -DCMAKE_BUILD_TYPE=Release
cmake --build ../muduo/build -j"$(nproc)"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEAT_MUDUO_ROOT=../muduo
cmake --build build -j"$(nproc)" --target eat-server client_smoke
```

初始化数据库。SQL 基线已从旧目录迁入 `migrations/base/`：

```bash
{
  echo "CREATE DATABASE IF NOT EXISTS eat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
  echo "USE eat;"
  sed 's/chatroom/eat/g' migrations/base/000_init.sql | tr -d '\r'; echo
  echo "SET @seed_pwd_hash = SHA2('disabled-login', 256);"
  tr -d '\r' < migrations/base/001_p8_business.sql \
    | sed 's/^--\([^ ]\)/-- \1/'; echo
  tr -d '\r' < migrations/base/foods_seed.sql \
    | sed 's/^--\([^ ]\)/-- \1/'; echo
  for f in migrations/add/00{2,3,4,5,6}_*.sql; do
    case "$f" in *_rollback.sql) continue;; esac
    tr -d '\r' < "$f" | sed 's/^--\([^ ]\)/-- \1/'; echo
  done
} > /tmp/eat_00_all.sql

mysql -uroot -p < /tmp/eat_00_all.sql
```

启动服务端：

```bash
export EAT_MYSQL_PASSWORD='你的MySQL密码'
export EAT_MYSQL_USER=root        # 示例配置默认是 eat，按实际账号覆盖
./build/eat-server --config deploy/server.toml.example
```

确认监听与放行端口：

```bash
sudo ufw allow 9443/tcp           # 使用 firewalld 时执行对应放行命令
ss -ltnp | grep 9443              # 期望 0.0.0.0:9443
hostname -I                       # 记下服务端局域网 IP
```

### 3. 启动客户端（Windows）

#### 3.1 GUI `eat-app.exe`（推荐体验页面）

从 GitHub Releases 下载 `eat-app-windows-x64.zip`，解压后目录形态：

```text
eat-app-windows-x64\
  eat-app.exe
  WebView2Loader.dll
  web\              # 与仓库根目录同一套前端资源
    index.html
    app.js
    bridge.js
    agent-adapter.js
    styles.css
    features.css
    agent.css
    assets\
```

双击 `eat-app.exe` 即可启动。首次启动会在同目录创建 `webview-data\`。关闭窗口会进入托盘，右键托盘图标选择“退出”才会结束进程。

当前 GUI 边界：

- 窗口、单实例、托盘、本地 WebView2 页面已验证。
- 页面会提示“本地模式”，因为 `bridge.js` 的 App 模式与业务 TCP 尚未接线。
- GUI 内暂不能完成服务端注册/档案/餐记录请求；需要真实业务联调时请使用 `client_smoke.exe`。
- 同目录没有 `eat-agent.exe` 时，Agent 对话不可用；页面仍可正常加载。

#### 3.2 控制台客户端 `client_smoke.exe`

从 Releases 下载 `eat-client-windows.zip`，或从构建机拷贝 `client_smoke.exe`（单文件、静态链接）。解压后：

```text
eat-client-windows\
  client_smoke.exe
  README.md
```

运行：

```powershell
.\client_smoke.exe --help
```

#### 3.3 自己构建客户端（开发者）

控制台客户端，MSVC（“x64 Native Tools Command Prompt for VS 2022”）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

控制台客户端，MSYS2 / MinGW（生成单文件）：

```bash
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -I. \
  tests/integration/client_smoke.cc \
  shared/protocol/FrameCodec.cc \
  shared/protocol/Crc32c.cc \
  shared/protocol/JsonValue.cc \
  shared/protocol/StrictJson.cc \
  -o client_smoke.exe -lws2_32
```

GUI，标准 CMake + MSVC + WebView2 SDK：

```powershell
$env:WEBVIEW2_ROOT = "C:\path\to\Microsoft.Web.WebView2\build\native"
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --target eat-app
```

产物：`build-win\client\app\Release\eat-app.exe`，同目录 `web\` 由 CMake 自动复制。完整 GUI 构建与 MinGW 已验证命令见 [`docs/WINDOWS_CLIENT_LAN_RUNBOOK.md`](docs/WINDOWS_CLIENT_LAN_RUNBOOK.md)。

### 4. 客户端连接服务端

#### 4.1 网络检查

在 Windows 上：

```powershell
ipconfig
ping <服务端IP>
Test-NetConnection <服务端IP> -Port 9443
```

`TcpTestSucceeded : True` 才能继续。若为 False，依次检查：服务端是否监听 `0.0.0.0:9443`、Linux 防火墙、两台机器是否同网段、Wi-Fi AP 隔离、VPN/代理是否劫持内网流量。

#### 4.2 首次注册并写档案

```powershell
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win01@test.local --password win-pass-123 `
  --register --patch-weight 66
```

说明：

- 登录 ID 会被服务端转小写，长度 3–128；密码长度 8–256。
- `--register` 只在首次使用；之后去掉该参数走登录。
- 示例密码仅用于本地联调，不要使用真实密码。

预期输出：

```text
handshake_ok user_key=<26位用户键> token=issued
profile_ok name=我 source=service
patch_ok
verify_weight=66
```

四项全部出现，说明 TCP 帧、握手鉴权、MySQL 读、MySQL 写、幂等键均已打通。

#### 4.3 二次登录与负向用例

```powershell
# 二次登录（不带 --register）
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win01@test.local --password win-pass-123 --patch-weight 67

# 写方法缺幂等键，预期 patch_error code=3001
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win01@test.local --password win-pass-123 `
  --patch-weight 68 --omit-idem

# 未握手直接发业务帧，预期 pre_handshake_error code=2001
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win01@test.local --password win-pass-123 --skip-handshake
```

#### 4.4 常见失败

| 现象 | 原因 | 处理 |
|---|---|---|
| `connect_failed` | 服务端未监听 / 防火墙 / IP 错 / 不同网段 | `ss -ltn \| grep 9443`、`ufw status`、`Test-NetConnection` |
| `handshake_error code=5001` | 服务端 MySQL 不可用或迁移未执行 | 查 `docker compose logs` 或服务端日志 |
| `handshake_error code=2002` | 账号或密码错误、令牌无效 | 确认密码 ≥8 位；首次是否加了 `--register` |
| `handshake_error code=2003` | 连续失败导致账号锁定 | 等待锁定窗口结束或换测试账号 |
| `patch_error code=3001` | 写方法缺 `idem_key` | 去掉 `--omit-idem` |
| GUI 页面提示“本地模式” | App 桥接尚未接线 | 属当前已知边界，用 `client_smoke.exe` 测业务 |

## 当前可测范围

本次局域网联调只覆盖「注册 → 登录 → 握手 → 读档案 → 改体重 → 读回」。页面内餐记录、菜单、好友、排行失败属于业务尚未迁移，不是网络故障。

## 仓库结构

```text
shared/protocol/      帧协议 / CRC32C / 严格 JSON（server、client、bench 同源）
server/               tcp、identity、app、business、database、cache
client/               Win32+WebView2 壳、TCP 传输、IPC、KeyVault
agent/                Python Agent（HTTP 开发入口 + IPC App 入口）
migrations/base/      P7/P8 SQL 基线（000_init / 001_p8_business / foods_seed）
migrations/add/       002–006 增量迁移
deploy/               Docker、systemd、sysctl、preflight
tests/                protocol / unit / integration / load
docs/                 架构、运行手册、设计、协议
```

## 测试

```bash
ctest --test-dir build --output-on-failure     # C++ 单元/协议测试
python -m unittest agent.tests.test_smoke      # Agent 单测
python tests/integration/ipc_smoke.py          # IPC
bash tests/integration/s1_smoke.sh             # 真实 TCP（需 MySQL）
```

## 贡献与安全

- 提交规范、分支与 PR 门禁见 [`CONTRIBUTING.md`](CONTRIBUTING.md)。
- 漏洞与密钥处理见 [`SECURITY.md`](SECURITY.md)。
- **禁止**提交 `.env`、Key、数据库密码、`dist/` 与构建产物。
- 当前业务 TCP 为明文，仅建议在可信局域网内使用；公网部署需先接 TLS。
- 可执行文件与安装包通过 GitHub Releases 分发，仓库只保留源码与构建脚本。

## 许可

当前未声明开源许可证，默认保留所有权利。如需开源，请先在仓库补充 `LICENSE` 并经维护者确认。
