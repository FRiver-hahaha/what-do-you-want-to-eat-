#!/usr/bin/env bash
set -euo pipefail

DB="${1:-}"
if [[ -z "$DB" ]]; then
  echo "usage: $0 <isolated-test-database>" >&2
  exit 2
fi

# 迁移只允许独立测试库；已知业务库一律拒绝。
case "$DB" in
  chatroom|chatroom_p7|chatroom_s0|chatroom_int)
    echo "refuse: '$DB' is a known non-isolated database" >&2
    exit 5
    ;;
esac

SOCKET="${CHATROOM_MYSQL_SOCKET:-/run/mysqld/mysqld.sock}"
MYSQL=(mysql --socket="$SOCKET" -uroot "$DB")
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

q() { "${MYSQL[@]}" -N -e "$1"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

# 前置：目标库必须是已初始化 user 表的独立库。
[[ "$(q "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='user'")" == "1" ]] \
  || fail "user table missing; initialize the isolated database first"

# Step 0B：同名表必须全部带迁移标记，否则视为来源不明并中止。
read -r existing marked <<< "$(q "SELECT COUNT(*), COALESCE(SUM(TABLE_COMMENT LIKE 'p8:migration=001\\_p8\\_business%'),0) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods')")"
if [[ "$existing" != "0" && "$marked" != "$existing" ]]; then
  fail "unmarked same-name tables detected (existing=$existing marked=$marked); manual review required"
fi

# user.agent_uid 若已存在，必须带迁移标记。
col_exists=$(q "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='user' AND COLUMN_NAME='agent_uid'")
if [[ "$col_exists" == "1" ]]; then
  col_marked=$(q "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='user' AND COLUMN_NAME='agent_uid' AND COLUMN_COMMENT LIKE 'p8:migration=001\\_p8\\_business%'")
  [[ "$col_marked" == "1" ]] || fail "user.agent_uid exists without migration marker; manual review required"
fi

# Step 4：u_demo seed 状态只允许“全新”或“已完成且一致”，其余一律中止。
# 回滚后 agent_uid 列可能已不存在，此时只按 email 判定并进入冲突分支。
if [[ "$col_exists" == "1" ]]; then
  read -r email_hit uid_hit same_hit <<< "$(q "SELECT (SELECT COUNT(*) FROM \`user\` WHERE email='u_demo@agent.local'), (SELECT COUNT(*) FROM \`user\` WHERE agent_uid='u_demo'), (SELECT COUNT(*) FROM \`user\` WHERE email='u_demo@agent.local' AND agent_uid='u_demo')")"
else
  email_hit=$(q "SELECT COUNT(*) FROM \`user\` WHERE email='u_demo@agent.local'")
  uid_hit=0
  same_hit=0
fi
if [[ "$email_hit" == "0" && "$uid_hit" == "0" ]]; then
  :
elif [[ "$email_hit" == "1" && "$uid_hit" == "1" && "$same_hit" == "1" ]]; then
  :
else
  fail "u_demo seed state conflict (email_hit=$email_hit uid_hit=$uid_hit same_hit=$same_hit); manual review required"
fi

# Step 1–6：变量与迁移、种子必须在同一 mysql 会话内执行。
SEED_SHA=$(head -c 32 /dev/urandom | sha256sum | cut -d' ' -f1)
{
  echo "SET @seed_pwd_hash='$SEED_SHA';"
  cat "$DIR/001_p8_business.sql"
  cat "$DIR/foods_seed.sql"
} | "${MYSQL[@]}"

# Step 7：迁移后断言。
[[ "$(q "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='user' AND COLUMN_NAME='agent_uid'")" == "1" ]] \
  || fail "agent_uid column missing after migration"
[[ "$(q "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods') AND TABLE_COMMENT LIKE 'p8:migration=001\\_p8\\_business%'")" == "4" ]] \
  || fail "expected 4 marked tables"
IDX=$(q "SELECT COUNT(DISTINCT CONCAT(TABLE_NAME,'.',INDEX_NAME)) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND ((TABLE_NAME='user' AND INDEX_NAME='uk_agent_uid') OR (TABLE_NAME='health_profile' AND INDEX_NAME='PRIMARY') OR (TABLE_NAME='meal_records' AND INDEX_NAME IN ('PRIMARY','idx_uid_date')) OR (TABLE_NAME='idempotency_records' AND INDEX_NAME IN ('PRIMARY','uk_uid_key','idx_created_at')) OR (TABLE_NAME='foods' AND INDEX_NAME='PRIMARY'))")
[[ "$IDX" == "8" ]] || fail "expected 8 distinct indexes, got $IDX"
[[ "$(q "SELECT COUNT(*) FROM foods")" == "16" ]] || fail "expected 16 foods"
[[ "$(q "SELECT COUNT(*) FROM \`user\` WHERE agent_uid='u_demo'")" == "1" ]] || fail "u_demo row missing"

echo "OK: migration applied and verified on $DB"