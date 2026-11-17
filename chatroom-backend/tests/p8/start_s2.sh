#!/usr/bin/env bash
set -euo pipefail
P8_ROOT="${P8_WORK_ROOT:-/home/m/eat-p8-s1}"
P8_BACKEND="${P8_BACKEND_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
P8_TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

D="$P8_BACKEND"

export CHATROOM_HTTP_BIND=127.0.0.1
export CHATROOM_HTTP_PORT=18080
export CHATROOM_HTTP_TOKEN=local-test-token
export CHATROOM_HTTP_USER_MODE=p4_demo
export CHATROOM_HTTP_WORKERS=16
export CHATROOM_HTTP_DB_POOL=16
export CHATROOM_MYSQL_HOST=/run/mysqld/mysqld.sock
export CHATROOM_MYSQL_PORT=3306
export CHATROOM_MYSQL_USER=root
export CHATROOM_MYSQL_PASSWORD=
export CHATROOM_MYSQL_DB=chatroom_p8
export CHATROOM_HTTP_TOKEN_STDOUT=0

exec "$D/build/bin/server_http"