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

oldest="$recordings/oldest.ts"
middle="$recordings/middle.ts"
spare="$recordings/spare.ts"
live="$recordings/live.ts"
printf '%080d' 1 >"$oldest"
printf '%080d' 2 >"$middle"
printf '%080d' 3 >"$spare"
printf 'live' >"$live"
touch -d '3 hours ago' "$oldest"
touch -d '2 hours ago' "$middle"
touch -d '90 minutes ago' "$spare"
touch -d '30 seconds ago' "$live"
floor=$((512 * 1024 * 1024))
if DOGGY_CONFIG="$TMPDIR/doggy.json" \
        DOGGY_RECORDINGS_DIR="$recordings" \
        DOGGY_SNAPSHOTS_DIR="$snapshots" \
        DOGGY_CLEANUP_FREE_BYTES=$((floor - 100)) \
        sh "$ROOT/packaging/cleanup-media.sh"; then
    if [ -f "$oldest" ] || [ -f "$middle" ]; then
        fail "cleanup deletes oldest recordings until min free space is met"
    elif [ -f "$spare" ] && [ -f "$live" ]; then
        pass "cleanup deletes oldest recordings until min free space is met"
    else
        fail "cleanup deletes oldest recordings until min free space is met"
    fi
else
    fail "cleanup deletes oldest recordings until min free space is met"
fi

if [ "$FAILS" -ne 0 ]; then
    exit 1
fi
echo "All cleanup-media tests passed."
exit 0
