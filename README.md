# 泛饭 FanFan（Eat App）

> 一个围绕「今天吃什么」的跨端应用：**Windows 桌面客户端 + Python AI Agent + Linux C++ 服务端**。
> 客户端负责交互与本地 AI 能力，服务端负责账号与真实饮食数据，MySQL / Redis 只允许服务端访问。

**当前版本**：Release `v0.3.0` · 分支 `feat/app-transport-scale`
**当前状态**：15 个业务方法已在 Linux 通过真实 TCP + MySQL + Redis 验证；Windows 安装包可用；单 exe、TLS、100 万在线压测尚未完成。

| 维度 | 状态 |
|---|---|
| 服务端 | C++17 + Muduo，Linux 单机，已实测 |
| 协议 | 40B 定长头 + JSON body + CRC32C，三端同源 |
| 业务 | 认证、档案、餐记录、统计、菜单、好友、排行（15 个方法） |
| 缓存 | Cache-Aside，写后 DEL、负缓存、单飞、熔断降级 |
| Agent | Python，LLM / RAG / MCP / Skill / 工具编排，支持 TCP 与 mock 两种模式 |
| Windows 客户端 | Win32 + WebView2，安装包 `v0.3.0`，托盘、单实例、会话持久化 |
| 未完成 | 单 exe、TLS、公网部署、100 万在线实测 |

