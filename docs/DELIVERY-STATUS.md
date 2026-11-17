# 本轮交付状态（S0–S2）

## 已完成

| 阶段 | 内容 | 证据 |
|---|---|---|
| S0 | CMake 修复（OpenSSL、Muduo 路径、offline_queue 源、config 工作目录、keyvault 平台分支、`-UNDEBUG`）；`TcpServerApp` 编译修复；`eat-server` 可构建 | `ctest` 12/12 |
| 线程池 | 新增 `server/app/ThreadPool.{h,cc}`（有界、拒绝、deadline、异常隔离、Stop 丢弃计数），`BusinessExecutor` 改为门面 | `thread_pool_test` |
| S1a | 容器 MySQL 8.4 测试实例 + `001→006` 迁移全部成功；`005` 主键修正 | `eat_s1` 12 表、两分区表各 25 分区 |
| S1b | `MySQLPool`/`DbConn`、`IdentityService`（注册/登录/scrypt/令牌）、握手 TokenVerifier、Config 密码与 env 覆盖 | 真实 TCP 握手 |
| S1c | `profile.get`/`profile.patch` 注册；`client_smoke`；集成脚本 | `S1_INTEGRATION_OK` |
| S2a | `bridge.js` 传输抽象；`client/ipc/IpcProtocol`；`agent/ipc_server.py`；IPC smoke | `IPC_SMOKE_OK` |
| S2b | Win32 + WebView2 壳、单实例、托盘、本地资源打包、Windows CMake | 代码已落，Windows 未验证 |
| S3（部分） | 修 `delete/update_meal` 假成功（标 `persisted=false`）、`/api/tools/call` 严格布尔、契约漂移（图片 10、随机 70/30、真实 DOM）、删除 `consume` 死码 | 51/51 + `node --check` |

## 验证命令与结果

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DEAT_MUDUO_ROOT=/home/friver/gitclone/muduo
cmake --build build -j16
ctest --test-dir build --output-on-failure          # 12/12 passed
python -m unittest agent.tests.test_smoke          # 51 passed
python tests/integration/ipc_smoke.py              # IPC_SMOKE_OK
EAT_MYSQL_PASSWORD=*** EAT_MYSQL_PORT=13306 EAT_MYSQL_DB=eat_s1 \
  bash tests/integration/s1_smoke.sh                # S1_INTEGRATION_OK
node --check app.js bridge.js agent-adapter.js      # ok
```

## 打包产物（本轮）

| 产物 | 内容 | 验证 |
|---|---|---|
| `dist/eat-server-linux-x86_64.tar.gz` | `eat-server`、`client_smoke`、`run.sh`、`server.toml.example`、`migrations/00_all.sql` | 解包后监听 `0.0.0.0:9443`，真实请求通过 |
| `dist/eat-client-windows.zip` | `client_smoke.cc`、`build-client.ps1`、`agent/`、`client/`、页面资源 | 需 Windows + VS2022 执行脚本生成 `client_smoke.exe` |
| `deploy/docker/*` | 多阶段 Dockerfile、compose（MySQL 8.4 + server）、entrypoint、自动迁移 | 镜像 `eat-server:0.1` 构建成功；容器内迁移 + 真实请求（注册/登录/档案）通过 |
| `docs/RUNBOOK.md` | 服务端/客户端运行与局域网连通步骤 | — |

本机实测：通过局域网地址 `10.101.17.32:19443` 完成注册、登录、`profile.get`、`profile.patch`（weight=66）并读回；解包后的 `9443` 服务同样通过。

**Windows GUI exe 未生成**：当前 Linux 环境无 MSVC/WebView2/SDK，无法交叉编译。`client_smoke.exe` 可在 Windows 上用 `build-client.ps1` 一键生成（纯 WinSock，无第三方依赖）；GUI App 需按 `client/app/CMakeLists.txt` 在 Windows 构建。

## 未完成（明确不宣称）

- Windows App：WebView2 壳未在 Windows 编译/安装/签名；DPAPI、托盘、单实例、断网冷启动均未验证。
- 服务端业务：仅 `profile.get/patch` + 认证；`meal/stats/food/friend/ranking` 仍依赖旧 P8 代码，未帧化。
- Redis：仅启动服务，服务端未接入 CacheClient；缓存一致性/降级用例未跑。
- `chatroom-backend/` 未删除（需确认）；`health_profile` 列名 007 未执行。
- 100 万连接：**阻断**，无压测数据。

## 需要用户确认的破坏性操作

1. 删除 `chatroom-backend/`（业务迁完后）。
2. 执行 `007_profile_user_key.sql`（列名 `agent_uid` → `user_key`，影响旧 P8 进程）。
3. 删除 Config 中 20+ 个未接线字段（需同步模板与文档）。
4. 将根目录页面资源移入 `client/resources/web/`（App 最终形态）。
## 版本库状态（2026-11-17）

- 本地已提交 4 个提交（`08263e9`、`4900801`、`4ad8311`、`ac7271c`），工作区干净。
- **推送阻断**：HTTPS 无凭据（`could not read Username`），远端仍为 `bc2ecb2`。
- 已导出 `dist/eat-agent-updates.bundle`（94 KB），可在 Windows 上导入后推送，步骤见 `AGENT_win.md` §8。
- 二进制与依赖不入库：Linux 通过 tarball / Docker 镜像，Windows 通过源码包 + `build-client.ps1` / CI artifact / Release。
- 企业级基线已入库：README、CONTRIBUTING、SECURITY、CHANGELOG、CODEOWNERS、PR 模板、Linux/Windows CI、EditorConfig、Clang-Format、GitAttributes、build/package 脚本。
