# Windows App 方案：把 ① 浏览器 + Python Agent 变成桌面应用

## 1. 目标与现状

- 目标：Windows 10 1809+ / 11 x64 安装包；页面资源随包；托盘常驻；单实例；断网可启动到可操作界面；只与服务端保持一条业务 TCP。
- 现状：`index.html`/`app.js`/`agent-adapter.js` 通过 `fetch` 访问 `127.0.0.1:8787`；Agent 是 `ThreadingHTTPServer`；`start-all.bat` 同时拉起 `:8787` 与 `:4173`。

## 2. 选型 ADR

| 方案 | 结论 | 理由 |
|---|---|---|
| **Win32 + WebView2（选定）** | ✅ | 页面 100% 复用；Win10 1809+ 支持；无额外运行时栈；壳体积小；可本地化资源 |
| WPF/WinUI3 | ❌ | 需要重写全部 UI，功能对等成本最高；引入 .NET 栈 |
| Electron | ❌ | 体积/内存大，引入 Node 栈，与 C++ 主线不符 |
| Tauri | ❌ | 引入 Rust 栈 |
| 内嵌浏览器指向远程页 | ❌ | 规格明确禁止远程首屏 |

**WebView2 交付方式**：优先使用 Evergreen Runtime；无 Runtime 的机器在安装包内附 Fixed Version。两者都要求页面资源在安装目录内，`file://` 或自定义 `app://` 协议加载，禁止远程 URL。

## 3. 进程与线程拓扑

```
EatApp.exe (Win32 壳, 主线程)
  ├─ WebView2 (渲染进程, 页面资源来自 <install>\web\)
  ├─ WebMessage 桥 (postMessage ⇄ native)
  ├─ TcpTransport (1 条业务 TCP, 帧循环独立线程)
  ├─ 命名管道服务端 \\.\pipe\EatAppAgent-{sidHash}-{instance}
  └─ 子进程 eat-agent.exe (Python Agent; 无控制台; 崩溃重启 ≤3 次)
        └─ LLM 直连 HTTPS/SSE（不承载业务数据）
```

- 页面 → 壳：`window.chrome.webview.postMessage({id, op, payload})`；壳 → 页面：`PostWebMessageAsJson`。
- 页面 → Agent 工具链：页面不直连 Agent；Agent 需要业务数据时经 IPC 请求壳，壳转成业务帧。
- 模型调用只在 `eat-agent.exe` 内；失败不触发 TCP 重连、不影响心跳。

## 4. 前端接入改造（Adapter 模式）

新增 `client/resources/web/transport/bridge.js`，暴露统一接口：

```
window.eatTransport = {
  chatStream(payload, onEvent) -> Promise
  request(op, payload)        -> Promise
  status()                    -> {mode: 'browser'|'app', online, base}
}
```

- 浏览器实现：沿用现有 `fetch` + SSE（**仅开发调试构建可用**，默认不随安装包发布）。
- App 实现：把 `chatStream` 转成 `postMessage`，由壳把 Agent 的事件流原样回投。
- `agent-adapter.js` 只依赖 `window.eatTransport`，不再直接 `fetch`；SSE 事件名/载荷不变（`meta/token/tool_call/confirm_required/done/...`）。
- `app.js` 的菜单/消费/记录接口同样经 `eatTransport.request`，不再硬编码 `127.0.0.1:8787`。

原理：把"传输方式"从业务逻辑里剥离（Strategy/Adapter），页面与 Agent 协议保持稳定，避免为 App 重写一遍 UI。

## 5. IPC 协议（Agent ⇄ 壳）

- 端点：`\\.\pipe\EatAppAgent-{userSidHash}-{appInstance}`；DACL 仅当前用户 SID；其他用户/进程连接被拒。
- 帧：`4B LE length + JSON UTF-8`，单条 ≤1MB；超限断开。
- 握手：`hello{version, app_version, agent_version}` → `hello_ack`；版本不匹配拒绝进入可用态。
- 存活：`ping`/`pong` 5s；工具调用 15s 超时；`hops` ≤3。
- 方法：`chat.stream`（Server→Agent）、`tool.call`（Agent→壳）、`cancel`、`shutdown`、`status`。

跨平台测试：POSIX 用 `AF_UNIX` + same length-prefix 实现；协议编解码与超时/取消在 Linux 单测覆盖。

## 6. 密钥与模型

