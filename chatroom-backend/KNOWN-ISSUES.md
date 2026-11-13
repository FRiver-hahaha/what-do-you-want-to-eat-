# KNOWN-ISSUES — 部署注意事项与已知限制

> 交付侧补充版。接手包 KNOWN-ISSUES.md（K-01~K-20）为完整产品级清单，本文件聚焦本轮交付实测发现 + 部署必读。

## A. 本轮实测环境差异（部署前必读）

1. **init.sql 与 MySQL 8.4**：脚本内 `--注释`（-- 后无空格）会被 MySQL 8.4 解析器拒绝（ERROR 1064）。MariaDB 与旧版 MySQL 兼容。处置（不改仓库文件）：
   `sed "s/^--\([^ ]\)/-- \1/" init.sql | mysql -uroot -p`
2. **Ubuntu MySQL socket 路径**：/run/mysqld/mysqld.sock（接手包测试脚本默认 /tmp/mysql.sock，需显式传 --mysql-socket）。
3. **自管服务测试脚本前置**：crash_recovery / config_guard 会自己启动服务实例。跑之前必须先停占用 8000 的实例并 source .env——否则脚本实例 bind 失败退出，端口探测被旧实例欺骗，且其实例构造期 resetState() 会干扰 state 断言（P5 实测踩过）。
4. **root 执行测试**：DB 断言脚本需 root + socket 免密；root shell 需先 `set -a && . ./.env && set +a`，否则服务以默认空密码连库 FATAL（这是凭据外置生效的正常表现，非缺陷）。

## B. 部署红线

1. **CHATROOM_SMTP_DRY_RUN 生产必须移除**：置 1 时不发真实邮件，验证码打日志，等于注册无真实校验。
2. **历史泄露凭据**：源码中原硬编码 SMTP 授权码已移除；部署前到邮箱后台重置授权码再填 .env。
3. **.env 权限**：chmod 600，已被 .gitignore 排除，禁止提交或写入日志/文档。
4. **Redis 无密码仅限开发**：生产设 requirepass 并在 .env 同步填写（连接池支持 AUTH，错密码启动即失败）。
5. **单实例部署**：验证码在进程内存 + 启动全表 resetState，多实例会互相踢号/丢验证码（接手包 K 系列同）。

## C. 已知限制（承接接手包 K-01~K-20，未新增处理）

- client 未适配验证码注册（K-03）；web 含聊天死入口；transit 依赖未改前端
- 在线状态无主动推送（轮询 type8）；无待审入群申请列表；无群主转让
- 密码 SHA256 无盐（弱口令可彩虹表，建议 bcrypt+迁移）
- 链路明文（建议前置 TLS 代理）；登录拒绝第二处而非踢旧
- message/file 表保留结构不写入（历史数据未清理，本轮始终 0 行）

## D. 本轮交付环境特有注意

- 构建环境为 WSL2 Ubuntu 26.04.1（GCC 15.2 / CMake 4.2.3 / MySQL 8.4.13 / Redis 8.0.5），与接手包验证环境（24.04/MariaDB 10.11）版本有差异但全部 113 项断言实测通过
- bin/ 下旧产物已在 P4 干净构建后统一（无 file_server）；evidence/ 为本轮实测原始日志，勿删
