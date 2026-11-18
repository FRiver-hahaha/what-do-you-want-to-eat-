# 想吃什么（Eat）作品说明

一个面向日常饮食的多端应用：PC 客户端里有一位叫 **饭饭** 的 AI 饮食助手，负责问诊式建档、配餐推荐、饮食记录、营养缺口分析和好友排行榜。服务端用 C++17 自建 TCP 帧协议承载业务，数据落在 MySQL，Redis 只做服务端缓存；Agent 用 Python 标准库实现，前端是原生 HTML/CSS/JS。

```text
┌──────────────────────────┐      ┌──────────────────────────┐
│ Windows 客户端            │      │ 浏览器（Web 端）          │
│ Win32 + WebView2 + IPC   │      │ http://<IP>:8000         │
└────────────┬─────────────┘      └────────────┬─────────────┘
             │ 本机 IPC / HTTP                  │ HTTP /api
             ▼                                 ▼
┌───────────────────────────────────────────────────────────┐
│ Python Agent（饭饭）  :8787                                │
│ 对话编排、工具调用、LLM 网关、验证码、会话与本地缓存         │
└────────────────────────────┬──────────────────────────────┘
                             │ 自定义 TCP 帧（32B 头 + CRC32C + JSON）
                             ▼
┌───────────────────────────────────────────────────────────┐
│ C++17 服务端 eat-server  :9443                             │
│ Muduo epoll 主从 Reactor、业务线程池、鉴权、限流、幂等       │
└───────────┬───────────────────────────────┬───────────────┘
            │ libmysqlclient                │ 自研 RESP 客户端
            ▼                               ▼
      MySQL 8.x（权威数据）            Redis（仅服务端连接）
```

## 0. 目录速览

| 路径 | 作用 |
|---|---|
| `server/` | TCP 服务、身份鉴权、业务 Service/DAO、连接治理 |
| `shared/protocol/` | 帧编解码、CRC32C、严格 JSON |
| `agent/` | Python Agent：对话、工具、LLM 网关、HTTP/IPC 入口 |
| `client/` | Win32 + WebView2 壳、TCP 传输、IPC、DPAPI |
| `index.html` / `app.js` / `*.css` | Web 端源码（发布时复制到 `dist/eat-web/`） |
| `migrations/` | 数据库基线与增量迁移 002–007 |
| `deploy/` | Docker、systemd、sysctl、配置模板 |
| `scripts/` | 依赖安装、构建、打包脚本 |
| `tests/` | 协议/单元/集成/负载测试 |
| `docs/` | 架构、协议、设计、运行手册、交接文档 |
| `.codeartsdoer/specs/` | 规格驱动开发的需求/设计/任务原文 |

---

## 1. 环境依赖说明与一键安装

### 1.1 依赖矩阵

**Linux 服务端（Ubuntu 22.04 / 24.04 / 25.04）**

| 依赖 | 最低版本 | 用途 | 是否必需 |
|---|---|---|---|
| GCC / G++ | 13（支持 C++17） | 编译服务端与测试 | 必需 |
| CMake | 3.20 | 唯一构建入口 | 必需 |
| OpenSSL（`libssl-dev`） | 3.0 | SHA-256、HMAC、随机数 | 必需 |
| MySQL Server | 8.0+（推荐 8.4） | 权威数据存储 | 必需 |
| MySQL 客户端库（`libmysqlclient-dev`） | 8.x | 服务端数据库访问 | 必需 |
| Redis Server | 7.x | 服务端缓存（单实例） | 可选 |
| Python | 3.11+ | Agent 与静态页服务 | 必需 |
| Muduo | v2.0.2 源码 | TCP 网络层 | 必需（脚本自动拉取） |
| curl / git / pkg-config | 发行版自带 | 依赖准备与验收 | 必需 |
| Node.js | 18+ | 仅开发期 `node --check` | 可选 |
| Docker + Compose | 24+ | 推荐部署方式 | 可选 |

