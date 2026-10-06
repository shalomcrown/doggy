#!/usr/bin/env bash
# Regression: the simulator wrapper builds the host executable, not a Pi package.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$ROOT/build-simulator.sh"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

if grep -q '^#!/usr/bin/env bash' "$SCRIPT"; then
    pass "build-simulator.sh is bash"
else
    fail "build-simulator.sh is bash"
fi

if grep -q 'set -eu' "$SCRIPT"; then
    pass "build-simulator.sh uses set -eu"
else
    fail "build-simulator.sh uses set -eu"
fi

if grep -q 'ubuntu-aarch64-cross' "$SCRIPT" || grep -q -- '--target package' "$SCRIPT"; then
    fail "build-simulator.sh must not package Pi firmware"
else
    pass "build-simulator.sh does not package Pi firmware"
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

if "$SCRIPT" --dry-run >"$TMPDIR/out" 2>&1 \
        && grep -q 'cmake --preset simulator' "$TMPDIR/out" \
        && grep -q 'cmake --build --preset simulator --target doggy-sim' "$TMPDIR/out" \
        && grep -q 'doggy-gz-bridge' "$TMPDIR/out"; then
    fail "dry-run configures the simulator preset without a bridge target"
else
    if grep -q 'cmake --preset simulator' "$TMPDIR/out" \
            && grep -q 'cmake --build --preset simulator --target doggy-sim' "$TMPDIR/out"; then
        pass "dry-run configures the simulator preset and doggy-sim"
    else
        fail "dry-run configures the simulator preset and doggy-sim"
    fi
fi

if [ "$FAILS" -eq 0 ]; then
    exit 0
fi
exit 1
