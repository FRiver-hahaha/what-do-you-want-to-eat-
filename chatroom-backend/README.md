# ChatRoom Backend（裁剪版）

基于 C++17 的账号 / 好友 / 在线状态后端服务。Reactor 模式 + 线程池架构。
私聊、群聊、消息记录、文件传输等聊天能力，以及群组管理功能，均已按需求整体裁剪。

> 来源声明见 [NOTICE](NOTICE)。

---

## 功能特性（保留）

- **账号**：注册（邮箱验证码）、密码登录、验证码登录、重置密码、退出、注销（级联删除）
- **好友**：申请、审批、拉黑、删除、好友列表（含在线状态）
- **在线状态**：登录 / 退出即时可见，进程崩溃重启自动复位
- **安全**：凭据全环境变量注入、Redis AUTH、验证码策略（限频/过期/次数上限/一次性）、CAS 原子登录、64KB 报文上限
- **配套端**：终端客户端 (TUI)、Web 网页端（经 WebSocket 中转）

## 已删除（本仓库不再提供）

- 聊天：私聊、群聊、聊天记录、消息推送、文件传输（含 file_server 二进制服务）、message/file 表
- 群组：创建、解散、成员管理、邀请、审批、管理员权限、群组列表（15–24 全部路由）、GroupModel、group_info/group_user 表

删除的协议 type 一律返回 `{"type":0,"code":0,"msg":"异常请求"}`。

---

## 环境依赖

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| C++ 编译器 | GCC 7+ / Clang 6+ | 支持 C++17 |
| CMake | 3.10+ | 构建系统 |
| MySQL | 5.7+ / 8.0 | 主数据库 |
| Redis | 6.0+ | 离线申请通知缓存 |
| hiredis | 1.0+ | Redis C 客户端 |
| libmysqlclient | 8.0+ | MySQL C API |
| libcurl | 7.60+ | 发送邮件 (SMTP) |
| OpenSSL | 1.1.1+ | SHA256、WebSocket 握手 |
| glog | 0.4+ | 日志库 |
| nlohmann/json | 3.10+ | JSON 解析 (头文件) |
| readline | 8.0+ | 客户端交互行编辑 |

### Ubuntu 安装示例

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake pkg-config \
    default-libmysqlclient-dev libhiredis-dev libcurl4-openssl-dev \
    libssl-dev libgoogle-glog-dev \
    nlohmann-json3-dev libreadline-dev \
    mysql-server redis-server
```

---

## 目录结构

```
chatroom-backend/
├── CMakeLists.txt              # 构建配置（server / client / transit / web 四个目标）
├── init.sql                    # 数据库初始化（2 张表 + 应用账号模板）
├── .env.example                # 运行配置模板（凭据经环境变量注入）
├── NOTICE                      # 上游来源与版权声明
├── tests/                      # 协议级回归测试脚本（Python，已按裁剪后功能修订）
├── evidence/                   # 构建 / 测试证据日志
├── server/                     # 主服务 (端口 8000)
│   ├── main.cc                 # 入口：主 Reactor + 子 Reactor 线程池
│   ├── common/Config.h         # 环境变量读取工具
│   ├── database/               # MySQL 连接池、Redis 封装（支持 AUTH）
│   ├── email/                  # SMTP 发送验证码（凭据外置）
│   ├── model/                  # User / UserModel / FriendModel
│   ├── net/                    # TcpServer、Connection（64KB 读上限）
│   ├── reactor/                # Reactor 事件循环
│   └── service/ChatService.cc  # 业务逻辑分发（13 个保留路由 0–12）
├── client/                     # 终端客户端 (TUI)：账号/好友操作
├── transit/                    # WebSocket ↔ TCP 中转 (端口 10000，纯文本 JSON)
└── web/                        # 静态 HTTP 服务 (端口 9000，仅托管 ./static)
```

---

## 构建与运行

### 1. 初始化数据库

```bash
sudo mysql < init.sql        # 建库 + 建 user/friend 两张表
# 应用账号：按 init.sql 内模板创建（密码经 .env 注入，权限仅 SELECT/INSERT/UPDATE/DELETE）
```

### 2. 配置并启动

```bash
cp .env.example .env && chmod 600 .env && vi .env
set -a && . ./.env && set +a

mkdir -p build && cd build && cmake .. && make -j$(nproc)
cd bin
./server        # 主服务 (8000)
./transit       # WebSocket 中转 (10000，网页端必需)
./web           # 静态页托管 (9000，可选)
./client        # 终端客户端（可选）
```

### 3. 运行配置（全部经环境变量，源码零硬编码）

见 `.env.example`：MySQL / Redis / SMTP 连接、端口、线程数、验证码策略。
`CHATROOM_SMTP_DRY_RUN=1` 仅供自动化测试（验证码打日志），生产必须移除。

---

## 端口汇总

| 服务 | 端口 | 说明 |
|------|------|------|
| 主服务 | 8000 | TCP 长连接，JSON 换行分帧 |
| WebSocket 中转 | 10000 | WS ↔ TCP，供网页端接入 |
| HTTP 静态前端 | 9000 | 仅托管 bin/static 目录 |

---

## 协议简述

客户端 ↔ 主服务 (8000)：JSON 文本，`\n` 分帧，`type` 字段区分业务。
保留 type：0–12（心跳/账号 0–7、好友 8–12）。
聊天（13/14/25/26/27）与群组（15–24）相关 type 已删除。

## 回归测试

`tests/` 内含 8 个协议级测试脚本（已按最终裁剪范围修订：群组与聊天均改为不可达断言，
库名经 --mysql-db 参数化）。用法与判据见 `TEST-REPORT.md` 与 `HANDOFF.md`。
本仓库交付证据见 `evidence/`（p7b-*.log 为最终裁剪版实测）。