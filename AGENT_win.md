# AGENT_win.md — Windows 构建与进度同步手册

> 目标：在 Windows 上完成 `client_smoke.exe`（立即可用）与 `eat-app.exe`（WebView2 GUI）的构建、局域网联调，并把进度同步回 GitHub。
> 当前事实基线：Linux 服务端已通过真实 TCP 验证；**Windows GUI 已在开发机用 MinGW + WebView2 SDK 编译并验证启动/页面渲染，业务桥接未接线**。

## 0. 先明确能力边界

| 目标 | 现在能否完成 |
|---|---|
| Windows 控制台客户端连接局域网服务端，跑注册/登录/档案读写 | ✅ 能（`client_smoke.exe`） |
| Windows WebView2 GUI 启动并显示页面 | ✅ 已构建验证（MinGW + WebView2 SDK） |
| GUI 内完成餐记录/菜单/统计/好友等全部业务 | ⛔ 不能。服务端仅帧化了认证 + `profile.*`；Agent 与 TCP 桥接未接线 |

任何“GUI 已跑通全部业务”的结论都必须有截图/日志/请求记录，否则只能写“未验证”。

## 1. 环境准备（一次性）

- Windows 10 1809+ / 11 x64
- **Visual Studio 2022 Build Tools**，勾选：
  - “使用 C++ 的桌面开发”
  - Windows 10/11 SDK
- **CMake 3.20+**（安装时勾选加入 PATH）
- **Git for Windows**
- **Python 3.11 x64**（Agent 打包用；`python --version` 可验证）
- **WebView2 SDK**：NuGet 包 `Microsoft.Web.WebView2`，解压/还原后记下 `build/native` 路径
- （可选）**Docker Desktop**：不装也能联调，服务端跑在 Linux 机器上即可

验证：

```powershell
cmake --version
cl
python --version
git --version
```

`cl` 需要在 **x64 Native Tools Command Prompt for VS 2022** 中执行；普通 PowerShell 找不到属正常。

## 2. 拉取仓库

```powershell
git clone https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
git checkout feat/app-transport-scale
git log -1 --oneline
```

> 如果远端尚未包含最新合并提交，先执行 `git pull --ff-only`；仍缺失则说明推送未完成，见第 8 节。

## 3. 第一步：构建 `client_smoke.exe`（最快验证）

方式 A（推荐，包内脚本）：

1. 解压 GitHub Release 中的 `eat-client-windows.zip`（或从仓库 `dist/` 自行生成）。
2. 在 **x64 Native Tools Command Prompt** 中：

```powershell
cd eat-client-windows
powershell -ExecutionPolicy Bypass -File .\build-client.ps1
```

方式 B（仓库内直接用 cl）：

```powershell
cl /nologo /std:c++17 /EHsc /O2 /I. `
  tests\integration\client_smoke.cc `
  shared\protocol\FrameCodec.cc shared\protocol\Crc32c.cc `
  shared\protocol\JsonValue.cc shared\protocol\StrictJson.cc `
  /Fe:client_smoke.exe ws2_32.lib
```

预期产物：`client_smoke.exe`（约 100–300 KB，无第三方 DLL）。

## 4. 第二步：联调服务端（局域网）

服务端机器（Linux）：

```bash
sudo ufw allow 9443/tcp
ss -ltn | grep 9443          # 0.0.0.0:9443
```

Windows：

```powershell
Test-NetConnection <服务端IP> -Port 9443

.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win@test.local --password win-pass-123 --register --patch-weight 66
```

期望输出：

```
handshake_ok user_key=... token=issued
profile_ok name=我 source=service
patch_ok
verify_weight=66
```

失败排查：

| 现象 | 处理 |
|---|---|
| `connect_failed` | 服务端未监听 / 防火墙 / IP 填错 / 不在同一网段 |
| `handshake_error code=5001` | 服务端 MySQL 不可用；查 `docker logs` 或服务端日志 |
| `handshake_error code=2002` | 密码错误或 token 无效 |
| `pre_handshake_error code=2001` | 正常（未握手直接发业务帧的负向用例） |

## 5. 第三步：构建 GUI `eat-app.exe`

### 5.1 准备 WebView2 SDK 与 Agent

```powershell
# 任选其一：NuGet 下载并解压
nuget install Microsoft.Web.WebView2 -Version 1.0.2592.51 -OutputDirectory packages
$env:WEBVIEW2_ROOT = (Resolve-Path "packages\Microsoft.Web.WebView2.*\build\native").Path
```

Agent 可执行文件（GUI 壳会拉起同目录 `eat-agent.exe`）：

```powershell
python -m pip install --upgrade pyinstaller
pyinstaller --noconfirm --onefile --name eat-agent `
  --add-data "agent;agent" `
  --hidden-import agent.ipc_server `
  agent\ipc_server.py
# 产物 dist\eat-agent.exe 复制到 GUI 输出目录
```

> 若暂不打包 Agent，可先只验证壳与页面加载；此时 IPC 会失败，属预期。

### 5.2 构建

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
  -DWEBVIEW2_ROOT="$env:WEBVIEW2_ROOT"
cmake --build build-win --config Release --target eat-app
```

产物目录（典型）：`build-win\client\app\Release\eat-app.exe` 与同目录 `web\`。
`web/` 由 CMake 构建后自动从仓库根复制（index/app/bridge/adapter/css/assets）。

### 5.3 安装目录形态

```
EatApp\
  eat-app.exe
  eat-agent.exe
  web\            ← 本地页面（禁止远程首屏）
