# doggy

Things to do with robot dogs. Firmware for a Freenove robot dog on **Raspberry Pi aarch64** (Pi 5 / 64-bit Raspberry Pi OS). There is no Windows target.

## Prerequisites

Run the installer once before building. First-class hosts: **Debian / Raspberry Pi OS Bookworm and Trixie**, and **Ubuntu 24.04**. Other distros are best-effort (unavailable packages are skipped; the script does not abort solely because the distro is untested).

```sh
./scripts/install-prereqs.sh                 # native Pi build tools + Qt Creator (default)
./scripts/install-prereqs.sh --mode cross    # Ubuntu aarch64 cross toolchain
./scripts/install-prereqs.sh --mode all      # native + cross
./scripts/install-prereqs.sh --dry-run       # preview without making changes
```

Native packages include compile deps (`cmake`, `ninja-build`, `g++`, `libi2c-dev`, `libdlib-dev`) and dev tools (`qtcreator`, `git`, `vim`, `zssh`, `lrzsz`).

## Native Raspberry Pi build

```sh
cmake --preset native-debug
cmake --build --preset native-debug
./build/native-debug/doggy
```

Use `native-release` for an optimised binary.

## Ubuntu → Raspberry Pi aarch64 cross-build

Cross-compilation needs a Raspberry Pi OS **sysroot** (headers and ARM libraries for i2c and dlib). Configure fails without it.

```sh
export DOGGY_SYSROOT=/path/to/pi-sysroot
cmake --preset ubuntu-aarch64-cross
cmake --build --preset ubuntu-aarch64-cross
```

Populate the sysroot from the Pi (example):

```sh
rsync -aH --info=progress2 pi-user@raspberrypi:/usr/ /path/to/pi-sysroot/usr/
```

## Tests

There is no C++ unit-test framework yet. CTest runs the prereqs-script checks:

```sh
cmake --preset native-debug
ctest --preset native-debug
```

Or: `bash tests/install-prereqs.test.sh`

## Source layout

Application sources live in `src/`. CMake stays at the repository root.