> 阅读顺序：本文 → [`docs/PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md) → [`docs/DEV_PROCESS.md`](docs/DEV_PROCESS.md)

---

## 1. 项目一览

### 1.1 三个组成部分

| 组成 | 技术栈 | 职责 | 源码 |
|---|---|---|---|
| **服务端** `eat-server` | C++17、Muduo、OpenSSL、MySQL、Redis | 账号/会话/档案/餐记录/统计/菜单/好友/排行；唯一访问数据库的角色 | `server/`、`shared/protocol/` |
| **Agent** `eat-agent.exe` | Python 3、标准库、可选 PyInstaller | 「饭饭」人格、LLM 对话、RAG、MCP、Skill、工具编排、TCP 业务调用 | `agent/` |
| **Windows 客户端** `eat-app.exe` | Win32、WebView2、原生 HTML/CSS/JS | 窗口、托盘、单实例、登录、验证码、本地页面渲染、IPC | `client/`、根目录前端资源 |

### 1.2 数据流

```text
页面(index.html/app.js)
   │  window.eatTransport
   ├─ 浏览器模式 ── fetch/SSE ──▶ Agent :8787 ── TCP ──▶ eat-server :9443
   └─ App 模式 ── postMessage ──▶ 壳 ── 命名管道 ──▶ Agent ── TCP ──▶ eat-server
                                                              ├─ MySQL（业务数据）
                                                              └─ Redis（缓存，仅服务端）
```

LLM 只由客户端侧 Agent 直连，服务端零 LLM、不持有模型 Key。

### 1.3 关键设计约束

- 服务端只支持 Linux（Muduo/epoll）；Windows 只构建客户端与 Agent。
- 业务传输是自研 TCP，不是 HTTP；WebSocket 不用于服务端推送。
- Redis 单实例、仅服务端连接、禁 pub/sub、禁客户端直连。
- 写方法必须带 `idem_key`；请求体里的 `user_key` 一律丢弃，身份只认连接上下文。
- 明文 TCP 只适合可信局域网；公网必须先接 TLS。

---

## 2. 环境依赖说明

### 2.1 依赖矩阵

| 依赖 | 版本要求 | 用途 | 适用端 |
|---|---|---|---|
| Linux | x86_64，Ubuntu 22.04/24.04/25.04 验证 | 运行服务端 | 服务端 |
| CMake | ≥ 3.16（Windows 建议 3.20+） | 构建 | 服务端 / 客户端 |
| C++ 编译器 | 支持 C++17（GCC 13 / MSVC v143） | 编译 | 服务端 / 客户端 |
| OpenSSL | 3.x（`libssl-dev` / vcpkg `openssl:x64-windows`） | scrypt、SHA-256、CRC 相关 | 服务端 / 客户端 |
| MySQL | 8.4（服务实例 + `libmysqlclient-dev`） | 业务数据 | 服务端 |
| Muduo | 2.0.x（建议 `v2.0.2` 源码） | 主从 Reactor 网络层 | 服务端 |
| Redis | 7.x（可选） | 缓存，不可用时 fail-open | 服务端 |
| Docker + Compose | 24+（推荐方案） | 一键拉起 MySQL 与服务端 | 服务端 |
| Python | 3.11+（3.13 实测；Windows 建议 3.11/3.12） | Agent、测试脚本 | Agent / 工具 |
| PyInstaller | 最新稳定版（仅打包时需要） | 生成 `eat-agent.exe` | Windows 打包 |
| WebView2 Runtime | Evergreen（Win11 通常自带） | 渲染本地页面 | Windows 客户端 |
| WebView2 SDK | `Microsoft.Web.WebView2` NuGet 包 | 编译 `eat-app.exe` | Windows 构建 |
| Visual Studio 2022 Build Tools | 勾选「使用 C++ 的桌面开发」 | MSVC + Windows SDK | Windows 构建 |
| Inno Setup | 6.x（仅生成安装包） | 桌面快捷方式安装包 | Windows 打包 |
| Node.js | 18+（仅开发期语法检查） | `node --check` | 工具 |

### 2.2 端口与网络

| 端口 | 进程 | 绑定地址 | 用途 | 是否需对外开放 |
|---|---|---|---|---|
| `9443` | `eat-server` | `0.0.0.0` | 业务 TCP | 局域网需要 |
| `8787` | Agent（开发期） | `0.0.0.0`（需显式指定） | 页面 HTTP/SSE | 局域网测试需要 |
| `8000` | 静态页 | `0.0.0.0` | Web 验收 | 局域网测试需要 |
| `3306` | MySQL | `127.0.0.1` 或容器网络 | 数据库 | **不要对外开放** |
| `6379` | Redis | `127.0.0.1` | 缓存 | **不要对外开放** |

> 同一台 Linux 上跑服务端 + Agent + 静态页即可完成最小联调；Windows 只需能访问 `9443`（GUI/业务）与 `8000`（Web）。

### 2.3 环境要求说明

- **服务端必须是 Linux**：Muduo 依赖 epoll，Windows 无等价物。
- **MySQL 推荐 Docker**：本机系统 MySQL 可能受 AppArmor/权限限制，容器方案更稳定；这也是 `.dev/start-lan.sh` 使用容器 `eat-mysql-docker2` 的原因。
- **Muduo 需要源码**：Ubuntu 没有可靠系统包，`scripts/install-deps.sh` 会拉取并编译 `v2.0.2`。
- **Agent 零第三方 pip 依赖**：只使用 Python 标准库；PyInstaller 仅用于生成单文件 exe。
- **Windows 产物必须在 Windows 构建**：Linux 构建机不能交叉编译出可用的 `eat-app.exe` / `client_smoke.exe`。
- **磁盘与内存**：开发/联调 2C4G 可跑；100 万连接需要独立压测环境，**当前没有实测数据，不要按连接数推算生产容量**。

---

## 3. 一键安装脚本

### 3.1 Linux 服务端：`scripts/install-deps.sh`

```bash
git clone https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
git checkout feat/app-transport-scale

bash scripts/install-deps.sh
```

**这条命令实际做了什么**（不是只打印提示）：

1. `apt-get update`，安装 `build-essential`、`cmake`、`pkg-config`、`git`、`curl`、`ca-certificates`、`libssl-dev`、`libmysqlclient-dev`、`default-mysql-client`、`redis-server`、`python3`；
2. 检查仓库同级目录 `../muduo`；不存在则 `git clone --depth 1 --branch v2.0.2`，已存在则复用；
3. 在 `../muduo/build/release-cpp11` 配置并编译 Muduo（跳过 examples）；
4. 打印构建、启动、验收命令。

**它不会做**：不安装 `mysql-server`、不创建数据库、不写密码、不修改 `sysctl`、不开放防火墙、不安装 Docker 本身。

**参数**：

```bash
bash scripts/install-deps.sh --skip-muduo        # 只装系统依赖
EAT_MUDUO_ROOT=/opt/muduo bash scripts/install-deps.sh
```

**验证安装**：

```bash
cmake --version
g++ --version
mysql --version
redis-server --version
ls ../muduo/build/release-cpp11/lib/libmuduo_net.a
```

### 3.2 Linux 构建与测试：`scripts/build-linux.sh`

```bash
bash scripts/build-linux.sh
```

该脚本会：

- 用 `EAT_MUDUO_ROOT`（默认 `../muduo`）配置 CMake；
- 编译全部目标（含 `eat-server`、`client_smoke`、单元测试）；
- 运行 `ctest`；
- 运行 Agent 单测（`EAT_DISABLE_DOTENV=1 EAT_MCP_DISABLE=1`）；
- 对 `app.js`、`bridge.js`、`agent-adapter.js` 做 `node --check`。

指定目录或构建类型：

```bash
EAT_MUDUO_ROOT=/opt/muduo BUILD_DIR=build-release BUILD_TYPE=Release bash scripts/build-linux.sh
```

> 默认输出到 `build/`。`.dev/start-lan.sh` 期望 `./build-release/eat-server`，本机复用一键启动时请用 `BUILD_DIR=build-release` 构建，或按实际目录修改脚本。

### 3.3 本机一键启动：`.dev/start-lan.sh`（仅当前开发机）

> **重要**：`.dev/` 被 `.gitignore` 忽略，`start-lan.sh` 与数据库容器配置**不在仓库里**。新克隆的环境没有这个脚本，请走 4.1（Docker）或 4.3（裸机）。本节描述的是当前开发机的本地脚本，用于在已有容器与配置的前提下快速拉起服务。

适用于「数据库容器已存在、只想把服务拉起来」的开发机：

```bash
bash .dev/start-lan.sh
```

**这条命令实际做了什么**：

1. 检查 Docker 容器 `eat-mysql-docker2`；未运行则 `docker start`，并等待 `mysqladmin ping`（最多 60s）；
2. 容器 IP 变化时生成 `.dev/route-a.runtime.conf`，避免配置里的旧 IP 失效；
3. 探测 Redis `6379`，未运行则 `redis-server --daemonize yes --bind 127.0.0.1`；启动失败只降级，不阻塞；
4. 若 `9443` 未监听，用 `.dev/route-a.conf` 启动 `./build-release/eat-server`；
5. 若 `8787` 未监听，或用 `0.0.0.0:8787` 但 `tcp_host` 为空，则用 `setsid` 重启 Agent（`EAT_AGENT_HOST=0.0.0.0 EAT_TCP_HOST=127.0.0.1 EAT_TCP_PORT=9443`）；
6. 若 `8000` 未监听，用 `python3 -m http.server --directory dist/eat-web` 启动静态页；
7. 打印三个端口的监听状态、局域网访问地址与 Agent 服务状态。

> 该脚本依赖本机已有的容器与 `.dev/` 配置，属于开发机脚本；新机器请先按第 4 节部署数据库。

### 3.4 Windows 客户端：`scripts/build-windows-oneclick.ps1`

```powershell
git clone -b feat/app-transport-scale https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
```

**脚本实际做的事**：

1. 检查 VS2022 C++ 工具链、CMake、Python；
2. 没有 vcpkg 时克隆并 bootstrap，安装 `openssl:x64-windows`；
3. 没有 WebView2 SDK 时下载 NuGet 包并解压到 `third_party\webview2`；
4. CMake 配置并编译 `eat-app.exe`；
5. 用 PyInstaller 打包 `eat-agent.exe`（`-SkipAgent` 可跳过）；
6. 组装 `dist\eat-app-windows-x64\`：exe、Agent、`web\`、OpenSSL DLL、`Start.bat`、`README.txt`；
7. 生成同名 zip。

生成安装包（桌面快捷方式）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
```

产物：`dist\EatAppSetup-x64.exe`。

### 3.5 手动安装（脚本不可用时）

Ubuntu：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config git ca-certificates \
  libssl-dev libmysqlclient-dev default-mysql-client redis-server python3
```

Muduo：

```bash
git clone --depth 1 --branch v2.0.2 https://github.com/chenshuo/muduo.git ../muduo
cmake -S ../muduo -B ../muduo/build/release-cpp11 \
  -DCMAKE_BUILD_TYPE=Release -DMUDUO_BUILD_EXAMPLES=OFF
cmake --build ../muduo/build/release-cpp11 -j"$(nproc)"
```

Windows 工具链：VS2022 Build Tools、CMake、vcpkg OpenSSL、WebView2 SDK、Python、Inno Setup 6。详细步骤见 [`AGENT_win.md`](AGENT_win.md) 与 [`docs/WINDOWS_CLIENT_LAN_RUNBOOK.md`](docs/WINDOWS_CLIENT_LAN_RUNBOOK.md)。

---

## 4. 部署运行步骤

### 4.1 方案 A：Docker（推荐，自动迁移）

```bash
export EAT_MYSQL_ROOT_PASSWORD='<强密码>'
docker compose -f deploy/docker/docker-compose.yml up -d --build
docker compose -f deploy/docker/docker-compose.yml logs -f eat-server
```

**这条命令实际做了什么**：

- 构建 `eat-server` 多阶段镜像：先编译 Muduo 与服务端，运行阶段只保留二进制与运行库；
- 启动 `mysql:8.4`，健康检查通过后才启动服务端；
- 服务端入口脚本等待 MySQL、执行 `00_all.sql`（000→006 全量迁移）、再启动；
- 宿主机暴露 `9443`；MySQL 只在容器网络内可达。

验收：

```bash
ss -ltn | grep 9443                 # 应看到 0.0.0.0:9443
docker compose -f deploy/docker/docker-compose.yml ps
docker compose -f deploy/docker/docker-compose.yml logs --tail=50 eat-server
```

> 首次构建需要外网（拉 Muduo 与 apt 包）；国内网络可先配置镜像或代理。

### 4.2 方案 B：Release 二进制包（最快）

从 GitHub Releases 下载 `eat-server-linux-x86_64.tar.gz`：

```bash
tar -xzf eat-server-linux-x86_64.tar.gz
cd eat-server-linux-x86_64
mysql --default-character-set=utf8mb4 -h 127.0.0.1 -uroot -p < migrations/00_all.sql
export EAT_MYSQL_PASSWORD='<强密码>'
./run.sh
```

包内：

```text
eat-server            # Release 二进制
client_smoke          # 帧级验证客户端
run.sh                # 启动脚本
server.toml.example   # 配置模板
migrations/00_all.sql # 000→006 全量迁移
```

运行依赖：glibc 2.39+（Ubuntu 24.04/25.04）、`libssl3`、`libmysqlclient`、`libstdc++6`。跨发行版建议用 Docker。

> `run.sh` 首次运行会从 `server.toml.example` 复制出 `server.toml`；启动前请确认 `mysql_host`、`mysql_db`、`mysql_user` 与本机实际一致，密码通过 `EAT_MYSQL_PASSWORD` 注入。

### 4.3 方案 C：裸机构建

```bash
bash scripts/install-deps.sh
bash scripts/build-linux.sh
```

或手动：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DEAT_MUDUO_ROOT=../muduo
cmake --build build-release -j"$(nproc)" --target eat-server client_smoke
```

启动：

```bash
# 000_init.sql 历史库名是 chatroom，本项目统一为 eat，需先替换
sed 's/chatroom/eat/g' migrations/base/000_init.sql \
  | mysql --default-character-set=utf8mb4 -h 127.0.0.1 -uroot -p

mysql --default-character-set=utf8mb4 -h 127.0.0.1 -uroot -p eat < migrations/base/001_p8_business.sql
mysql --default-character-set=utf8mb4 -h 127.0.0.1 -uroot -p eat < migrations/base/foods_seed.sql
for f in migrations/add/00{2,3,4,5,6}_*.sql; do
  case "$f" in *_rollback.sql) continue;; esac
  tr -d '\r' < "$f" | sed 's/^--\([^ ]\)/-- \1/' \
    | mysql --default-character-set=utf8mb4 -h 127.0.0.1 -uroot -p eat
done

export EAT_MYSQL_PASSWORD='<强密码>'
./build-release/eat-server --config deploy/server.toml.example
```

> 更稳妥的做法是直接使用打包产物中的 `dist/eat-server-linux-x86_64/migrations/00_all.sql`，该文件已清洗换行、注释与库名。

**迁移注意**：

- 文件若含 CRLF 或 `--注释` 缺空格，MySQL 8.4 会报 1064；打包脚本已清洗，手动导入时建议同样处理；
- 必须显式 `--default-character-set=utf8mb4`，否则中文菜名乱码；
- `*_rollback.sql` 不要混进正向迁移；
- 分区表主键必须包含分区列 `(id, created_day)`，否则报 1503。

### 4.4 启动后的真实验收

#### 4.4.1 端口检查

```bash
ss -ltn | grep -E ':9443|:8787|:8000'
```

三个端口都应在 `0.0.0.0` 或预期地址上监听。若 `8787` 只在 `127.0.0.1`，局域网客户端连不上，需要重启 Agent 并加 `--host 0.0.0.0`。

#### 4.4.2 控制台客户端（不依赖 GUI）

在 Windows 上：

```powershell
Test-NetConnection <服务端IP> -Port 9443
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win01@test.local --password <测试密码> `
  --register --patch-weight 66
```

预期：

```text
handshake_ok user_key=<26位用户键> token=issued
profile_ok name=我 source=service
patch_ok
verify_weight=66
```

#### 4.4.3 验证码 + 登录 + 菜单（HTTP 开发路径）

```bash
# 1) 取验证码（body 需要 host/port）
curl -s -X POST http://127.0.0.1:8787/api/service/captcha \
  -H 'Content-Type: application/json' \
  -d '{"host":"127.0.0.1","port":9443}'
# 从返回 SVG 的 <text> 节点提取字符

# 2) 注册/登录
curl -s -X POST http://127.0.0.1:8787/api/service/login \
  -H 'Content-Type: application/json' \
  -d '{"host":"127.0.0.1","port":9443,"login_id":"test_lan_1130","password":"<测试密码>","register":true,"captcha_id":"<id>","captcha_code":"<code>"}'

# 3) 查菜单（meal_type 只收中文：早餐/午餐/晚餐/加餐）
curl -s 'http://127.0.0.1:8787/api/menu?meal_type=%E5%8D%88%E9%A4%90'
```

`meal_type` 传 `lunch` 会返回 `source=service_error`，必须用中文。

#### 4.4.4 完整测试命令

```bash
# 构建目录按实际使用的替换：scripts/build-linux.sh 默认 build/
# 若按 4.3 用 build-release 构建，则把下面的 build 改成 build-release
ctest --test-dir build --output-on-failure
python3 -m unittest agent.tests.test_smoke
PYTHONPATH=. python3 tests/integration/ipc_smoke.py
PYTHONPATH=. python3 tests/integration/service_tcp_test.py --host 127.0.0.1 --port 9443
PYTHONPATH=. python3 tests/integration/agent_service_client_test.py 127.0.0.1 9443
```

### 4.5 Windows 客户端运行

三条路径，按需要选：

| 路径 | 需要安装 | 能测什么 |
|---|---|---|
| **A. Web** | 浏览器 | 登录、菜单、档案、统计、好友、排行 |
| **B. Release 安装包** | `EatAppSetup-x64.exe` | 同 A，外加安装、托盘、会话持久化 |
| **C. 源码构建** | VS2022 工具链 | 在 B 基础上可改 UI、改客户端行为 |

**Web**：

```text
http://<服务端IP>:8000
```

> 不要用 `file://` 直接双击 HTML，必须走 HTTP；否则 `fetch` 与静态资源行为不一致。

**安装包**：安装后双击桌面 **Eat App** 快捷方式。首次启动在界面填写服务端地址、账号、密码和图形验证码，不需要改 JSON。

**源码构建**：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
cd dist\eat-app-windows-x64
.\eat-app.exe
```

关闭窗口进入托盘；右键托盘图标「退出」才结束进程。

### 4.6 局域网连通与排障

#### 4.6.1 网络自检（Windows）

```powershell
Test-NetConnection <服务端IP> -Port 9443
Test-NetConnection <服务端IP> -Port 8000
```

两者都应为 `TcpTestSucceeded : True`。

#### 4.6.2 常见故障

| 现象 | 可能原因 | 处理 |
|---|---|---|
| `connect_failed` | 服务端没监听 / 防火墙 / 不同网段 / VPN | 先 `ss -ltn \| grep 9443`，再查防火墙与网络 |
| `handshake_error code=5001` | MySQL 不可用或迁移未执行 | 查容器日志与服务端日志，确认 `00_all.sql` 已执行 |
| `handshake_error code=2002` | 账号/密码错误或令牌无效 | 确认密码 ≥ 8 位；首次是否 `--register` |
| `handshake_error code=2003` | 连续失败锁定 | 等待 5 分钟或换测试账号 |
| `patch_error code=3001` | 写方法缺 `idem_key` | 不要加 `--omit-idem` |
| `pre_handshake_error code=2001` | 未握手直接发业务帧 | 先握手 |
| 页面显示「本地模式」 | Agent 未收到 TCP 配置 | 检查 `/api/service/status` 的 `tcp_host` |
| 菜单/搜索为空 | 中文被二次编码 | 用 `--default-character-set=utf8mb4` 重导 |
| GUI `Failed to fetch` | 页面地址仍指向 `127.0.0.1` | 填写服务端局域网地址；GUI 不占 8787 |
| 长时间后请求失败 | 会话过期 `token_expired` | 重新登录即可恢复 |
| 页面样式异常/资源 404 | `dist/eat-web/` 是旧产物 | 重新同步前端资源或走构建脚本 |

#### 4.6.3 网络与安全边界

- 当前是**明文 TCP**，只适合可信局域网；公网必须先接 TLS。
- 不要把 `3306` / `6379` 暴露到局域网或公网。
- 不要在仓库根目录直接跑 `python3 -m http.server`：会把 `.dev/` 下的密码文件、`dump.rdb`、`.git` 一起暴露。静态页请用 `--directory dist/eat-web`。
- 仓库不得出现真实 Key、数据库密码、`.env`；只能保留 `.env.example`。

### 4.7 停止与清理

Docker：

```bash
docker compose -f deploy/docker/docker-compose.yml down
# 连同数据卷一起删除（会丢业务数据，确认后再执行）
docker compose -f deploy/docker/docker-compose.yml down -v
```

裸机：

```bash
pkill -f './build-release/eat-server'
pkill -f 'python3 -m agent'
pkill -f 'http.server 8000'
redis-cli shutdown nosave        # 若 Redis 由本机启动
```

---

## 5. 额外使用的开源组件说明

> 本节只列项目实际使用、且不在「语言标准库」范围内的组件。仓库自身未声明开源许可证，默认保留所有权利；下表用于说明第三方依赖的来源与合规注意。

### 5.1 组件清单

| 组件 | 版本 | 许可证 | 用途 | 是否随产物分发 |
|---|---|---|---|---|
| **Muduo** | `v2.0.2`（源码） | BSD 3-Clause | 服务端主从 Reactor、TCP 连接管理 | 静态链接进 `eat-server` |
| **OpenSSL** | 3.x | Apache License 2.0 | scrypt、SHA-256、随机数、TLS（规划） | 链接 `libcrypto`/`libssl`；Windows 附 DLL |
| **MySQL / libmysqlclient** | 8.4 | GPLv2 + FOSS Exception | 业务数据存储、C API | 服务端依赖客户端库；MySQL 服务单独部署 |
| **Redis** | 7.x | 7.2 及以前 BSD-3-Clause；7.4 起 RSALv2/SSPLv1 | 旁路缓存 | 不随包分发，按部署环境单独安装 |
| **WebView2 SDK / Runtime** | Evergreen / Fixed Version | Microsoft 专有许可（免费使用） | Windows 本地页面渲染 | SDK 仅构建期；Runtime 由系统/安装包装 |
| **Python** | 3.11+ | PSF License | Agent 运行时 | PyInstaller 打包时嵌入 |
| **PyInstaller** | 最新稳定版 | GPLv2 + Bootloader Exception | 生成 `eat-agent.exe` | 仅构建期工具 |
| **Inno Setup** | 6.x | Inno Setup License（BSD 风格） | Windows 安装包与快捷方式 | 仅构建期工具 |
| **CMake** | ≥ 3.16 | BSD 3-Clause | 构建系统 | 仅构建期工具 |
| **GCC / libstdc++** | GCC 13 | GPLv3 + Runtime Library Exception | 编译与 C++ 运行库 | `libstdc++6` 随系统 |
| **Docker 基础镜像** | `ubuntu:24.04` | Ubuntu 镜像许可 | 构建/运行环境 | 仅容器镜像 |
| **Node.js** | 18+ | MIT | 仅 `node --check` 语法检查 | 不分发 |

### 5.2 各组件的具体用法

#### Muduo

- 只用于服务端网络层：`muduo::net::EventLoop`、`TcpServer`、`Buffer`。
- 不在系统包里找 Muduo，构建时通过 `EAT_MUDUO_ROOT` 指向源码树。
- `scripts/install-deps.sh` 会拉取 `v2.0.2` 并编译到 `build/release-cpp11`。
- 许可证为 BSD 3-Clause，静态链接时保留其版权声明即可。

#### OpenSSL

- `PasswordHasher` 使用 `EVP_PBE_scrypt`；
- `TokenVerifier`、`CacheKeyBuilder` 使用 SHA-256；
- `Crc32c` 为自研查表实现，不依赖 OpenSSL；
- 版本要求 3.x；Windows 通过 vcpkg 安装 `openssl:x64-windows`，运行时可能需要 `libcrypto-3-x64.dll`、`libssl-3-x64.dll`。

#### MySQL

- 服务端通过 `libmysqlclient` 的 C API 访问，`MySQLPool` 负责连接池、借出超时、语句超时。
- MySQL 服务本身不随项目分发；部署方需自行安装或使用 `mysql:8.4` 容器。
- 注意 GPLv2 + FOSS Exception 的适用条件；若将本项目闭源分发，需评估链接方式与许可证义务。

#### Redis

- 只作为服务端缓存，单实例、仅服务端连接、禁 pub/sub。
- `RedisClient` 是自研最小 RESP 客户端，**不依赖 hiredis**；命令白名单只含 GET/SETEX/DEL/MGET/EXISTS/PING。
- Redis 7.4 起许可证改为 RSALv2/SSPLv1；若计划商业分发，请锁定 7.2 或评估许可证。

#### WebView2

- 非开源组件，按 Microsoft 许可免费使用。
- SDK 只在构建期需要；运行期依赖目标机安装 Evergreen Runtime（Win11 通常自带）。
- 无 Runtime 的机器可在安装包中附 Fixed Version，但会增大体积。

#### Python / PyInstaller

- Agent 只使用标准库，没有 `requirements.txt` 运行时依赖。
- `PyInstaller --onefile` 生成 `eat-agent.exe`；启动时释放到临时目录，首次启动会稍慢。
- PyInstaller 的 GPLv2 + Bootloader Exception 允许打包专有程序，但需保留其许可声明。

#### Inno Setup

- 生成 `EatAppSetup-x64.exe`，创建桌面快捷方式、开始菜单项与卸载入口。
- 只在 Windows 打包机上需要，不进入最终用户运行时。

### 5.3 前端与素材

- 前端为原生 HTML/CSS/JS，**无 React/Vue、无 npm、无 CDN、无外部字体**。
- 字体使用系统字体栈（SF Pro / PingFang SC / Microsoft YaHei / system-ui）。
- `assets/agent-avatar.png` 为项目自带素材；如替换为第三方图片，请自行确认授权。
- 没有引入任何前端开源库，因此不存在 npm 依赖锁文件与许可证扫描问题。

### 5.4 合规建议

1. 对外分发前补 `LICENSE`；当前仓库未声明开源许可证，默认保留所有权利。
2. 保留 Muduo、OpenSSL、PyInstaller 等组件的版权与许可声明。
3. MySQL / Redis 的许可证随版本变化，商业分发前锁定版本并复核。
4. 不要把模型 API Key、数据库密码、`.env` 打进安装包或镜像。
5. 服务端与客户端都不得内置真实密钥；Key 只由最终用户在客户端侧配置。

---

## 6. 码道在项目开发中的应用总结

> 本文所说的「码道」指华为云码道（CodeArts）代码智能体。本节总结它在本项目中实际承担的工作，而不是泛泛介绍功能。

### 6.1 使用方式总览

| 能力 | 在本项目中的用法 | 产出位置 |
|---|---|---|
| **Spec 规格驱动** | 先把需求写成可验收的规格，再写设计、再拆任务 | `.codeartsdoer/specs/app_transport_scale/{spec,design,tasks}.md` |
| **需求审查** | 审查 PR #2 的可跑通性与百万并发口径 | `.codeartsdoer/specs/p8_backend_check/spec.md` |
| **代码生成** | 按任务生成协议、连接治理、线程池、身份、缓存、业务、Agent、客户端、打包脚本 | `server/`、`shared/`、`client/`、`agent/`、`scripts/` |
| **代码重构** | 三线合并、身份统一、协议统一、线程池门面化、传输抽象 | `docs/design/03-refactor-cleanup.md` + 实际提交 |
| **代码审查** | 风险复审：并发、资源、鉴权、密钥、越权 | 审查结论进入 `docs/architecture-status.md` |
| **死代码清理** | 全仓扫描无调用方代码与契约漂移 | `CHANGELOG.md` 的 Changed/Fixed |
| **设计与美学** | 登录、引导、页面重设计，统一视觉 | `redesign.css`、`login.css`、`index.html` |
| **测试与验证** | 生成/补全单测、集成脚本、负向用例 | `tests/` |
| **文档沉淀** | 架构、运行手册、交接文档、Release 说明 | `docs/`、`README.md`、`AGENT*.md` |

### 6.2 规格驱动：先定义「怎么证明」

项目没有直接从「写一个服务端」开始，而是先完成三层文档：

1. **spec（需求规格）**：定义组件职责、硬约束、验收判据、能力差距矩阵。R2–R6 多轮修订，逐条记录口径变化（例如 API Key 从服务端回退到客户端）。
2. **design（技术设计）**：裁决技术选型（Win32+WebView2、Muduo、40B 帧、JSON、Cache-Aside、命名管道），给出进程/线程拓扑、表结构、缓存键规则。
3. **tasks（任务规划）**：拆成 13 个任务组、102 个子任务、11 个阶段门（G1–G11），每个任务标注处置方式与验证方式。

这样做的直接好处：当需求从 R4 改到 R5、再到 R6 时，可以精确定位「哪些条款被回退、哪些继续有效」，而不是全仓重写。

### 6.3 技能协同：不同角色互相验证

项目启用了多个专项技能（见 `.codeartsdoer/skills/ProjectSkillStatus.txt`）：

| 技能 | 职责 | 在项目中的实际作用 |
|---|---|---|
| `code-refactoring` | 行为不变的结构优化 | 拆分 `TcpServerApp`、把 `BusinessExecutor` 改为门面、统一命名 |
| `code-reviewer` | 风险复审 | 发现连接计数下溢、`confirmed` 真值绕过、密钥输出风险 |
| `design-pattern-applier` | 识别耦合并套用模式 | 传输层 Adapter/Strategy（`eatTransport`）、Service/DAO 分层、门面模式 |
| `dead-code-eliminator` | 扫描无调用方代码 | 清理 Agent/前端死代码与契约漂移 |
| `unslop` | 去除「AI 味」的 UI | 登录页与主页面的视觉重设计，统一间距、配色、字体与状态反馈 |

协同方式不是「一个技能改完就结束」，而是：

```text
code-refactoring 改结构
      ↓
code-reviewer 复审风险
      ↓
design-pattern-applier 判断是否值得抽象
      ↓
dead-code-eliminator 清理残留
      ↓
测试全绿后再提交
```

### 6.4 代码生成：按层落地，不跨层猜

码道生成代码时遵循了项目的分层约束：

- **协议层**：只生成与 `protocol.h` 常量一致的编解码，不自行发明字段；
- **连接层**：状态机、心跳、限流、背压各自独立可测；
- **业务层**：Service 不感知网络，DAO 不感知协议；
- **Agent 层**：业务数据只能经 `ServiceClient` 走 TCP，不直连数据库；
- **客户端层**：页面只认 `eatTransport`，不直接碰端口；
- **脚本层**：构建、打包、启动都封装成可重复执行的脚本。

生成后不是「写完就算」，而是每层都配测试：协议有 `frame_codec_test` / `strict_json_test`，连接治理有 `connection_governance_test`，线程池有 `thread_pool_test`，身份有 `identity_test` / `token_verifier_test`，缓存有 `cache_test`，客户端有 `offline_queue_test` / `keyvault_test`。

### 6.5 重构：先立测试基线，再动结构

重构前先跑一遍全部测试，确认基线是绿的；重构过程中遵守：

- 每步不超过 3 个文件或 2000 行；
- 一步一提交，失败立即回退；
- 重构与功能变更分离；
- 删除代码前全仓 grep 确认无调用方。

实际完成的重构包括：三线合并、身份统一、协议统一、缓存统一、线程池门面化、前端传输抽象、死代码清理。每一步之后都重新跑 `ctest` 与 Agent 单测，确保行为不变。

### 6.6 排错：从现象追到根因，再补验证

码道在排错中的主要价值不是「直接给答案」，而是把排查过程结构化：

1. **先复现**：给出最小命令与预期输出；
2. **再定位**：区分环境问题、配置问题、代码问题、数据问题；
3. **修根因**：例如分区唯一键不是改 SQL 绕过，而是按 MySQL 规则调整主键；
4. **补验证**：修完后加负向用例，防止回归；
5. **写文档**：把现象、根因、修复写进交接文档，避免下一个人重复踩坑。

典型例子见 [`docs/DEV_PROCESS.md`](docs/DEV_PROCESS.md) 的 T-01（分区 1503）、T-02（CRLF/注释 1064）、T-12（mock 假数据）、T-18（GUI 地址错误）。

### 6.7 验证：用证据说话

码道在项目中反复强调「未实测不得宣称」。具体表现为：

- 文档中的容量结论必须带 `已验证 / 理论推导 / 未验证 / 阻断` 标注；
- Windows 相关结论必须写「未在 Windows 验证」，不能因为代码写完就标完成；
- 交付必须包含可运行产物、运行步骤、真实请求输出与能力边界清单；
- 每次用户问「现在还在跑吗」，都重新检查端口、进程与一次业务接口，而不是复用上一轮结论。

### 6.8 沉淀：把一次性对话变成可复用资产

项目留下了多层沉淀：

- `AGENT.md` / `AGENT_Linux.md`：Agent 能力、对话轮次、环境准备、已知差异；
- `docs/architecture.md` / `architecture-status.md` / `DELIVERY-STATUS.md`：架构事实、任务状态、交付状态；
- `docs/design/00–04`：总方案、后端合并、Windows App、重构清理、Linux 测试；
- `docs/WINDOWS_CLIENT_TEST_HANDOFF.md` / `WINDOWS_TEAMMATE_TEST_GUIDE.md`：面向队友的可执行交接；
- `CHANGELOG.md`：对外可见的变更；
- `.codeartsdoer/specs/`：需求、设计、任务的原始规格。

这些文档的共同特点是：**可执行**（有命令）、**可验证**（有预期输出）、**有边界**（写明不做什么、没验证什么）。

### 6.9 有效做法与注意事项

**有效做法**：

1. 规格先行，避免边写边改需求；
2. 测试基线 + 小步提交，重构可回退；
3. 技能分工 + 互相复审，结构改动有第二双眼睛；
4. 证据分级，文档不写「应该可以」；
5. 脚本化交付，用户不需要逐行粘贴命令；
6. 把排错过程写成编号条目，形成可检索的故障库。

**注意事项**：

1. 码道生成的是「候选实现」，必须用测试与真实请求验证，不能直接宣称完成；
2. 跨平台结论必须到目标平台验证（Windows GUI 不能在 Linux 上「看起来没问题」就标完成）；
3. 生成代码要遵守项目既有分层与命名，不要引入新的框架或第二套协议；
4. 密钥、密码、`.env` 不得进入生成内容、日志或文档；
5. 对容量、性能、并发这类结论，没有压测数据就明确写「未验证/阻断」。

---

## 7. 目录结构

```text
.
├── README.md                     # 本文
├── AGENT.md                      # Agent 能力说明（历史，部分口径已更新）
├── AGENT_Linux.md                # Linux 侧交接与对话轮次
├── AGENT_win.md                  # Windows 编译指南
├── CHANGELOG.md                  # 变更日志
├── CMakeLists.txt                # 唯一构建入口
├── index.html / app.js / bridge.js / agent-adapter.js
├── styles.css / features.css / agent.css / login.css / redesign.css
├── assets/                       # 本地素材
├── shared/protocol/              # 帧协议、CRC32C、严格 JSON
├── server/
│   ├── app/                      # 配置、日志、指标、线程池、服务注册
│   ├── tcp/                      # Muduo 服务端、连接治理、分发
│   ├── identity/                 # 密码、令牌、会话、验证码
│   ├── business/                 # profile / meal / stats / food / friend / ranking
│   ├── database/                 # MySQLPool、RedisClient
│   └── cache/                    # Cache-Aside 组件
├── client/
│   ├── app/win32/                # Win32 + WebView2 壳
│   ├── transport/                # TCP 传输、离线队列
│   ├── ipc/                      # 4B LE + JSON
│   └── keyvault/                 # Windows DPAPI
├── agent/                        # Python Agent
├── migrations/base/              # 000 / 001 / foods_seed
├── migrations/add/               # 002–006 增量与回滚
├── deploy/                       # Docker、systemd、sysctl、配置模板
├── scripts/                      # 安装、构建、打包脚本
├── packaging/                    # 发布说明、Inno Setup
├── tests/                        # protocol / unit / integration / load
└── docs/                         # 架构、协议、设计、交接、过程记录
```

---

## 8. 文档索引

| 文档 | 内容 |
|---|---|
| [`docs/PROJECT_OVERVIEW.md`](docs/PROJECT_OVERVIEW.md) | 项目详细介绍：定位、场景、架构、协议、数据、安全、边界 |
| [`docs/DEV_PROCESS.md`](docs/DEV_PROCESS.md) | 代码生成、重构、排错过程记录 |
| [`docs/architecture.md`](docs/architecture.md) | 合并后架构事实 |
| [`docs/architecture-status.md`](docs/architecture-status.md) | 任务级落地状态与阻断项 |
| [`docs/DELIVERY-STATUS.md`](docs/DELIVERY-STATUS.md) | 交付状态与验证证据 |
| [`docs/RUNBOOK.md`](docs/RUNBOOK.md) | 服务端/客户端运行手册 |
| [`docs/protocol/v1.md`](docs/protocol/v1.md) | 40B 帧协议规范 |
| [`docs/ROUTE-A-HANDOFF.md`](docs/ROUTE-A-HANDOFF.md) | 路线 A 实施交接 |
| [`docs/WINDOWS_CLIENT_TEST_HANDOFF.md`](docs/WINDOWS_CLIENT_TEST_HANDOFF.md) | Windows 测试交接（Web + GUI + 源码） |
| [`docs/WINDOWS_TEAMMATE_TEST_GUIDE.md`](docs/WINDOWS_TEAMMATE_TEST_GUIDE.md) | 队友测试指南 |
| [`docs/design/00-master-plan.md`](docs/design/00-master-plan.md) | S0–S4 总方案 |
| [`AGENT_win.md`](AGENT_win.md) | Windows 编译与打包 |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | 分支、提交、PR 门禁 |
| [`SECURITY.md`](SECURITY.md) | 漏洞与密钥处理 |

---

## 9. 能力边界（必须知道）

1. **100 万同时在线是压测上限，不是承诺**。当前没有压测数据，不能对外宣称达标。
2. **明文 TCP**。只适合可信局域网；公网必须先接 TLS。
3. **服务端只支持 Linux**。Windows 只构建客户端与 Agent。
4. **Windows 产物需在 Windows 构建**。Linux 构建机不能产出可用的 exe。
5. **页面历史入口的边界**：页面「点选记录」「删除/修改记录」的旧入口不写服务端；判断服务端是否正常，以协议方法调用为准。
6. **无模型 Key 时对话降级**到本地规则引擎，业务数据仍真实。
7. **Redis 不可用时缓存 fail-open**，业务继续但性能下降。
8. **单 exe、TLS、MSIX 签名、断网冷启动、多用户并发**未完成或未验证。

---

## 10. 贡献、安全与许可

### 10.1 贡献

- 分支与提交规范见 [`CONTRIBUTING.md`](CONTRIBUTING.md)。
- 改动前先让测试基线全绿；重构与功能变更分开提交。
- 涉及协议、身份、缓存的改动，必须同步更新 `docs/protocol/v1.md` 与相关测试。
- 纯前端 UI 改动不需要改服务端；只要仍调用既有 TCP 方法，服务端不需要重启或改参数。

### 10.2 安全

- **禁止**提交 `.env`、真实 Key、数据库密码、`login.json`、`dist/`、构建产物。
- 密钥只从环境变量或本地被忽略的配置文件读取。
- 门禁扫描工具不得原样输出敏感文件内容；任何出现在终端/日志的 Key 都应视为已泄露并轮换。
- 漏洞报告流程见 [`SECURITY.md`](SECURITY.md)。
- 公网部署前必须：接 TLS、限制 MySQL/Redis 访问、启用验证码、限制握手速率、配置防火墙。

### 10.3 许可

当前仓库未声明开源许可证，默认保留所有权利。如需开源，请先补充 `LICENSE` 并经维护者确认。

第三方组件的许可证与合规注意见本文第 5 节。

---

## 11. 快速命令速查

```bash
# 安装依赖
bash scripts/install-deps.sh

# 构建 + 测试
bash scripts/build-linux.sh

# 本机拉起服务端 / Agent / 静态页（脚本在 .dev/，未入库；新环境用 4.1 或 4.3）
bash .dev/start-lan.sh

# 检查端口
ss -ltn | grep -E ':9443|:8787|:8000'

# 跑测试（构建目录按实际替换）
ctest --test-dir build --output-on-failure
python3 -m unittest agent.tests.test_smoke
PYTHONPATH=. python3 tests/integration/ipc_smoke.py
PYTHONPATH=. python3 tests/integration/service_tcp_test.py --host 127.0.0.1 --port 9443

# 打包服务端
bash scripts/package-linux.sh

# Windows 一键构建 / 安装包
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
```
