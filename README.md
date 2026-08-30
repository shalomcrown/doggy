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
# https://<pi-ip>/   (port 443; http://<pi-ip>/ redirects)
# unprivileged local: DOGGY_HTTPS_PORT=8443 DOGGY_HTTP_PORT=8080 ./build/native-debug/doggy
```

- **Home** runs the homing pose
- The table lists each named servo (PWM and angle) with a slider and an **Off** button. The slider sends `POST /api/servos/{id}` `{ "angle" }` after **100ms idle**. Off sends `{ "enabled": false }` (PWM 0; angle unknown). The page refreshes PWM/angle from `GET /api/status` every 200 ms without resetting a slider you are dragging.
- The IMU section shows the last body MPU6050 sample (accel in g, gyro in °/s, temperature in °C).
- The battery section shows pack voltage from the ADS7830 (channel 0).
- The title line (`<h1>` and the browser tab) shows `Doggy <version>` from `GET /api/status`.
- **Configuration** loads `GET /api/config` and saves with `PUT /api/config`. Servo channel changes apply immediately and the servo table rebuilds. I2C bus and address changes are stored and take effect after a restart. Set a **system PIN** here (4–64 characters, hashed in `doggy.json`).
- **Power** can restart `doggy.service`, reboot, or shut down the Pi. Each call is `POST /api/system` with the PIN (not the Unix password). Shutdown asks you to type `SHUTDOWN`. Privilege comes from a packaged polkit rule for user `doggy`.

HTTPS: `DOGGY_HTTPS_PORT` (default `443`). HTTP: `DOGGY_HTTP_PORT` (default `80`) only redirects to HTTPS. Unprivileged runs need ports above 1024. TLS files: `DOGGY_TLS_CERT` / `DOGGY_TLS_KEY`, or `tls.crt` / `tls.key` next to the JSON config (generated on first start if missing; browsers warn once on the self-signed cert). HTML: `DOGGY_WEB_ROOT` or `/usr/share/doggy` after the DEB is installed. Config: `DOGGY_CONFIG` or `/etc/doggy/doggy.json` (JSON; servo channels and I2C bus/address for the PCA9685, IMU, and ADS7830). If the file is missing, the process writes compiled defaults there (`postinst` creates `/etc/doggy` owned by `doggy`). A present but invalid file stops startup. `GET /api/config` returns that file shape (hex addresses) plus `system.pin_set` (never the PIN or hash). `PUT /api/config` writes i2c/servos, remaps channels now, and applies I2C on the next process start. `POST /api/system/pin` sets the power PIN. `GET /api/status` lists the stamped `version`, hardware errors, cached IMU and battery readings, and servo PWM/angle (`angle` is `null` when PWM is 0, including at startup). The page shows errors at the top. The firmware main loop samples the IMU and battery ADC every 200 ms independently of the web UI.

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

CMake configure fetches cpp-httplib v0.52.0, plog 1.1.11, zlib 1.3.1, nlohmann/json 3.11.3, and Mbed TLS 3.6.7 (needs network once, or set `FETCHCONTENT_SOURCE_DIR_CPP_HTTPLIB` / `FETCHCONTENT_SOURCE_DIR_PLOG` / `FETCHCONTENT_SOURCE_DIR_ZLIB` / `FETCHCONTENT_SOURCE_DIR_JSON` / `FETCHCONTENT_SOURCE_DIR_MBEDTLS`). The system PIN is hashed with Mbed TLS SHA-256; HTTPS uses the same Mbed TLS tree.

Each build stamps the version as `1.0.0-YYYY-MM-DD-HHMM-<short-git-hash>` (local time). The DEB filename uses the same string. The process prints it when it starts; `GET /api/status` and the web title line show it too.

## Target on-disk layout

After `apt-get install` of `shaloms-doggy` the Pi has:

```
/usr/bin/doggy                         firmware (systemd ExecStart)
/usr/share/doggy/index.html            web UI
/etc/systemd/system/doggy.service      unit (User=doggy)
/etc/doggy/                            created at install (User=doggy, mode 0755)
/etc/doggy/doggy.json                  written on first start if missing
/etc/doggy/tls.crt                     self-signed cert (created on first start)
/etc/doggy/tls.key                     TLS private key (mode 0600, not in the DEB)
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


## Note for later
### Mandatory Configuration Checklist
The module defaults to generalized 915 MHz settings out of the box. To comply with Israeli wireless telegraph laws, you must plug the USB into a PC and use the Waveshare configuration software (or raw AT commands) to constrain the hardware:
- Set the Center Frequency: Lock the frequency explicitly within 917.0 MHz to 920.0 MHz.
- Enable LBT (Listen Before Talk): Turn on the LBT function in the configuration tool. Israeli regulations require devices to monitor channel environmental noise to prevent jamming other spectrum users.