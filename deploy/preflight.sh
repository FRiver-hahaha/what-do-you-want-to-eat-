#!/usr/bin/env bash
set -euo pipefail

fail() {
  echo "preflight-failed: $1"
  exit 1
}

command -v mysql >/dev/null 2>&1 || fail "mysql-client-missing"
command -v redis-cli >/dev/null 2>&1 || fail "redis-cli-missing"

nofile=$(ulimit -n)
[ "$nofile" -ge 1500000 ] || fail "nofile-too-small:$nofile"

nr_open=$(cat /proc/sys/fs/nr_open)
[ "$nr_open" -ge 2000000 ] || fail "fs.nr_open-too-small:$nr_open"

somaxconn=$(cat /proc/sys/net/core/somaxconn)
[ "$somaxconn" -ge 65535 ] || fail "somaxconn-too-small:$somaxconn"

redis_instances=$(pgrep -c redis-server || true)
[ "$redis_instances" -le 1 ] || fail "multiple-redis-instances:$redis_instances"

if [ "$redis_instances" -eq 1 ]; then
  redis_pubsub=$(redis-cli INFO clients | awk -F: '/pubsub_clients/{print $2}' | tr -d '\r')
  [ "${redis_pubsub:-0}" = "0" ] || fail "redis-pubsub-active:$redis_pubsub"
fi

if command -v ss >/dev/null 2>&1; then
  ss -ltn | grep -q ':9443 ' || fail "tcp-port-not-listening:9443"
fi

echo "preflight-ok"