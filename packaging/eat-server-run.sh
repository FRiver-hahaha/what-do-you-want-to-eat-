#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG="${EAT_CONFIG:-$DIR/server.toml}"
if [[ ! -f "$CONFIG" ]]; then
    cp "$DIR/server.toml.example" "$CONFIG"
    echo "[run] created $CONFIG (edit mysql_host/mysql_db before production use)"
fi
if [[ -z "${EAT_MYSQL_PASSWORD:-}" ]]; then
    echo "[run] EAT_MYSQL_PASSWORD is not set" >&2
    exit 1
fi
exec "$DIR/eat-server" --config "$CONFIG"