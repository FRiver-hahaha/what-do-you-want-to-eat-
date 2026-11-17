# v0.2.0 · 服务端业务帧化、Redis 缓存与 Agent TCP

发布日期：2026-11-17
分支：`feat/app-transport-scale`
提交：`95a5a46`

## 概要

本版本把路线 A 的服务端与 Agent 数据通道落到可运行状态：服务端恢复 15 个业务方法并接入 Redis Cache-Aside，Agent 从 HTTP 改为 40 字节帧 TCP，Windows 壳补齐 `service.login` / `service.request` 桥接代码。

## 服务端

- 恢复 `meal.create / recent / patch / delete`
- 恢复 `stats.nutrition_gap / weekly`
- 恢复 `food.menu / search`
- 恢复 `friend.list / request / accept / remove / block`
- 恢复 `ranking.friends`
- 业务身份统一使用握手得到的 `user_key`
- 餐记录幂等：同键同内容重放，同键异内容返回 3003
- 餐修改/删除使用 `version` 乐观锁（冲突返回 3005）

## Redis Cache-Aside

- 新增 RESP 客户端（连接池、命令白名单、超时）
- 新增 CacheClient：CacheValue、TTL 抖动、熔断 fail-open
- 已接入档案、统计缺口/周统计、菜单、搜索、好友列表
- 写操作后删除相关键；搜索键包含规范化 query 与 tags
- Redis 未启动或故障时业务继续走 MySQL

## Agent

- 新增 `agent/transport/tcp_frame.py`（40 字节帧 + CRC32C）
- `ServiceClient` 支持 TCP；配置服务端后严格模式不再回退 mock
- 新增 `service.configure`、`service.request` IPC 消息

## Windows 客户端

- 页面菜单、随机推荐、消费区间改走 `eatTransport.request`
- 壳新增 `service.login` 握手、会话持久化（`%LOCALAPPDATA%\EatApp\session.json`）
- Agent 启动时注入 TCP/Token 环境变量与数据目录
- **代码已补，但尚未在 Windows 编译验证**

## 资产

| 文件 | 大小 | SHA-256 |
|---|---|---|
| `eat-server-linux-x86_64.tar.gz` | 1.8 MB | `b0d9e179c8c6b9fd170de85b3175a1a515f10eb660f403b5ee2f6fd9d2bd0e4b` |
| `eat-client-windows.zip` | 345 KB | `79a781df7d180fa73af8e49d1dd16d81f4652e490aec4106c72416944e8bd0be` |

服务端包包含：`eat-server`、`client_smoke`、`run.sh`、`server.toml.example`、`migrations/00_all.sql`。
客户端包是**源码 + 构建脚本 + Agent + 前端资源**，不包含预编译 exe；Windows 构建见 `AGENT_win.md`。

## 部署

```bash
tar -xzf eat-server-linux-x86_64.tar.gz
cd eat-server-linux-x86_64
mysql --default-character-set=utf8mb4 -uroot -p < migrations/00_all.sql
redis-server --port 6379 --daemonize yes --save '' --appendonly no
export EAT_MYSQL_PASSWORD='你的密码'
./run.sh
```

必须使用 `--default-character-set=utf8mb4`，否则中文菜名会被二次编码。

## 验证记录

- `service_tcp_test.py`：注册/登录 → 档案 → 餐幂等/冲突 → 统计 → 修改/版本冲突 → 菜单/搜索 → 好友 → 排行 → 删除，全通过。
- `agent_service_client_test.py`：Agent 以 TCP 完成全部数据方法；坏端口返回 `source=service_error`，不回退 mock。
- Redis：键可观察、写后失效、停机降级、恢复回填均通过。
- 迁移：`000→006` 在全新 MySQL 8.4 + `utf8mb4` 下全部成功。

## 已知限制

- 单 exe 未完成；GUI 仍依赖同目录 `web/`、`eat-agent.exe` 与 OpenSSL DLL。
- 页面登录表单未实现。
- 负缓存命中路径未接入业务。
- Windows GUI 未在 Windows 上编译验证。
- 业务 TCP 为明文，仅建议在可信局域网使用。
