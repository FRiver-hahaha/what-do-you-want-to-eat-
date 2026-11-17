# Windows 客户端运行与局域网联调手册

> 适用分支：`feat/app-transport-scale`（2026-11-17 拉取到 `8c15ee2`）
> 本文覆盖：本机已生成的 exe、在另一台 Windows 电脑运行客户端的步骤、Linux 服务端启动、同一局域网业务测试与失败排查。

## 0. 本次实际完成情况

| 项目 | 结果 | 证据 |
|---|---|---|
| 拉取远端最新分支 | 完成，`bc2ecb2 → 8c15ee2`（8 个提交） | `git log -1` |
| 全仓库扫描 | 完成，重点核对 README/AGENT_win/RUNBOOK/协议/部署文档与对应代码 | 见第 1、9 节 |
| 生成 Windows 客户端 exe | **完成**：`dist/eat-client-windows/client_smoke.exe` | 3.1 MB，静态链接 |
| exe 本机可执行验证 | 完成：`--help` 正常；连接不存在端口返回 `connect_failed`（退出码 3） | 本机实测 |
| exe 端到端帧验证 | 完成：本地 mock 帧服务端（`127.0.0.1:19443`）跑通握手 → `profile.get` → `profile.patch` → 读回，四项输出正确、CRC32C 校验通过 | 见第 10 节 |
| 生成 GUI `eat-app.exe` | **未完成**：本机无 MSVC、CMake、WebView2 SDK | 见第 7 节 |
| Linux 服务端 | 未在本机启动（本机无 Linux/Muduo/MySQL） | 需切换 Linux 后执行第 5 节 |

## 1. 结论先行：现在能测什么

当前服务端只帧化了**认证 + 档案**，不是完整业务后端。

| 能力 | 状态 | 说明 |
|---|---|---|
| 注册 / 登录 / 握手鉴权 | ✅ 可测 | `client_smoke.exe` 走真实 TCP 帧 |
| `profile.get` / `profile.patch` | ✅ 可测 | 写库、幂等键、字段白名单 |
| `meal.*` / `stats.*` / `food.*` / `friend.*` / `ranking.*` | ⛔ 不可测 | 仍在 `chatroom-backend/`，未迁移到新 TCP 服务端 |
| Redis 缓存 | ⛔ 未接线 | 服务端当前不依赖 Redis 也能启动 |
| GUI `eat-app.exe` | ⚠️ 代码已落盘，未在 Windows 验证 | Agent↔TCP 桥接也未接线 |
| 100 万在线 | ⛔ 阻断 | 无压测数据，不要宣称 |

因此本次局域网联调的目标是：**注册 → 登录 → 握手 → 读档案 → 改体重 → 读回 → 幂等/鉴权负向用例**。不要用 GUI 页面里的餐记录、菜单、好友、排行来判定客户端“坏了”。

## 2. 已生成的客户端包

```
dist/eat-client-windows/
  client_smoke.exe    # 单文件，静态链接，仅依赖 Windows 系统 DLL
  README.md           # 面向另一台电脑的简版运行说明
```

可直接把整个目录拷到目标 Windows 电脑；也可以拷贝 `dist/eat-client-windows.zip` 后解压。

> `dist/` 与 `*.exe` 被 `.gitignore` 忽略，不会进入 Git。从 GitHub 全新克隆的机器上没有这两个产物，需要按第 4 节重新生成，或下载 GitHub Actions 的 `client_smoke-windows-x64` artifact。

`client_smoke.exe` 面向 Windows 10/11 x64，使用系统自带 `WS2_32.dll` 与 UCRT，不需要安装 VC++ 运行库、Python、WebView2。

## 3. 在另一台 Windows 电脑上运行（最快路径）

### 3.1 拷贝与确认

1. 用 U 盘、共享目录或内网下载，把 `eat-client-windows` 目录拷到目标机，例如 `C:\EatClient\`。
2. 打开 PowerShell，进入该目录：

```powershell
cd C:\EatClient
.\client_smoke.exe --help
```

看到 `client_smoke --host ...` 即说明 exe 可执行。

### 3.2 网络检查（必须先做）

在 Windows 上执行：

```powershell
ipconfig
ping <服务端IP>
Test-NetConnection <服务端IP> -Port 9443
```

判定：

- `PingSucceeded : True`：同网段可达。
- `TcpTestSucceeded : True`：9443 端口已放行。
- 任一为 `False`：先查服务端监听、Linux 防火墙、Wi-Fi AP 隔离、两台机器是否在同一网段。

### 3.3 首次注册并写档案

```powershell
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win01@test.local --password win-pass-123 `
  --register --patch-weight 66
```

