#!/usr/bin/env bash
set -uo pipefail
P8_ROOT="${P8_WORK_ROOT:-/home/m/eat-p8-s1}"
P8_BACKEND="${P8_BACKEND_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
P8_TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

D="$P8_BACKEND"
EV="$D/evidence/p8-s3"
mkdir -p "$EV"

MYSQL=(mysql --socket=/run/mysqld/mysqld.sock -uroot --default-character-set=utf8mb4 chatroom_p8)

pkill -f "build/bin/server_http" 2>/dev/null
sleep 0.5

echo "=== S3 fixture ==="
"${MYSQL[@]}" < $P8_TESTS/s3_fixture.sql 2>&1 | tee "$EV/00-fixture.log"
echo "FIXTURE_RC=${PIPESTATUS[0]}"

nohup bash "$P8_TESTS/start_s2.sh" > "$EV/01-server.log" 2>&1 &
for _ in $(seq 1 80); do
  if curl -sf http://127.0.0.1:18080/health >/dev/null 2>&1; then
    break
  fi
  sleep 0.25
done

echo "=== health ==="
curl -s http://127.0.0.1:18080/health
echo

echo "=== S3 HTTP tests ==="
python3 $P8_TESTS/p8s3_http_test.py 2>&1 | tee "$EV/02-http-tests.log"
echo "HTTP_RC=${PIPESTATUS[0]}"

echo "=== S3 Agent contract ==="
python3 $P8_TESTS/agent_s3_contract_test.py 2>&1 | tee "$EV/03-agent-contract.log"
echo "AGENT_RC=${PIPESTATUS[0]}"

exit 0