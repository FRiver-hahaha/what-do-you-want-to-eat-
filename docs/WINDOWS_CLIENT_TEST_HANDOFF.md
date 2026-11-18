# Windows 客户端测试交接说明（Web + GUI + 二次开发）

> 服务端：Linux `10.101.17.32`，TCP 端口 `9443`（明文，仅限可信局域网）
> 代码分支：`feat/app-transport-scale`
> GUI 安装包 Release：`v0.3.0`（对应提交 `3e8f5ad`，当前分支只比它多一个文档提交）
> 本文读者：负责 Windows 侧测试与前端改造的队友。

服务端由 Linux 侧保持运行，不需要队友启动数据库、Redis 或服务端进程。
队友只需要能访问 `10.101.17.32` 的 `9443`（GUI/Web 业务）和 `8000`（网页）端口。

---

## 0. 先做网络自检

在 Windows PowerShell 执行：

```powershell
Test-NetConnection 10.101.17.32 -Port 9443
Test-NetConnection 10.101.17.32 -Port 8000
```

预期都是 `TcpTestSucceeded : True`。

- 9443 不通：不同 WiFi/网段、AP 隔离或防火墙。先换到与服务端相同的网络；仍不通就联系服务端同学。
- 8000 不通但 9443 通：只影响网页测试，不影响 GUI。
- 服务端机器重启后不会自动恢复，连不上时先找服务端同学确认。

---

## 1. 三种测试路径怎么选

| 路径 | 需要安装 | 能测什么 | 适合谁 |
|---|---|---|---|
| A. Web | 浏览器 | 登录、菜单、档案、统计、好友、排行 | 快速验收，5 分钟 |
| B. Release 安装包 | 下载 exe | 与 A 相同的业务，外加安装、托盘、会话持久化 | 测 GUI 最终形态 |
| C. 源码构建 | VS2022 等工具链 | 在 B 基础上可改前端 UI、改客户端行为 | 要改 UI / 改代码的人 |

B 和 C 的业务能力相同；区别只是 C 能改代码并重新编译。

> **重要：前端修改完全在 Windows 侧闭环。**
> 队友可以无限次修改页面、重新构建、重启客户端，Linux 服务端**不需要改任何参数、不需要重启、不需要重新部署**。
> 服务端只认既有的 TCP 方法；只要客户端仍然调用这些方法，改 UI 文案、布局、样式、前端交互都与服务端无关。
> 唯一需要服务端配合的，是新增/修改协议方法、改帧格式或新增数据库字段——纯前端改造不涉及这些。

---

## 2. 路径 A：Web 测试（零安装）

### 2.1 打开

浏览器访问：

```text
http://10.101.17.32:8000
```

不要用 `file://` 双击 HTML，必须走 HTTP。

### 2.2 首次：注册自己的账号（推荐）

1. 页面在“登录”标签，先点“注册”标签。
2. 展开“服务器设置”，确认地址 `10.101.17.32`、端口 `9443`（默认已填好，IP 变了就手动改）。
3. 账号填一个自己的，例如 `win02@test.local`。
4. 密码至少 8 位。
5. 输入右侧验证码，看不清点图片刷新。
6. 点“进入饭饭”。

> 已有联调账号 `win01@test.local`，密码向服务端同学单独索取；不要把它写进脚本、日志或截图。
> 多人测试请各自注册 `win02@...`、`win03@...`，共用账号会互相覆盖档案数据。

### 2.3 之后：直接登录

切回“登录”标签，填账号、密码、验证码即可。地址和账号会记在浏览器本地，下次自动带出。

### 2.4 Web 冒烟清单

- [ ] 页面正常打开，无白屏、无样式错乱
- [ ] 验证码图片显示，点击可刷新
- [ ] 注册新账号成功并进入主界面
- [ ] 退出后重新登录成功
- [ ] 菜单页显示真实菜品
- [ ] 档案页修改体重并保存，刷新后仍是新值
- [ ] 统计页有周报数据（新账号允许为空）
- [ ] 好友/排行页能打开（新账号列表为空属正常）
- [ ] 故意输错密码，提示“账号或密码错误”
- [ ] 故意输错验证码，提示“验证码错误或已过期”

### 2.5 Web 路径限制

- 浏览器连的是 **Linux 侧的共享 Agent（8787）**，它是单会话的：一个人登录会把另一个人的 `user_key` 顶掉。**同一时间只安排一个人做 Web 测试**；多人并行请走 GUI。
- 页面里点击菜品“记录”只写浏览器 localStorage，不会写服务端（详见第 6 节能力矩阵）。
- 聊天功能需要 `DEEPSEEK_API_KEY`，服务端未配置，属预期。

