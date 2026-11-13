# BUILD-REPORT — 构建报告

> 环境：WSL2 Ubuntu（唯一构建环境，Windows 本机 MinGW 无 sys/epoll.h 不可用）。
> 所有构建在本轮真实执行，非静态推断。

## 1. 环境

| 项 | 实测值 |
|---|---|
| OS | Ubuntu 26.04.1 LTS x86_64（WSL2, systemd） |
| GCC / G++ | 15.2.0 |
| CMake | 4.2.3 |
| GNU Make | 4.4.1 |
| MySQL | 8.4.13（apt mysql-server 8.4.13-0ubuntu0.26.04.2） |
| Redis | 8.0.5 |
| hiredis / nlohmann-json / glog | 1.2.0 / 3.11.3 / 0.6.0 |
| libcurl / OpenSSL / readline | 8.18.0 / 3.5.5 / 8.3 |
| Python（测试） | 3.14.4 |

与接手包验证环境（Ubuntu 24.04.3 / g++ 13.3 / MariaDB 10.11 / Redis 7.0.15）的主要差异均已实测兼容，唯一注意项为 init.sql 注释解析（见 KNOWN-ISSUES.md）。

## 2. 依赖安装

apt 一次性安装 147 个包，退出码 0（build-essential、cmake、pkg-config、default-libmysqlclient-dev、libhiredis-dev、libcurl4-openssl-dev、libgoogle-glog-dev、nlohmann-json3-dev、libssl-dev、libreadline-dev、mysql-server、redis-server）。
其中 libreadline-dev 不在接手包 apt 清单但为 CMakeLists client 目标链接所需，属必要补装。
REFACTOR-PLAN 步骤 3 四项判据逐条核对通过：pkg-config hiredis=1.2.0、/usr/include/nlohmann/json.hpp、/usr/include/glog/logging.h、/usr/include/mysql/mysql.h 均存在。

## 3. 数据库初始化

- systemctl start mysql redis-server：MySQL（socket /run/mysqld/mysqld.sock）、Redis PONG
- init.sql 导入：MySQL 8.4 解析器对 `--注释`（-- 后无空格）报 ERROR 1064。处置：sed 管道补空格导入，仓库文件零修改
- 结果：chatroom 库 + 6 张表 + chatserver 账号（host=localhost，TCP 127.0.0.1 实测可连）

## 4. 五次构建记录

| 阶段 | 命令 | cmake | make | error | 产物 |
|---|---|---|---|---|---|
| P0 基线 | mkdir build && cmake .. && make -j | 0 | 0 | 0 | server/client/file_server/transit/web |
| P1 裁剪后 | 增量 | 0 | 0 | 0 | server（1.8MB→1.6MB）；file_server 目标已移除 |
| P2 运行时修复后 | 增量 | 0 | 0 | 0 | server |
| P3 安全加固后 | 增量 | 0 | 0 | 0 | server |
| P4 干净构建 | rm -rf build && cmake .. && make -j | 0 | 0 | 0 | server/client/transit/web（无 file_server） |

P4 干净构建日志归档：evidence/build-cmake.log、evidence/build-make.log。

## 5. 结论

全部阶段构建零 error，产物符合预期（file_server 在 01 补丁后从构建系统移除，P4 干净构建产物中已不存在）。
