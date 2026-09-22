#!/usr/bin/env bash
#
# install-prereqs.sh
#
# Install prerequisites for building doggy on Raspberry Pi OS (native) and
# for aarch64 cross-compilation from Ubuntu.
#
# Default --mode follows the host arch: aarch64/arm64 → native, otherwise
# cross. Cross compilation uses the Ubuntu aarch64 toolchain only; firmware
# dependencies (zlib, I2C SMBus, IMU vectors) are built from source.
#
# First-class hosts: Debian/Raspberry Pi OS Bookworm and Trixie, Ubuntu 24.04.
# Other distros continue best-effort (warn, skip unavailable packages, do not abort).
#
# There is no Windows target for firmware. The only robot runtime is Raspberry Pi
# aarch64. golang-go is required to build the doggy-lora sidecar (and to
# cross-compile it with GOARCH=arm64). Laptop operator clients are x86_64:
# Ubuntu DEB and Windows NSIS (`--mode windows` installs nsis and
# nsis-common stubs/plugins; no MinGW). Watch firmware (`./build-watch.sh`)
# needs PlatformIO Core 6.2.0 or newer; Debian/Ubuntu apt still ships an
# incompatible 4.x package, so native and cross modes install it with pipx.
#
# Usage:
#   ./install-prereqs.sh [options]
#
# Options:
#   --mode <native|cross|windows|all>   Which prereqs to install (default: auto from arch)
#   --print-plan                Print detected distro and package plan, then exit
#   --dry-run                   Print what would be done, do not execute
#   -h, --help                  Show this help
set -u
# ── defaults ──────────────────────────────────────────────────────────────────
MODE=""
MODE_SOURCE="auto"
OS_RELEASE_FILE="${OS_RELEASE_FILE:-/etc/os-release}"
DRY_RUN=0
PRINT_PLAN=0
MISSING_COUNT=0
WARN_COUNT=0
APT_UPDATED=0
OS_ID=""
OS_VERSION_ID=""
OS_CODENAME=""
OS_LIKE=""
OS_PRETTY=""
OS_FAMILY="other"
OS_KNOWN=0
# ── colours ───────────────────────────────────────────────────────────────────
if [ -t 1 ]; then
    C_RED='\033[0;31m'; C_GREEN='\033[0;32m'
    C_YELLOW='\033[1;33m'; C_BLUE='\033[0;34m'; C_RESET='\033[0m'
else
    C_RED=''; C_GREEN=''; C_YELLOW=''; C_BLUE=''; C_RESET=''
fi
# ── output helpers ────────────────────────────────────────────────────────────
info()  { printf '%b[INFO]%b  %s\n' "$C_BLUE"   "$C_RESET" "$1"; }
ok()    { printf '%b[OK]%b    %s\n' "$C_GREEN"  "$C_RESET" "$1"; }
warn()  { WARN_COUNT=$((WARN_COUNT+1));
          printf '%b[WARN]%b  %s\n' "$C_YELLOW" "$C_RESET" "$1"; }
fail()  { MISSING_COUNT=$((MISSING_COUNT+1));
          printf '%b[FAIL]%b  %s\n' "$C_RED"    "$C_RESET" "$1"; }
step()  { printf '%b[STEP]%b  %s\n' "$C_YELLOW" "$C_RESET" "$1"; }
die()   { printf '%b[ERROR]%b %s\n' "$C_RED"    "$C_RESET" "$1" >&2; exit 1; }
run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%b[DRY-RUN]%b  %s\n' "$C_YELLOW" "$C_RESET" "$*"
        return 0
    fi
    "$@"
}
is_pi_arch() {
    case "$1" in aarch64|arm64) return 0 ;; *) return 1 ;; esac
}
# Determine sudo invocation
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 \
        && SUDO="sudo" \
        || warn "Not running as root and sudo not found; privileged installs may fail."
fi
# ── arg parsing ───────────────────────────────────────────────────────────────
while [ "$#" -gt 0 ]; do
    case "$1" in
        --mode)
            [ "$#" -ge 2 ] || die "--mode requires an argument (try --help)"
            MODE="$2"
            MODE_SOURCE="flag"
            shift 2 ;;
        --print-plan)            PRINT_PLAN=1;                shift   ;;
        --dry-run)               DRY_RUN=1;                   shift   ;;
        -h|--help)
            grep '^#' "$0" | grep -v '^#!' | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) die "Unknown argument: $1 (try --help)" ;;
    esac
