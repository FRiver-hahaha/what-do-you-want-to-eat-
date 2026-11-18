# Windows 客户端测试交接说明

> 适用版本：`v0.3.0`（提交 `3e8f5ad`，分支 `feat/app-transport-scale`）
> 读者：负责在 Windows 电脑上测试 Eat App 客户端的队友，以及提供 Linux 服务端的一方。
> 结论先行：**只做测试不需要拉代码、不需要改代码**；只有想自行修改界面或客户端行为时才需要克隆全量仓库并重新打包。

---

## 1. 服务端侧准备（Linux，由服务端同学完成）

```bash
git fetch --tags
git checkout v0.3.0
```

Docker 方式（推荐，启动时自动执行迁移）：

```bash
export EAT_MYSQL_ROOT_PASSWORD='换成一个强密码'
docker compose -f deploy/docker/docker-compose.yml up -d --build
docker compose -f deploy/docker/docker-compose.yml logs -f eat-server
sudo ss -ltnp | grep 9443        # 必须是 0.0.0.0:9443
sudo ufw allow 9443/tcp          # 使用 ufw 时
```

裸机方式见 `docs/WINDOWS_CLIENT_LAN_RUNBOOK.md` 第 5.2 节。

提供给 Windows 测试方的信息只有三项：

| 项目 | 示例 |
|---|---|
| 服务器地址 | Linux 局域网 IP，如 `192.168.1.50` |
| 端口 | `9443` |
| 测试账号 | 如 `win01@test.local`（首次勾选“注册新账号”，之后取消勾选） |

当前服务端未接线 Redis，不需要启动 Redis；Redis 只允许服务端访问，客户端不得直连。

---

## 2. 测试路径 A：安装包（推荐，零代码）

拿到 `EatAppSetup-x64.exe`（由 Windows 构建方生成，约 14MB）。该文件在 `.gitignore` 中，不会随 Git 仓库分发，需要用安装包文件、U 盘或内网共享传递；如需公网分发，请在 GitHub Releases 的 `v0.3.0` 手动上传。

1. 双击 `EatAppSetup-x64.exe`，按提示安装（无需管理员权限）。
2. 双击桌面 **Eat App** 快捷方式。
3. 在“连接服务端”表单填写：

| 字段 | 填写内容 |
|---|---|
| 服务器地址 | Linux 局域网 IP |
| 端口 | `9443` |
| 账号 | 测试账号 |
| 密码 | 至少 8 位 |
| 验证码 | 输入右侧图片中的 4 位字符，看不清点击刷新 |
| 注册新账号 | 首次使用勾选，账号已存在时不要勾 |

4. 点击“连接”。成功后进入主界面，会话保存到 `%LOCALAPPDATA%\EatApp\session.json`，下次启动不再询问。
5. 聊天功能需要模型 Key，其余页面不需要：

```powershell
setx DEEPSEEK_API_KEY "你的Key"
```

设置后需重新打开 Eat App。

---

## 3. 测试路径 B：免安装目录

把 `dist/eat-app-windows-x64/` 整个目录拷到目标机，例如 `C:\EatClient\`，双击其中的 `eat-app.exe` 即可。

目录必须保持完整：

```text
eat-app-windows-x64\
  eat-app.exe
  eat-agent.exe
  WebView2Loader.dll
  web\                 # 页面资源，缺失会白屏
```

不要只复制 `eat-app.exe`，否则会提示找不到 `eat-agent.exe`。

---

## 4. 测试路径 C：控制台协议客户端

仅用于验证 TCP 帧与握手链路，不涉及页面。可从 GitHub Actions 的 `client_smoke-windows-x64` artifact 下载，或在 Windows 构建机执行 `scripts\build-windows.ps1` 生成。

```powershell
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login win01@test.local --password win-pass-123 `
  --register --patch-weight 66
```

预期输出：

```text
handshake_ok user_key=<用户键> token=issued
profile_ok name=我 source=service
patch_ok
verify_weight=66
```

四项全部出现，说明 TCP 帧、握手鉴权、MySQL 读写、幂等键均已打通。

---

## 5. 是否需要拉取全部代码、是否需要自己修改

| 目标 | 是否需要拉代码 | 是否需要改代码 |
|---|---|---|
| 只测试现有客户端 | 否，用安装包或免安装目录 | 否 |
| 只验证协议链路 | 否，用 `client_smoke.exe` | 否 |
| 修改界面 / 交互 / 客户端行为 | 是，需要全量克隆 | 是，改完必须重新打包 |

需要改代码时：

```powershell
git clone -b feat/app-transport-scale https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
```