说明：

- 登录 ID 会被服务端转小写，长度 3–128；密码长度 8–256。
- `--register` 只在首次使用；第二次起去掉该参数，走登录路径。
- 测试密码不要写进脚本、日志或截图。

### 3.4 预期输出

```text
handshake_ok user_key=<26位用户键> token=issued
profile_ok name=我 source=service
patch_ok
verify_weight=66
```

四项都出现，说明：TCP 帧、握手鉴权、MySQL 读、MySQL 写、幂等键全部打通。

### 3.5 二次登录与负向用例

```powershell
# 已注册账号再次登录（不带 --register）
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

### 3.6 失败排查表

| 现象 | 原因 | 处理 |
|---|---|---|
| `connect_failed` | 服务端未监听 / 防火墙 / IP 错 / 不同网段 | `ss -ltn \| grep 9443`、`ufw status`、`Test-NetConnection` |
| `handshake_error code=5001` | 服务端 MySQL 不可用 | 查 `docker logs` 或服务端日志，确认迁移已执行 |
| `handshake_error code=2002` | 账号或密码错误、令牌无效 | 确认密码 ≥8 位；首次是否加了 `--register` |
| `handshake_error code=2003` | 账号被锁定（连续失败 5 次） | 等待锁定窗口结束或换测试账号 |
| `handshake_recv_failed reason=recv_failed` | 服务端握手阶段异常断开 | 查服务端日志；确认协议版本 1.0 |
| `patch_error code=3001` | 写方法缺 `idem_key` | 去掉 `--omit-idem` |
| `patch_error code=5001` | 写库失败 | 查 MySQL 连接与 `health_profile` 表 |
| 页面/GUI 业务失败 | 业务未帧化、GUI 未验证 | 属于当前已知边界，见第 1、7 节 |

## 4. 在本机重新生成 exe

### 4.1 Msys2 / MinGW 静态编译（本次使用）

```bash
g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -I. \
  tests/integration/client_smoke.cc \
  shared/protocol/FrameCodec.cc \
  shared/protocol/Crc32c.cc \
  shared/protocol/JsonValue.cc \
  shared/protocol/StrictJson.cc \
  -o dist/eat-client-windows/client_smoke.exe -lws2_32
```

验证：

```bash
ldd dist/eat-client-windows/client_smoke.exe   # 只应看到 ntdll/KERNEL32/KERNELBASE/ucrtbase/WS2_32/RPCRT4
./dist/eat-client-windows/client_smoke.exe --help
```

### 4.2 MSVC（目标机有 VS2022 时）

在 “x64 Native Tools Command Prompt for VS 2022” 中：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

产物为仓库根目录 `client_smoke.exe`。

### 4.3 GitHub Actions

`.github/workflows/windows.yml` 的 `console-client` job 会构建 `client_smoke.exe` 并上传 artifact：

```text
client_smoke-windows-x64
```

在 GitHub Actions 页面下载即可，无需本机装 MSVC。

## 5. Linux 服务端启动（局域网）

### 5.1 推荐：Docker（自动迁移、屏蔽依赖差异）

在 Linux 仓库根目录：

```bash
export EAT_MYSQL_ROOT_PASSWORD='换成一个强密码'
docker compose -f deploy/docker/docker-compose.yml up -d --build
docker compose -f deploy/docker/docker-compose.yml logs -f eat-server
```

说明：

- 首次构建会从 GitHub 拉取并编译 Muduo，需要外网。
- 容器启动时自动执行 `001→006` 迁移。
- 服务端监听 `0.0.0.0:9443`。
- 当前 compose 未包含 Redis；服务端未接线 Redis，不影响本次认证/档案联调。

### 5.2 裸机二进制

依赖（Ubuntu 示例）：

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

数据库迁移：

```bash
mysql -uroot -p -e "CREATE DATABASE IF NOT EXISTS eat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
```

`001` 基线仍在 `chatroom-backend/`。按 `deploy/docker/Dockerfile` 的同一拼装逻辑生成一次性迁移文件：

```bash
cd /path/to/what-do-you-want-to-eat-
{
  echo "CREATE DATABASE IF NOT EXISTS eat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
  echo "USE eat;"
  sed 's/chatroom/eat/g' chatroom-backend/init.sql | tr -d '\r'; echo
  echo "SET @seed_pwd_hash = SHA2('disabled-login', 256);"
  tr -d '\r' < chatroom-backend/server/http/migrations/001_p8_business.sql \
    | sed 's/^--\([^ ]\)/-- \1/'; echo
  tr -d '\r' < chatroom-backend/server/http/migrations/foods_seed.sql \
    | sed 's/^--\([^ ]\)/-- \1/'; echo
  for f in migrations/add/00{2,3,4,5,6}_*.sql; do
    case "$f" in *_rollback.sql) continue;; esac
    tr -d '\r' < "$f" | sed 's/^--\([^ ]\)/-- \1/'; echo
  done
} > /tmp/eat_00_all.sql