---

## 3. 路径 B：Release 安装包（GUI，零构建）

### 3.1 下载

```text
https://github.com/FRiver-hahaha/what-do-you-want-to-eat-/releases/download/v0.3.0/EatAppSetup-x64.exe
```

Release 页面（可看说明）：

```text
https://github.com/FRiver-hahaha/what-do-you-want-to-eat-/releases/tag/v0.3.0
```

安装包约 14 MB，无需管理员权限。解压/下载后若被 SmartScreen 拦截，选择“更多信息 → 仍要运行”。

### 3.2 安装与启动

1. 双击 `EatAppSetup-x64.exe`，按提示安装到 `%LOCALAPPDATA%\EatApp`。
2. 双击桌面 **泛饭** 快捷方式。
3. 首次启动显示“连接服务端”页面。

依赖：Windows 11 自带 WebView2 Runtime；Windows 10 如白屏，安装微软的 WebView2 Evergreen Runtime 后重试。

### 3.3 登录

与 Web 完全一致：

- “登录 / 注册”标签；
- “服务器设置”里地址 `10.101.17.32`、端口 `9443`；
- 首次先注册自己的账号，之后用“登录”；
- 输入图形验证码，点“进入饭饭”。

会话保存在：

```text
%LOCALAPPDATA%\EatApp\session.json
```

下次启动直接进主界面。

### 3.4 GUI 冒烟清单

业务项与 2.4 相同，另外补充：

- [ ] 安装完成，桌面快捷方式可启动
- [ ] 首次启动窗口正常，无白屏
- [ ] 登录后关闭程序，再次启动免登录
- [ ] 窗口缩放、最小化、恢复不白屏不闪退
- [ ] 托盘图标存在且菜单可用（未实现则记录现象）
- [ ] 重复双击不会开出第二个主窗口
- [ ] 退出后任务管理器里没有残留的 `eat-app.exe` / `eat-agent.exe`

### 3.5 服务端 IP 变化时

不用重装。删除会话文件后重新打开，在“服务器设置”里填新地址：

```powershell
Remove-Item "$env:LOCALAPPDATA\EatApp\session.json" -Force -ErrorAction SilentlyContinue
```

---

## 4. 路径 C：从源码构建 GUI

### 4.0 服务端零改动：可以无限次改、无限次构建

这一节先回答一个关键问题：**队友在自己电脑上改前端、重新编译、反复启动，服务端需不需要动？答案是不需要。**

服务端只通过 `10.101.17.32:9443` 暴露既有的 TCP 方法（验证码、注册登录、档案、菜单、餐记录、统计、好友、排行）。客户端界面怎么改，只要仍然调用这些方法，服务端就不需要任何调整。

**队友不需要、也不会改到的服务端内容：**

| 服务端对象 | 是否需要改 |
|---|---|
| `.dev/route-a.conf`（地址、端口、数据库、Redis） | 不需要 |
| `eat-server` 进程与 `9443` 端口 | 不需要，也不用重启 |
| MySQL / Redis | 不需要，也不用改数据 |
| Web 侧共享的 Agent（`8787`） | 不需要 |
| 数据库表结构、账号白名单、服务端开关 | 不需要 |
| 服务端代码的编译与部署 | 不需要 |

**队友每次构建只影响自己这台 Windows：**

- `dist\eat-app-windows-x64\` 整个目录；
- 自己机器上的 WebView2 缓存；
- 自己注册的测试账号数据（写进服务端测试库，属于正常业务数据）。

**可以无限次循环：**

```text
改 index.html / app.js / *.css / assets
        ↓
构建（或只覆盖 web\ 快速预览）
        ↓
启动（或重启）泛饭
        ↓
