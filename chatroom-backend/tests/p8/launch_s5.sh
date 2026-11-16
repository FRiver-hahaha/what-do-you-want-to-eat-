#!/usr/bin/env bash
set -uo pipefail
P8_ROOT="${P8_WORK_ROOT:-/home/m/eat-p8-s1}"
P8_BACKEND="${P8_BACKEND_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
P8_TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

D="$P8_BACKEND"
EV="$D/evidence/p8-s5"
mkdir -p "$EV"

MYSQL=(mysql --socket=/run/mysqld/mysqld.sock -uroot --default-character-set=utf8mb4 chatroom_p8)

pkill -f "build/bin/server_http" 2>/dev/null
sleep 0.5

echo "=== S5 fixture: 复位 u_demo 档案基线 ==="
"${MYSQL[@]}" < $P8_TESTS/reset_profile.sql 2>&1 | tee "$EV/00-reset.log"
echo "RESET_RC=${PIPESTATUS[0]}"

setsid nohup bash "$P8_TESTS/start_s2.sh" > "$EV/01-server.log" 2>&1 < /dev/null &
for _ in $(seq 1 80); do
  if curl -sf http://127.0.0.1:18080/health >/dev/null 2>&1; then
    break
  fi
  sleep 0.25
done

echo "=== health ==="
curl -s http://127.0.0.1:18080/health
echo

export S5_RUN="${S5_RUN:-$(date +%Y%m%d%H%M%S)}"
echo "$S5_RUN" > /tmp/s5_run
echo "S5_RUN=$S5_RUN"

echo "=== S5 E2E (链路 + 一致性) ==="
python3 $P8_TESTS/s5_e2e_test.py 2>&1 | tee "$EV/02-e2e.log"
echo "E2E_RC=${PIPESTATUS[0]}"

echo "=== S5 exception/边界 ==="
python3 $P8_TESTS/s5_exception_test.py 2>&1 | tee "$EV/03-exception.log"
echo "EXC_RC=${PIPESTATUS[0]}"

echo "=== S5 DB verify ==="
python3 $P8_TESTS/verify_s5_db.py 2>&1 | tee "$EV/04-db-verify.log"
echo "DB_RC=${PIPESTATUS[0]}"

echo "=== S5 restart: write phase ==="
python3 $P8_TESTS/s5_restart_check.py write 2>&1 | tee "$EV/05-restart-write.log"
echo "RESTART_WRITE_RC=${PIPESTATUS[0]}"

exit 0