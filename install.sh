#!/usr/bin/env bash
#
# install.sh
#
# Copy a doggy Debian package to a remote machine and install it over SSH.
#
# Usage:
#   ./install.sh [options] <user@host> [path/to.deb]
#
# If the .deb path is omitted, the newest *.deb under build/ is used
# (override search root with DOGGY_DEB_ROOT).
#
# Options:
#   --dry-run     Print scp/ssh commands, do not execute
#   -h, --help    Show this help
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
DEB_ROOT="${DOGGY_DEB_ROOT:-$ROOT/build}"
DRY_RUN=0
HOST=""
DEB=""
# ── colours ───────────────────────────────────────────────────────────────────
if [ -t 1 ]; then
    C_RED='\033[0;31m'; C_GREEN='\033[0;32m'
    C_YELLOW='\033[1;33m'; C_BLUE='\033[0;34m'; C_RESET='\033[0m'
else
    C_RED=''; C_GREEN=''; C_YELLOW=''; C_BLUE=''; C_RESET=''
fi
info() { printf '%b[INFO]%b  %s\n' "$C_BLUE" "$C_RESET" "$1"; }
ok()   { printf '%b[OK]%b    %s\n' "$C_GREEN" "$C_RESET" "$1"; }
die()  { printf '%b[ERROR]%b %s\n' "$C_RED" "$C_RESET" "$1" >&2; exit 1; }
run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%b[DRY-RUN]%b  %s\n' "$C_YELLOW" "$C_RESET" "$*"
        return 0
    fi
    "$@"
}
# ── arg parsing ───────────────────────────────────────────────────────────────
POSITIONAL=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help)
            grep '^#' "$0" | grep -v '^#!' | sed 's/^# \{0,1\}//'
            exit 0 ;;
        --) shift
            while [ "$#" -gt 0 ]; do
                POSITIONAL+=("$1")
                shift
            done
            break ;;
        -*) die "Unknown argument: $1 (try --help)" ;;
        *) POSITIONAL+=("$1"); shift ;;
    esac
done
if [ "${#POSITIONAL[@]}" -eq 0 ]; then
    die "Missing host. Usage: ./install.sh <user@host> [path/to.deb]"
fi
if [ "${#POSITIONAL[@]}" -gt 2 ]; then
    die "Too many arguments. Usage: ./install.sh <user@host> [path/to.deb]"
fi
HOST="${POSITIONAL[0]}"
if [ "${#POSITIONAL[@]}" -eq 2 ]; then
    DEB="${POSITIONAL[1]}"
fi
# ── validate host (no option injection) ───────────────────────────────────────
case "$HOST" in
    *.deb) die "First argument must be user@host, not a .deb path" ;;
    -*) die "Invalid host (must not start with -): $HOST" ;;
esac
# Allow optional user@ and a hostname / IPv4 — no spaces, metacharacters, or paths.
case "$HOST" in
    *[!A-Za-z0-9._@-]*) die "Invalid host characters: $HOST" ;;
esac
case "$HOST" in
    *@) die "Invalid host: $HOST" ;;
    @*) die "Invalid host: $HOST" ;;
    *@*@*) die "Invalid host: $HOST" ;;
esac
# ── resolve .deb ──────────────────────────────────────────────────────────────
find_newest_deb() {
    local f newest=""
    [ -d "$DEB_ROOT" ] || return 1
    while IFS= read -r f; do
        if [ -z "$newest" ]; then
            newest="$f"
        elif [ "$f" -nt "$newest" ]; then
            newest="$f"
        fi
    done < <(find "$DEB_ROOT" -type f -name '*.deb' 2>/dev/null)
    [ -n "$newest" ] || return 1
    printf '%s' "$newest"
}
if [ -z "$DEB" ]; then
    DEB="$(find_newest_deb)" || die \
        "No .deb found under $DEB_ROOT. Build one with: ./build.sh"
fi
[ -f "$DEB" ] || die "Debian package is not a file: $DEB"
case "$DEB" in
    *.deb) ;;
    *) die "File is not a .deb: $DEB" ;;
esac
REMOTE_NAME="$(basename -- "$DEB")"
case "$REMOTE_NAME" in
    *[!A-Za-z0-9._+-]*|"") die "Unsafe .deb filename: $REMOTE_NAME" ;;
esac
REMOTE_PATH="/tmp/$REMOTE_NAME"
# ── copy + install ────────────────────────────────────────────────────────────
echo
info "Host:     $HOST"
info "Package:  $DEB"
info "Remote:   $REMOTE_PATH"
[ "$DRY_RUN" -eq 1 ] && info "Dry-run: no SSH or copy will be made"
echo
run scp "$DEB" "$HOST:$REMOTE_PATH"
# -t: sudo needs a TTY to prompt; key-based SSH no longer provides one.
# Do not set DEBIAN_FRONTEND=noninteractive: postinst may ask to reboot after enabling I2C.
run ssh -t "$HOST" \
    "sudo apt-get install -y '$REMOTE_PATH'"
[ "$DRY_RUN" -eq 1 ] || ok "Installed $REMOTE_NAME on $HOST"
exit 0