done
HOST_MACHINE="${DOGGY_HOST_MACHINE:-$(uname -m)}"
if [ -z "$MODE" ]; then
    if is_pi_arch "$HOST_MACHINE"; then
        MODE="native"
    else
        MODE="cross"
    fi
    MODE_SOURCE="auto"
fi
case "$MODE" in native|cross|windows|all) ;; *) die "Invalid --mode '$MODE' (try --help)"; esac
export DEBIAN_FRONTEND=noninteractive
os_release_val() {
    local key="$1"
    [ -f "$OS_RELEASE_FILE" ] || return 0
    awk -F= -v key="$key" '
        $1 == key {
            sub(/^[^=]+=/, "")
            gsub(/"/, "")
            print
            exit
        }
    ' "$OS_RELEASE_FILE"
}
detect_os() {
    OS_ID="$(os_release_val ID)"
    OS_VERSION_ID="$(os_release_val VERSION_ID)"
    OS_CODENAME="$(os_release_val VERSION_CODENAME)"
    OS_LIKE="$(os_release_val ID_LIKE)"
    OS_PRETTY="$(os_release_val PRETTY_NAME)"
    OS_FAMILY="other"
    OS_KNOWN=0
    case "$OS_ID" in
        debian|ubuntu|raspbian|linuxmint) OS_FAMILY="debian" ;;
        *)
            case " $OS_LIKE " in
                *" debian "*|*" ubuntu "*) OS_FAMILY="debian" ;;
            esac
            ;;
    esac
    case "$OS_ID" in
        debian|raspbian)
            case "$OS_CODENAME" in bookworm|trixie) OS_KNOWN=1 ;; esac
            ;;
        ubuntu)
            case "$OS_CODENAME" in noble) OS_KNOWN=1 ;; esac
            case "$OS_VERSION_ID" in 24.04*) OS_KNOWN=1 ;; esac
            ;;
    esac
    if [ "$OS_KNOWN" -eq 0 ]; then
        warn "Untested distro '${OS_PRETTY:-${OS_ID:-unknown}}'; continuing best-effort."
    fi
}
native_packages() {
    printf '%s' "ca-certificates cmake ninja-build g++ build-essential pkg-config git golang-go i2c-tools qtcreator zssh lrzsz vim"
}
cross_packages() {
    printf '%s' "ca-certificates cmake ninja-build pkg-config git golang-go protobuf-compiler gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
}
windows_packages() {
    printf '%s' "ca-certificates cmake ninja-build pkg-config git golang-go protobuf-compiler nsis nsis-common"
}
watch_packages() {
    printf '%s' "python3 python3-venv python3-pip pipx"
}
print_plan() {
    printf 'os_id=%s\n' "$OS_ID"
    printf 'os_codename=%s\n' "$OS_CODENAME"
    printf 'os_version_id=%s\n' "$OS_VERSION_ID"
    printf 'os_family=%s\n' "$OS_FAMILY"
    printf 'os_known=%s\n' "$OS_KNOWN"
    printf 'host_arch=%s\n' "$HOST_MACHINE"
    printf 'mode=%s\n' "$MODE"
    printf 'mode_source=%s\n' "$MODE_SOURCE"
    printf 'target=raspberry-pi-aarch64\n'
    printf 'windows_firmware=unsupported\n'
    printf 'windows_operator=nsis\n'
    printf 'devtools=included\n'
    printf 'sysroot=not-required\n'
    printf 'native_packages=%s\n' "$(native_packages)"
    printf 'cross_packages=%s\n' "$(cross_packages)"
    printf 'windows_packages=%s\n' "$(windows_packages)"
    printf 'watch_packages=%s\n' "$(watch_packages)"
    printf 'watch_tool=platformio>=6.2.0 via pipx\n'
    if [ "$OS_KNOWN" -eq 0 ]; then
        printf 'warning=untested distro; continuing best-effort\n'
    fi
}
apt_pkg_available() {
    local cand
    command -v apt-cache >/dev/null 2>&1 || return 1
    cand="$(apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ { print $2; exit }')"
    [ -n "$cand" ] && [ "$cand" != "(none)" ]
}
ensure_apt_updated() {
    if [ "$APT_UPDATED" -eq 1 ]; then
        return 0
    fi
    if command -v apt-get >/dev/null 2>&1; then
        step "apt-get update"
        run $SUDO apt-get update || warn "apt-get update failed; install may not work"
        APT_UPDATED=1
        return 0
    fi
    warn "apt-get not found; skipping package installs (best-effort)."
    return 1
}
apt_install_best_effort() {
    local pkg avail=""
    ensure_apt_updated || return 1
    for pkg in "$@"; do
        if apt_pkg_available "$pkg"; then
            avail="$avail $pkg"
        else
            warn "Package not available on this distro, skipping: $pkg"
        fi
    done
    avail="${avail# }"
    if [ -z "$avail" ]; then
        warn "No requested apt packages are available."
        return 1
    fi
    step "apt-get install $avail"
    # shellcheck disable=SC2086
    run $SUDO apt-get install -y $avail \
        || warn "apt-get install failed (best-effort, continuing)"
}
platformio_version() {
    pio --version 2>/dev/null | awk '{print $NF}'
}
platformio_version_ok() {
    local version="$1"
    [ -n "$version" ] || return 1
    [ "$(printf '%s\n' 6.2.0 "$version" | sort -V | head -n 1)" = 6.2.0 ]
}
ensure_platformio() {
    local version=""
    export PATH="${HOME}/.local/bin:${PATH}"
    step "apt-get install (Python + pipx for PlatformIO)"
    # shellcheck disable=SC2046
    apt_install_best_effort $(watch_packages)
    if command -v pio >/dev/null 2>&1; then
        version="$(platformio_version)"
        if platformio_version_ok "$version"; then
            ok "pio $version ($(command -v pio))"
            return 0
        fi
        warn "pio $version is older than 6.2.0; upgrading with pipx"
    fi
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%b[DRY-RUN]%b  pipx install --force %s\n' \
            "$C_YELLOW" "$C_RESET" "'platformio>=6.2.0'"
        return 0
    fi
    if command -v pipx >/dev/null 2>&1; then
        step "pipx install platformio>=6.2.0"
        if command -v pio >/dev/null 2>&1; then
            run pipx upgrade platformio \
                || run pipx install --force 'platformio>=6.2.0' \
                || warn "pipx could not upgrade PlatformIO"
        else
            run pipx install 'platformio>=6.2.0' \
                || warn "pipx could not install PlatformIO"
        fi
        run pipx ensurepath || true
    else
        warn "pipx not found; skip PlatformIO (needed for ./build-watch.sh)"
    fi
    hash -r 2>/dev/null || true
    if command -v pio >/dev/null 2>&1; then
        version="$(platformio_version)"
        if platformio_version_ok "$version"; then
            ok "pio $version ($(command -v pio))"
            return 0
        fi
        fail "pio $version is older than 6.2.0"
        return 1
    fi
    fail "pio (PlatformIO Core >= 6.2.0 required for ./build-watch.sh)"
}
verify_commands() {
    local cmd
    for cmd in "$@"; do
        if command -v "$cmd" >/dev/null 2>&1; then
            ok "$cmd ($(command -v "$cmd"))"
        else
            fail "$cmd (still missing after apt install)"
        fi
    done
}
# CPack NSIS needs stubs/plugins (MUI2, nsExec). Debian splits those into
# nsis-common; they are not in the nsis compiler package.
verify_nsis_extras() {
    local f
    for f in \
        /usr/share/nsis/Include/MUI2.nsh \
        /usr/share/nsis/Plugins/x86-unicode/nsExec.dll
    do
        if [ -f "$f" ]; then
            ok "$f"
        else
            fail "NSIS extra file missing: $f (install nsis-common)"
        fi
    done
}
# ── distro detection / plan ───────────────────────────────────────────────────
detect_os
if [ "$PRINT_PLAN" -eq 1 ]; then
    print_plan
    exit 0
