#!/usr/bin/env bash
# Regression tests for ./install-prereqs.sh distro handling and modes.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$ROOT/install-prereqs.sh"
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

if [ -x "$SCRIPT" ]; then
    pass "install-prereqs.sh is executable"
else
    fail "install-prereqs.sh is executable"
fi

if bash -n "$SCRIPT"; then
    pass "install-prereqs.sh is valid bash"
else
    fail "install-prereqs.sh is valid bash"
fi

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

if grep -q 'sysroot=not-required' "$TMPDIR/plan-trixie"; then
    pass "plan does not require a sysroot"
else
    fail "plan does not require a sysroot"
fi

for pkg in cmake ninja-build g++ qtcreator vim git zssh lrzsz i2c-tools; do
    if grep -E "(^| )${pkg}( |$)" "$TMPDIR/plan-trixie" >/dev/null \
            || grep -q "native_packages=.*${pkg}" "$TMPDIR/plan-trixie"; then
        pass "native plan includes $pkg"
    else
        fail "native plan includes $pkg"
    fi
done

for pkg in libi2c-dev libdlib-dev zlib1g-dev; do
    if grep -q "native_packages=.*${pkg}" "$TMPDIR/plan-trixie"; then
        fail "native plan does not include $pkg"
    else
        pass "native plan does not include $pkg"
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

# ── Auto mode from host arch ─────────────────────────────────────────────────
if DOGGY_HOST_MACHINE=aarch64 run_plan "$TMPDIR/trixie" "$TMPDIR/plan-auto-pi"; then
    if grep -q 'mode=native' "$TMPDIR/plan-auto-pi" \
            && grep -q 'mode_source=auto' "$TMPDIR/plan-auto-pi"; then
        pass "aarch64 auto mode is native"
    else
        fail "aarch64 auto mode is native"
    fi
else
    fail "aarch64 auto mode is native (print-plan failed)"
fi

if DOGGY_HOST_MACHINE=arm64 run_plan "$TMPDIR/trixie" "$TMPDIR/plan-auto-arm64" \
        && grep -q 'mode=native' "$TMPDIR/plan-auto-arm64"; then
    pass "arm64 auto mode is native"
else
    fail "arm64 auto mode is native"
fi

if DOGGY_HOST_MACHINE=x86_64 run_plan "$TMPDIR/noble" "$TMPDIR/plan-auto-x86"; then
    if grep -q 'mode=cross' "$TMPDIR/plan-auto-x86" \
            && grep -q 'mode_source=auto' "$TMPDIR/plan-auto-x86" \
            && grep -q 'sysroot=not-required' "$TMPDIR/plan-auto-x86"; then
        pass "x86_64 auto mode is cross without sysroot"
    else
        fail "x86_64 auto mode is cross without sysroot"
    fi
else
    fail "x86_64 auto mode is cross without sysroot (print-plan failed)"
fi

if DOGGY_HOST_MACHINE=x86_64 run_plan "$TMPDIR/noble" \
        "$TMPDIR/plan-flag-native" --mode native \
        && grep -q 'mode=native' "$TMPDIR/plan-flag-native" \
        && grep -q 'mode_source=flag' "$TMPDIR/plan-flag-native"; then
    pass "--mode native on x86_64 overrides auto-detect"
else
    fail "--mode native on x86_64 overrides auto-detect"
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