mysql -uroot -p < /tmp/eat_00_all.sql
```

不要用 `scripts/package-linux.sh` 做这件事：该脚本需要 `build-release/eat-server`，并且会删除并重建整个 `dist/` 目录。

启动：

```bash
export EAT_MYSQL_PASSWORD='你的MySQL密码'
export EAT_MYSQL_USER=root             # 示例配置默认是 eat，按实际账号覆盖
./build/eat-server --config deploy/server.toml.example
```

`deploy/server.toml.example` 中 `mysql_user = eat`；若没有该用户，必须用 `EAT_MYSQL_USER` 覆盖，或先在 MySQL 建用户并授权。

### 5.3 防火墙与监听确认

```bash
sudo ufw allow 9443/tcp        # 使用 ufw 时
sudo ss -ltnp | grep 9443      # 应为 0.0.0.0:9443
```

若使用 firewalld：

```bash
sudo firewall-cmd --add-port=9443/tcp --permanent
sudo firewall-cmd --reload
```

### 5.4 服务端当前支持的方法

以 `server/app/ServiceRegistry.cc` 为准：

- 握手：`access_token` 校验 / 登录 / `action=register` 注册后登录
- `auth.login` / `auth.register`
- `profile.get`
- `profile.patch`（必须带 `idem_key`）

其余业务方法尚未注册，返回未知方法错误属于预期。

## 6. 局域网联调验收清单

按顺序执行并留存证据（命令、原始输出、时间、服务端 IP）：

- [ ] Linux：`ss -ltn | grep 9443` 显示 `0.0.0.0:9443`
- [ ] Linux：防火墙已放行 9443
- [ ] Windows：`Test-NetConnection <IP> -Port 9443` 为 True
- [ ] Windows：首次 `--register --patch-weight 66` 输出四项成功
- [ ] Windows：去掉 `--register` 再登录成功
- [ ] Windows：`profile.patch` 改成 67 后读回 67
- [ ] Windows：`--omit-idem` 返回 3001
- [ ] Windows：`--skip-handshake` 返回 2001
- [ ] Windows：错误密码返回 2002
- [ ] 服务端日志能看到对应握手/请求记录，且日志不含明文密码或 token
- [ ] 负向验证：GUI 页面餐记录/菜单/好友失败属于“业务未帧化”，不是网络故障

## 7. GUI `eat-app.exe` 的构建与运行（未验证）

本机没有 MSVC、CMake、WebView2 SDK，因此**没有生成 `eat-app.exe`**。以下流程需要在装有 VS2022 的 Windows 目标机执行；即使构建成功，也仍受“Agent↔TCP 未接线、业务未帧化”限制。

### 7.1 前置

- Visual Studio 2022 Build Tools，勾选“使用 C++ 的桌面开发”和 Windows SDK
- CMake 3.20+
- WebView2 SDK（NuGet `Microsoft.Web.WebView2`，设置 `WEBVIEW2_ROOT` 到 `build/native`）
- Python 3.11 + PyInstaller（打包 `eat-agent.exe`）
- 目标机安装 WebView2 Runtime（Win11 通常自带）

### 7.2 构建

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
  -DWEBVIEW2_ROOT="C:\path\to\Microsoft.Web.WebView2\build\native"
cmake --build build-win --config Release --target eat-app
```

打包 Agent：

```powershell
python -m pip install --upgrade pyinstaller
pyinstaller --noconfirm --onefile --name eat-agent `
  --add-data "agent;agent" `
  --hidden-import agent.ipc_server `
  agent\ipc_server.py
```

### 7.3 安装目录形态

```text
EatApp\
  eat-app.exe
  eat-agent.exe
  web\            # 构建时自动从仓库根复制
