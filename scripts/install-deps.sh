#!/usr/bin/env bash
# Linux 服务端环境一键安装脚本（Ubuntu 22.04 / 24.04 / 25.04）。
#
# 做的事：
#   1. 用 apt 安装编译与运行依赖：C++17 工具链、CMake、OpenSSL、MySQL 客户端库、
#      Redis、Python 3、curl、git。
#   2. 检查仓库同级目录下的 Muduo 源码；不存在则克隆 v2.0.2 并编译。
#   3. 打印构建、启动与验收命令。
#
# 不做的事：
#   - 不安装 mysql-server（推荐 Docker mysql:8.4，避免本机 AppArmor / 权限问题）。
#   - 不修改 sysctl、不开放防火墙、不创建数据库、不写入任何密码。
#   - 不安装 Docker 本身（只检测是否可用并给出提示）。
#
# 用法：
#   bash scripts/install-deps.sh
#   bash scripts/install-deps.sh --skip-muduo
#   EAT_MUDUO_ROOT=/opt/muduo bash scripts/install-deps.sh
#
# 退出码：0 成功；非 0 表示依赖安装或 Muduo 构建失败。

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MUDUO_DIR="${EAT_MUDUO_ROOT:-$ROOT/../muduo}"
SKIP_MUDUO=0

for arg in "$@"; do
    case "$arg" in
        --skip-muduo) SKIP_MUDUO=1 ;;
        -h|--help)
            sed -n '1,30p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *)
            echo "未知参数：$arg（可用：--skip-muduo）" >&2
            exit 2
            ;;
    esac
done

if ! command -v apt-get >/dev/null 2>&1; then
    echo "当前系统没有 apt-get。本脚本仅覆盖 Debian/Ubuntu；" >&2
    echo "其他发行版请手动安装：gcc/g++、cmake、libssl-dev、mysql 客户端开发库、redis、python3。" >&2
    exit 1
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "需要 root 权限安装系统包，但当前既不是 root 也没有 sudo。" >&2
        exit 1
    fi
fi

echo "==> [1/3] 安装系统依赖（apt）"
$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    git \
    ca-certificates \
    curl \
    libssl-dev \
    libmysqlclient-dev \
    default-mysql-client \
    redis-server \
    python3

echo "==> [2/3] 检查工具版本"
printf '    cmake : %s\n' "$(cmake --version | head -1)"
printf '    g++   : %s\n' "$(g++ --version | head -1)"
printf '    python: %s\n' "$(python3 --version 2>&1)"
printf '    mysql : %s\n' "$(mysql --version 2>&1 || echo '未找到客户端')"
printf '    redis : %s\n' "$(redis-server --version 2>&1 | head -1 || echo '未找到 redis-server')"

if [ "$SKIP_MUDUO" -eq 0 ]; then
    echo "==> [3/3] 准备 Muduo（$MUDUO_DIR）"
    if [ -f "$MUDUO_DIR/muduo/net/TcpServer.h" ]; then
        echo "    已存在 Muduo 源码，跳过克隆"
    elif [ -d "$MUDUO_DIR" ] && [ -n "$(ls -A "$MUDUO_DIR" 2>/dev/null || true)" ]; then
        echo "    目录 $MUDUO_DIR 非空但不含 muduo/net/TcpServer.h。" >&2
        echo "    请删除该目录，或用 EAT_MUDUO_ROOT=/其他/路径 重新执行。" >&2
        exit 1
    else
        git clone --depth 1 --branch v2.0.2 https://github.com/chenshuo/muduo.git "$MUDUO_DIR"
    fi

    if [ ! -f "$MUDUO_DIR/build/release-cpp11/lib/libmuduo_net.a" ]; then
        cmake -S "$MUDUO_DIR" -B "$MUDUO_DIR/build/release-cpp11" \
            -DCMAKE_BUILD_TYPE=Release \
            -DMUDUO_BUILD_EXAMPLES=OFF
        cmake --build "$MUDUO_DIR/build/release-cpp11" -j"$(nproc)"
    else
        echo "    Muduo 已编译，复用 $MUDUO_DIR/build/release-cpp11/lib"
    fi
else
    echo "==> [3/3] 按参数跳过 Muduo"
fi

cat <<EOF

依赖安装完成。下一步：

  1) 构建并跑测试：
       bash scripts/build-linux.sh
     或指定 Muduo：
       EAT_MUDUO_ROOT=$MUDUO_DIR bash scripts/build-linux.sh

  2) 启动数据库（推荐 Docker，首次执行）：
       export EAT_MYSQL_ROOT_PASSWORD='<强密码>'
       docker compose -f deploy/docker/docker-compose.yml up -d --build

  3) 本机已有数据库容器时，一键拉起服务端 / Agent / 静态页：
       bash .dev/start-lan.sh

  4) 验收端口：
       ss -ltn | grep -E ':9443|:8787|:8000'

注意：本脚本不创建数据库、不写密码、不改防火墙；Redis 仅监听本机时无需对外开放。
EOF

if ! command -v docker >/dev/null 2>&1; then
    echo
    echo "提示：未检测到 docker。若使用 Docker 方案，请先安装 Docker Engine 与 Compose 插件。" >&2
fi
