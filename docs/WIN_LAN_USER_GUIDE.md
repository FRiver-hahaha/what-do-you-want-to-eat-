# Windows 用户安装与登录指南

> 适用：拿到 `EatAppSetup-x64.exe` 的 Windows 用户。
> 当前示例服务端地址：`10.101.17.32:9443`。
> 更换 WiFi 后如果服务端 IP 变了，按本文第七节操作。

---

## 一、安装

1. 双击 `EatAppSetup-x64.exe`。
2. 按提示完成安装（安装到当前用户目录，不需要管理员权限）。
3. 安装完成后，桌面会出现 **Eat App** 快捷方式，开始菜单也有入口。

以后只需要双击桌面的 **Eat App**。

---

## 二、首次登录

双击桌面快捷方式后，会显示“连接服务端”：

| 项目 | 填写内容 |
|---|---|
| 服务器地址 | `10.101.17.32`（已预填，可修改） |
| 端口 | `9443` |
| 账号 | 你的账号，例如 `win01@test.local` |
| 密码 | 你的密码，至少 8 位 |
| 验证码 | 输入右侧图片里的 4 位字符 |
| 首次使用，注册新账号 | 账号不存在时勾选；已存在时不要勾 |

验证码看不清时，点击验证码图片刷新。

点击“连接”：

- 成功后自动进入主界面，下次启动不再询问；
- 会话保存到 `%LOCALAPPDATA%\EatApp\session.json`；
- 数据由 Agent 通过 TCP 访问 Linux 服务端。

---

## 三、确认网络

如果连接失败，先在 PowerShell 执行：

```powershell
Test-NetConnection 10.101.17.32 -Port 9443
```

必须看到：

```text
TcpTestSucceeded : True
```

否则检查：

- 是否和服务端连接同一个 WiFi
- 是否连到了访客网络或手机热点
- 路由器是否开启 AP 隔离
- Linux 侧是否监听 `0.0.0.0:9443`

---

## 四、聊天功能（可选）

菜单、餐记录、统计、好友、排行不需要模型 Key。

聊天需要 DeepSeek Key：

```powershell
setx DEEPSEEK_API_KEY "你的Key"
```

设置后重新打开 **Eat App**。

---

## 五、验证真实数据

1. 在页面里记录一条餐。
2. 在 Linux 服务端查询：

```bash
mysql -uroot -p -e "use eat_route_a2; select id,user_key,date,text from meal_records order by id desc limit 5;"
redis-cli --scan --pattern 'eat:v1:*' | head
```

能看到你的记录和缓存键，说明链路已生效。

---

## 六、常见问题

| 现象 | 处理 |
|---|---|
| 一直停在登录界面 | 先执行第三节的 `Test-NetConnection` |
| 验证码错误 | 点击验证码图片刷新，重新输入 4 位字符 |
| 账号已存在 | 取消勾选“注册新账号”，直接登录 |
| 密码错误 | 确认密码；首次注册密码至少 8 位 |
| 页面能打开但没数据 | 旧会话指向了旧 IP；按第七节清理 |
| 白屏 | 检查安装目录下是否有 `web\index.html`；安装 WebView2 Runtime |
| 提示找不到 `eat-agent.exe` | 不要只复制 `eat-app.exe`，用安装包重新安装 |
| 聊天报错 | 没有设置 `DEEPSEEK_API_KEY` |

---

## 七、更换 WiFi / 服务端 IP 变化

1. 托盘图标右键退出 **Eat App**。
2. PowerShell 执行：

```powershell
Remove-Item "$env:LOCALAPPDATA\EatApp\session.json" -Force -ErrorAction SilentlyContinue
```

3. 重新双击桌面快捷方式。
4. 在登录界面填写新的服务器地址，端口仍为 `9443`，重新输入账号、密码和验证码。

不需要重新安装，也不需要改配置文件。

---

## 八、如果还没有安装包

需要在 Windows 构建机上生成：

```powershell
git clone -b feat/app-transport-scale https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
```

产物：

```text
dist\EatAppSetup-x64.exe
```

完整构建说明见仓库根目录 `AGENT_win.md`。
