#!/usr/bin/env bash
# Regression tests for scripts/install-prereqs.sh distro handling and modes.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/scripts/install-prereqs.sh"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

write_os_release() {
    local dest="$1"
    shift
    printf '%s\n' "$@" >"$dest"
}

run_plan() {
    local os_file="$1" out="$2"
    shift 2
    OS_RELEASE_FILE="$os_file" "$SCRIPT" --print-plan "$@" >"$out" 2>&1
}

# ── --print-plan must exist and succeed on Trixie ────────────────────────────
write_os_release "$TMPDIR/trixie" \
    'ID=debian' \
    'VERSION_ID=13' \
    'VERSION_CODENAME=trixie' \
    'ID_LIKE=debian' \
    'PRETTY_NAME="Debian GNU/Linux 13 (trixie)"'

if run_plan "$TMPDIR/trixie" "$TMPDIR/plan-trixie" --mode native; then
    pass "print-plan exits 0 on debian/trixie"
else
    fail "print-plan exits 0 on debian/trixie (exit $?)"
fi

if grep -q 'os_known=1' "$TMPDIR/plan-trixie"; then
    pass "debian/trixie marked known"
else
    fail "debian/trixie marked known"
fi

if grep -q 'windows=unsupported' "$TMPDIR/plan-trixie"; then
    pass "plan declares windows unsupported"
else
    fail "plan declares windows unsupported"
fi

if grep -q 'devtools=included' "$TMPDIR/plan-trixie"; then
    pass "native plan includes devtools by default"
else
    fail "native plan includes devtools by default"
fi

for pkg in cmake ninja-build g++ libi2c-dev libdlib-dev zlib1g-dev \
           qtcreator vim git zssh lrzsz; do
    if grep -E "(^| )${pkg}( |$)" "$TMPDIR/plan-trixie" >/dev/null \
            || grep -q "native_packages=.*${pkg}" "$TMPDIR/plan-trixie"; then
        pass "native plan includes $pkg"
    else
        fail "native plan includes $pkg"
    fi
done

# ── Ubuntu 24.04 / noble is a known distro ───────────────────────────────────
write_os_release "$TMPDIR/noble" \
    'ID=ubuntu' \
    'VERSION_ID=24.04' \
    'VERSION_CODENAME=noble' \
    'ID_LIKE=debian' \
    'PRETTY_NAME="Ubuntu 24.04 LTS"'

if run_plan "$TMPDIR/noble" "$TMPDIR/plan-noble" --mode native \
        && grep -q 'os_known=1' "$TMPDIR/plan-noble" \
        && grep -q 'qtcreator' "$TMPDIR/plan-noble"; then
    pass "ubuntu 24.04/noble is known and includes qtcreator"
else
    fail "ubuntu 24.04/noble is known and includes qtcreator"
fi

# ── Bookworm is a known distro ───────────────────────────────────────────────
write_os_release "$TMPDIR/bookworm" \
    'ID=debian' \
    'VERSION_ID=12' \
    'VERSION_CODENAME=bookworm' \
    'ID_LIKE=debian'

if run_plan "$TMPDIR/bookworm" "$TMPDIR/plan-bookworm" --mode native \
        && grep -q 'os_known=1' "$TMPDIR/plan-bookworm"; then
    pass "debian/bookworm marked known"
else
    fail "debian/bookworm marked known"
fi

write_os_release "$TMPDIR/raspbian" \
    'ID=raspbian' \
    'VERSION_ID=12' \
    'VERSION_CODENAME=bookworm' \
    'ID_LIKE=debian'

if run_plan "$TMPDIR/raspbian" "$TMPDIR/plan-raspbian" --mode native \
        && grep -q 'os_known=1' "$TMPDIR/plan-raspbian"; then
    pass "raspbian/bookworm marked known"
else
    fail "raspbian/bookworm marked known"
fi

# ── Unknown distro: warn, do not abort ───────────────────────────────────────
write_os_release "$TMPDIR/fedora" \
    'ID=fedora' \
    'VERSION_ID=41' \
    'PRETTY_NAME="Fedora Linux 41"'

if run_plan "$TMPDIR/fedora" "$TMPDIR/plan-fedora" --mode native; then
    pass "print-plan does not abort on unknown distro"
else
    fail "print-plan does not abort on unknown distro (exit $?)"
fi

if grep -q 'os_known=0' "$TMPDIR/plan-fedora"; then
    pass "unknown distro marked os_known=0"
else
    fail "unknown distro marked os_known=0"
fi

if grep -Eq 'best.effort|untested' "$TMPDIR/plan-fedora"; then
    pass "unknown distro reports best-effort"
else
    fail "unknown distro reports best-effort"
fi

# ── Cross mode installs aarch64 toolchain, not MinGW / Windows ───────────────
if run_plan "$TMPDIR/noble" "$TMPDIR/plan-cross" --mode cross; then
    pass "print-plan exits 0 for --mode cross"
else
    fail "print-plan exits 0 for --mode cross"
fi

if grep -q 'g++-aarch64-linux-gnu' "$TMPDIR/plan-cross"; then
    pass "cross plan includes g++-aarch64-linux-gnu"
else
    fail "cross plan includes g++-aarch64-linux-gnu"
fi

if grep -qi mingw "$TMPDIR/plan-cross"; then
    fail "cross plan must not mention mingw"
else
    pass "cross plan must not mention mingw"
fi

# ── Windows is rejected ──────────────────────────────────────────────────────
if OS_RELEASE_FILE="$TMPDIR/trixie" "$SCRIPT" --mode windows --print-plan \
        >"$TMPDIR/plan-windows" 2>&1; then
    fail "--mode windows must be rejected"
else
    pass "--mode windows must be rejected"
fi

if grep -qiE 'unknown argument|--mode windows|Invalid --mode' \
        "$TMPDIR/plan-windows"; then
    pass "--mode windows reports an error"
else
    fail "--mode windows reports an error"
fi

# ── Default mode is native ───────────────────────────────────────────────────
if run_plan "$TMPDIR/trixie" "$TMPDIR/plan-default"; then
    if grep -q 'mode=native' "$TMPDIR/plan-default"; then
        pass "default mode is native"
    else
        fail "default mode is native"
    fi
else
    fail "default mode is native (print-plan failed)"
fi

# ── No pip --user (PEP 668) ──────────────────────────────────────────────────
if grep -E 'pip[[:space:]]+install[[:space:]]+--user' "$SCRIPT" >/dev/null 2>&1; then
    fail "script must not pip install --user (PEP 668)"
else
    pass "script must not pip install --user (PEP 668)"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%s\n' "---- captured plans ----"
    for f in "$TMPDIR"/plan-*; do
        [ -f "$f" ] || continue
        printf '== %s ==\n' "$(basename "$f")"
        cat "$f"
    done
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All install-prereqs.sh tests passed."
exit 0
