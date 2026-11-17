# P8 验收脚本（S1–S5）

本目录是 P8 阶段（HTTP 业务后端 `server_http`）在独立工作区 `~/eat-p8-s1` 中真实执行过的
验收脚本，已随代码一并提交，便于复核与复现。

## 环境前提

- Linux/WSL2，MySQL 本地实例（测试使用 Unix socket `/run/mysqld/mysqld.sock`）。
- 测试库固定为 `chatroom_p8`（迁移脚本会拒绝已知业务库）。
- Agent 代码需可导入（`agent/data/service_client.py`、`agent/tools/diet_tools.py`）。
- 服务以 `server_http` 二进制运行，监听 `127.0.0.1:18080`，Token `local-test-token`
  （仅为本地验收值，非真实凭据）。

## 路径约定

脚本不写死工作区路径，可通过环境变量覆盖：

| 变量 | 含义 | 默认值 |
|---|---|---|
| `P8_WORK_ROOT` | 含 `agent/` 的仓库根（Agent 导入用） | `/home/m/eat-p8-s1` |
| `P8_BACKEND_DIR` | `chatroom-backend` 目录 | 本脚本目录上两级 |

## 主要脚本

| 文件 | 用途 |
|---|---|
| `p8s1_http_test.py` … `p8s4_http_test.py` | 各阶段 HTTP 契约与业务用例 |
| `agent_contract_test.py` / `agent_s3_contract_test.py` / `agent_s4_contract_test.py` | Agent → C++ 真实调用契约（禁止 mock 回退） |
| `verify_s2_db.py` … `verify_s5_db.py` | 直接对比 MySQL，独立复算哈希/幂等键/健康分 |
| `s5_e2e_test.py` | 11 条业务链路 + 跨日期/档案一致性 |
| `s5_exception_test.py` | 认证、越权、非法 JSON、幂等冲突、截断 |
| `s5_restart_check.py` | 重启前后持久性与幂等回放（`write` / `verify` 两阶段） |
| `s5_pool_test.py` | 30 并发（需实例 `CHATROOM_HTTP_DB_POOL=1`，端口 18081） |
| `s5_pool_exhaust_test.py` | 锁表制造连接耗尽，验证限时兜底与恢复 |
| `s5_pool_selfheal_test.py` | 停库打空连接池后自动重建、容量上限（端口 18082，`POOL=4`） |
| `s5_db_down_test.py` | MySQL 停机/恢复（需 systemd 与 root 权限） |
| `launch_s2.sh` … `launch_s5.sh` | 各阶段一键启动并跑测试 |
| `regress_s2.sh` … `regress_s5.sh` | 各阶段全回归 |
| `reset_profile.sql` / `s3_fixture.sql` | 档案复位与 S3 排行榜夹具（幂等） |

## 说明

- 证据文件位于 `chatroom-backend/evidence/p8-s1 … p8-s5`，包含首轮失败现场
  （`*.fail*.log`）与最终通过记录，未做删改。
- `s5_pool_selfheal_test.py`、`s5_db_down_test.py` 会启停本机 MySQL 服务，只应在隔离环境执行。
- 迁移与回滚见 `server/http/migrations/`，设计文档见 `docs/p8/`。