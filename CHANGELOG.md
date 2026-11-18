# Changelog

本文件记录对外可见的变更，格式参考 Keep a Changelog，版本遵循 Semantic Versioning。

## [Unreleased]

### Added

- 服务端合并：`server/database`（MySQLPool/DbConn）、`server/identity/IdentityService`、`server/business/profile`、`server/app/ServiceRegistry`。
- 通用线程池 `server/app/ThreadPool`，`BusinessExecutor` 改为其门面（有界、拒绝、deadline、异常隔离）。
- 协议方法：握手（token / 登录 / 注册）、`auth.login`、`auth.register`、`profile.get`、`profile.patch`。
- 客户端传输抽象 `bridge.js`，`client/ipc/IpcProtocol`，`agent/ipc_server.py`。
- Windows 壳骨架：`client/app/win32`（Win32 + WebView2、单实例、托盘、本地资源）。
- 打包与部署：Linux tarball 脚本、Windows 客户端源码包、Docker 多阶段镜像与 compose。
- 企业级仓库基线：README、CONTRIBUTING、SECURITY、CODEOWNERS、CI、EditorConfig、Clang-Format、GitAttributes。
- Windows 构建手册 `AGENT_win.md`。

### Changed

- `005_meal_partition.sql`：幂等账本主键改为 `(id, created_day)`，修复 MySQL 1503。
- 配置新增 `mysql_password/mysql_socket/redis_password`，支持环境变量覆盖。
- 契约修正：图片上限 10、随机比例 70/30、真实 DOM 选择器。
- `delete_meal`/`update_meal` 明确标记 `persisted=false`，不再暗示已落库。

### Fixed

- `TcpServerApp`：FrameCodec 引用、`Buffer::peek` 类型、连接计数下溢、64 帧后续读、`OnTimer` O(n) 扫描。
- 信号处理不再在 handler 内执行阻塞关闭，改为 `loop->quit()` 后有序停止。
- `/api/tools/call` 的 `confirmed="false"` 被当作 true 的确认绕过。
- Docker 迁移：排除 `*_rollback.sql`，避免正向迁移后误执行回滚；运行阶段使用 bash 展开文件列表。

### Known limitations

- `meal/stats/food/friend/ranking` 未帧化。
- Redis CacheClient 未接线。
- Windows GUI 未在目标机编译验证；Agent↔TCP 桥接未完成。
- 明文 TCP，公网需 TLS。
- 100 万在线无实测数据（阻断）。
