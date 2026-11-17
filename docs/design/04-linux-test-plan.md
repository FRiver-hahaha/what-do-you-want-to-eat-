# Linux 服务端测试方案

## 1. 环境现状（本机实测）

| 依赖 | 状态 | 影响 |
|---|---|---|
| cmake 3.31.6 | ✅ | 可构建根工程 |
| MySQL 8.4.7 客户端 + `mysql.h` | ✅ | 可跑迁移与 DAO |
| Redis 7.0.15（含 server） | ✅ | 可跑缓存用例 |
| hiredis / `hiredis.h` | ✅ | 可写 CacheClient |
| OpenSSL / `libcrypto` | ✅ | scrypt、CRC 无关、SHA-256 可用 |
| **Muduo** | ❌ | `eat-server` 不生成（`CMakeLists.txt:83-91`） |
| **glog** | ❌ | 仅 `chatroom-backend` 需要；合并后移除 |

## 2. 依赖准备

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libssl-dev \
  libmysqlclient-dev libhiredis-dev libcurl4-openssl-dev \
  mysql-server redis-server

# Muduo：Ubuntu 无稳定包时源码构建（v2.0.x）
git clone --depth 1 --branch v2.0.2 https://github.com/chenshuo/muduo.git third_party/muduo
cmake -S third_party/muduo -B third_party/muduo/build -DCMAKE_BUILD_TYPE=Release
cmake --build third_party/muduo/build -j"$(nproc)"
sudo cmake --install third_party/muduo/build
```

合并阶段把 `find_path` 改为 `find_package`/`MUDUO_ROOT` 双路径，找不到时打印 `STATUS`，禁止静默跳过。

## 3. 构建目标矩阵

| 目标 | 依赖 | 期望 |
|---|---|---|
| `eat_protocol` / `eat_server_core` | 无（仅 Threads/OpenSSL） | 始终可构建 |
| `frame_codec_test` / `strict_json_test` / `connection_governance_test` / `frame_dispatcher_test` / `cache_test` / `identity_test` / `token_verifier_test` | 同上 | ctest 全绿 |
| `config_test` | `deploy/server.toml.example` | 设 `WORKING_DIRECTORY` 后全绿 |
| `offline_queue_test` | `client/transport/OfflineQueue.cc` | 补源文件后可链接 |
| `keyvault_test` | `_WIN32` | Windows 注册；Linux 跳过或断言 `unsupported_platform` |
| `bench_tcp` | `<netdb.h>` | 修头文件后可构建 |
| `eat-server` | Muduo + OpenSSL | 构建成功；`ldd` 有 `libssl` |
| `client_smoke` | TcpTransport | 新增：握手→心跳→`profile.get` |
| `tests/integration/*`（pytest） | MySQL/Redis | 分阶段跑 |

## 4. S0 基线缺陷修复清单

1. `CMakeLists.txt:73-75`：`offline_queue_test` 增加 `client/transport/OfflineQueue.cc`。
2. `CMakeLists.txt:57`：`config_test` 设置 `WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}`。
3. 新增 `keyvault_test` 目标，仅 `WIN32` 注册；Linux 增加"平台不支持"用例。
4. `CMakeLists.txt` 测试目标加 `-UNDEBUG`（或统一改 Debug 构建），避免 Release 下 `assert` 空跑。
5. `server/tcp/TcpServerApp.h:16`/`.cc:16`：`server/tcp/FrameCodec.h` → `shared/protocol/FrameCodec.h`；`FrameCodec` → `protocol::FrameCodec`；补 `<vector>`。
6. `bench_tcp.cc`：补 `<netdb.h>`/`<sys/socket.h>`（POSIX 分支）。
7. `find_package(OpenSSL REQUIRED)` 替代裸 `crypto` 链接。
8. Muduo 查找失败时 `message(STATUS "eat-server skipped: muduo not found")`。

## 5. 测试分层与命令

```bash
# U：单元与协议
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

# A：Agent（无网络、无 Key）
python -m unittest agent.tests.test_smoke -v

# F：前端语法
node --check app.js && node --check agent-adapter.js

# I：MySQL/Redis 集成（S1 起）
sudo systemctl start mysql redis-server
mysql -ueat -p eat < migrations/base/001_p8_business.sql
for f in migrations/add/00{2,3,4,5,6}_*.sql; do mysql -ueat -p eat < "$f"; done

# R：真实帧链路（S1 起）
./build/eat-server --config deploy/server.toml.example &
./build/client_smoke --host 127.0.0.1 --port 9443 --token <test-token>

# P：压测冒烟（S4）
./build/bench_tcp --host 127.0.0.1 --port 9443 --connections 1000
```

## 6. 集成用例（S1 验收）

| 用例 | 断言 |
|---|---|
| 未握手业务帧 | `2001` 且断连；计数 +1 |
| 握手成功 | `handshake_resp` 含 `heartbeat_interval_s=60`、`idle_timeout_s=165`、`max_frame_size` |
| 写方法缺幂等键 | `3001`，DB 零写入 |
| 同键同 payload 重发 | `meal_records` 增量 1；响应为首次结果 |
| 同键异 payload | `3003`，DB 零写入 |
| 缓存命中 | MySQL `Com_select` 零增长 |
| 写后立即读 | 返回新值（模拟窗口内 1000 次读） |
| 停 Redis | 只读成功率 100%，心跳正常，连接不重连 |
| 只读降级恢复 | "故障中写入→恢复→立即读" = 新值 |
| 越权 | 访问他人 `user_key` 返回 3002/3004，不泄露存在性 |
| 日志脱敏 | 扫描无 token/密码/Key/body 原文 |
| 心跳空载 | 60s 内 MySQL `Questions` 与 Redis `commandstats` 零增长 |

## 7. 证据与判定纪律

- 证据目录：`evidence/s0/`、`evidence/s1/`、`evidence/s2/`、`evidence/s3/`、`evidence/s4/`。
- 每份证据含：命令、原始输出、时间、通过/失败、未验证项。
- 容量结论只允许四种标注：**已验证事实 / 理论推导 / 未验证 / 阻断**。
- 未实测的连接数、QPS、延迟一律写"未验证"；`bench_tcp` 当前只是冒烟版，不得作为 1M 依据。
- 迁移前后记录：行数、抽样哈希、`information_schema.partitions`、`EXPLAIN` 结果。

## 8. 本机限制

- 容器内 systemd 可能不可用：MySQL/Redis 需手动 `mysqld_safe`/`redis-server --daemonize yes` 启动。
- 无 Windows 工具链：`eat-app`、DPAPI、WebView2 用例只能在 Windows 执行（见 `02-windows-app.md` 第 10 节）。
- 无压测机：1M 连接保持【阻断】，不做任何"理论可达"结论。