**Windows 客户端（Windows 10/11 x64）**

| 依赖 | 版本 | 用途 | 是否必需 |
|---|---|---|---|
| MSVC（Visual Studio 2022 Build Tools） | v143 | 编译 `eat-app` 壳 | 构建时必需 |
| CMake | 3.20+ | 构建 | 构建时必需 |
| WebView2 SDK（NuGet） | 最新 | 嵌入浏览器 | 构建时必需（脚本自动下载） |
| vcpkg | 最新 | 依赖管理 | 构建时必需（脚本自动克隆） |
| Python + PyInstaller | 3.11+ | 打包 `eat-agent.exe` | 构建时必需 |
| WebView2 Runtime | Evergreen | 运行页面 | 运行必需（Win11 自带） |
| Inno Setup | 6.x | 生成安装包 | 可选（仅出安装包时） |

> 客户端运行时不需要安装 Python、MySQL、Redis；数据库与鉴权全部在服务端。

### 1.2 Linux：一键安装依赖

```bash
bash scripts/install-deps.sh
```

脚本自动完成：

1. `apt-get` 安装 `build-essential`、`cmake`、`pkg-config`、`git`、`curl`、`libssl-dev`、`libmysqlclient-dev`、`default-mysql-client`、`redis-server`、`python3`；
2. 在仓库同级目录 `../muduo` 检查 Muduo，缺失则克隆 `v2.0.2` 并编译；
3. 打印构建、启动、验收命令。

可选参数：

```bash
bash scripts/install-deps.sh --skip-muduo          # 已有 Muduo，跳过
EAT_MUDUO_ROOT=/opt/muduo bash scripts/install-deps.sh
```

脚本**不做**的事（避免误以为装完就能跑）：不安装 `mysql-server`、不装 Docker、不改 sysctl、不开防火墙、不建库、不写任何密码。

### 1.3 Linux：一键构建与测试

```bash
bash scripts/build-linux.sh
```

等价于：CMake Release 配置 → 编译 → `ctest` → `python3 -m unittest agent.tests.test_smoke` → 三个前端 JS 的 `node --check`。

可用环境变量覆盖：

```bash
BUILD_DIR=build-release BUILD_TYPE=Release EAT_MUDUO_ROOT=/opt/muduo bash scripts/build-linux.sh
```

### 1.4 Windows：一键构建客户端

在仓库根目录用 PowerShell 执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
```

脚本自动：检查 CMake/Git → 缺失时克隆 vcpkg → 下载 WebView2 SDK 到 `third_party\webview2` → CMake 配置并编译 `eat-app.exe` → `pip install pyinstaller` 并打包 `eat-agent.exe` → 复制 `web/` 与 OpenSSL DLL 到 `dist\eat-app-windows-x64\`。

可选参数与产物：

```powershell
# 指定 vcpkg 与输出目录，跳过 Agent 打包
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1 `
  -VcpkgRoot C:\vcpkg -OutDir dist\eat-app-windows-x64 -SkipAgent
```

