#!/usr/bin/env bash
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FAILS=0
pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT
recordings="$TMPDIR/recordings"
snapshots="$TMPDIR/snapshots"
mkdir -p "$recordings" "$snapshots"
printf '{}' >"$TMPDIR/doggy.json"
# Python json will default retain_hours 24; stamp old/new files.
old="$recordings/pi-2020-01-01-000000.ts"
new="$recordings/pi-2099-01-01-000000.ts"
printf 'old' >"$old"
printf 'new' >"$new"
touch -d '2020-01-01' "$old"

if DOGGY_CONFIG="$TMPDIR/doggy.json" \
        DOGGY_RECORDINGS_DIR="$recordings" \
        DOGGY_SNAPSHOTS_DIR="$snapshots" \
        sh "$ROOT/packaging/cleanup-media.sh"; then
    if [ -f "$old" ]; then
        fail "cleanup removes files older than retain_hours"
    elif [ -f "$new" ]; then
        pass "cleanup removes files older than retain_hours"
    else
        fail "cleanup removes files older than retain_hours"
    fi
else
    fail "cleanup removes files older than retain_hours"
fi

if [ "$FAILS" -ne 0 ]; then
    exit 1
fi
echo "All cleanup-media tests passed."
exit 0