看效果，继续改
```

次数不限。第一次构建会下载 vcpkg、OpenSSL、WebView2 SDK，之后是增量编译，速度会明显加快。

**什么情况才需要找服务端同学：**

- 新增一个协议方法，或修改现有方法的参数/返回结构；
- 修改 `shared/protocol/` 的帧格式；
- 需要新的数据库字段或新的业务能力；
- 服务端本身连不上（先按第 0 节自检）。

只改 UI 的文案、配色、布局、图标、交互流程，**不需要服务端做任何事**。

**账号也不需要服务端配合**：队友在“注册”标签自己创建账号即可，不需要服务端预先建号、不需要加白名单、不需要改配置。

只有需要改 UI、改客户端行为，或想自己产出安装包时才走这条。

### 4.1 环境要求

| 依赖 | 要求 |
|---|---|
| 系统 | Windows 10/11 x64 |
| Visual Studio 2022 | 勾选“使用 C++ 的桌面开发” |
| CMake | 3.20+，加入 PATH |
| Git | 任意较新版本 |
| Python | 3.11 或 3.12，安装时勾选 Add to PATH |
| 网络 | 能访问 github.com、nuget.org（脚本要下 vcpkg 和 WebView2 SDK） |
| Inno Setup 6 | 仅打安装包时需要 |
| 磁盘 | 预留约 5 GB |

### 4.2 获取源码

方式一：

```powershell
git clone -b feat/app-transport-scale https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
```

方式二（无 Git 权限时，从 Linux 侧下载源码包）：

```text
http://10.101.17.32:8000/dl/eat-client-src.zip
```

解压后进入 `eat-client-src` 目录。注意：构建脚本自身仍需要 Git，因为 vcpkg 要用它。

### 4.3 一键构建

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
```

脚本会依次：

1. 检查 CMake / Git / VS2022 MSVC / Python；
2. 准备 `C:\vcpkg`，安装 `openssl:x64-windows`；
3. 下载 WebView2 SDK 到 `third_party\webview2`；
4. CMake 配置 `build-win` 并编译 `eat-app.exe`；
5. PyInstaller 打包 `eat-agent.exe`；
6. 组装 `dist\eat-app-windows-x64\`；
7. 生成 `dist\eat-app-windows-x64.zip`。

可用参数：

```powershell
# vcpkg 已有且不在默认位置
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1 -VcpkgRoot D:\vcpkg

# 已有 WebView2 SDK
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1 -WebView2Root C:\sdk\webview2

# 只编 GUI，不打包 Agent（缺 eat-agent.exe 时程序无法启动，不推荐）
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1 -SkipAgent
```

### 4.4 运行

```powershell
dist\eat-app-windows-x64\Start.bat
```

目录必须完整：

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

### 4.5 打安装包（可选）

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
```

产物：`dist\EatAppSetup-x64.exe`。

---

## 5. 修改前端 UI 并重新编译启动（重点）

### 5.1 可以改的文件

页面资源都在仓库根目录，Web 与 GUI 共用同一套：

| 文件 | 作用 |
|---|---|
| `index.html` | 页面结构 |
| `app.js` | 页面交互与业务逻辑 |
| `bridge.js` | Web / WebView2 双端传输适配 |
| `agent-adapter.js` | Agent 请求适配 |
| `styles.css` / `features.css` / `agent.css` / `login.css` / `redesign.css` | 样式 |
| `assets/` | 图片与图标 |
| `agent/` | Python Agent 逻辑（需要重新打包 eat-agent.exe） |
| `client/app/win32/main.cc` | Win32 窗口壳、登录桥接（需要重新编译） |

**不要改**：

- `shared/protocol/`：40 字节定长头 + CRC32C 帧格式；
- `server/`：服务端业务语义；
- 任何“客户端直连 MySQL / Redis”的改动。

### 5.2 开发循环：快速预览（不重新编译）

页面在 GUI 里是从 exe 同目录的 `web\` 加载的（`file:///<安装目录>/web/index.html`），所以改完可以直接替换运行目录中的文件：

1. 修改仓库根目录的 `index.html`、`*.css`、`*.js`；
2. 把改过的文件覆盖到：
   - 源码构建版：`dist\eat-app-windows-x64\web\`
   - 安装版：`%LOCALAPPDATA%\EatApp\web\`
3. 完全退出 泛饭 再启动（不是最小化）；
4. 看到改动即成功。

说明：

- `web\` 目录里必须同时包含 `index.html`、`app.js`、`bridge.js`、`agent-adapter.js`、5 个 CSS 和 `assets\`；
- GUI 的 WebView2 用户数据在 exe 同目录的 `webview-data\`，若页面仍是旧版，退出程序后删除该目录再启动；
- 快速预览只用于看效果，最终交付仍要重新构建，否则别人安装到的还是旧页面；
- 这套“改文件 → 覆盖 `web\` → 重启”的动作可以无限次重复，**每次都不涉及服务端**，也不需要重启 `eat-server` 或改动任何服务端配置。

### 5.3 正式重建（页面改动）

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
dist\eat-app-windows-x64\Start.bat
```

