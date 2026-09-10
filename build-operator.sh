#!/usr/bin/env bash
#
# build-operator.sh
#
# Package the laptop LoRa operator client (x86_64 only). Firmware is not built.
# linux → shaloms-doggy-lora-operator amd64 DEB
# windows → NSIS installer (Go windows/amd64 + makensis; no MinGW)
#
# Usage:
#   ./build-operator.sh [linux|windows|all] [options]
#
# Options:
#   --dry-run     Print cmake commands, do not execute
#   -h, --help    Show this help
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
DRY_RUN=0
TARGET="all"
# ── colours ───────────────────────────────────────────────────────────────────
if [ -t 1 ]; then
    C_RED='\033[0;31m'; C_YELLOW='\033[1;33m'; C_RESET='\033[0m'
else
    C_RED=''; C_YELLOW=''; C_RESET=''
fi
die() { printf '%b[ERROR]%b %s\n' "$C_RED" "$C_RESET" "$1" >&2; exit 1; }
run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%b[DRY-RUN]%b  %s\n' "$C_YELLOW" "$C_RESET" "$*"
        return 0
    fi
    "$@"
}
# ── arg parsing ───────────────────────────────────────────────────────────────
while [ "$#" -gt 0 ]; do
    case "$1" in
        linux|windows|all) TARGET="$1"; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help)
            grep '^#' "$0" | grep -v '^#!' | sed 's/^# \{0,1\}//'
            exit 0 ;;
        -*) die "Unknown argument: $1 (try --help)" ;;
        *) die "Unknown argument: $1 (try --help)" ;;
    esac
done
package_preset() {
    local preset="$1"
    run cmake --preset "$preset"
    run cmake --build --preset "$preset" --target package
}
case "$TARGET" in
    linux)
        package_preset operator-linux-amd64
        ;;
    windows)
        package_preset operator-windows-amd64
        ;;
    all)
        package_preset operator-linux-amd64
        package_preset operator-windows-amd64
        ;;
esac
