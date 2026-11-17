# Eat 服务端（Linux x86_64）运行说明

## 1. 直接运行（本机依赖已具备时）

```bash
# 依赖：glibc 2.39+（Ubuntu 24.04/25.04）、libssl3、libmysqlclient、libstdc++6
export EAT_MYSQL_PASSWORD='你的MySQL密码'
./run.sh
```

- 默认读取同目录 `server.toml`（可从 `server.toml.example` 复制）。
- 监听 `0.0.0.0:9443`，局域网可直接访问。
- 数据库：`mysql_host/mysql_port/mysql_db/mysql_user` 在 `server.toml`，密码只走环境变量。
- 首次部署需执行 `migrations/00_all.sql`（已处理 CRLF 与注释格式）：

```bash
mysql -h 127.0.0.1 -uroot -p < migrations/00_all.sql
```

## 2. 局域网访问

```bash
sudo ufw allow 9443/tcp        # 若启用了 ufw
ss -ltn | grep 9443            # 应为 0.0.0.0:9443
```

Windows 客户端连接 `--host <这台机器的局域网IP> --port 9443`。

## 3. 推荐：Docker（屏蔽系统依赖差异）

```bash
export EAT_MYSQL_ROOT_PASSWORD='换成强密码'
docker compose -f docker-compose.yml up -d --build
docker compose logs -f eat-server
```

镜像内包含 MySQL 8.4 与服务端；首次启动自动执行迁移。端口 `9443` 直接映射到宿主机。

## 4. 验证

```bash
./client_smoke --host 127.0.0.1 --port 9443 \
  --login test@local --password test-pass-123 --register --patch-weight 66
```

期望输出：`handshake_ok`、`profile_ok name=我 source=service`、`patch_ok`、`verify_weight=66`。

## 5. 当前能力边界

- 已实现：注册、登录、握手鉴权、`profile.get`、`profile.patch`。
- 未实现：`meal.*`、`stats.*`、`food.*`、`friend.*`、`ranking.*`、Redis 缓存、TLS。
- 仅 `client_smoke`（帧级控制台客户端）可跑通上述真实请求；WebView2 GUI 客户端尚未在 Windows 验证。