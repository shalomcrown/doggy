#!/usr/bin/env bash
# Regression tests for ./doggy SSH helper (no live SSH).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/doggy"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

if [ -x "$SCRIPT" ]; then
    pass "doggy script is executable"
else
    fail "doggy script is executable"
fi

if "$SCRIPT" --dry-run >"$TMPDIR/no-host" 2>&1; then
    fail "missing host exits non-zero"
else
    pass "missing host exits non-zero"
fi

if "$SCRIPT" --dry-run -- '-oProxyCommand=x' >"$TMPDIR/dash-host" 2>&1; then
    fail "ssh option-shaped host is rejected"
else
    pass "ssh option-shaped host is rejected"
fi

mkdir -p "$TMPDIR/with-zssh"
printf '#!/bin/sh\nexit 0\n' >"$TMPDIR/with-zssh/zssh"
chmod +x "$TMPDIR/with-zssh/zssh"

if PATH="$TMPDIR/with-zssh:$PATH" "$SCRIPT" --dry-run doggy-1.local \
        >"$TMPDIR/zssh-direct" 2>&1; then
    pass "dry-run with host exits 0"
else
    fail "dry-run with host exits 0"
fi

if grep -q zssh "$TMPDIR/zssh-direct" \
        && grep -q 'zssh --' "$TMPDIR/zssh-direct" \
        && grep -q 'doggy@doggy-1.local' "$TMPDIR/zssh-direct" \
        && grep -q StrictHostKeyChecking=no "$TMPDIR/zssh-direct"; then
    pass "prefers zssh as doggy@host without host-key prompts"
else
    fail "prefers zssh as doggy@host without host-key prompts"
fi

if grep -q UserKnownHostsFile=/dev/null "$TMPDIR/zssh-direct"; then
    pass "does not use the real known_hosts file"
else
    fail "does not use the real known_hosts file"
fi

if PATH="$TMPDIR/with-zssh:$PATH" "$SCRIPT" --dry-run other@doggy-1.local \
        >"$TMPDIR/zssh-user" 2>&1; then
    if grep -q 'doggy@doggy-1.local' "$TMPDIR/zssh-user" \
            && grep -q 'other@' "$TMPDIR/zssh-user"; then
        fail "target login user is always doggy"
    elif grep -q 'doggy@doggy-1.local' "$TMPDIR/zssh-user"; then
        pass "target login user is always doggy"
    else
        fail "target login user is always doggy"
    fi
else
    fail "target login user is always doggy"
fi

if PATH="$TMPDIR/with-zssh:$PATH" \
        "$SCRIPT" --dry-run pi.local gw.example jump2 \
        >"$TMPDIR/jumps" 2>&1; then
    if grep -q -- '-J' "$TMPDIR/jumps" \
            && grep -q 'gw.example' "$TMPDIR/jumps" \
            && grep -q 'jump2' "$TMPDIR/jumps" \
            && grep -q 'doggy@pi.local' "$TMPDIR/jumps"; then
        pass "extra args are ProxyJump hops in order"
    else
        fail "extra args are ProxyJump hops in order"
    fi
else
    fail "extra args are ProxyJump hops in order"
fi

mkdir -p "$TMPDIR/ssh-only"
printf '#!/bin/sh\nexit 0\n' >"$TMPDIR/ssh-only/ssh"
chmod +x "$TMPDIR/ssh-only/ssh"
ln -sf "$(command -v bash)" "$TMPDIR/ssh-only/bash"

if PATH="$TMPDIR/ssh-only" "$SCRIPT" --dry-run doggy-1.local \
        >"$TMPDIR/ssh-fallback" 2>&1; then
    if grep -q zssh "$TMPDIR/ssh-fallback"; then
        fail "falls back to ssh when zssh is missing"
    elif grep -q ssh "$TMPDIR/ssh-fallback" \
            && grep -q 'doggy@doggy-1.local' "$TMPDIR/ssh-fallback"; then
        pass "falls back to ssh when zssh is missing"
    else
        fail "falls back to ssh when zssh is missing"
    fi
else
    fail "falls back to ssh when zssh is missing"
fi

if grep -Eq '\[DRY-RUN\]' "$TMPDIR/zssh-direct"; then
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

echo "All doggy SSH helper tests passed."
exit 0
