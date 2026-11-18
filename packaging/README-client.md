# 泛饭客户端（Windows）构建与运行

## 1. 立即可用的控制台客户端（无需 WebView2）

前提：Windows 10/11 x64，已安装 **Visual Studio 2022 Build Tools（C++ 桌面开发）**。

在“x64 Native Tools Command Prompt for VS 2022”中：

```powershell
powershell -ExecutionPolicy Bypass -File .\build-client.ps1
```

产物：`client_smoke.exe`。

运行（把地址换成服务端局域网 IP）：

```powershell
.\client_smoke.exe --host 192.168.1.20 --port 9443 `
  --login test@local --password test-pass-123 --register --patch-weight 66
```

期望：`handshake_ok` → `profile_ok ... source=service` → `patch_ok` → `verify_weight=66`。

## 2. 完整 GUI App（WebView2，需在 Windows 构建）

需要：

- Visual Studio 2022（MSVC x64）
- CMake 3.20+
- WebView2 SDK（NuGet `Microsoft.Web.WebView2`，展开后设置 `WEBVIEW2_ROOT` 到 `build/native`）
- Python 3.11 + PyInstaller（把 `agent/` 打成 `eat-agent.exe`）

```powershell
$env:WEBVIEW2_ROOT = "C:\path\to\Microsoft.Web.WebView2\build\native"
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --target eat-app
```

壳会从安装目录 `web/` 加载本地页面（构建时自动复制），并拉起同目录的 `eat-agent.exe`。

## 3. 当前边界（重要）

- `client_smoke.exe` 走真实 TCP 帧，已验证可完成注册/登录/握手/档案读写。
- GUI App 的 WebView2 壳已在开发机编译并验证页面加载；
  托盘、单实例、DPAPI 尚未验证，Agent 与业务 TCP 的桥接也尚未接线，
  因此 **GUI App 暂不能承诺端到端业务可用**。
- 不要把 `--password` 写进脚本或日志；生产环境应改为交互输入/凭据库。
