#!/usr/bin/env bash
set -uo pipefail
P8_ROOT="${P8_WORK_ROOT:-/home/m/eat-p8-s1}"
P8_BACKEND="${P8_BACKEND_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
P8_TESTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

EV=$P8_BACKEND/evidence/p8-s4
MYSQL=(mysql --socket=/run/mysqld/mysqld.sock -uroot chatroom_p8)
mkdir -p "$EV"

"${MYSQL[@]}" < $P8_TESTS/reset_profile.sql

echo "=== S2 suite (clean profile baseline) ==="
python3 $P8_TESTS/p8s2_http_test.py 2>&1 | tee "$EV/05-s2-regression.log"
echo "S2_RC=${PIPESTATUS[0]}"

"${MYSQL[@]}" < $P8_TESTS/reset_profile.sql

echo "=== S1 regression (clean profile baseline) ==="
python3 $P8_TESTS/p8s1_http_test.py 2>&1 | tee "$EV/06-s1-regression.log"
echo "S1_RC=${PIPESTATUS[0]}"

echo "=== S2 DB verification ==="
python3 $P8_TESTS/verify_s2_db.py 2>&1 | tee "$EV/07-s2-db-verify.log"
echo "DB_S2_RC=${PIPESTATUS[0]}"

echo "=== S3 HTTP regression ==="
python3 $P8_TESTS/p8s3_http_test.py 2>&1 | tee "$EV/08-s3-regression.log"
echo "S3_RC=${PIPESTATUS[0]}"

echo "=== S3 Agent regression ==="
python3 $P8_TESTS/agent_s3_contract_test.py 2>&1 | tee "$EV/09-s3-agent-regression.log"
echo "S3_AGENT_RC=${PIPESTATUS[0]}"

echo "=== S3 DB verification ==="
python3 $P8_TESTS/verify_s3_db.py 2>&1 | tee "$EV/10-s3-db-verify.log"
echo "DB_S3_RC=${PIPESTATUS[0]}"

echo "=== evidence files ==="
ls -l "$EV"