脚本会把根目录最新页面复制到 `dist\eat-app-windows-x64\web\`。这个脚本可以反复执行，每次都会覆盖上一次的产物；**服务端不需要做任何改动或重启**。CMake 的复制清单固定为：

```text
index.html, app.js, bridge.js, agent-adapter.js,
styles.css, features.css, agent.css, login.css, redesign.css, assets/
```

如果你新增了新的 CSS/JS 文件，**必须同时修改 `client/app/CMakeLists.txt` 的复制列表**，否则 GUI 里加载不到。

### 5.4 正式重建（改了 C++ 壳或 Agent）

- 只改 `client/app/win32/`：重跑 `build-windows-oneclick.ps1`；
- 改了 `agent/`：同样重跑脚本，它会重新生成 `eat-agent.exe`；
- 只改页面：脚本仍会重新编译，但速度可接受；也可以用 5.2 的快速预览先验证。

### 5.5 安装版如何看到新改动

两条路：

1. 重新构建安装包并重装：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
# 双击新的 dist\EatAppSetup-x64.exe 覆盖安装
```

2. 不重装，直接把新 `web\` 覆盖到 `%LOCALAPPDATA%\EatApp\web\`，然后重启程序。这适合自己验证，不适合分发给别人。

### 5.6 改 UI 后的自检

- [ ] 页面有可见变化（标题、颜色、文案任选一处明显标记）
- [ ] 浏览器打开 `http://10.101.17.32:8000` 看到同样变化（说明根目录文件已改对）
- [ ] GUI 重启后看到同样变化（说明 `web\` 已更新）
- [ ] 登录、菜单、档案保存仍正常（改 UI 不能破坏业务）
- [ ] 重新执行构建脚本后，`dist\eat-app-windows-x64\web\` 内是修改后的文件

> 推送代码前先提交到自己的分支，不要直接改 `feat/app-transport-scale` 主分支；需要合并时提 PR。

---

## 6. 业务能力矩阵（2026-11-18 实测）

服务端已注册的方法：登录/注册/验证码、档案读写、餐记录增删改查、统计周报与营养缺口、菜单与搜索、好友增删拉黑、好友排行。

| 业务 | 页面/客户端入口 | 服务端 | 实测状态 |
|---|---|---|---|
| 验证码 | 登录/注册页 | `auth.captcha` | 可用 |
| 注册、登录 | 登录/注册页 | `auth.register/login` | 可用，已实测落库 |
| 档案读取 | 档案页 | `profile.get` | 可用，`source=service` |
| 档案保存 | 档案页保存 | `profile.patch` | 可用，体重修改已实测落库 |
| 菜单 | 菜单页 | `food.menu` | 可用，返回真实菜品 |
| 餐记录读取 | 记录页 | `meal.recent` | 可用 |
| 餐记录写入 | Agent 工具 `record_meal` | `meal.create` | 可用，已实测落库 |
| 餐记录写入 | 页面点选菜品“记录” | — | **仅写浏览器 localStorage，不落服务端** |
| 餐记录修改/删除 | Agent 工具 `update_meal`/`delete_meal` | `meal.patch/delete` 已实现 | **Agent 工具尚未接线服务端**，调用返回 `persisted=false` |
| 统计周报 | 报告页 | `stats.weekly` | 可用 |
| 营养缺口 | 报告页 | `stats.nutrition_gap` | 可用 |
| 好友列表 | 好友页 | `friend.list` | 可用（新账号为空） |
| 加好友/接受/删除/拉黑 | 好友页操作 | `friend.*` | 服务端已实现，页面入口按实际界面操作 |
| 好友排行 | 排行页 | `ranking.friends` | 可用（新账号为空） |
| 聊天 | 聊天页 | 需要模型 Key | 未配置 Key 时不可用，属预期 |
| 全量同步 | — | `sync.pull` | 未实现 |

**结论**：队友可以完整跑通“注册 → 登录 → 档案读写 → 菜单 → 餐记录写入 → 统计 → 好友/排行”的服务端链路；但“页面点选记录”和“页面删除/修改记录”目前不写服务端，不要用它们判断服务端故障。

---

## 7. 回传格式

```text
测试路径：Web / 安装包 / 源码构建
测试机：Windows 10/11，WebView2 是否已装
网络：9443=?, 8000=?
登录：成功 / 失败（附提示原文）
菜单：正常 / 空 / 报错（附提示）
档案保存：成功 / 失败（附提示）
餐记录写入：成功 / 未测
免登录：生效 / 不生效
UI 改动：改了什么文件、快速预览是否生效、重新构建后是否生效
异常：截图 + 复现步骤
构建日志（源码路径）：最后 30 行
```

安全要求：

- 不要把密码、token、`session.json` 内容发到群里或截图里；
- 不要新增客户端直连 MySQL / Redis；
- TCP 是明文，仅限可信局域网，不要做公网端口映射。

---

## 8. 失败排查

| 现象 | 原因 | 处理 |
|---|---|---|
| 网页打不开 | 8000 未监听、IP 错、AP 隔离 | `Test-NetConnection 10.101.17.32 -Port 8000`，确认同网段 |
| 登录提示连接失败 | 9443 不通、服务端 IP 变了 | `Test-NetConnection ... -Port 9443`，改“服务器设置”里的地址 |
| 验证码错误 | 一次性，约 120 秒过期 | 点图片刷新后重输 |
| 账号已存在 | 用了“注册”标签但账号已注册 | 切回“登录”标签 |
| 密码错误 | 密码不对或不足 8 位 | 核对密码；注册至少 8 位 |
| 页面能打开但没数据 | 旧会话指向旧 IP | 删 `%LOCALAPPDATA%\EatApp\session.json` 后重登 |
| GUI 白屏 | `web\` 缺失或 WebView2 Runtime 未装 | 检查安装目录；安装 Evergreen Runtime |
| 提示找不到 `eat-agent.exe` | 只复制了 `eat-app.exe` | 使用完整目录或安装包 |
| 改了 UI 但没变化 | 改的是仓库根目录，运行的是 `web\` 副本 | 按 5.2 覆盖 `web\`，或重新构建 |
| 新增 CSS/JS 在 GUI 里 404 | `CMakeLists.txt` 复制清单没加 | 把新文件加入复制列表后重建 |
| 构建报 WebView2 not found | SDK 下载失败 | 检查代理；或 `-WebView2Root` 指定已有 SDK |
| 构建报 OpenSSL 缺失 | vcpkg 安装失败 | 重跑；或 `-VcpkgRoot` 指定已有 vcpkg |
| 构建报找不到 Python | 未安装或未加 PATH | 装 Python 3.11/3.12，勾选 Add to PATH |
| 构建报找不到 cmake | CMake 不在 PATH | 安装 CMake 3.20+ 并加入 PATH |
| 聊天报错 | 没有模型 Key | 设置 `DEEPSEEK_API_KEY` 后重启 |
| 服务端连不上且自检失败 | 服务端未运行或网络隔离 | 联系服务端同学，不要自己动数据库 |

---

## 9. 服务端同学侧核对（不含任何密码）

确认监听：

```bash
ss -ltn | grep -E ':9443|:8787|:8000'
```

确认账号是否落库（密码从本机 `.dev` 配置读取，不要写进仓库）：

```bash
docker exec eat-mysql-docker2 mysql -uroot -p"$EAT_MYSQL_ROOT_PASSWORD" -e \
  "SELECT account_id,login_id,registered_at FROM eat_route_a2.account ORDER BY account_id DESC LIMIT 5;"