生成安装包（需 Inno Setup 6）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
# 产物：EatAppSetup-x64.exe
```

### 1.5 Agent 的依赖

Agent 只使用 **Python 标准库**（`http.server`、`sqlite3`、`urllib`、`threading` 等），无需 `pip install`。模型调用走 OpenAI 兼容 HTTP 接口，密钥从环境变量读取：

```bash
cp .env.example .env
# 编辑 .env，填入 DEEPSEEK_API_KEY=...
```

没有 Key 时自动降级到本地规则引擎 `rule-fallback`，断网也能完成基础问答与记录。

### 1.6 部署前校验

```bash
bash deploy/preflight.sh
```

检查：`mysql` / `redis-cli` 是否可用、`ulimit -n ≥ 1500000`、`fs.nr_open ≥ 2000000`、`net.core.somaxconn ≥ 65535`、Redis 是否单实例且无 pubsub、`9443` 是否已监听。输出 `preflight-ok` 才算通过。

---

## 2. 部署运行步骤说明

### 2.1 端口与访问方式（先看这张表）

| 端口 | 协议 | 谁在用 | 浏览器能否直接打开 |
|---|---|---|---|
| `9443` | 自定义 TCP 帧 | 客户端 / Agent → 服务端 | **不能**，不是 HTTP |
| `8787` | HTTP | Web 页面 `/api/*` → Agent | 能，但只用于接口 |
| `8000` | HTTP | 静态页（`dist/eat-web`） | 能，页面入口 |
| `9080`（示例） | HTTP | 管理/健康检查 | 视配置 |

本地开发时 `admin_port` 在 `deploy/server.toml.example` 是 `9080`，`.dev/route-a.conf` 用的是 `19080`，以实际配置为准。

### 2.2 方案 A：Docker 一键部署（推荐首次运行）

```bash
export EAT_MYSQL_ROOT_PASSWORD='替换成强密码'
docker compose -f deploy/docker/docker-compose.yml up -d --build
```

这条命令实际做了什么：

1. 构建 `eat-server` 多阶段镜像（编译 Muduo 与服务端，运行阶段只保留二进制与运行库）；
2. 启动 `mysql:8.4`，字符集 `utf8mb4`，健康检查通过后才启动服务端；
3. 服务端入口脚本等待 MySQL、执行合并后的 `00_all.sql`（`000 → 007` 全量迁移）、再启动；
4. 只对宿主机暴露 `9443`，MySQL 仅在容器网络内可达。

验收：

```bash
docker compose -f deploy/docker/docker-compose.yml ps
docker compose -f deploy/docker/docker-compose.yml logs -f eat-server
ss -ltn | grep 9443
```

### 2.3 方案 B：裸机部署

**1）准备数据库**

```bash
mysql --default-character-set=utf8mb4 -uroot -p -e "CREATE DATABASE IF NOT EXISTS eat DEFAULT CHARSET utf8mb4;"
```

**2）执行迁移（二选一）**

- 用打包产物里的合并脚本（推荐，已处理历史库名与新行、注释）：

```bash
bash scripts/package-linux.sh
mysql --default-character-set=utf8mb4 -uroot -p eat \
  < dist/eat-server-linux-x86_64/migrations/00_all.sql
```

- 直接执行源码迁移（注意 `000_init.sql` 里的历史库名是 `chatroom`，必须先替换；`001` 依赖 `@seed_pwd_hash`）：

```bash
sed 's/chatroom/eat/g' migrations/base/000_init.sql | mysql --default-character-set=utf8mb4 -uroot -p eat
mysql --default-character-set=utf8mb4 -uroot -p eat < migrations/base/foods_seed.sql
for f in migrations/add/00{2,3,4,5,6,7}_*.sql; do
  case "$f" in *_rollback.sql) continue;; esac
  mysql --default-character-set=utf8mb4 -uroot -p eat < "$f"
done
```

> `001_p8_business.sql` 需要会话变量 `@seed_pwd_hash`，最稳妥是直接用上面的 `00_all.sql`。迁移必须带 `--default-character-set=utf8mb4`，否则中文菜名会被二次编码，菜单与搜索为空。

**3）启动 Redis（可选，但推荐）**

```bash
redis-server --port 6379 --daemonize yes --save '' --appendonly no
redis-cli ping    # PONG
```

约束：只允许服务端连接；单实例；不使用订阅发布。

**4）配置服务端**

```bash
cp deploy/server.toml.example /etc/eat/server.toml
```

密码不走配置文件，只走环境变量：

```bash
export EAT_MYSQL_PASSWORD='你的MySQL密码'
./build-release/eat-server --config /etc/eat/server.toml
```

关键配置项：`tcp_port`、`mysql_host/mysql_port/mysql_db`、`db_pool_size`、`business_threads`、`max_frame_size`（默认 64 KiB，硬上限 256 KiB）。

**5）systemd 常驻**

```bash
sudo useradd -r -s /usr/sbin/nologin eat
sudo install -d -o eat -g eat /opt/eat /etc/eat
sudo cp deploy/systemd/eat-server.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now eat-server
```

大连接场景需要先应用 `deploy/sysctl/*.conf`，否则 `preflight.sh` 会失败。

### 2.4 启动 Agent 与 Web 端

```bash
# Agent（饭饭），Web 与 Windows 客户端共用
python3 -m agent --host 0.0.0.0 --port 8787

# 静态页：必须只服务 dist/eat-web，不要用仓库根目录
python3 -m http.server 8000 --bind 0.0.0.0 --directory dist/eat-web
```

浏览器打开 `http://<服务端IP>:8000`。登录页会自动使用当前页面的主机名去连 `8787` 和 `9443`。

> 不要在仓库根目录直接跑 `python3 -m http.server`：会把 `.env`、`.dev/`（含数据库密码）一起暴露。

本地开发机上已有 `.dev/` 环境时，可以用幂等脚本一次拉起三件套：

```bash
bash .dev/start-lan.sh
```

它只操作本机：启动/复用 MySQL 容器、探测 Redis、补起 `eat-server`（9443）、Agent（8787）与静态页（8000）。

### 2.5 Windows 客户端

- **安装包**：运行 `EatAppSetup-x64.exe`，桌面快捷方式启动；
- **免安装包**：解压 `dist/eat-client-windows.zip`，运行 `eat-app.exe`；
- 登录页填服务端地址（host 填 `10.x.x.x`，端口 `9443`），首次使用注册账号；
- 客户端自带本地 Agent，不需要用户另开 8787。

### 2.6 验收清单

```bash
# 1) 单元测试
ctest --test-dir build-release --output-on-failure

# 2) 服务端端口
ss -ltn | grep -E ':9443|:8787|:8000'

# 3) Agent 与身份状态
curl -s http://127.0.0.1:8787/api/service/status

# 4) 帧级冒烟（注册/握手/档案读写）
./build-release/client_smoke --host 127.0.0.1 --port 9443 \
  --login demo@local --password '...' --register
```

预期：测试全绿；`status` 返回 `mode=tcp`；`client_smoke` 输出 `handshake_ok` → `profile_ok ... source=service`。

### 2.7 升级与迁移

- 新增迁移按 `migrations/add/00N_name.sql` + `_rollback.sql` 成对提交，并同步 6 处清单（`scripts/package-linux.sh`、`deploy/docker/Dockerfile`、README 与三份文档的 glob）；
- 服务端升级：重新编译 → 重启 `eat-server`（C++ 改动必须重启）；
- Agent 升级：重启 Agent。**会清空内存里的 `user_key`，前端需要重新登录一次**；
- 前端升级：同步 9 个前端文件到 `dist/eat-web/`（`app.js`、`index.html` 与 7 个 CSS/适配文件），浏览器强刷即可，不需要重启服务端。

### 2.8 常见问题

| 现象 | 原因与处理 |
|---|---|
| 浏览器打开 `http://IP:9443` 失败 | 正常。9443 是自定义 TCP 帧协议，用客户端连接，不要用浏览器 |
| 别的电脑能开 8000 但不能登录 | 8787 不通（防火墙、AP 隔离、代理）；用 `Test-NetConnection IP -Port 8787` 验证 |
| 登录页超时 / `connection_closed` | 页面预填的 IP 已过期（换网后 IP 变了），改成当前服务端 IP |
| 两个浏览器登录后互相“顶号” | Web 共享同一个 8787 Agent，单会话；要同时用请各自跑独立 Agent |
| 菜单/搜索为空 | 迁移没带 `--default-character-set=utf8mb4`，中文被二次编码 |
| 上传头像后好友看不到 | 好友端是旧客户端（旧 Agent 只读本地 SQLite），需要在 Windows 重新打包分发 |
| 图片提示“太大/读取失败” | 上传前会压缩到 ≤32 KiB；换一张常规 JPEG/PNG |

---

## 3. 额外使用的开源组件说明

项目自身代码未附许可证文件（仓库当前无 `LICENSE`）。以下是被引入、编译或随产物分发的第三方组件：

| 组件 | 版本 / 来源 | 用途 | 许可证 | 是否修改 |
|---|---|---|---|---|
| Muduo | v2.0.2（`chenshuo/muduo`） | 服务端 TCP 主从 Reactor | BSD 2-Clause | 未修改，源码编译 |
| OpenSSL | 3.x（系统包） | 哈希、HMAC、随机数、DPAPI 相关加密 | Apache-2.0 | 未修改 |
| MySQL Server | 8.4（官方镜像） | 权威数据存储 | GPLv2（含 FOSS Exception） | 未修改 |
| libmysqlclient | 8.x（系统包） | 服务端访问 MySQL | GPLv2（含 FOSS Exception） | 未修改 |
| Redis Server | 7.x（系统包） | 服务端缓存 | BSD-3-Clause（7.4 起改为 RSALv2/SSPL，以实际发行版为准） | 未修改 |
| CMake | 3.20+ | 构建 | BSD-3-Clause | 仅构建工具 |
| GCC / libstdc++ | 13+ | 编译与运行时库 | GPLv3 + GCC Runtime Library Exception | 仅工具链 |
| Python | 3.11+ | Agent 运行时 | PSF-2.0 | 未修改 |
| PyInstaller | 最新 | 把 Agent 打成 `eat-agent.exe` | GPL-2.0（含打包例外） | 仅构建工具 |
| Microsoft WebView2 | Evergreen Runtime | 客户端内嵌浏览器 | Microsoft 专有许可（允许随应用分发） | 未修改 |
| Inno Setup | 6.x | 生成 Windows 安装包 | Inno Setup License | 仅构建工具 |
| Node.js | 18+ | 开发期 JS 语法检查 | MIT | 仅开发工具 |
| clang-format | 可选 | C++ 代码格式化 | Apache-2.0 with LLVM Exception | 仅开发工具 |
| Unsplash 图片 | 外链占位图 | 页面默认封面/示例图 | Unsplash License | 未内置，仅 URL 引用 |

说明：

- 服务端的 Redis 客户端是项目内自研的 RESP 实现，不依赖 `hiredis`；
- 帧协议、CRC32C、严格 JSON、线程池、连接治理、业务分层均为项目自研；
- 分发闭源商业版本时，需要自行复核 MySQL 客户端库的 GPL/FOSS Exception 适用性，以及 Redis 具体版本的许可证；
- WebView2 Runtime 由 Windows 11 自带，Windows 10 需安装 Evergreen Runtime。

---

## 4. 码道在项目开发中的应用总结

项目从“三套后端并存”收敛到单一 TCP 架构、补齐业务链路、重做前端视觉，整个过程都在 CodeArts 码道内完成。码道不是只用来补全代码，而是按“规格 → 设计 → 实现 → 审查 → 验证 → 沉淀”的链路参与。

### 4.1 用到的能力

| 能力 | 在项目中的实际用法 |
|---|---|
| 规格驱动（Spec） | 先把需求写成可验收的 `spec.md`、`design.md`、`tasks.md`，再进入实现；原文保留在 `.codeartsdoer/specs/app_transport_scale/`、`p8_backend_check/` |
| `design-pattern-applier` | 识别传输层三态（Web / IPC / TCP）耦合并落成 Adapter + Strategy（`eatTransport`）；服务端 Service/DAO 分层与 `ServiceRegistry` 门面 |
| `code-refactoring` | 拆分超大文件、统一命名、把业务执行器改为门面，保持行为不变 |
| `code-reviewer` | 复审连接计数下溢、`confirmed` 真值绕过、密钥输出风险、跨用户越权等问题 |
| `dead-code-eliminator` | 扫描无调用方代码，清理 Agent/前端死代码与契约漂移 |
| `unslop` | 重做登录页与主页面视觉：统一间距、配色、图标、状态反馈，去掉模板化“AI 味” |
| 单元测试生成 | 为 Python/TS 函数生成用例，覆盖分支与异常路径 |
| 文档与经验沉淀 | 产出 README、RUNBOOK、Windows 交接文档；把踩过的坑写进经验库，后续会话可直接复用 |

### 4.2 几个有代表性的场景

1. **服务端业务迁移**：把 Profile/Meal/Stats/Food/Friend/Ranking 逐个从旧实现迁到分级 Service/DAO，用 `code-reviewer` 复审并发与鉴权细节，最后用真实 TCP 用例验证。
2. **排行榜封面/头像上云**：在 64 KiB 帧上限下，没有硬塞图片，而是新增 `profile_media` 表 + `cover_ver/avatar_ver` 版本号，客户端按版本按需拉取；写入只认连接身份，读取只允许本人或已确认好友，并用三个临时账号做了越权与伪造 `user_key` 的对抗测试。
3. **前端视觉重制**：用 `unslop` 先定方向再改细节（间距、色彩、图标、文案），而不是堆渐变和光效。
4. **交付可复现**：每次改动都要求“构建通过 + 真实请求证据”，例如服务端 `ctest` 全绿、`client_smoke` 握手成功、Agent 侧 `POST /api/profile/cover` 返回真实版本号，而不是只看编译通过。

### 4.3 使用体会与边界

- **收益**：跨 C++/Python/JS 三类改动时，码道能保持同一套术语与架构约束（例如“Redis 只允许服务端连接”“Agent 只通过 ServiceClient 访问业务”），减少返工；
- **需要人工把关的地方**：破坏性操作（删库、删文件、清缓存、强推）必须先确认；跨端协议与帧大小这类约束，AI 容易给出“看起来能跑”的方案，必须用真实请求验证；
- **经验沉淀的价值**：把“帧上限 64 KiB”“迁移清单散落 6 处”“Agent 重启会清空 `user_key`”这类非代码事实写进记忆后，后续会话不再重复踩坑。

---

## 5. 能力边界与已知限制

- 菜品“记录”目前仍写入浏览器 `localStorage`，服务端的 `meal.patch` / `meal.delete` 尚未接到前端；
- 100 万连接是设计目标与压测脚本口径，**没有真实百万在线实测数据**；
- 服务端 TCP 为明文传输，公网部署需要在前置层加 TLS；
- Windows GUI 的壳、IPC 通道与安装包已具备，托盘、单实例、DPAPI 等仍需在目标 Windows 机器上复验；
- Web 端所有浏览器共享同一个 Agent 进程，同一时间只能维持一个账号会话；
- 仓库曾用于联调的测试账号（含 `media_*@test.local`）需要清理并轮换测试密码。

## 6. 相关文档

| 文档 | 内容 |
|---|---|
| `docs/architecture.md` | 当前权威架构与已打通链路 |
| `docs/protocol/v1.md` | 帧协议字段与错误码 |
| `docs/RUNBOOK.md` | Linux 服务端运行与排障 |
| `docs/WINDOWS_CLIENT_LAN_RUNBOOK.md` | Windows 客户端与局域网联调 |
| `docs/ROADMAP.md` | 后续四项方向（真实菜单、模型/API、独立 App、分布式与 io_uring） |
| `docs/DELIVERY-STATUS.md` | 本轮交付物、验证证据与未完成项 |
| `AGENT.md` / `AGENT_Linux.md` / `AGENT_win.md` | Agent 能力、Linux 交接、Windows 编译指南 |
| `CHANGELOG.md` | 版本变更记录 |
