# doggy

Things to do with robot dogs. Firmware for a Freenove robot dog on **Raspberry Pi aarch64** (Pi 5 / 64-bit Raspberry Pi OS). There is no Windows target.

## Prerequisites

Run the installer once before building. First-class hosts: **Debian / Raspberry Pi OS Bookworm and Trixie**, and **Ubuntu 24.04**. Other distros are best-effort (unavailable packages are skipped; the script does not abort solely because the distro is untested).

```sh
./install-prereqs.sh                         # native on aarch64/arm64; cross otherwise
./install-prereqs.sh --mode native           # force Pi native packages
./install-prereqs.sh --mode cross            # Ubuntu aarch64 cross toolchain
./install-prereqs.sh --mode all              # native + cross
./install-prereqs.sh --dry-run               # preview without making changes
```

Native packages include compile deps (`cmake`, `ninja-build`, `g++`) and dev tools (`qtcreator`, `git`, `vim`, `zssh`, `lrzsz`, `i2c-tools`). zlib, I2C SMBus, and IMU vectors are compiled from this tree — no `libdlib-dev`, `libi2c-dev`, or sysroot.

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
- The battery section shows pack voltage from the ADS7830 (channel 0).
- The title line (`<h1>` and the browser tab) shows `Doggy <version>` from `GET /api/status`.

Port: `DOGGY_HTTP_PORT` (default `8080`). HTML: `DOGGY_WEB_ROOT` or `/usr/share/doggy` after the DEB is installed. `GET /api/status` lists the stamped `version`, hardware errors (for example I2C bus missing), and the cached IMU and battery readings; the page shows errors at the top. The firmware main loop samples the IMU and battery ADC every 200 ms independently of the web UI.

Logs go to `/var/log/doggy/doggy.log` (override with `DOGGY_LOG_DIR`). The file is rolled on every start and when it exceeds the size cap; 10 files are kept and rolled copies are named `doggy-YYYY-MM-DD-HHMM.log.gz`. `/api/...` requests are logged except successful `GET /api/status` (the UI polls it at 5 Hz). HTTP failures (4xx/5xx) and hardware errors are logged at error severity. The packaged unit sets `LogsDirectory=doggy` so the directory is owned by user `doggy`.

## Package and install on a remote Pi

```sh
./build.sh                                  # native-release on aarch64; cross otherwise
./install.sh user@hostname                  # newest .deb under build/
./install.sh user@hostname path/to.deb      # explicit package
./install.sh --dry-run user@hostname        # print scp/ssh only
```

`./build.sh` uses `uname -m`: `aarch64` / `arm64` → `native-release`; any other arch → `ubuntu-aarch64-cross`. No sysroot. The `.deb` is Debian architecture `arm64` even when packaged on an amd64 host. `--dry-run` prints the cmake commands without running them. Equivalent manual commands:

```sh
cmake --preset native-release
cmake --build --preset native-release --target package
```

The script copies the `.deb` to `/tmp` over SSH and runs `sudo apt-get install`. The package creates system user `doggy` if needed, enables I2C in `/boot/firmware/config.txt` when missing, and may ask you to reboot. It then enables and **restarts** `doggy.service` (`User=doggy`, I2C via the `i2c` group) before any reboot prompt. An upgrade does not stop the unit in `prerm`, so a failed `postinst` cannot leave the dog down.

CMake configure fetches cpp-httplib v0.18.3, plog 1.1.11, and zlib 1.3.1 (needs network once, or set `FETCHCONTENT_SOURCE_DIR_CPP_HTTPLIB` / `FETCHCONTENT_SOURCE_DIR_PLOG` / `FETCHCONTENT_SOURCE_DIR_ZLIB`).

Each build stamps the version as `1.0.0-YYYY-MM-DD-HHMM-<short-git-hash>` (local time). The DEB filename uses the same string. The process prints it when it starts; `GET /api/status` and the web title line show it too.

## Target on-disk layout

After `apt-get install` of `shaloms-doggy` the Pi has:

```
/usr/bin/doggy                         firmware (systemd ExecStart)
/usr/share/doggy/index.html            web UI
/etc/systemd/system/doggy.service      unit (User=doggy)
/var/log/doggy/                        created at install and on start
/var/log/doggy/doggy.log               current log
/var/log/doggy/doggy-YYYY-MM-DD-HHMM.log.gz
/etc/modules-load.d/shaloms-doggy-i2c.conf   only if postinst enabled I2C
```

The package does not install the repo-root `./doggy` SSH helper. `postinst` may edit `/boot/firmware/config.txt` (or `/boot/config.txt`) to turn on I2C; that file is not owned by the package.

`cmake --install` without packaging uses prefix `/usr/local` (`/usr/local/bin/doggy`, `/usr/local/share/doggy/index.html`) unless you set `CMAKE_INSTALL_PREFIX`.

## Ubuntu → Raspberry Pi aarch64 cross-build

On a non-Pi host, `./install-prereqs.sh` installs `g++-aarch64-linux-gnu`. Then:

```sh
./build.sh
```

or:

```sh
cmake --preset ubuntu-aarch64-cross
cmake --build --preset ubuntu-aarch64-cross --target package
```

No Pi sysroot: zlib is fetched by CMake, I2C uses the kernel `I2C_SMBUS` ioctl, and IMU math uses `Vec3` instead of dlib.

## Tests

CTest covers installer scripts, the SSH helper, PWM math, HTTP API, and log rotation (no I2C):

```sh
cmake --preset native-debug
ctest --preset native-debug
```

Or: `bash tests/install-prereqs.test.sh`

## Source layout

Application sources live in `src/`. CMake stays at the repository root. Installed paths are under **Target on-disk layout**.
