#!/usr/bin/env bash
# Regression tests for ./build.sh host detection and preset selection (no cmake).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/build.sh"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

if [ -x "$SCRIPT" ]; then
    pass "build.sh is executable"
else
    fail "build.sh is executable"
fi

if bash -n "$SCRIPT"; then
    pass "build.sh is valid bash"
else
    fail "build.sh is valid bash"
fi

run_dry() {
    local out="$1"
    shift
    env "$@" "$SCRIPT" --dry-run >"$out" 2>&1
}

if run_dry "$TMPDIR/aarch64" DOGGY_HOST_MACHINE=aarch64; then
    pass "aarch64 dry-run exits 0"
else
    fail "aarch64 dry-run exits 0"
fi

if grep -q 'cmake --preset native-release' "$TMPDIR/aarch64" \
        && grep -q 'cmake --build --preset native-release --target package' "$TMPDIR/aarch64" \
        && ! grep -q ubuntu-aarch64-cross "$TMPDIR/aarch64"; then
    pass "aarch64 uses native-release package"
else
    fail "aarch64 uses native-release package"
fi

if run_dry "$TMPDIR/arm64" DOGGY_HOST_MACHINE=arm64 \
        && grep -q 'cmake --preset native-release' "$TMPDIR/arm64" \
        && ! grep -q ubuntu-aarch64-cross "$TMPDIR/arm64"; then
    pass "arm64 uses native-release package"
else
    fail "arm64 uses native-release package"
fi

if run_dry "$TMPDIR/x86" DOGGY_HOST_MACHINE=x86_64; then
    pass "x86_64 dry-run exits 0 without sysroot"
else
    fail "x86_64 dry-run exits 0 without sysroot"
fi

if grep -q 'cmake --preset ubuntu-aarch64-cross' "$TMPDIR/x86" \
        && grep -q 'cmake --build --preset ubuntu-aarch64-cross --target package' "$TMPDIR/x86" \
        && ! grep -q native-release "$TMPDIR/x86" \
        && ! grep -q DOGGY_SYSROOT "$TMPDIR/x86"; then
    pass "x86_64 uses ubuntu-aarch64-cross without sysroot"
else
    fail "x86_64 uses ubuntu-aarch64-cross without sysroot"
fi

if "$SCRIPT" --dry-run --not-a-flag >"$TMPDIR/bad-arg" 2>&1; then
    fail "unknown argument exits non-zero"
else
    pass "unknown argument exits non-zero"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All build.sh tests passed."
exit 0
