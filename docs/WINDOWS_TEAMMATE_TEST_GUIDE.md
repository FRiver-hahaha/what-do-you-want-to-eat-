# Windows 队友测试指南（Web + GUI）

> 服务端当前运行在 Linux：`10.112.151.15:9443`
> 代码分支：`feat/app-transport-scale`（提交 `8d2cdac`）
> 本文分两条路径：**Web 测试**（零安装，5 分钟）和 **GUI 测试**（在 Windows 本机构建后运行）。

---

## 0. 先确认网络（两条路径都先做）

在 Windows PowerShell 中执行：

```powershell
Test-NetConnection 10.112.151.15 -Port 9443
Test-NetConnection 10.112.151.15 -Port 8000
Test-NetConnection 10.112.151.15 -Port 8787
```

三项都应为 `TcpTestSucceeded : True`。

- 只有 9443 不通：多半是不同 WiFi/网段、公司或校园网的 AP 隔离，需让服务端同学换网络或找网管放行。
- 9443 通但 8000 不通：静态页问题，直接反馈。
- 8787 只在 Web 路径需要；GUI 路径用本机 Agent，不依赖它。

---

## 1. 路径 A：Web 测试（推荐先做，零安装）

### 1.1 打开页面

浏览器访问：

```text
http://10.112.151.15:8000
```

不要用 `file://` 直接打开 HTML，页面必须通过 HTTP 提供。

### 1.2 登录

1. 页面默认在“登录”标签。
2. 点开“服务器设置”，确认地址为 `10.112.151.15`、端口为 `9443`（地址已默认填好，IP 变了就手动改）。
3. 账号：`win01@test.local`
4. 密码：`<测试密码>`
5. 验证码：输入右侧图片里的 4 位字符，看不清点图片刷新。
6. 点“进入饭饭”。

> 该账号已由服务端同学注册好，**不要切换到“注册”标签**。
> 如需自己的账号：切到“注册”标签，账号写成 `win02@test.local` 这类，密码至少 8 位，验证码照填，再提交。

### 1.3 Web 冒烟清单

- [ ] 页面正常打开，无白屏、无样式错乱
- [ ] 验证码图片能显示，点击能刷新
- [ ] 登录后进入主界面
- [ ] 菜单页能看到真实菜品（不是空列表、不是“示例数据”）
- [ ] 档案页修改体重并保存，提示成功
- [ ] 关闭浏览器再打开页面，仍在登录状态（免重复登录）
- [ ] 故意输错密码，提示“账号或密码错误”
- [ ] 故意输错验证码，提示“验证码错误或已过期”
- [ ] 点“退出登录”后回到登录页

### 1.4 Web 路径的限制

- 浏览器连接的是**服务端同学的共享 Agent（8787）**，它是单会话的：你登录后会把对方或其他同学的 `user_key` 顶掉。**同一时间只安排一个人用 Web 测试**，或大家各自用 GUI 路径。
- 聊天功能不可用：需要 `DEEPSEEK_API_KEY`，服务端未配置，属预期。
- 菜单、档案、记录等是共用服务端测试库 `eat_route_a2` 的数据，不是正式环境。

---

## 2. 路径 B：GUI 测试（Windows 本机构建）

GUI 由 `eat-app.exe`（Win32 + WebView2 壳）和 `eat-agent.exe`（本地 Agent，PyInstaller 打包）组成。服务端同学使用的是 Linux，**无法产出 Windows exe**，必须在 Windows 上构建。仓库已带一键脚本。

### 2.1 测试机要求

| 依赖 | 要求 | 说明 |
|---|---|---|
| 系统 | Windows 10/11 x64 | Win11 自带 WebView2 Runtime；Win10 需装 Evergreen Runtime |
| Visual Studio 2022 | 勾选“使用 C++ 的桌面开发” | 提供 MSVC x64 工具链 |
| CMake | 3.20+，在 PATH 中 | VS 自带一份，但脚本要求 PATH 能直接调用 |
| Git | 任意较新版本 | 克隆仓库、vcpkg 也要用 |
| Python | 3.11 或 3.12，勾选 Add to PATH | 只用于把 Agent 打成 exe |
| 网络 | 能访问 github.com 与 nuget.org | 脚本会自动下载 vcpkg 和 WebView2 SDK |
| 磁盘 | 预留 5 GB 左右 | vcpkg + OpenSSL + 构建中间产物 |
| Inno Setup 6 | 可选 | 只有要生成 `EatAppSetup-x64.exe` 安装包时才需要 |

### 2.2 获取源码

