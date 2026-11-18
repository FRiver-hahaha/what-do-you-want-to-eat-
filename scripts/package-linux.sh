#!/usr/bin/env bash
# Produce distributable archives under dist/ (binaries are NOT committed).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
DIST="$ROOT/dist"
SERVER_DIR="$DIST/eat-server-linux-x86_64"
CLIENT_DIR="$DIST/eat-client-windows"

if [[ ! -x "$BUILD_DIR/eat-server" ]]; then
    echo "missing $BUILD_DIR/eat-server; run scripts/build-linux.sh first" >&2
    exit 1
fi

rm -rf "$SERVER_DIR" "$CLIENT_DIR"
mkdir -p "$SERVER_DIR/migrations" "$CLIENT_DIR/tests/integration" "$CLIENT_DIR/shared" "$CLIENT_DIR/assets"

cp "$BUILD_DIR/eat-server" "$BUILD_DIR/client_smoke" "$SERVER_DIR/"
cp "$ROOT/deploy/server.toml.example" "$SERVER_DIR/"
cp "$ROOT/packaging/eat-server-run.sh" "$SERVER_DIR/run.sh"
cp "$ROOT/packaging/README-server.md" "$SERVER_DIR/README.md"
chmod +x "$SERVER_DIR/eat-server" "$SERVER_DIR/client_smoke" "$SERVER_DIR/run.sh"

{
    echo "CREATE DATABASE IF NOT EXISTS eat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
    echo "USE eat;"
    sed 's/chatroom/eat/g' "$ROOT/migrations/base/000_init.sql" | tr -d '\r'; echo
    echo "SET @seed_pwd_hash = SHA2('disabled-login', 256);"
    tr -d '\r' < "$ROOT/migrations/base/001_p8_business.sql" \
        | sed 's/^--\([^ ]\)/-- \1/'; echo
    tr -d '\r' < "$ROOT/migrations/base/foods_seed.sql" \
        | sed 's/^--\([^ ]\)/-- \1/'; echo
    for f in "$ROOT"/migrations/add/00{2,3,4,5,6,7}_*.sql; do
        case "$f" in *_rollback.sql) continue;; esac
        tr -d '\r' < "$f" | sed 's/^--\([^ ]\)/-- \1/'; echo
    done
} > "$SERVER_DIR/migrations/00_all.sql"

cp "$ROOT/packaging/README-client.md" "$CLIENT_DIR/README.md"
cp "$ROOT/packaging/build-client.ps1" "$CLIENT_DIR/build-client.ps1"

cp "$ROOT/tests/integration/client_smoke.cc" "$CLIENT_DIR/tests/integration/"
cp -r "$ROOT/shared/protocol" "$CLIENT_DIR/shared/"
cp "$ROOT"/bridge.js "$ROOT"/index.html "$ROOT"/app.js "$ROOT"/agent-adapter.js \
   "$ROOT"/styles.css "$ROOT"/features.css "$ROOT"/agent.css "$CLIENT_DIR/"
cp -r "$ROOT"/assets/. "$CLIENT_DIR/assets/"
cp -r "$ROOT/client" "$ROOT/agent" "$CLIENT_DIR/"
find "$CLIENT_DIR/agent" -name '__pycache__' -type d -prune -exec rm -rf {} + 2>/dev/null || true
rm -rf "$CLIENT_DIR/agent/tests" "$CLIENT_DIR/agent/data"/*.db

tar -czf "$DIST/eat-server-linux-x86_64.tar.gz" -C "$DIST" eat-server-linux-x86_64
rm -f "$DIST/eat-client-windows.zip"
(cd "$CLIENT_DIR" && python3 -m zipfile -c ../eat-client-windows.zip .)

echo "packaged:"
ls -lh "$DIST/eat-server-linux-x86_64.tar.gz" "$DIST/eat-client-windows.zip"
