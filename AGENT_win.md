# Windows 编译指南（Agent / GUI / 控制台客户端）

> 适用分支：`feat/app-transport-scale`
> 重要边界：`eat-server` 只支持 **Linux**（依赖 Muduo/epoll），Windows 上只构建 `client_smoke.exe`、`eat-app.exe` 与 Python Agent。
> 服务端请在 Linux 上编译，见 [`README.md`](README.md) 或 [`packaging/README-server.md`](packaging/README-server.md)。

## 1. 前置条件

| 组件 | 要求 | 用途 |
|---|---|---|
| Visual Studio 2022 Build Tools | 勾选 “使用 C++ 的桌面开发”（MSVC v143 + Windows SDK） | 编译 C++ |
| CMake | 3.20+，加入 PATH | 生成 VS 工程 |
| OpenSSL | 3.0+ x64（推荐 vcpkg） | `eat_protocol` 与 KeyVault 链接 |
| WebView2 SDK | `Microsoft.Web.WebView2` NuGet 包 | 仅 GUI |
| Python | 3.11 或 3.12（3.14 未验证） | Agent 与 PyInstaller |
| WebView2 Runtime | Win11 通常自带；Win10 需安装 Evergreen Runtime | 运行 GUI |

OpenSSL 通过 vcpkg 安装（只需一次）：

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install openssl:x64-windows
```

> 使用动态 OpenSSL 时，构建产物旁边需要 `libcrypto-3-x64.dll`、`libssl-3-x64.dll`；
> 它们位于 `C:\vcpkg\installed\x64-windows\bin`。

## 2. 获取代码

```powershell
git clone -b feat/app-transport-scale https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
```

## 3. 编译控制台客户端 `client_smoke.exe`

最简单的方式（不需要 CMake/OpenSSL）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

产物为仓库根目录的 `client_smoke.exe`。

等价的手动命令：

```powershell
cl /nologo /std:c++17 /EHsc /O2 /I. `
  tests\integration\client_smoke.cc `
  shared\protocol\FrameCodec.cc shared\protocol\Crc32c.cc `
  shared\protocol\JsonValue.cc shared\protocol\StrictJson.cc `
  /Fe:client_smoke.exe ws2_32.lib
```

验证：

```powershell
.\client_smoke.exe --host <Linux服务端IP> --port 9443 `
  --login win01@test.local --password win-pass-123 --register --patch-weight 66
```

期望：`handshake_ok`、`profile_ok`、`patch_ok`、`verify_weight=66`。

## 4. 编译 GUI `eat-app.exe`

### 4.1 准备 WebView2 SDK

从 NuGet 下载并解压（版本号以 NuGet 页面为准）：

```powershell
mkdir third_party\webview2 -Force
curl.exe -L -o webview2.zip https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/<版本号>
Expand-Archive webview2.zip third_party\webview2 -Force
```

`WEBVIEW2_ROOT` 指向包含 `build\native` 的目录：

```powershell
$env:WEBVIEW2_ROOT = "$PWD\third_party\webview2"
```

### 4.2 构建

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-win --config Release --target eat-app
```

或使用一键脚本：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Gui `
  -WebView2Root "$PWD\third_party\webview2"
```

产物：

```text
build-win\client\app\Release\eat-app.exe
build-win\client\app\Release\web\         # CMake 自动复制的前端资源
```

把 `libcrypto-3-x64.dll`、`libssl-3-x64.dll` 复制到 `eat-app.exe` 同目录。

### 4.3 运行目录

```text
eat-app.exe
eat-agent.exe              # 由第 5 步生成
web\                       # 若缺失，从仓库根目录复制
libcrypto-3-x64.dll
libssl-3-x64.dll
```

双击 `eat-app.exe`。首次启动创建 `webview-data\`；关闭窗口进入托盘，右键托盘图标“退出”才会结束进程。

## 5. 打包 Agent `eat-agent.exe`

```powershell
py -3.12 -m pip install --upgrade pyinstaller
py -3.12 -m PyInstaller --noconfirm --onefile --name eat-agent `
  --paths . `
  --add-data "agent/config;agent/config" `
  --add-data "agent/skills;agent/skills" `
  agent/ipc_server.py
