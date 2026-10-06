#!/usr/bin/env bash
# doggy-sim serves the rover page on plain HTTP without Gazebo.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-}"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

if [ -z "$BIN" ] || [ -x "$BIN" ]; then
    :
else
    fail "doggy-sim binary is executable"
    exit 1
fi
if [ -z "$BIN" ]; then
    fail "doggy-sim path was passed"
    exit 1
fi
pass "doggy-sim binary is executable"

TMPDIR=$(mktemp -d)
PID=""
trap 'if [ -n "$PID" ]; then kill "$PID" 2>/dev/null || true; fi; rm -rf "$TMPDIR"' EXIT
mkdir -p "$TMPDIR/home"
export HOME="$TMPDIR/home"
export DOGGY_CONFIG="$TMPDIR/home/doggy.json"
export DOGGY_SIM_SPAWN=0
export DOGGY_SIM_HTTP_PORT=18765
export DOGGY_LOG_DIR="$TMPDIR/log"
mkdir -p "$DOGGY_LOG_DIR"

"$BIN" >"$TMPDIR/out" 2>"$TMPDIR/err" &
PID=$!
ready=0
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if python3 - "$DOGGY_SIM_HTTP_PORT" <<'PY'
import sys, urllib.request
port = sys.argv[1]
urllib.request.urlopen("http://127.0.0.1:%s/api/status" % port, timeout=1).read()
PY
    then
        ready=1
        break
    fi
    sleep 0.2
done

if [ "$ready" -eq 1 ]; then
    pass "simulator HTTP status responds"
else
    fail "simulator HTTP status responds"
    cat "$TMPDIR/err" >&2 || true
fi

python3 - <<'PY' >"$TMPDIR/status" || true
import urllib.request
print(urllib.request.urlopen("http://127.0.0.1:18765/api/status", timeout=2).read().decode())
PY
python3 - <<'PY' >"$TMPDIR/page" || true
import urllib.request
print(urllib.request.urlopen("http://127.0.0.1:18765/", timeout=2).read().decode())
PY

if grep -q '"ROVER"' "$TMPDIR/status" && grep -q 'simulator link down' "$TMPDIR/status"; then
    pass "status is a rover and reports the link down without Gazebo"
else
    fail "status is a rover and reports the link down without Gazebo"
fi

if grep -q 'id="map"' "$TMPDIR/page"; then
    pass "rover page is served on the simulator port"
else
    fail "rover page is served on the simulator port"
fi

python3 - <<'PY' >"$TMPDIR/cameras" || true
import urllib.error
import urllib.request
try:
    urllib.request.urlopen("http://127.0.0.1:18765/api/cameras", timeout=2)
    print("unexpected")
except urllib.error.HTTPError as ex:
    print(ex.code)
PY
if grep -qx '404' "$TMPDIR/cameras"; then
    pass "cameras stay unpublished when Gazebo is not spawned"
else
    fail "cameras stay unpublished when Gazebo is not spawned"
fi

kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
PID=""

if [ "$FAILS" -eq 0 ]; then
    exit 0
fi
exit 1
