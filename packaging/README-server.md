# 泛饭服务端（Linux x86_64）运行说明

## 1. 发布包内容

```text
eat-server-linux-x86_64/
  eat-server
  client_smoke
  run.sh
  server.toml.example
  migrations/00_all.sql
  README.md
```

运行依赖：glibc 2.39+（Ubuntu 24.04/25.04）、`libssl3`、`libmysqlclient`、`libstdc++6`。

## 2. 直接运行

```bash
mysql --default-character-set=utf8mb4 -uroot -p < migrations/00_all.sql
export EAT_MYSQL_PASSWORD='你的MySQL密码'
./run.sh
```

- 默认读取同目录 `server.toml`（可从 `server.toml.example` 复制）。
- 监听 `0.0.0.0:9443`，局域网可直接访问。
- 数据库配置：`mysql_host/mysql_port/mysql_db/mysql_user` 在 `server.toml`，密码只走环境变量。
- 迁移必须带 `--default-character-set=utf8mb4`，否则中文菜名会被二次编码，菜单与搜索为空。

## 3. Redis（可选）

```bash
redis-server --port 6379 --daemonize yes --save '' --appendonly no
redis-cli ping     # PONG
```

配置项（`server.toml`）：

```toml
redis_host = 127.0.0.1
redis_port = 6379
cache_get_timeout_ms = 20
cache_connect_timeout_ms = 50
cache_breaker_failures = 3
cache_breaker_open_ms = 5000
cache_gen = 1

captcha_required = 1
captcha_ttl_s = 120
```

- `captcha_required = 1` 时，注册和登录都必须携带服务端下发的图形验证码。
- 验证码通过未认证方法 `auth.captcha` 获取，一次性使用，默认 120 秒过期。
- 批量部署/自动化脚本可临时设为 `0`；正式环境建议保持 `1`。

- 缓存键前缀 `eat:v1:<cache_gen>:`。
- 档案、餐记录、好友写操作会删除相关键；`menu/search` 使用 TTL 抖动。
- Redis 未启动或故障时自动 fail-open，业务继续查 MySQL，不会拒绝请求。

## 4. 局域网访问

```bash
sudo ufw allow 9443/tcp        # 若启用了 ufw
ss -ltn | grep 9443            # 应为 0.0.0.0:9443
```

Windows 客户端连接 `--host <这台机器的局域网IP> --port 9443`。

## 5. 推荐：Docker

```bash
export EAT_MYSQL_ROOT_PASSWORD='换成强密码'
docker compose -f docker-compose.yml up -d --build
docker compose logs -f eat-server
```

镜像内包含 MySQL 8.4 与服务端；首次启动自动执行迁移。端口 `9443` 直接映射到宿主机。

## 6. 验证

基础握手/档案：

```bash
./client_smoke --host 127.0.0.1 --port 9443 \
  --login test@local --password test-pass-123 --register --patch-weight 66
```

期望：`handshake_ok`、`profile_ok name=我 source=service`、`patch_ok`、`verify_weight=66`。

完整业务（需要 Python 与带源码的仓库）：

```bash
PYTHONPATH=. python3 tests/integration/service_tcp_test.py --host 127.0.0.1 --port 9443
```

覆盖：注册/登录、档案、餐记录幂等与冲突、最近餐、营养缺口、周统计、修改/版本冲突、删除、菜单、搜索、好友申请/接受/列表/删除、好友排行。

## 7. 已实现能力

- 认证：注册、登录、握手、令牌哈希、限流与锁定。
- 档案：`profile.get` / `profile.patch`。
- 餐记录：`meal.create` / `recent` / `patch` / `delete`，`idem_key` 幂等。
- 统计：`stats.nutrition_gap` / `stats.weekly`。
- 菜品：`food.menu` / `food.search`（过敏与忌口过滤）。
- 好友：`friend.list` / `request` / `accept` / `remove` / `block`。
- 排行：`ranking.friends`。
- 缓存：Redis Cache-Aside、写后 DEL、熔断降级。

## 8. 能力边界

- 业务 TCP 仍是明文，仅建议在可信局域网使用；公网部署需先接 TLS。
- 单 exe Windows GUI 未完成；Windows 需要按 `AGENT_win.md` 编译。
- 负缓存命中路径尚未接入业务。
- 100 万在线仍缺压测数据。
