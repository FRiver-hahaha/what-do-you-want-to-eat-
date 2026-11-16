#!/usr/bin/env bash
set -uo pipefail
P8_ROOT="${P8_WORK_ROOT:-/home/m/eat-p8-s1}"
P8_BACKEND="${P8_BACKEND_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
P8_TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

D="$P8_BACKEND"
EV="$D/evidence/p8-s2"
mkdir -p "$EV"

pkill -f "build/bin/server_http" 2>/dev/null
sleep 0.5

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

echo "=== http tests ==="
python3 "$P8_TESTS/p8s2_http_test.py" 2>&1 | tee "$EV/02-http-tests.log"
echo "TEST_RC=${PIPESTATUS[0]}"
exit 0