```

双击 `eat-app.exe`：
- 首次启动创建 `webview-data`；
- 托盘常驻，关窗进托盘，右键“退出”才断开；
- 若页面白屏：确认 `web\index.html` 存在、WebView2 Runtime 已安装。

### 5.4 已验证 / 待验证清单（逐项打勾）

- [x] `eat-app.exe` 能启动并加载本地页面
- [x] WebView2 渲染进程与 `webview-data` 生成
- [x] 页面视觉与 Web 端一致（PrintWindow 截图验证）
- [ ] 断网冷启动仍能显示界面（不显示伪造业务数据）
- [ ] 单实例：重复双击只激活已有窗口
- [ ] 托盘：关闭窗口后进程仍在，双击托盘恢复
- [ ] `eat-agent.exe` 被拉起且 IPC 握手成功
- [ ] 页面 `window.eatTransport.mode === "app"`
- [ ] 仅 1 条业务 TCP（`netstat -ano | findstr 9443`）
- [ ] 无 `:8787` 监听（App 模式）
- [ ] Key 存取（DPAPI）与“未配置 Key”提示
- [ ] MSIX/安装包可安装、卸载干净

## 6. 进度同步规范

每完成一步，在**本文件末尾的进度表**追加一行，并提交：

```powershell
git add AGENT_win.md
git commit -m "docs(win): record <步骤> result"
```

表格字段：日期 / 步骤 / 命令摘要 / 结果（通过/失败/阻塞）/ 证据路径（日志或截图）。

提交前必须：

```powershell
ctest --test-dir build-win -C Release --output-on-failure   # 若构建了测试
python -m unittest agent.tests.test_smoke
node --check app.js
node --check bridge.js
```

## 7. 企业级仓库约定（与 CONTRIBUTING 一致）

- 分支：`feat/*`、`fix/*`、`chore/*`；不直接推 `main`。
- 提交：Conventional Commits（`feat(scope): ...`、`docs: ...`、`chore: ...`）。
- 禁止提交：`.env`、Key、密码、`dist/`、`build*/`、`*.log`、`data/agent/`。
- PR 必须包含：变更说明、验证命令与输出、影响范围、回滚方式。
- 不夹带格式化噪音；不提交 IDE 临时文件。

## 8. 上传 GitHub

### 8.1 当前状态（2026-11-17）

Linux 侧已通过 SSH 推送成功，远端 `feat/app-transport-scale` = `3e8d8f1`。Windows 上直接 clone 即可获得全部提交：

```
08263e9 chore(repo): add enterprise metadata, CI and packaging scripts
4900801 feat(server): merge MySQL identity, profile methods and bounded thread pool
4ad8311 feat(client): add transport bridge, IPC protocol and Windows shell; align agent contract
ac7271c chore(deploy): add Docker image, compose stack and migration entrypoint
3fd8a1d docs: record push blocker, git bundle and Windows import steps
b7246ae docs: update Windows handoff after successful SSH push
3e8d8f1 fix(ci): build Windows client from repository sources instead of ignored dist
```

`dist/eat-agent-updates.bundle` 仅作为离线备份保留，正常联网时**不需要**手动导入。

### 8.2 可选：离线 bundle 导入（无法直接访问 GitHub 时）

```powershell
git clone https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
git checkout feat/app-transport-scale

# 把 bundle 放到仓库根目录后：
git fetch ..\eat-agent-updates.bundle feat/app-transport-scale:incoming
git merge --ff-only incoming
git branch -D incoming

git push -u origin feat/app-transport-scale
```

首次推送会弹出 Git Credential Manager 登录（浏览器授权），或使用 Personal Access Token（`repo` 权限）作为 HTTPS 密码。**不要**把 token 写进脚本或仓库。

如果从零开始提交（没有 bundle）：

```powershell
git status
git add <files>
git commit -m "feat(client): build Windows shell"
git push -u origin feat/app-transport-scale
```

### 8.3 发布二进制（不要提交进 Git）

二进制与依赖通过 **GitHub Release** 分发，仓库只保留源码与构建脚本：

```powershell
gh release create v0.1.0 `
  .\dist\eat-client-windows.zip `
  .\dist\eat-server-linux-x86_64.tar.gz `
  --title "v0.1.0" --notes "S0-S2: server merge, identity/profile, Windows shell"
```

没有 `gh` 时：GitHub 网页 → Releases → Draft a new release → 上传两个附件。
CI 还会把 `client_smoke.exe` 作为 workflow artifact 产出（见 `.github/workflows/windows.yml`）。

## 9. 本轮已知阻断与风险

1. **GUI 未验证**：WebView2 壳、托盘、单实例、DPAPI、断网冷启动都还没有 Windows 证据。
2. **Agent↔TCP 未接线**：`eat-agent.exe` 目前只处理 `agent.chat`，业务数据仍需 App 侧桥接；GUI 内菜单/记录接口会失败。
3. **业务未帧化**：`meal/stats/food/friend/ranking` 未注册，页面业务请求无服务端支持。
4. **明文 TCP**：仅可信局域网；公网需先接 TLS。
5. **迁移目录**：001 基线已迁入 `migrations/base/`，`chatroom-backend/` 已删除；业务 Handler 仍需按历史设计重写。

## 10. 进度表

| 日期 | 步骤 | 命令摘要 | 结果 | 证据 |
|---|---|---|---|---|
| 2026-11-17 | Linux 侧基线 | ctest / unittest / s1_smoke / docker | 通过 | `docs/DELIVERY-STATUS.md` |
| 2026-11-17 | Windows 控制台客户端 | MSYS2 g++ 静态编译 `client_smoke.exe`；mock 帧服务端验证握手/档案读写 | 通过 | `docs/WINDOWS_CLIENT_LAN_RUNBOOK.md` |
|  |  |  |  |  |
