#!/usr/bin/env bash
# S1 integration smoke: real TCP handshake -> auth -> profile.get/patch.
# Requires a migrated MySQL database and the built binaries. Secrets are read
# from the environment (EAT_MYSQL_PASSWORD); never echoed by this script.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${EAT_TEST_BIN_DIR:-$ROOT/build}"
PORT="${EAT_TEST_PORT:-19443}"
HOST="${EAT_TEST_HOST:-127.0.0.1}"
DB_HOST="${EAT_MYSQL_HOST:-127.0.0.1}"
DB_PORT="${EAT_MYSQL_PORT:-3306}"
DB_USER="${EAT_MYSQL_USER:-root}"
DB_NAME="${EAT_MYSQL_DB:-eat_s1}"
LOGIN="smoke-$(date +%s)-$$@test.local"
PASSWORD="smoke-pass-123"
WORK="$(mktemp -d)"
PID=""

cleanup() {
    if [[ -n "$PID" ]]; then kill "$PID" 2>/dev/null || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT

cat > "$WORK/server.conf" <<EOF
max_connections = 256
listen_backlog = 128
heartbeat_interval_s = 60
idle_timeout_s = 165
sub_reactors = 2
max_frame_size = 65536
max_frame_hard_limit = 262144
send_high_watermark = 262144
send_low_watermark = 65536
global_send_high_watermark = 33554432
business_threads = 8
business_queue_capacity = 256
task_timeout_ms = 3000
db_pool_size = 4
db_pool_max = 4
tcp_port = $PORT
admin_port = 19080
mysql_host = $DB_HOST
mysql_port = $DB_PORT
mysql_db = $DB_NAME
mysql_user = $DB_USER
EOF

"$BIN/eat-server" --config "$WORK/server.conf" > "$WORK/server.log" 2>&1 &
PID=$!
for _ in $(seq 1 30); do
    if (echo > "/dev/tcp/$HOST/$PORT") >/dev/null 2>&1; then break; fi
    sleep 0.5
done
if ! kill -0 "$PID" 2>/dev/null; then
    echo "FAIL server did not start"; tail -n 20 "$WORK/server.log"; exit 1
fi

fail() { echo "FAIL $1"; tail -n 30 "$WORK/server.log"; exit 1; }

OUT1="$("$BIN/client_smoke" --host "$HOST" --port "$PORT" --login "$LOGIN" \
    --password "$PASSWORD" --register --patch-weight 71)"
echo "$OUT1"
grep -q "handshake_ok" <<<"$OUT1" || fail "handshake"
grep -q "profile_ok name=我 source=service" <<<"$OUT1" || fail "profile.get"
grep -q "patch_ok" <<<"$OUT1" || fail "profile.patch"
grep -q "verify_weight=71" <<<"$OUT1" || fail "patch persistence"

OUT2="$("$BIN/client_smoke" --host "$HOST" --port "$PORT" --login "$LOGIN" \
    --password "$PASSWORD" --patch-weight 90 --omit-idem)"
echo "$OUT2"
grep -q "patch_error code=3001" <<<"$OUT2" || fail "missing idem_key"

OUT3="$("$BIN/client_smoke" --host "$HOST" --port "$PORT" --token dummy --skip-handshake)"
echo "$OUT3"
grep -q "pre_handshake_error code=2001" <<<"$OUT3" || fail "handshake_required"

OUT4="$("$BIN/client_smoke" --host "$HOST" --port "$PORT" --token invalid-token || true)"
echo "$OUT4"
grep -q "handshake_error code=2002" <<<"$OUT4" || fail "unauthorized"

echo "S1_INTEGRATION_OK"