# KNOWN-ISSUES — 部署注意事项与已知限制（P7 最终版）

## A. 部署红线

1. **CHATROOM_SMTP_DRY_RUN 生产必须移除**：置 1 时不发真实邮件，验证码打日志。
2. **历史泄露凭据**：源码原硬编码 SMTP 授权码已移除；部署前到邮箱后台重置。
3. **.env 权限 600**，已被 .gitignore 排除，禁止提交或写入日志/文档。
4. **Redis 无密码仅限开发**：生产设 requirepass 并同步 .env（连接池支持 AUTH）。
5. **单实例部署**：验证码在进程内存 + 启动全表 resetState，多实例互相冲突。
6. **web 只托管 ./static**：CMake 将 index.html 复制到 bin/static/，工作目录其余内容不对外暴露。

## B. 环境差异（部署前必读）

1. init.sql 的 `--注释`（-- 后无空格）在 MySQL 8.4 报 ERROR 1064（MariaDB 兼容）。
   处置：`sed "s/^--\([^ ]\)/-- \1/" init.sql | mysql`（不改文件本体）。
2. Ubuntu MySQL socket：/run/mysqld/mysqld.sock（测试脚本需显式传参）。
3. 应用账号按 init.sql 模板创建（替换强密码占位符），权限仅
   SELECT/INSERT/UPDATE/DELETE；密码经 CHATROOM_MYSQL_PASSWORD 注入，勿写源码。
4. 存量旧库（如曾用 6 表版初始化）需自行评估是否清理 message/file/group_* 表
   —— 本交付的 init.sql 只新建 user/friend，不会自动删旧表。

## C. 已知限制（产品级，本轮未处理）

- client/web 为最小裁剪（功能入口已删，UI 未重设计）；注册流程已带验证码，
  client 注册页尚未加 code 输入项（服务端已强制，前端补齐后可用验证码注册）
- 密码 SHA256 无盐（建议 bcrypt+迁移）
- 链路明文（建议前置 TLS 代理）
- 登录拒绝第二处而非踢旧；在线状态无主动推送（轮询 type 8）
- 上游仓库无 LICENSE（见 NOTICE，合并前建议取得上游作者授权）

## D. 测试注意

- 自管服务脚本（crash_recovery/config_guard）跑前停 8000 服务并 source .env
- 测试统一用独立库（--mysql-db），禁止指向生产库
- WSL 发行版空闲回收会杀后台服务：测试需单命令自包含执行