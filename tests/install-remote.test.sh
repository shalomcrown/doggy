#!/usr/bin/env bash
# Regression tests for ./install.sh (no live SSH).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/install.sh"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

if [ -x "$SCRIPT" ]; then
    pass "install.sh is executable"
else
    fail "install.sh is executable"
fi

# ── missing host ─────────────────────────────────────────────────────────────
if "$SCRIPT" --dry-run >"$TMPDIR/no-host" 2>&1; then
    fail "missing host exits non-zero"
else
    pass "missing host exits non-zero"
fi

# ── .deb used as host ────────────────────────────────────────────────────────
printf 'fake' >"$TMPDIR/used-as-host.deb"
if "$SCRIPT" --dry-run "$TMPDIR/used-as-host.deb" >"$TMPDIR/deb-as-host" 2>&1; then
    fail "a .deb path as host is rejected"
else
    pass "a .deb path as host is rejected"
fi

# ── missing package ──────────────────────────────────────────────────────────
mkdir -p "$TMPDIR/empty-build"
if DOGGY_DEB_ROOT="$TMPDIR/empty-build" "$SCRIPT" --dry-run pi@host \
        >"$TMPDIR/no-deb" 2>&1; then
    fail "no .deb under DOGGY_DEB_ROOT exits non-zero"
else
    pass "no .deb under DOGGY_DEB_ROOT exits non-zero"
fi

if grep -qi package "$TMPDIR/no-deb"; then
    pass "missing .deb mentions how to package"
else
    fail "missing .deb mentions how to package"
fi

# ── host starting with dash ──────────────────────────────────────────────────
printf 'fake' >"$TMPDIR/ok.deb"
if "$SCRIPT" --dry-run -- '-oProxyCommand=x' "$TMPDIR/ok.deb" \
        >"$TMPDIR/dash-host" 2>&1; then
    fail "ssh option-shaped host is rejected"
else
    pass "ssh option-shaped host is rejected"
fi

# ── explicit deb + dry-run ───────────────────────────────────────────────────
if "$SCRIPT" --dry-run pi@robot "$TMPDIR/ok.deb" >"$TMPDIR/dry" 2>&1; then
    pass "dry-run with explicit .deb exits 0"
else
    fail "dry-run with explicit .deb exits 0"
fi

if grep -q 'scp' "$TMPDIR/dry" && grep -q 'ok.deb' "$TMPDIR/dry"; then
    pass "dry-run prints scp of the .deb"
else
    fail "dry-run prints scp of the .deb"
fi

if grep -q 'apt-get install' "$TMPDIR/dry" && grep -q '/tmp/ok.deb' "$TMPDIR/dry"; then
    pass "dry-run prints apt-get install of /tmp/<deb>"
else
    fail "dry-run prints apt-get install of /tmp/<deb>"
fi

if grep -Eq 'ssh[[:space:]]+pi@robot' "$TMPDIR/dry"; then
    pass "dry-run prints ssh to the given host"
else
    fail "dry-run prints ssh to the given host"
fi

# ── newest .deb under DOGGY_DEB_ROOT ─────────────────────────────────────────
mkdir -p "$TMPDIR/pkgs"
printf 'old' >"$TMPDIR/pkgs/old.deb"
printf 'new' >"$TMPDIR/pkgs/new.deb"
touch -d '2020-01-01 00:00:00' "$TMPDIR/pkgs/old.deb"
touch -d '2026-08-22 00:00:00' "$TMPDIR/pkgs/new.deb"

if DOGGY_DEB_ROOT="$TMPDIR/pkgs" "$SCRIPT" --dry-run user@pi \
        >"$TMPDIR/newest" 2>&1; then
    pass "dry-run with auto-detected .deb exits 0"
else
    fail "dry-run with auto-detected .deb exits 0"
fi

if grep -q 'new.deb' "$TMPDIR/newest" && grep -q 'old.deb' "$TMPDIR/newest"; then
    fail "auto-detect uses only the newest .deb"
elif grep -q 'new.deb' "$TMPDIR/newest"; then
    pass "auto-detect uses only the newest .deb"
else
    fail "auto-detect uses only the newest .deb"
fi

# ── no live transport in dry-run ─────────────────────────────────────────────
if grep -Eq '\[DRY-RUN\]' "$TMPDIR/dry"; then
    pass "dry-run lines are marked [DRY-RUN]"
else
    fail "dry-run lines are marked [DRY-RUN]"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%s\n' "---- captured ----"
    for f in "$TMPDIR"/*; do
        [ -f "$f" ] || continue
        printf '== %s ==\n' "$(basename "$f")"
        cat "$f"
    done
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All install.sh tests passed."
exit 0