fi
# ── banner ────────────────────────────────────────────────────────────────────
echo
info "Mode:                  $MODE ($MODE_SOURCE)"
info "Distro:                ${OS_PRETTY:-${OS_ID:-unknown}} (${OS_CODENAME:-n/a})"
info "Host arch:             $HOST_MACHINE"
info "Target:                Raspberry Pi aarch64"
info "Windows firmware:      unsupported"
info "Windows operator:      NSIS (./build-operator.sh windows)"
[ "$DRY_RUN" -eq 1 ] && info "Dry-run: no changes will be made"
echo
# ═════════════════════════════════════════════════════════════════════════════
# NATIVE (Raspberry Pi / aarch64 host)
# ═════════════════════════════════════════════════════════════════════════════
if [ "$MODE" = "all" ] || [ "$MODE" = "native" ]; then
    info "────── Native Raspberry Pi prerequisites ──────"
    if ! is_pi_arch "$HOST_MACHINE"; then
        warn "Native mode on $HOST_MACHINE: the robot target is Raspberry Pi aarch64. Omit --mode on Ubuntu for the cross toolchain."
    fi
    step "apt-get install (build tools, I2C tools, Qt Creator and editors)"
    # shellcheck disable=SC2046
    apt_install_best_effort $(native_packages)
    verify_commands cmake ninja g++ pkg-config git go
    ensure_platformio
    echo
