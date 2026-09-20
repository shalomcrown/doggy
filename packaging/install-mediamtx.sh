#!/bin/sh
set -eu

version=v1.21.0
bin="${DOGGY_MEDIAMTX_BIN:-/usr/lib/doggy/mediamtx}"
version_file="${DOGGY_MEDIAMTX_VERSION_FILE:-/usr/lib/doggy/mediamtx.version}"

installed_version() {
    if [ -x "$bin" ]; then
        "$bin" --version 2>/dev/null || true
    fi
}

# Skip the GitHub download when the pinned binary is already in place.
if installed="$(installed_version)" \
        && printf '%s' "$installed" | grep -Fq "$version"; then
    printf '%s\n' "$version" >"$version_file"
    exit 0
fi

architecture=$(dpkg --print-architecture)

case "$architecture" in
    amd64)
        archive_arch=amd64
        checksum=e02e34c3337a35f20ac9e5aa31524566108964e6e37dbc46cf8292169f6c792b
        ;;
    arm64)
        archive_arch=arm64
        checksum=a8113b5928ba1a934b81557b61b8a07954b76921a4b567d54c7f086f8b39d9a2
        ;;
    *)
        echo "unsupported MediaMTX architecture: $architecture" >&2
        exit 1
        ;;
esac

archive="mediamtx_${version}_linux_${archive_arch}.tar.gz"
url="https://github.com/bluenviron/mediamtx/releases/download/${version}/${archive}"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

curl --fail --location --proto '=https' --tlsv1.2 \
    --output "$temporary/$archive" "$url"
printf '%s  %s\n' "$checksum" "$temporary/$archive" | sha256sum --check -
tar -xzf "$temporary/$archive" -C "$temporary" mediamtx
install -D -m 0755 "$temporary/mediamtx" "$bin"
printf '%s\n' "$version" >"$version_file"
