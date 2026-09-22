#!/usr/bin/env bash
# Contract tests for the optional PlatformIO watch build entry point.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/build-watch.sh"
PLATFORMIO="$ROOT/watch/platformio.ini"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# ================================================================================

if [ -x "$SCRIPT" ]; then
    pass "build-watch.sh is executable"
else
    fail "build-watch.sh is executable"
fi

if bash -n "$SCRIPT"; then
    pass "build-watch.sh is valid bash"
else
    fail "build-watch.sh is valid bash"
fi

if grep -q 'twatch-s3' "$SCRIPT" \
        && grep -q 'waveshare-c6' "$SCRIPT" \
        && grep -q -- '--project-dir' "$SCRIPT"; then
    pass "build-watch.sh names both PlatformIO environments"
else
    fail "build-watch.sh names both PlatformIO environments"
fi

if "$SCRIPT" --dry-run all >"$TMPDIR/dry-run.log" 2>&1 \
        && grep -q 'pio run.*twatch-s3' "$TMPDIR/dry-run.log" \
        && grep -q 'pio run.*waveshare-c6' "$TMPDIR/dry-run.log"; then
    pass "all dry-run builds both watches"
else
    fail "all dry-run builds both watches"
fi

if "$SCRIPT" --dry-run unknown >/dev/null 2>&1; then
    fail "unknown watch exits non-zero"
else
    pass "unknown watch exits non-zero"
fi

if [ -f "$PLATFORMIO" ] \
        && grep -q '^\[env:twatch-s3\]$' "$PLATFORMIO" \
        && grep -q '^\[env:waveshare-c6\]$' "$PLATFORMIO"; then
    pass "PlatformIO declares both watch environments"
else
    fail "PlatformIO declares both watch environments"
fi

if grep -q "pipx install 'platformio>=6.2.0'" "$ROOT/install-prereqs.sh"; then
    pass "install-prereqs.sh installs PlatformIO with pipx"
else
    fail "install-prereqs.sh installs PlatformIO with pipx"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All build-watch.sh tests passed."