```

双击 `eat-app.exe` 后：创建 `webview-data`、加载本地 `web/index.html`、尝试拉起同目录 `eat-agent.exe` 并通过命名管道通信。

### 7.4 已知阻断

- GUI 从未在 Windows 实机编译验证（仓库文档明确标注）。
- `main.cc` 目前只做 WebView2 壳 + 命名管道转发；业务 TCP 请求尚未接入。
- 页面业务接口（餐记录/菜单/统计/好友/排行）服务端未帧化。
- 因此 GUI 只能用于验证“壳能启动、页面能加载、IPC 能握手”，不能用于完整业务验收。

GitHub Actions 的 `gui-app` job 可在 `workflow_dispatch` 下手动触发并产出 `eat-app-windows-x64` artifact，但该 job 配置为 `continue-on-error: true`，不构成已验证结论。

## 8. 安全与风险

- 当前 TCP 为**明文**，只适合可信局域网；公网部署必须先接 TLS。
- 测试密码只在命令行交互输入或本地临时使用，不要写入脚本、日志、聊天记录。
- 不要把 MySQL 密码、模型 API Key 提交到仓库；服务端密码只走 `EAT_MYSQL_PASSWORD`。
- `client_smoke.exe` 的 `--save-token` 会把令牌写入本地文件，测试完请删除。
- 客户端与服务端都不应直连 Redis；Redis 仅服务端使用。

## 9. 关键代码与文档扫描结论

| 文件 | 作用 | 扫描结论 |
|---|---|---|
| `README.md` | 三端架构与功能状态 | 明确 GUI 未验证、业务未帧化、百万在线阻断 |
| `AGENT_win.md` | Windows 构建/联调/上传手册 | 与本手册流程一致；GUI 验证清单仍未打勾 |
| `packaging/README-client.md` | 客户端包说明 | 只承诺 `client_smoke.exe` 可跑真实 TCP |
| `docs/RUNBOOK.md` | 服务端/客户端运行手册 | Docker 与裸机步骤可用；强调明文 TCP |
| `docs/DELIVERY-STATUS.md` | 交付证据与未完成项 | 记录 Linux 真实请求通过、Windows GUI 未生成 |
| `docs/architecture.md` | 当前代码事实权威入口 | 服务端仅 auth/profile；Redis/业务未接线 |
| `docs/design/02-windows-app.md` | Windows App 方案 | 壳/IPC/托盘设计完整；A-2 首连顺序风险已登记 |
| `docs/protocol/v1.md` | 40B 帧协议 | 与 `client_smoke` 实际编码一致 |
| `server/app/ServiceRegistry.cc` | 方法注册 | 只有握手、`auth.*`、`profile.*` |
| `client/app/win32/main.cc` | GUI 壳 | WebView2 + 托盘 + 单实例 + IPC；未接业务 TCP |
| `bridge.js` | 页面传输抽象 | 浏览器 `fetch+SSE` / App `postMessage` 双模式 |
| `agent/ipc_server.py` | Agent 命名管道 | 只处理 `hello/ping/agent.chat/shutdown` |

### 文档与代码的一致性

- **一致**：README、RUNBOOK、DELIVERY-STATUS、architecture 都承认 GUI 未在 Windows 验证、业务未帧化、Redis 未接线、百万在线无实测。
- **一致**：`client_smoke.cc` 实际只发握手、`profile.get`、`profile.patch`，与文档描述的 S1 范围相同。
- **风险点**：`AGENT_win.md` 第 5 节的 GUI 构建步骤可执行，但第 5.4 节的验证清单全部未打勾；不要把“构建成功”等同于“业务可用”。
- **风险点**：Docker 方案依赖构建期从 GitHub 拉取 Muduo；离线 Linux 机器需要预先准备 Muduo 源码或使用已构建镜像。
## 10. 本次实际验证输出

验证方式：在本机启动一个只实现帧编解码与三个方法的 mock TCP 服务端（`127.0.0.1:19443`），再用已生成的 `client_smoke.exe` 发起真实 TCP 请求。目的是证明 **exe 本身可执行、帧编码正确、能解析响应并完成完整流程**；mock 不代表真实服务端，真实环境仍以第 5、6 节为准。

客户端输出：

```text
handshake_ok user_key=01MOCKUSERKEY0000000000000 token=issued
profile_ok name=我 source=service
patch_ok
verify_weight=66
```

服务端侧观察到的帧序列（已确认 CRC32C 全部通过）：

```text
frame 1  handshake_req  action=register
frame 3  request        method=profile.get
frame 3  request        method=profile.patch  weight=66.0 + idem_key
frame 3  request        method=profile.get
```

说明：mock 只验证客户端二进制与协议链路；注册写库、scrypt、幂等持久化、MySQL 错误码仍必须用真实 Linux 服务端验证。
