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

Native packages include compile deps (`cmake`, `ninja-build`, `g++`, `libi2c-dev`, `libdlib-dev`, `zlib1g-dev`) and dev tools (`qtcreator`, `git`, `vim`, `zssh`, `lrzsz`).

## Native Raspberry Pi build

```sh
cmake --preset native-debug
cmake --build --preset native-debug
./build/native-debug/doggy
```

Use `native-release` for an optimised binary.

## SSH to a dog as user `doggy`

The repo-root `./doggy` script is an SSH helper (not the firmware). It uses `zssh` when that is on `PATH`, otherwise `ssh`. The first argument is the target; further arguments are jump hosts. Host-key prompts are disabled (LAN convenience; connections are not authenticated against `known_hosts`).

```sh
./doggy doggy-1.local
./doggy doggy-1.local bastion.example
./doggy --dry-run doggy-1.local jump1 jump2
```

Login on the target is always `doggy@<host>`. Jump hops are passed as given (`user@hop` is allowed). A hop without `user@` uses OpenSSH’s default: the same user as the destination (`doggy`).

## Web UI

The firmware binary stays running and serves a single page (LAN, no login):

```sh
./build/native-debug/doggy
# http://<pi-ip>:8080
```

- **Home** runs the homing pose
- The table lists each named servo (last commanded PWM and angle) with a slider. The slider sends `POST /api/servos/{id}` after **100ms idle**
- The IMU section shows the last body MPU6050 sample (accel in g, gyro in °/s, temperature in °C). The page polls `GET /api/status` every 200 ms.

Port: `DOGGY_HTTP_PORT` (default `8080`). HTML: `DOGGY_WEB_ROOT` or `/usr/share/doggy` after the DEB is installed. `GET /api/status` lists hardware errors (for example I2C bus missing) and the cached IMU reading; the page shows errors at the top. The firmware main loop samples the IMU every 200 ms independently of the web UI.

Logs go to `/var/log/doggy/doggy.log` (override with `DOGGY_LOG_DIR`). The file is rolled on every start and when it exceeds the size cap; 10 files are kept and rolled copies are named `doggy-YYYY-MM-DD-HHMM.log.gz`. `/api/...` requests are logged except successful `GET /api/status` (the UI polls it at 5 Hz). HTTP failures (4xx/5xx) and hardware errors are logged at error severity. The packaged unit sets `LogsDirectory=doggy` so the directory is owned by user `doggy`.

## Package and install on a remote Pi

```sh
cmake --preset native-release
cmake --build --preset native-release --target package
./install.sh user@hostname                  # newest .deb under build/
./install.sh user@hostname path/to.deb      # explicit package
./install.sh --dry-run user@hostname        # print scp/ssh only
```

The script copies the `.deb` to `/tmp` over SSH and runs `sudo apt-get install`. The package creates system user `doggy` if needed, enables I2C in `/boot/firmware/config.txt` when missing, and may ask you to reboot. It then enables and **restarts** `doggy.service` (`User=doggy`, I2C via the `i2c` group) before any reboot prompt. An upgrade does not stop the unit in `prerm`, so a failed `postinst` cannot leave the dog down.

CMake configure fetches cpp-httplib v0.18.3 and plog 1.1.11 (needs network once, or set `FETCHCONTENT_SOURCE_DIR_CPP_HTTPLIB` / `FETCHCONTENT_SOURCE_DIR_PLOG`).

Each build stamps the version as `1.0.0-YYYY-MM-DD-HHMM-<short-git-hash>` (local time). The DEB filename uses the same string. The process prints it when it starts.

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

CTest covers installer scripts, the SSH helper, PWM math, HTTP API, and log rotation (no I2C):

```sh
cmake --preset native-debug
ctest --preset native-debug
```

Or: `bash tests/install-prereqs.test.sh`

## Source layout

Application sources live in `src/`. CMake stays at the repository root.