```

确认档案写入：

```bash
docker exec eat-mysql-docker2 mysql -uroot -p"$EAT_MYSQL_ROOT_PASSWORD" -e \
  "SELECT agent_uid,weight,updated_at FROM eat_route_a2.health_profile ORDER BY updated_at DESC LIMIT 5;"
```

测试结束清理临时账号（把 login_id 与 agent_uid 换成实际值）：

```sql
DELETE FROM eat_route_a2.session
 WHERE account_id IN (SELECT account_id FROM eat_route_a2.account WHERE login_id='win02@test.local');
DELETE FROM eat_route_a2.account WHERE login_id='win02@test.local';
DELETE FROM eat_route_a2.health_profile WHERE agent_uid='<该账号的 user_key>';
```

---

## 10. 版本与产物对照

| 项 | 值 |
|---|---|
| 安装包 | `EatAppSetup-x64.exe` @ Release `v0.3.0`（约 14.3 MB） |
| Release 对应提交 | `3e8f5ad` |
| 当前分支 HEAD | `8d2cdac`，仅比 Release 多本文档提交 |
| 源码构建产物 | `dist\eat-app-windows-x64\` 与 `dist\eat-app-windows-x64.zip` |
| Web 静态页 | `http://10.101.17.32:8000` |
| 服务端 TCP | `10.101.17.32:9443` |
| 测试库 | `eat_route_a2`（临时数据，不是正式环境） |
