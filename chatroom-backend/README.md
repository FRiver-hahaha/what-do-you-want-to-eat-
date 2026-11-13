# ChatRoom

基于 C++17 的高性能聊天室系统，采用 Reactor 模式 + 线程池架构，支持私聊、群聊、文件传输、断点续传、WebSocket 网页端接入。

---

## 功能特性

- **即时通讯**：私聊、群聊、好友申请、群邀请/申请、消息历史记录
- **文件传输**：上传/下载、断点续传、大文件分片传输
- **多端支持**：终端客户端 (TUI)、Web 网页端 (WebSocket)
- **高性能**：Reactor 多线程模型、epoll ET 模式、连接池复用
- **数据持久化**：MySQL 存储用户/好友/群组/消息，Redis 缓存离线消息
- **邮件验证**：注册/登录/改密邮箱验证码

---

## 环境依赖

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| C++ 编译器 | GCC 7+ / Clang 6+ | 支持 C++17 |
| CMake | 3.10+ | 构建系统 |
| MySQL | 5.7+ / 8.0 | 主数据库 |
| Redis | 6.0+ | 离线消息缓存 |
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
    build-essential cmake \
    libmysqlclient-dev libhiredis-dev libcurl4-openssl-dev \
    libssl-dev libgoogle-glog-dev \
    nlohmann-json3-dev libreadline-dev \
    mysql-server redis-server
```

---

## 目录结构

```
ChatRoom/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 本文档
├── init.sql                    # 数据库初始化脚本
├── client/                     # 终端客户端 (TUI)
│   ├── main.cc                 # 入口：连接服务器、启动网络/IO 线程、认证菜单
│   ├── pool.h                  # 线程池
│   ├── common/                 # 通用工具、颜色、全局变量
│   │   ├── common.h
│   │   └── common.cc
│   ├── net/                    # TcpClient、协议收发、文件传输
│   │   ├── net.h
│   │   ├── net.cc
│   │   ├── TcpClient.h
│   │   ├── TcpClient.cc
│   │   └── filetransfer.h
│   └── page/                   # 页面/菜单
│       ├── auth.h              # 认证页面
│       ├── auth.cc
│       ├── friend.h            # 好友页面
│       ├── friend.cc
│       ├── group.h             # 群聊页面
│       ├── group.cc
│       ├── settings.h          # 设置页面
│       └── settings.cc
├── server/                     # 聊天主服务 (端口 8000)
│   ├── main.cc                 # 入口：主 Reactor + 子 Reactor 线程池
│   ├── pool.h                  # 线程池
│   ├── database/               # MySQL 连接池、Redis 封装
│   │   ├── MySQL.h
│   │   ├── MySQL.cc
│   │   ├── MySQLPool.h
│   │   ├── MySQLPool.cc
│   │   ├── Redis.h
│   │   └── Redis.cc
│   ├── email/                  # SMTP 发送验证码
│   │   ├── EmailSender.h
│   │   └── EmailSender.cc
│   ├── model/                  # 数据模型：User/Friend/Group/Message
│   │   ├── User.h
│   │   ├── User.cc
│   │   ├── UserModel.h
│   │   ├── UserModel.cc
│   │   ├── FriendModel.h
│   │   ├── FriendModel.cc
│   │   ├── GroupModel.h
│   │   ├── GroupModel.cc
│   │   ├── MessageModel.h
│   │   └── MessageModel.cc
│   ├── net/                    # TcpServer、Connection
│   │   ├── TcpServer.h
│   │   ├── TcpServer.cc
│   │   ├── Connection.h
│   │   └── Connection.cc
│   ├── reactor/                # Reactor 事件循环
│   │   ├── Reactor.h
│   │   └── Reactor.cc
│   ├── service/                # ChatService：业务逻辑分发、离线消息落盘
│   │   ├── ChatService.h
│   │   └── ChatService.cc
│   └── file.cc                 # 文件服务 (端口 8001)
├── transit/                    # WebSocket ↔ TCP 中转 (端口 10000)
│   ├── transit.cc              # 单进程 epoll ET，处理 WS 握手/分帧、后端长连接、文件隧道
│   └── pool.h                  # 线程池 (仅做 SHA1)
└── web/                        # 静态 HTTP 前端 (端口 9000)
    ├── main.cc                 # httplib 托管 index.html
    ├── index.html              # 网页聊天界面
    └── httplib.h               # 单头文件 HTTP 库
```

---

## 构建与运行

### 1. 克隆仓库

```bash
git clone git@github.com:lzh070702/ChatRoom.git
```

### 2. 初始化数据库

项目根目录提供了 `init.sql`，一条命令完成建库、建用户、建表：

```bash
cd ChatRoom
sudo mysql < init.sql
```

> **注意**：数据库配置硬编码在 `server/service/ChatService.cc:45-46`（用户 `chatserver`、密码 `123456`、库名 `chatroom`），如需修改请改源码重新编译。

### 3. 启动 Redis

```bash
redis-server --daemonize yes
```

### 4. 构建项目

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

构建产物在 `build/bin/`：
- `server`          — 聊天主服务 (8000)
- `file_server`     — 文件传输服务 (8001)
- `client`          — 终端客户端
- `transit`         — WS↔TCP 中转 (10000)
- `web`             — 静态 HTTP 前端 (9000)

### 5. 运行服务端 (按顺序启动)

```bash
cd bin

# 终端 1: 聊天主服务
./server

# 终端 2: 文件传输服务
./file_server

# 终端 3: WebSocket 中转 (网页端必需)
./transit

# 终端 4: 静态 Web 前端 (可选)
./web
```

### 6. 运行客户端

```bash
cd bin

# 默认连接 127.0.0.1:8000
./client

# 仅指定地址(默认端口为 8000)
./client <host>

# 指定地址/端口
./client <host> <port>

# 网页端(可以同时支持cli和web)：启动 ./web 后，浏览器访问 http://<server-ip>:9000/index.html
# WebSocket 连接地址：ws://<server-ip>:10000
```


---

## 端口汇总

| 服务 | 端口 | 说明 |
|------|------|------|
| 聊天主服务 | 8000 | TCP 长连接，JSON 换行分帧 |
| 文件传输 | 8001 | 二进制协议，支持断点续传 |
| WebSocket 中转 | 10000 | WS ↔ TCP，供网页端接入 |
| HTTP 静态前端 | 9000 | 托管 index.html |

---

## 配置说明

主要配置硬编码在代码中，需修改源码后重新编译：

| 文件 | 配置项 |
|------|--------|
| `server/service/ChatService.cc:45-51` | MySQL/Redis 连接池参数、数据库账号密码 |
| `server/main.cc:14,29` | 主服务线程数、监听端口 |
| `server/file.cc:29,46` | 文件服务端口、存储目录 |
| `transit/transit.cc:31-33` | WS 端口、后端端口、文件端口 |
| `web/main.cc:5` | HTTP 端口 |
| `client/main.cc:20-21` | 默认连接地址/端口 |

---

## 协议简述

- **客户端↔主服务 (8000)**：JSON 文本，`\n` 分帧，`type` 字段区分业务 (1=注册, 2=登录, 13=私聊, 25=群聊…)
- **客户端↔文件服务 (8001)**：二进制帧头 + 文件名 + 数据，支持上传/下载/断点续传
- **网页↔中转 (10000)**：标准 WebSocket，文本帧转发 JSON，二进制帧透传文件服务