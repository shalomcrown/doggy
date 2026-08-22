#!/usr/bin/env bash
# Regression tests for per-build version stamp (timestamp + short git hash).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/cmake/doggy-version.cmake"
CMAKE="$ROOT/CMakeLists.txt"
CPACK_STAMP="$ROOT/cmake/cpack-stamp.cmake"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

if [ -f "$SCRIPT" ]; then
    pass "cmake/doggy-version.cmake exists"
else
    fail "cmake/doggy-version.cmake exists"
fi

if grep -q 'CPACK_PROJECT_CONFIG_FILE' "$CMAKE" \
        && grep -q 'cpack-stamp.cmake' "$CMAKE"; then
    pass "CMakeLists uses CPack project config to restamp at package time"
else
    fail "CMakeLists uses CPack project config to restamp at package time"
fi

if grep -q 'doggy_version_stamp' "$CMAKE" && grep -q 'ALL' "$CMAKE"; then
    pass "CMakeLists stamps version on every build"
else
    fail "CMakeLists stamps version on every build"
fi

if [ -f "$CPACK_STAMP" ] && grep -q 'doggy-version.cmake' "$CPACK_STAMP"; then
    pass "cpack-stamp.cmake includes doggy-version.cmake"
else
    fail "cpack-stamp.cmake includes doggy-version.cmake"
fi

HEADER="$TMPDIR/doggy_version.h"
if [ -f "$SCRIPT" ] && cmake \
        -DDOGGY_VERSION_BASE=1.0.0 \
        -DDOGGY_GIT_ROOT="$ROOT" \
        -DDOGGY_VERSION_HEADER="$HEADER" \
        -P "$SCRIPT"; then
    pass "stamp script writes a header"
else
    fail "stamp script writes a header"
fi

if [ -f "$HEADER" ] && grep -Eq \
        '#define DOGGY_VERSION "1\.0\.0-[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{4}-[0-9a-f]+"' \
        "$HEADER"; then
    pass "version is 1.0.0-YYYY-MM-DD-HHMM-<githash>"
else
    fail "version is 1.0.0-YYYY-MM-DD-HHMM-<githash>"
fi

if [ -f "$HEADER" ] && command -v git >/dev/null 2>&1; then
    expected=$(git -C "$ROOT" rev-parse --short HEAD)
    if grep -q -- "-${expected}\"" "$HEADER"; then
        pass "header git hash matches rev-parse --short HEAD"
    else
        fail "header git hash matches rev-parse --short HEAD"
    fi
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%s\n' "---- captured ----"
    if [ -f "$HEADER" ]; then
        cat "$HEADER"
    fi

    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All version-stamp tests passed."
exit 0