方式一（有 Git）：

```powershell
git clone -b feat/app-transport-scale https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
```

方式二（浏览器下载源码包，解压后进入目录）：

```text
http://10.112.151.15:8000/dl/eat-client-src.zip
```

解压后目录名是 `eat-client-src`，进入该目录再执行构建命令。

### 2.3 一键构建

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
```

脚本会自动完成：

1. 检查 CMake / Git / VS2022（MSVC x64）/ Python；
2. 没有 vcpkg 就克隆并 bootstrap，安装 `openssl:x64-windows`；
3. 没有 WebView2 SDK 就下载 NuGet 包并解压到 `third_party\webview2`；
4. 用 CMake 配置 `build-win` 并编译 `eat-app.exe`；
5. 用 PyInstaller 打包 `eat-agent.exe`；
6. 组装运行目录 `dist\eat-app-windows-x64\`（含 `eat-app.exe`、`eat-agent.exe`、`web\`、OpenSSL DLL、`Start.bat`、`README.txt`）；
7. 压缩为 `dist\eat-app-windows-x64.zip`。

常见可选参数：

```powershell
# vcpkg 不在 C:\vcpkg 时
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1 -VcpkgRoot D:\vcpkg

# 已经准备好 WebView2 SDK 时
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1 -WebView2Root C:\sdk\webview2

# 只想编 GUI、不打包 Agent（不推荐，运行时缺 eat-agent.exe）
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1 -SkipAgent
```

### 2.4 运行 GUI

```powershell
dist\eat-app-windows-x64\Start.bat
```

或直接双击 `dist\eat-app-windows-x64\eat-app.exe`。

**目录必须完整**，不要只把 `eat-app.exe` 单独拷走：

```text
eat-app-windows-x64\
  eat-app.exe
  eat-agent.exe
  web\                 # 页面资源，缺失会白屏
  libcrypto-3-x64.dll
  libssl-3-x64.dll
  Start.bat
  README.txt
```

### 2.5 GUI 登录

首次启动会显示与 Web 相同的登录页：

1. “登录”标签；
2. “服务器设置”里地址填 `10.112.151.15`，端口 `9443`（页面默认已预填该地址）；
3. 账号 `win01@test.local`，密码 `<测试密码>`；
4. 输入图形验证码；
5. 点“进入饭饭”。

登录成功后会话保存到：

```text
%LOCALAPPDATA%\EatApp\session.json
```

下次启动直接进主界面，不再询问。

> 服务端 IP 变了：关闭程序，删除 `%LOCALAPPDATA%\EatApp\session.json`，重开后在“服务器设置”里填新地址。若只是临时换网且还能连上旧 IP，无需删文件。

### 2.6 GUI 冒烟清单

构建与启动：

- [ ] `build-windows-oneclick.ps1` 退出码为 0
- [ ] `dist\eat-app-windows-x64\eat-app.exe` 存在
- [ ] `dist\eat-app-windows-x64\eat-agent.exe` 存在
- [ ] `dist\eat-app-windows-x64\web\index.html` 存在
- [ ] 双击 `Start.bat` 能打开窗口，无白屏

登录与业务：

- [ ] 首次启动显示登录页，地址已预填 `10.112.151.15`
- [ ] 验证码图片能显示，点击能刷新
- [ ] 登录成功进入主界面
- [ ] 菜单页显示真实菜品
- [ ] 档案页修改体重并保存成功
- [ ] 关闭后再启动，直接进入主界面（免登录）
- [ ] 错误密码提示“账号或密码错误”
- [ ] 错误验证码提示“验证码错误或已过期”

GUI 特有行为：

- [ ] 窗口缩放不白屏、不闪退
- [ ] 托盘图标存在，右键菜单可用（如未实现，记录现象即可）
- [ ] 重复双击不会开出第二个主窗口（单实例）
- [ ] 退出程序后 `eat-app.exe` 与 `eat-agent.exe` 都不残留
- [ ] 断网后再启动，能给出可理解的错误提示而不是静默空白

### 2.7 可选：生成安装包

需要 Inno Setup 6，并且已完成 2.3：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
```

产物：

```text
dist\EatAppSetup-x64.exe
```

安装后桌面出现 **Eat App** 快捷方式，默认安装到 `%LOCALAPPDATA%\EatApp`。

### 2.8 GUI 已知边界

