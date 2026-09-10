#!/usr/bin/env bash
# Regression: operator wrapper packages linux DEB and/or Windows NSIS via CPack.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$ROOT/build-operator.sh"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

if grep -q '^#!/usr/bin/env bash' "$SCRIPT"; then
    pass "build-operator.sh is bash"
else
    fail "build-operator.sh is bash"
fi

if grep -q 'set -eu' "$SCRIPT"; then
    pass "build-operator.sh uses set -eu"
else
    fail "build-operator.sh uses set -eu"
fi

if grep -q -- 'operator-linux-amd64' "$SCRIPT" \
        && grep -q -- '--target package' "$SCRIPT"; then
    pass "linux path uses operator-linux-amd64 package"
else
    fail "linux path uses operator-linux-amd64 package"
fi

if grep -q -- 'operator-windows-amd64' "$SCRIPT" \
        && grep -q -- '--target package' "$SCRIPT"; then
    pass "windows path uses operator-windows-amd64 package"
else
    fail "windows path uses operator-windows-amd64 package"
fi

if grep -q 'ubuntu-aarch64-cross' "$SCRIPT" || grep -q 'native-release' "$SCRIPT"; then
    fail "build-operator.sh must not package Pi firmware"
else
    pass "build-operator.sh does not package Pi firmware"
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

if "$SCRIPT" --dry-run linux >"$TMPDIR/linux" 2>&1 \
        && grep -q 'cmake --preset operator-linux-amd64' "$TMPDIR/linux" \
        && grep -q 'cmake --build --preset operator-linux-amd64 --target package' "$TMPDIR/linux" \
        && ! grep -q operator-windows-amd64 "$TMPDIR/linux"; then
    pass "dry-run linux only configures linux operator"
else
    fail "dry-run linux only configures linux operator"
fi

if "$SCRIPT" --dry-run windows >"$TMPDIR/win" 2>&1 \
        && grep -q 'cmake --preset operator-windows-amd64' "$TMPDIR/win" \
        && ! grep -q operator-linux-amd64 "$TMPDIR/win"; then
    pass "dry-run windows only configures windows operator"
else
    fail "dry-run windows only configures windows operator"
fi

if "$SCRIPT" --dry-run all >"$TMPDIR/all" 2>&1 \
        && grep -q operator-linux-amd64 "$TMPDIR/all" \
        && grep -q operator-windows-amd64 "$TMPDIR/all"; then
    pass "dry-run all packages both clients"
else
    fail "dry-run all packages both clients"
fi

if "$SCRIPT" --dry-run --not-a-flag >"$TMPDIR/bad" 2>&1; then
    fail "unknown argument exits non-zero"
else
    pass "unknown argument exits non-zero"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi
echo "All build-operator.sh tests passed."
exit 0