```

产物：`dist\eat-agent.exe`，复制到 `eat-app.exe` 同目录。

说明：

- 壳以参数 `--ipc` 启动 Agent，命名管道为 `\\.\pipe\EatAppAgent-local-1`。
- 壳会注入 `EAT_APP_SID=local`、`EAT_APP_INSTANCE=1` 与 `EAT_AGENT_DATA_DIR=%LOCALAPPDATA%\EatApp\data`。
- 如果手动运行 Agent，需自行设置 `EAT_AGENT_DATA_DIR`，否则 onefile 临时目录退出即丢。

## 6. 连接 Linux 服务端

服务端需在 Linux 上监听 `0.0.0.0:9443`，并在同一局域网可达：

```powershell
Test-NetConnection <服务端IP> -Port 9443
```

首次启动 GUI 会显示“连接服务端”表单，直接填地址和账号即可，不需要改 JSON。

### 6.1 方式 A：首次启动在界面登录（推荐）

双击 `eat-app.exe` 后，页面会显示“连接服务端”表单：

- 服务器地址：Linux 局域网 IP，例如 `192.168.1.50`
- 端口：`9443`
- 账号、密码
- 首次使用勾选“注册新账号”

点击“连接”后，壳会完成 TCP 握手、保存会话到 `%LOCALAPPDATA%\EatApp\session.json`、注入 TCP 环境变量并启动 Agent。下次启动自动进入，不再询问。

### 6.2 方式 B：`login.json` 自动登录（批量部署）

无人值守或多台机器部署时，可以在 `eat-app.exe` 同目录（或 `%LOCALAPPDATA%\EatApp\`）放 `login.json`：

```json
{
  "host": "192.168.1.50",
  "port": 9443,
  "login_id": "win01@test.local",
  "password": "win-pass-123",
  "display_name": "Windows 用户",
  "register": true
}
```

启动顺序：

1. 壳读取 `login.json`，完成 TCP 握手；
2. 会话写入 `%LOCALAPPDATA%\EatApp\session.json`；
3. 注入 `EAT_TCP_HOST/PORT/USER_KEY/SERVER_TOKEN` 与 `EAT_SERVICE_REQUIRED=1`；
4. 启动 `eat-agent.exe`，页面直接使用服务端真实数据。

首次使用 `"register": true`；账号已存在时改成 `false`。成功后可以删除 `login.json`，下次直接复用 `session.json`。

> `login.json` 含明文密码，已在 `.gitignore` 中忽略；不要提交或随包分发。

### 6.3 方式 C：页面发送 `service.login`（调试）

页面协议已就绪，可通过 WebView2 DevTools 或在页面脚本中发送：

```javascript
window.chrome.webview.postMessage({
  type: "service.login",
  id: "login-1",
  host: "<服务端IP>",
  port: 9443,
  login_id: "win01@test.local",
  password: "win-pass-123",
  register: true,
  display_name: "Windows 用户"
});
```

成功后会收到 `service.login.response`，会话写入：

```text
%LOCALAPPDATA%\EatApp\session.json
```

壳随后通过命名管道发送 `service.configure`，Agent 以 TCP 帧访问服务端。
在没有会话时，Agent 可能以 mock 开发模式启动，不能作为真实数据验收。

开发阶段也可以直接给 Agent 注入环境变量，跳过壳登录：

```powershell
$env:EAT_TCP_HOST = "<服务端IP>"
$env:EAT_TCP_PORT = "9443"
$env:EAT_USER_KEY = "<user_key>"
$env:EAT_SERVER_TOKEN = "<access_token>"
$env:EAT_SERVICE_REQUIRED = "1"
.\eat-agent.exe --ipc
```

`user_key` 与 `access_token` 可通过 `client_smoke.exe --register` 的握手输出或服务端日志获取。

## 7. 验证清单

```powershell
# 1. 网络
Test-NetConnection <服务端IP> -Port 9443

# 2. 控制台真实请求
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win01@test.local --password win-pass-123

# 3. Agent 数据通道（Linux 上也可直接运行）
py -3.12 tests\integration\agent_service_client_test.py <服务端IP> 9443
```

服务端侧可观察 Redis 键：

```bash
redis-cli --scan --pattern 'eat:v1:*' | head
```

## 8. 已知限制

- 单 exe 未完成：`eat-app.exe` 仍需要同目录 `web\`、`eat-agent.exe` 与 OpenSSL DLL。
- GUI 桥接代码（登录表单、`service.login`、`service.request`、`login.json` 自动登录）尚未在 Windows 上编译验证；网页模式已在 Linux 上验证。
- Windows 上不能运行 `eat-server`；MySQL/Redis 只允许服务端连接。
- 业务 TCP 为明文，仅建议在可信局域网使用。
- 如果 `cmake` 报找不到 OpenSSL，确认 `CMAKE_TOOLCHAIN_FILE` 指向 vcpkg，并已安装 `openssl:x64-windows`。
- 如果 GUI 白屏，检查 `web\index.html` 是否存在，以及是否安装了 WebView2 Runtime。

## 9. 相关文档

- 服务端与局域网启动：[`README.md`](README.md)
- 路线 A 交接与验收状态：[`docs/ROUTE-A-HANDOFF.md`](docs/ROUTE-A-HANDOFF.md)
- 服务端发布包说明：[`packaging/README-server.md`](packaging/README-server.md)