- GUI 的 exe 尚未在真实 Windows 机器上由服务端同学验证过；首次构建若失败，属预期内的排查项，请把完整报错发回。
- 聊天功能需要 `DEEPSEEK_API_KEY`，未设置时其他页面仍可用。
- 单 exe 打包（把 `web\` 与 `eat-agent.exe` 嵌入 `eat-app.exe`）尚未实现，运行目录必须保持完整。
- 业务 TCP 是明文，仅限可信局域网，不要拿到公网演示。
- 菜单/档案/记录/统计等页面依赖服务端方法是否已帧化；当前登录、验证码、菜单、档案已验证，统计/好友/排行若报错请记录具体提示，不一定是客户端 bug。

---

## 3. 统一验收清单（回传结果用）

按下面格式回传，能快速定位问题：

```text
测试路径：Web / GUI
测试机：Windows 10/11，浏览器或 exe 版本
网络检查：9443=?, 8000=?, 8787=?
登录：成功 / 失败（附提示原文）
菜单：正常 / 空 / 报错（附提示）
档案保存：成功 / 失败（附提示）
免登录：生效 / 不生效
异常现象：截图 + 复现步骤
构建日志（GUI）：最后 30 行
```

注意：**不要把密码、token、session.json 内容发到群里或截图里**。

---

## 4. 失败排查

| 现象 | 可能原因 | 处理 |
|---|---|---|
| 网页打不开 | 8000 未监听、IP 错、AP 隔离 | `Test-NetConnection 10.112.151.15 -Port 8000`；确认同网段 |
| 登录一直失败、提示连接失败 | 9443 不通、服务端 IP 变了 | `Test-NetConnection ... -Port 9443`；在“服务器设置”改地址 |
| 验证码错误 | 验证码一次性，约 120 秒过期 | 点图片刷新后重输 |
| 账号已存在 | 用了“注册”标签但账号已注册 | 切回“登录”标签 |
| 密码错误 | 密码不对或不足 8 位 | 核对密码；注册时至少 8 位 |
| 页面能打开但没数据 | 旧会话指向旧 IP | 删除 `%LOCALAPPDATA%\EatApp\session.json` 后重登 |
| GUI 白屏 | `web\` 缺失或没装 WebView2 Runtime | 检查目录；安装 Evergreen Runtime |
| 提示找不到 `eat-agent.exe` | 只复制了 `eat-app.exe` | 使用完整目录或安装包 |
| 构建报 WebView2 not found | SDK 下载失败（网络/代理） | 重跑脚本，或 `-WebView2Root` 指定已有 SDK |
| 构建报 OpenSSL 缺失 | vcpkg 安装失败 | 重跑；或 `-VcpkgRoot` 指定已有 vcpkg |
| 构建报找不到 Python | 未装或未加 PATH | 安装 Python 3.11/3.12，勾选 Add to PATH |
| 构建报找不到 cmake | CMake 不在 PATH | 安装 CMake 3.20+ 并加入 PATH |
| 聊天报错 | 没有模型 Key | 设置 `DEEPSEEK_API_KEY` 后重启 |

---

## 5. 服务端同学侧核对命令

看端口：

```bash
ss -ltn | grep -E ':9443|:8787|:8000'
```

看某个账号是否真的落库（库名是测试库 `eat_route_a2`）：

```bash
docker exec eat-mysql-docker2 mysql -uroot -peat_test_root_pwd -e \
  "SELECT account_id,login_id,registered_at FROM eat_route_a2.account ORDER BY account_id DESC LIMIT 5;"
docker exec eat-mysql-docker2 mysql -uroot -peat_test_root_pwd -e \
  "SELECT agent_uid,weight,updated_at FROM eat_route_a2.health_profile ORDER BY updated_at DESC LIMIT 5;"
```

联调结束后清理临时账号（示例：`win01@test.local`）：

```sql
DELETE FROM eat_route_a2.session
 WHERE account_id IN (SELECT account_id FROM eat_route_a2.account WHERE login_id='win01@test.local');
DELETE FROM eat_route_a2.account WHERE login_id='win01@test.local';
DELETE FROM eat_route_a2.health_profile WHERE agent_uid='XYE13M7KNVCETAW6059FSDW5G0';
```

---

## 6. 当前服务端状态（2026-11-18）

| 项目 | 值 |
|---|---|
| 局域网地址 | `10.112.151.15` |
| TCP 业务端口 | `9443`（明文） |
| Agent HTTP | `8787` |
| Web 静态页 | `8000` |
| 测试库 | `eat_route_a2` |
| 已有测试账号 | `win01@test.local` / `<测试密码>` |
| 服务端启动方式 | Linux 侧 `bash .dev/start-lan.sh` |