- `KeyVault`：DPAPI（`CryptProtectData`，CurrentUser），路径 `%LOCALAPPDATA%\EatApp\secrets.dat`；`store/load/revoke/rotate/describe`；UI 只显示"已配置/来源/指纹前 8 位"。
- 三路径：平台 Key（出厂占位）→ 用户自带 Key（优先）→ 无 Key 走 `rule-fallback`；401/403 引导更换 Key，不显示为网络错误。
- 出网：`LlmEgress`（域名白名单、连接 5s/首帧 20s/空闲 30s、重试 ≤2 且流开始后不重试、取消 ≤500ms、证书强制校验）。
- 服务端零 LLM 负向门禁：CI 静态扫描 + 运行期 `ss/lsof` 无模型域名出网。

## 7. 桌面行为

| 能力 | 规则 |
|---|---|
| 单实例 | `Local\EatApp.Instance` 互斥；重复启动激活已有窗口 |
| 托盘 | `Shell_NotifyIcon`；关窗最小化到托盘；"退出"才断开 TCP 并停 Agent |
| 开机自启 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，默认关 |
| 离线 | 服务端不可达：页面显示"无法连接服务端"+重试倒计时；禁止显示 mock 业务数据 |
| 页面资源 | 随包 `<install>\web\`；构建期扫描禁止 `http(s)://...index.html` 远程首屏 |
| 升级 | MSIX 增量升级；失败回滚上一版本 |
| 卸载 | 清理 Run 项、托盘、缓存目录；不残留服务 |

## 8. 功能对等清单（S2 验收逐项打勾）

记录与编辑、今日营养/评分、7 天趋势、健康报告、档案全字段、历史、猜餐与再猜、菜单分页/搜索/随机、消费水平与日常菜分组、积分签到/商城/背景、好友榜/周榜、Agent 对话流式、语气、专家组合、权限（manual/full）、上下文面板与压缩、图片粘贴（≤10 张）、市场安装/卸载/导入、报告与技能导出下载。

## 9. 构建与打包

- `if(WIN32)` 才构建 `eat-app`/`eat-agent` 相关目标；Linux 构建跳过并打印 `STATUS`。
- 工具链：MSVC 2022 + CMake；WebView2 SDK（NuGet/`find_package`）；MSIX Packaging Tool 或 `MakeAppx`；签名证书由发布环境注入。
- Python Agent 打包：`python -m zipapp`/embeddable 发行包，或 PyInstaller（构建期工具，不引入运行时第三方依赖）；产物名 `eat-agent.exe`。
- 安装包内容：`EatApp.exe`、`web/`、`eat-agent.exe`、`agent/`（只读）、WebView2 Runtime（可选 Fixed Version）、LICENSE/NOTICE。

## 10. 验证矩阵与 Linux 边界

| 验证项 | Windows | Linux |
|---|---|---|
| 安装/卸载/签名/升级 | ✅ 必做 | ❌ 不可 |
| WebView2 本地资源与断网冷启动 | ✅ | ❌ |
| DPAPI 存取/撤销/轮换 | ✅ | ❌（返回 `unsupported_platform`） |
| 单实例/托盘/自启 | ✅ | ❌ |
| IPC 长度前缀编解码/超时/取消 | ✅ | ✅ 单测（AF_UNIX 模拟） |
| TcpTransport 握手/心跳/重连 | ✅ | ✅（需先修首连顺序缺陷） |
| 只 1 条业务 TCP / 无 `:8787` | ✅ 抓包 | ⚠️ 静态扫描 |
| Agent 能力回归（RAG/MCP/Skill） | ✅ | ✅ 现有 51 项单测 |

## 11. 风险与阻断

| 编号 | 风险 | 处理 |
|---|---|---|
| A-1 | Linux 无法编译/运行 Win32 壳 | 明确阶段 S2 需 Windows 目标机；Linux 只交付可测的协议/桥接逻辑 |
| A-2 | `TcpTransport::Start()` 先握手后启动读线程 → 首连必失败 | S2 第一项修复：先启动读线程再握手，或改异步握手 |
| A-3 | WebView2 版本碎片化 | 检测 Runtime，缺失时引导安装或使用 Fixed Version |
| A-4 | 页面仍残留 `fetch(:8787)` | 构建期 `grep` 门禁 + 单入口抓包验收 |
| A-5 | Key 可被逆向提取 | 按 R6 声明"本地加密只提高门槛"，不上线"不可提取"话术 |