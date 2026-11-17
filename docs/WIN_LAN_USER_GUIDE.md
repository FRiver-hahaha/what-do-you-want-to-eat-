# Windows 用户连接指南（局域网）

> 适用：已经拿到 `eat-app-windows-x64.zip` 的 Windows 用户。
> 当前示例服务端地址：`10.225.197.15:9443`。
> 如果 Linux 服务端换了 WiFi 或 IP 发生变化，按本文第八节操作，不需要重新编译。

---

## 一、先确认网络

Windows 电脑必须和 Linux 服务端在**同一个 WiFi / 局域网**。

打开 PowerShell：

```powershell
Test-NetConnection 10.225.197.15 -Port 9443
```

必须看到：

```text
TcpTestSucceeded : True
```

如果是 `False`，先不要继续，依次检查：

- 两台机器是否连接同一个 WiFi
- Windows 是否连到了“访客网络”
- 路由器是否开启了 AP 隔离
- Linux 防火墙是否放行 `9443`

Linux 侧可以这样确认：

```bash
hostname -I
ss -ltn | grep ':9443'
sudo ufw allow 9443/tcp
```

---

## 二、清除上一次 WiFi 的旧连接记录

换网络后，程序里仍保存着旧 IP，必须清理一次。

1. 退出程序：右下角托盘图标右键 → **退出**。

2. 打开 PowerShell，执行：

```powershell
Remove-Item "$env:LOCALAPPDATA\EatApp\session.json" -Force -ErrorAction SilentlyContinue
Remove-Item "$env:LOCALAPPDATA\EatApp\login.json" -Force -ErrorAction SilentlyContinue
```

3. 如果程序目录里还有 `login.json`，也删除，或把其中的 `host` 改成新的服务端 IP。

---

## 三、启动程序

进入程序目录，双击：

```text
Start.bat
```

或者直接双击：

```text
eat-app.exe
```

启动后会显示“连接服务端”界面。

---

## 四、填写登录信息

| 项目 | 填写内容 |
|---|---|
| 服务器地址 | `10.225.197.15` |
| 端口 | `9443` |
| 账号 | 你的账号，例如 `win01@test.local` |
| 密码 | 你的密码，至少 8 位 |
| 首次使用，注册新账号 | 账号不存在时勾选；已存在时**不要勾** |

点击“连接”。

连接成功后：

- 页面自动进入
- 会话保存到 `%LOCALAPPDATA%\EatApp\session.json`
- 下次启动不再询问

---

## 五、聊天功能（可选）

菜单、餐记录、统计、好友、排行不需要模型 Key。

聊天需要 DeepSeek Key：

```powershell
setx DEEPSEEK_API_KEY "你的Key"
```

设置后关闭程序，再重新打开。

---

## 六、验证是否真的连上了

在 Windows 上：

1. 页面菜单应显示真实菜品，而不是本地示例数据。
2. 随便记录一条餐。
3. 在 Linux 服务端上查询：

```bash
mysql -uroot -p -e "use eat_route_a2; select id,user_key,date,text from meal_records order by id desc limit 5;"
redis-cli --scan --pattern 'eat:v1:*' | head
```

如果使用正式库，把 `eat_route_a2` 换成实际数据库名。

能看到刚才的记录和缓存键，说明页面 → Agent → TCP → 服务端链路已经生效。

---

## 七、常见问题

| 现象 | 处理 |
|---|---|
| 一直停在登录界面 | 先在 PowerShell 执行 `Test-NetConnection 10.225.197.15 -Port 9443` |
| 提示账号已存在 | 取消勾选“注册新账号”，直接登录 |
| 提示密码错误 | 确认账号密码；首次注册时密码至少 8 位 |
| 页面能打开但菜单没数据 | 旧 `session.json` 没清干净；按第二节重做 |
| 双击后白屏 | 检查程序目录下是否有 `web\index.html`；确认已安装 WebView2 Runtime |
| 提示找不到 eat-agent.exe | 程序目录必须包含 `eat-agent.exe`，不能只复制 `eat-app.exe` |
| 聊天报错 | 没有设置 `DEEPSEEK_API_KEY` |
| 登录一直失败 | 检查服务端是否监听 `0.0.0.0:9443`，以及 Windows 是否在同一网段 |

---

## 八、以后 IP 再变化怎么办

只要 Linux 服务端换了 WiFi，或路由器重新分配了 IP：

1. 在 Linux 上执行：

```bash
hostname -I
```

记下新的局域网 IP。

2. 在 Windows 上退出程序。
3. 删除旧会话：

```powershell
Remove-Item "$env:LOCALAPPDATA\EatApp\session.json" -Force -ErrorAction SilentlyContinue
```

4. 重新打开程序，在登录界面填写新的 IP。
5. 端口仍然是 `9443`。

不需要重新编译，也不需要改代码。

---

## 九、如果还没有程序目录

需要先在一台 Windows 构建机上生成运行目录：

```powershell
git clone -b feat/app-transport-scale https://github.com/FRiver-hahaha/what-do-you-want-to-eat-.git
cd what-do-you-want-to-eat-
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
```

然后把下面的压缩包发给用户：

```text
dist\eat-app-windows-x64.zip
```

完整构建说明见仓库根目录的 `AGENT_win.md`。