fi
# ═════════════════════════════════════════════════════════════════════════════
# UBUNTU → RASPBERRY PI aarch64 CROSS
# ═════════════════════════════════════════════════════════════════════════════
if [ "$MODE" = "all" ] || [ "$MODE" = "cross" ]; then
    info "────── Ubuntu aarch64 cross-compilation prerequisites ──────"
    if is_pi_arch "$HOST_MACHINE"; then
        warn "Already on $HOST_MACHINE; native builds do not need a cross compiler."
    fi
    step "apt-get install (aarch64 cross toolchain)"
    # shellcheck disable=SC2046
    apt_install_best_effort $(cross_packages)
    verify_commands cmake ninja aarch64-linux-gnu-gcc aarch64-linux-gnu-g++ go protoc
    ensure_platformio
    echo
fi
# ═════════════════════════════════════════════════════════════════════════════
# WINDOWS OPERATOR (NSIS on Linux host; Go GOOS=windows, no MinGW)
# ═════════════════════════════════════════════════════════════════════════════
if [ "$MODE" = "windows" ]; then
    info "────── Windows operator packaging prerequisites ──────"
    step "apt-get install (NSIS compiler + nsis-common stubs/plugins + Go)"
    # shellcheck disable=SC2046
    apt_install_best_effort $(windows_packages)
    verify_commands cmake ninja go protoc makensis
    verify_nsis_extras
    echo
fi
# ═════════════════════════════════════════════════════════════════════════════
# SUMMARY
# ═════════════════════════════════════════════════════════════════════════════
if [ "$MISSING_COUNT" -eq 0 ]; then
    printf '%bAll prerequisites satisfied.%b' "$C_GREEN" "$C_RESET"
    [ "$WARN_COUNT" -gt 0 ] \
        && printf ' %b(%d warning(s))%b' "$C_YELLOW" "$WARN_COUNT" "$C_RESET"
    printf '\n'
    echo
    info "Then package firmware with:"
    printf '  ./build.sh\n'
    info "Watch firmware (LilyGO S3 / Waveshare C6):"
    printf '  ./build-watch.sh all\n'
    printf '  ./install-watch-s3.sh\n'
    printf '  ./install-watch-c6.sh\n'
    info "Laptop operator clients (amd64):"
    printf '  ./install-prereqs.sh --mode windows   # once, for NSIS\n'
    printf '  ./build-operator.sh                   # Ubuntu DEB + Windows NSIS\n'
    exit 0
fi
printf '%b%d prerequisite(s) still missing after installation attempts.%b\n' \
    "$C_RED" "$MISSING_COUNT" "$C_RESET"
[ "$WARN_COUNT" -gt 0 ] && \
    printf '%bWarnings: %d%b\n' "$C_YELLOW" "$WARN_COUNT" "$C_RESET"
exit 1