前置环境：Visual Studio 2022（C++ 桌面开发）、CMake 3.20+、vcpkg 安装 `openssl:x64-windows`、WebView2 SDK、Python 3.11/3.12，生成安装包还需 Inno Setup 6。

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
```

产物：

```text
dist\eat-app-windows-x64\
dist\EatAppSetup-x64.exe
```

同步最新代码：

```powershell
git fetch origin
git pull --ff-only origin feat/app-transport-scale
```

---

## 6. 允许修改的范围

| 可以改 | 不要改 |
|---|---|
| 根目录 `index.html`、`app.js`、`bridge.js`、`agent-adapter.js`、`*.css`、`assets/` | `shared/protocol/`（40 字节定长头 + CRC32C 帧格式） |
| `client/app/win32/`（窗口壳、托盘、登录流程） | `server/` 的协议语义与服务端业务方法 |
| `agent/`（Agent 逻辑、`agent/ipc_server.py` 路由） | 客户端直连 MySQL / Redis 的任何改动 |

关键注意点：

- 构建产物中的 `web/` 是**拷贝**，不是软链接。改完根目录页面后必须重新执行打包脚本，否则运行的是旧页面。
- 客户端不得新增对 MySQL / Redis 的直连；客户端到服务端只走 TCP 帧。
- 不要提交 `DEEPSEEK_API_KEY`、`login.json`、明文密码或任何令牌；`login.json` 已在 `.gitignore` 中。
- 测试密码不要写入脚本、日志、截图或聊天记录。

---

## 7. 验收清单

按顺序执行并记录结果：

- [ ] Linux：`ss -ltn | grep 9443` 显示 `0.0.0.0:9443`
- [ ] Linux：防火墙已放行 9443
- [ ] Windows：`Test-NetConnection <服务端IP> -Port 9443` 为 `TcpTestSucceeded : True`
- [ ] Windows：首次注册并登录成功，可进入主界面
- [ ] Windows：在档案页修改体重并保存
- [ ] Linux：能查到刚落库的档案

```bash
mysql -uroot -p -e "use eat; select agent_uid,weight,updated_at from health_profile order by updated_at desc limit 5;"
```

- [ ] Windows：重新读取档案，体重仍为修改后的值
- [ ] Windows：去掉 `--register` 再次登录成功
- [ ] Windows：`--omit-idem` 返回 `patch_error code=3001`
- [ ] Windows：`--skip-handshake` 返回 `pre_handshake_error code=2001`
- [ ] Windows：错误密码返回 `handshake_error code=2002`
- [ ] 服务端日志能看到对应握手/请求记录，且不含明文密码或令牌

---

## 8. 已知边界（这些失败属预期，不是 bug）

| 项目 | 现状 |
|---|---|
| 注册 / 登录 / 验证码 / 握手 | 可用 |
| `profile.get` / `profile.patch` | 可用 |
| 菜单、餐记录、统计、好友、排行 | 服务端尚未帧化这些方法，页面调用会报错 |
| Redis 缓存 | 服务端未接线 |
| 100 万在线 | 无压测数据，不作承诺 |
| TCP 传输 | 明文，仅限可信局域网 |
| GUI 托盘、单实例、DPAPI | 部分未在真实 Windows 环境完成验证 |

因此验收范围应聚焦“登录 + 档案 + 协议负向用例”，不要用页面中的菜单/记录/统计/好友/排行来判定客户端故障。

---

## 9. 失败排查

| 现象 | 原因 | 处理 |
|---|---|---|
| 一直停在登录界面 / `connect_failed` | 服务端未监听、防火墙、IP 错、不同网段 | `Test-NetConnection <IP> -Port 9443`；Linux 侧查 `ss -ltnp`、`ufw status` |
| 验证码错误 | 验证码一次性、约 120 秒过期 | 点击图片刷新后重新输入 |
| 账号已存在 | 重复勾选了注册 | 取消勾选“注册新账号” |
| 密码错误 | 密码不正确或不足 8 位 | 确认密码；首次注册至少 8 位 |
| 页面能打开但没数据 | 旧会话指向了旧 IP | 删除 `%LOCALAPPDATA%\EatApp\session.json` 后重新登录 |
| 白屏 | 缺少 `web\index.html` 或 WebView2 Runtime | 检查安装目录；安装 WebView2 Evergreen Runtime |
| 提示找不到 `eat-agent.exe` | 只复制了 `eat-app.exe` | 使用安装包或完整目录 |
| 聊天报错 | 未设置模型 Key | 设置 `DEEPSEEK_API_KEY` 后重启 |

更换 WiFi 或服务端 IP 变化时：

```powershell
Remove-Item "$env:LOCALAPPDATA\EatApp\session.json" -Force -ErrorAction SilentlyContinue
```

然后重新双击快捷方式，在登录界面填写新地址，不需要重新安装。

---

## 10. 相关文档

- `docs/WIN_LAN_USER_GUIDE.md`：面向普通用户的安装与登录说明
- `docs/WINDOWS_CLIENT_LAN_RUNBOOK.md`：局域网联调、服务端启动与失败排查
- `AGENT_win.md`：Windows 构建、Agent 打包与登录方式
- `packaging/README-client.md`：客户端包结构与已知限制
