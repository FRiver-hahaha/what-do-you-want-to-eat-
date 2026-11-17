#!/usr/bin/env bash
# Build and test the Linux side (server + protocol + tests).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MUDUO_ROOT="${EAT_MUDUO_ROOT:-${MUDUO_ROOT:-}}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

if [[ -z "$MUDUO_ROOT" ]]; then
    if [[ -d "$ROOT/../muduo/muduo/net" ]]; then
        MUDUO_ROOT="$ROOT/../muduo"
    else
        echo "EAT_MUDUO_ROOT is required (path to a Muduo source tree)" >&2
        exit 1
    fi
fi

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DEAT_MUDUO_ROOT="$MUDUO_ROOT"
cmake --build "$BUILD_DIR" -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure

if command -v python3 >/dev/null 2>&1; then
    (cd "$ROOT" && EAT_DISABLE_DOTENV=1 EAT_MCP_DISABLE=1 \
        python3 -m unittest agent.tests.test_smoke)
fi

if command -v node >/dev/null 2>&1; then
    for file in app.js bridge.js agent-adapter.js; do
        node --check "$ROOT/$file"
    done
fi

echo "linux build + tests OK ($BUILD_TYPE)"
