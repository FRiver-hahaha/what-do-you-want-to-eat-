# BUILD-REPORT — 构建报告（P7 最终裁剪版）

> 环境：WSL2 Ubuntu。所有构建为本轮真实执行。

## 1. 环境

| 项 | 实测值 |
|---|---|
| OS | Ubuntu 26.04.1 LTS x86_64（WSL2, systemd） |
| GCC / G++ | 15.2.0 |
| CMake | 4.2.3 |
| GNU Make | 4.4.1 |
| MySQL | 8.4.13 |
| Redis | 8.0.5 |
| hiredis / nlohmann / glog | 1.2.0 / 3.11.3 / 0.6.0 |
| libcurl / OpenSSL / readline | 8.18.0 / 3.5.5 / 8.3 |
| Python（测试） | 3.14.4 |

## 2. 依赖安装（一次性，147 包）

build-essential cmake pkg-config default-libmysqlclient-dev libhiredis-dev
libcurl4-openssl-dev libgoogle-glog-dev nlohmann-json3-dev libssl-dev
libreadline-dev mysql-server redis-server。
（libreadline-dev 为 client 目标链接所需，属必要补装）

## 3. 数据库初始化

- chatroom_p7 独立验证库（不触碰既有业务库）
- init.sql（2 表版）管道替换库名导入，退出码 0
- chatserver 最小权限：GRANT SELECT, INSERT, UPDATE, DELETE ON chatroom_p7.*（实测连接验证通过）

## 4. P7 两轮干净构建记录

| 轮次 | 内容 | cmake | make | error | 产物 |
|---|---|---|---|---|---|
| P7-1 | 删聊天残留后 | 0 | 0* | 0（transit 孤立括号修复后 0） | server/client/transit/web/static |
| P7-2 | 删群组后（最终版） | 0 | 0 | 0 | server/client/transit/web/static |

auth.cc 群组推送残留清理后增量重建：exit 0，error 0。

## 5. 结论

最终裁剪版（删聊天 + 删群组）干净构建零 error，产物 4 目标 +
static/index.html；file_server 目标不存在。