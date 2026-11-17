#!/usr/bin/env bash
set -euo pipefail

MYSQL_HOST="${EAT_MYSQL_HOST:-mysql}"
MYSQL_PORT="${EAT_MYSQL_PORT:-3306}"
MYSQL_DB="${EAT_MYSQL_DB:-eat}"
TCP_PORT="${EAT_TCP_PORT:-9443}"

echo "[entrypoint] waiting for mysql ${MYSQL_HOST}:${MYSQL_PORT}"
for _ in $(seq 1 120); do
    if (echo > "/dev/tcp/${MYSQL_HOST}/${MYSQL_PORT}") >/dev/null 2>&1; then
        break
    fi
    sleep 1
done

if ! (echo > "/dev/tcp/${MYSQL_HOST}/${MYSQL_PORT}") >/dev/null 2>&1; then
    echo "[entrypoint] mysql not reachable" >&2
    exit 1
fi

# Migrations are idempotent for 001/002/003/004/006; 005 uses ALTER TABLE and is
# guarded by the schema_migration record.
NEEDS_MIGRATION=1
if mysql -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "${EAT_MYSQL_USER:-root}" \
        -p"${EAT_MYSQL_PASSWORD}" -N -e \
        "SELECT COUNT(*) FROM ${MYSQL_DB}.schema_migration WHERE migration_id='005_meal_partition'" \
        2>/dev/null | grep -qx 1; then
    NEEDS_MIGRATION=0
fi

if [[ "$NEEDS_MIGRATION" == "1" ]]; then
    echo "[entrypoint] applying migrations to ${MYSQL_DB}"
    mysql -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "${EAT_MYSQL_USER:-root}" \
        -p"${EAT_MYSQL_PASSWORD}" < /opt/eat/initdb/00_all.sql
fi

cat > /opt/eat/etc/server.toml <<EOF
max_connections = ${EAT_MAX_CONNECTIONS:-1000000}
listen_backlog = 65535
heartbeat_interval_s = 60
idle_timeout_s = 165
sub_reactors = ${EAT_SUB_REACTORS:-8}
max_frame_size = 65536
max_frame_hard_limit = 262144
send_high_watermark = 262144
send_low_watermark = 65536
global_send_high_watermark = 536870912
business_threads = ${EAT_BUSINESS_THREADS:-16}
business_queue_capacity = 8192
task_timeout_ms = 3000
db_pool_size = ${EAT_DB_POOL_SIZE:-16}
db_pool_max = 64
db_borrow_timeout_ms = 200
db_statement_timeout_ms = 2500
rate_per_conn_per_s = 60
rate_per_conn_burst = 120
tcp_port = ${TCP_PORT}
admin_port = 9080
mysql_host = ${MYSQL_HOST}
mysql_port = ${MYSQL_PORT}
mysql_db = ${MYSQL_DB}
mysql_user = ${EAT_MYSQL_USER:-root}
redis_host = ${EAT_REDIS_HOST:-redis}
redis_port = ${EAT_REDIS_PORT:-6379}
EOF

echo "[entrypoint] starting eat-server on 0.0.0.0:${TCP_PORT}"
exec /opt/eat/bin/eat-server --config /opt/eat/etc/server.toml