# 运行手册（服务端 Linux + 客户端 Windows）

## 一、服务端（Linux）

### 方案 A：Docker（推荐，屏蔽依赖差异）

```bash
export EAT_MYSQL_ROOT_PASSWORD='强密码'
docker compose -f deploy/docker/docker-compose.yml up -d --build
docker compose -f deploy/docker/docker-compose.yml logs -f eat-server
```

- 监听：`0.0.0.0:9443`（宿主机防火墙需放行）。
- 首次启动自动执行 `001→006` 迁移。
- 数据卷：`mysql-data`。

### 方案 B：裸机二进制

```bash
tar -xzf dist/eat-server-linux-x86_64.tar.gz
cd eat-server-linux-x86_64
mysql -h 127.0.0.1 -uroot -p < migrations/00_all.sql
export EAT_MYSQL_PASSWORD='强密码'
./run.sh
```

依赖：glibc 2.39+、libssl3、libmysqlclient、libstdc++6。跨发行版建议用方案 A。

## 二、客户端（Windows）

### 立即可用：控制台真实请求客户端

1. 安装 VS 2022 Build Tools（C++ 桌面开发）。
2. 解压 `dist/eat-client-windows.zip`，在 x64 Native Tools 命令行运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build-client.ps1
.\client_smoke.exe --host <服务端IP> --port 9443 `
  --login test@local --password test-pass-123 --register --patch-weight 66
```

期望：`handshake_ok` → `profile_ok ... source=service` → `patch_ok` → `verify_weight=66`。

### GUI App

需在 Windows 上用 VS2022 + CMake + WebView2 SDK 构建（见 `dist/eat-client-windows/README.md`）。
**当前未在 Windows 实机验证，不能宣称端到端可用。**

## 三、局域网连通性

1. 服务端确认监听：`ss -ltn | grep 9443` → `0.0.0.0:9443`。
2. 放行端口：`sudo ufw allow 9443/tcp`（或 firewalld 对应规则）。
3. 在 Windows 上先测端口：`Test-NetConnection <服务端IP> -Port 9443`。
4. 再运行 `client_smoke.exe`。

## 四、已在本机验证

- Linux 上通过局域网 IP `10.101.17.32:19443` 完成注册/登录/档案读写；
- `ctest` 12/12、Agent 51/51、IPC smoke、S1 集成脚本均通过；
- Docker 镜像 `eat-server:0.1` 构建成功（299MB）；容器内自动迁移 + 启动 + 注册/登录/档案读写全部通过（`29444` 映射验证）。

## 五、安全提醒

- 不要把 MySQL 密码、Agent Key 写入仓库、命令行历史或日志。
- 当前链路为明文 TCP，仅建议在可信局域网内试用；公网部署需先接 TLS。
- 服务端 `healthz/metrics`、Redis 缓存、限流策略尚未全部接线。