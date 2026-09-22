# doggy

Things to do with robot dogs. Firmware for a Freenove robot dog on **Raspberry Pi aarch64** (Pi 5 / 64-bit Raspberry Pi OS). The robot runtime is not a Windows target. Laptop LoRa operator clients are **x86_64** (Ubuntu DEB and Windows NSIS).

## Prerequisites

Run the installer once before building. First-class hosts: **Debian / Raspberry Pi OS Bookworm and Trixie**, and **Ubuntu 24.04**. Other distros are best-effort (unavailable packages are skipped; the script does not abort solely because the distro is untested).

```sh
./install-prereqs.sh                         # native on aarch64/arm64; cross otherwise
./install-prereqs.sh --mode native           # force Pi native packages
./install-prereqs.sh --mode cross            # Ubuntu aarch64 cross toolchain
./install-prereqs.sh --mode all              # native + cross
./install-prereqs.sh --mode windows        # nsis + nsis-common + go (Windows operator installer)
# Native and cross modes also install pipx and PlatformIO Core >= 6.2.0
# for ./build-watch.sh (Debian/Ubuntu apt PlatformIO is too old).
./install-prereqs.sh --dry-run               # preview without making changes
```

Native packages include compile deps (`cmake`, `ninja-build`, `g++`, `golang-go`) and dev tools (`qtcreator`, `git`, `vim`, `zssh`, `lrzsz`, `i2c-tools`). `golang-go` builds the `doggy-lora` sidecar (and `GOARCH=arm64` when packaging for the Pi). zlib, I2C SMBus, and IMU vectors are compiled from this tree — no `libdlib-dev`, `libi2c-dev`, or sysroot.

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

- **Home** runs the homing pose (dog only)
- On a **dog**, the table lists each named servo (PWM and angle) with a slider and an **Off** button. The slider sends `POST /api/servos/{id}` `{ "angle" }` after **100ms idle**. Off sends `{ "enabled": false }` (PWM 0; angle unknown). The page refreshes PWM/angle from `GET /api/status` every 200 ms without resetting a slider you are dragging.
- On a **rover**, `/` is `rover.html`: steering and speed sliders (0 centered, `[-1, 1]`) send `POST /api/drive` after 100 ms idle. A local on-screen joystick sits to the right of a reserved camera panel; its inner circle is a dead zone (no steering, speed 0). Releasing the stick springs it to center. **Stop** (`POST /api/stop`) coasts motors and **Brake** (`POST /api/brake`) short-brakes them; both set commanded `speed`/`turn` to 0. The page zeros the drive and per-motor speed sliders after those calls (and after per-motor Coast/Brake) without sending a second `/api/drive`. Drive is an arcade mix: effective turn is `turn * (turn_gain_min + (1 - turn_gain_min) * |speed|)` (default `turn_gain_min` 0.25), then that value is added to the left motors and subtracted from the right, and both sides are scaled together if either would leave `[-1, 1]`. At rest a full steer is a reduced in-place spin; at full speed the same stick still uses full turn. Mixed magnitude maps linearly to 0–4095 PWM ticks; sign selects direction. The table shows each motor’s commanded PWM, enable, and configured wiring direction. The page also sends rover-only `POST /api/heartbeat` every 750 ms over Wi-Fi or LoRa. If the page closes or the link fails, the rover coasts after `robot.gcs_timeout_s` (default 3 seconds; 2–60). Direct API clients must heartbeat or reissue motion inside that timeout; `GET /api/status` never keeps motion alive.
- The camera panel plays WebRTC from MediaMTX (`GET /api/cameras`). MPEG-TS is recorded continuously while a stream is published (`GET /api/recordings`). **Snapshot** grabs a JPEG (`POST /api/snapshots`). Hourly cleanup uses `media.retain_hours` (default 24).
- The IMU section shows the last body MPU6050 sample (accel in g, gyro in °/s, temperature in °C).
- The battery section shows pack voltage from the ADS7830 (channel 0).
- The title line (`<h1>` and the browser tab) shows `Doggy <version>` from `GET /api/status`; both pages show the current Linux time in UTC.
- **Configuration** loads `GET /api/config` and saves with `PUT /api/config`. Robot type is `DOG` or `ROVER` (missing type in the file is a dog); `robot.gcs_timeout_s` defaults to 3 and accepts 2–60 seconds. Changing type requires the system PIN, schedules a service restart, and the page tells you to **refresh** afterwards. Servo channels and each motor's PCA9685 `pwm`, `in2`, and `in1` channels apply immediately; motor channels must be unique and in 0–15. I2C bus and address changes are stored and take effect after a restart. Rover defaults use bonnet address `0x60` and clockwise motor triples FL 2/3/4, FR 5/6/7, RR 8/9/10, RL 11/12/13. **LoRa** (`lora.enabled`, `device`, `baud`, `band` `HF`|`LF`, `txch`, `rxch` 0–80, `lbt` 0–255) is stored the same way and does not restart `doggy`; the `doggy-lora` sidecar opens that USB serial port at the chosen baud (default 115200, 8N1) then applies `AT+LBT`, `AT+TXCH`, and `AT+RXCH`. LBT defaults to the module's factory value `0`. HF channel 18 is 868 MHz; LF channel 23 is 433 MHz. An 8–128 byte **air passphrase** (`lora.air_key`) is hashed with SHA-256 to derive the AES-256 key; GET JSON only reports `air_key_set`. Both ends must use the same strong passphrase. Set a **system PIN** here (4–64 characters, hashed in `doggy.json`).
- When a save is rejected, the status line shows the reason from the server (for example `Save failed: config lora.txch out of range`). Failed API calls are logged with their JSON error body; a config-write failure logs the file path server-side but does not return it.
- **Power** can restart `doggy.service`, reboot, or shut down the Pi. Each call is `POST /api/system` with the PIN (not the Unix password). Shutdown asks you to type `SHUTDOWN`. Privilege comes from a packaged polkit rule for user `doggy`.

HTTPS: `DOGGY_HTTPS_PORT` (default `443`). HTTP: `DOGGY_HTTP_PORT` (default `80`) only redirects to HTTPS. Unprivileged runs need ports above 1024. TLS files: `DOGGY_TLS_CERT` / `DOGGY_TLS_KEY`, or `tls.crt` / `tls.key` next to the JSON config (generated on first start if missing; browsers warn once on the self-signed cert). HTML: `DOGGY_WEB_ROOT` or `/usr/share/doggy` after the DEB is installed (`index.html`, `rover.html`, `lora.html`). Config: `DOGGY_CONFIG` or `/etc/doggy/doggy.json` (JSON; `robot.type`, `lora`, servo channels, motor `pwm`/`in2`/`in1` channels plus enable/direction, and I2C bus/address for the PCA9685, IMU, and ADS7830). If the file is missing, the process writes compiled defaults there (`postinst` creates `/etc/doggy` owned by `doggy`). A present but invalid file stops startup. `GET /api/config` returns that file shape (hex addresses) plus `system.pin_set` (never the PIN or hash). `PUT /api/config` overlays those fields; a type change also needs `pin` and returns 202 before restart. `POST /api/system/pin` sets the power PIN. `GET /api/status` includes `"type"`, the stamped `version`, hardware errors, cached IMU and battery readings, and either servo PWM/angle (`angle` is omitted when PWM is 0) or rover motors plus last `speed`/`turn`. Domain models are defined in `proto/doggy.proto` (HTTP uses ProtoJSON; the LoRa hop uses binary protobuf).

Existing rover configs using `motors.<name>.channel` must be migrated before
startup: replace `channel` with `pwm` and add `in2` and `in1` for every motor.
Also set `i2c.servo_board.address` to `0x60` for the Adafruit bonnet if an older
config persisted the previous `0x40` default.

The DEB also installs `doggy-lora` (`User=doggy`, `dialout`). Robot mode reads public radio fields from `https://127.0.0.1/api/config` (self-signed cert) and the air passphrase from `/etc/doggy/doggy.json` (the passphrase is not in public GET JSON). It opens `lora.device` at `lora.baud` (115200 8N1 unless you change it), writes Waveshare AT (`+++`, 200 ms pause, then `AT+LBT=0..255`, `AT+TXCH`, `AT+RXCH`, `AT+EXIT`, waiting for `OK` after each command), **keeps the port open**, and answers operator HTTP requests over LoRa (binary protobuf + AES-256-GCM).

Laptop operator (separate packages, not the Pi DEB): `./build-operator.sh` builds an **amd64** Ubuntu package `shaloms-doggy-lora-operator` and/or a Windows NSIS installer. Both bind `http://127.0.0.1:8765/`. Ubuntu enables `doggy-lora-operator.service` and a desktop icon. Windows installs a LocalSystem service `DoggyLoraOperator` (needed for USB serial) and Start Menu/Desktop shortcuts to that URL. Settings live in `/var/lib/doggy-lora/lora.json` or `%ProgramData%\doggy\lora.json`. `/api/lora` is that local file; dog/rover `/api/*` is proxied over the radio. Set the same air passphrase on both ends.

## Protobuf models and transports

`proto/doggy.proto` is the single source of truth for configuration, status, commands, API payloads, and the LoRa request/response envelope. Do not hand-write parallel C++ or Go wire-model classes.

- CMake runs `protoc` and generates C++ into `build/<preset>/generated/doggy.pb.{h,cc}`.
- `protoc-gen-go` generates `lora/internal/pb/doggy.pb.go`; generated protobuf files are build output and are not edited by hand.
- The HTTPS API serializes those generated messages as ProtoJSON, preserving the `.proto` field names.
- The LoRa air hop serializes generated `AirRequest` and `AirResponse` messages as binary protobuf. It then applies AES-256-GCM, fragments the ciphertext into COBS-delimited frames, and protects each frame with CRC32C.
- Each fragment requires an ACK and is sent at most three times. Missing ACKs use bounded exponential waits (1.5, 3, then 6 seconds) plus up to 500 ms of jitter to avoid synchronized retransmissions.
- Native builds can build `protoc` from the pinned protobuf source fetched by CMake. Cross-builds use the host `protoc` because an aarch64 target executable cannot run on the amd64 build host.

Logs go to `/var/log/doggy/doggy.log` (override with `DOGGY_LOG_DIR`). The file is rolled on every start and when it exceeds the size cap; 10 files are kept and rolled copies are named `doggy-YYYY-MM-DD-HHMM.log.gz`. The log directory is `0755` and log files are world-readable (`0644`). `/api/...` requests are logged except successful `GET /api/status` (the UI polls it at 5 Hz). Config saves log the resulting robot type and whether it changed; they never log the PIN. HTTP failures (4xx/5xx) and hardware errors are logged at error severity. The packaged unit sets `LogsDirectory=doggy` so the directory is owned by user `doggy`.

## Package and install on a remote Pi

```sh
./build.sh                                  # native-release on aarch64; cross otherwise
./install.sh user@hostname                  # newest shaloms-doggy_*.deb under build/
./install.sh user@hostname path/to.deb      # explicit package
./install.sh --dry-run user@hostname        # print scp/ssh only
```

`./build.sh` uses `uname -m`: `aarch64` / `arm64` → `native-release`; any other arch → `ubuntu-aarch64-cross`. No sysroot. The `.deb` is Debian architecture `arm64` even when packaged on an amd64 host. `--dry-run` prints the cmake commands without running them. Equivalent manual commands:

```sh
cmake --preset native-release
cmake --build --preset native-release --target package
```

## Watch firmware

`watch/` is a separate PlatformIO project for the LilyGO T-Watch S3 and
Waveshare ESP32-C6 Touch AMOLED 2.06. It is not packaged in the Pi DEB.

```sh
./build-watch.sh all
./install-watch-s3.sh                 # flash LilyGO T-Watch S3
./install-watch-c6.sh                 # flash Waveshare ESP32-C6
```

The first slice provides an LVGL clock, ESP-Touch v2 Wi-Fi provisioning, NTP,
and a swipe-accessible fixed UTC-offset settings page (default UTC+3:00). See
`watch/README.md` for flashing and hardware verification. Flash is pinned to
esptool 5.3.1 (5.4.0 crashes mid-upload). Rover control and watch LoRa are
deferred.

The script copies the `.deb` to `/tmp` over SSH and runs `sudo apt-get install`. Copy and install share one SSH connection (ControlMaster), so the login password is asked once; `sudo` may still ask once if the account is not passwordless. The package creates system user `doggy` if needed, enables I2C in `/boot/firmware/config.txt` when missing, and may ask you to reboot. It then enables and **restarts** `doggy.service` and `doggy-lora.service` (`User=doggy`, I2C via the `i2c` group, USB serial via `dialout`) before any reboot prompt. An upgrade does not stop the unit in `prerm`, so a failed `postinst` cannot leave the dog down.

CMake configure fetches cpp-httplib v0.52.0, plog 1.1.11, zlib 1.3.1, nlohmann/json 3.11.3, and Mbed TLS 3.6.7 (needs network once, or set `FETCHCONTENT_SOURCE_DIR_CPP_HTTPLIB` / `FETCHCONTENT_SOURCE_DIR_PLOG` / `FETCHCONTENT_SOURCE_DIR_ZLIB` / `FETCHCONTENT_SOURCE_DIR_JSON` / `FETCHCONTENT_SOURCE_DIR_MBEDTLS`). The system PIN is hashed with Mbed TLS SHA-256; HTTPS uses the same Mbed TLS tree.

Each build stamps the version as `1.0.0-YYYY-MM-DD-HHMM-<short-git-hash>` (local time). The DEB filename uses the same string. The process prints it when it starts; `GET /api/status` and the web title line show it too.

## Target on-disk layout

After `apt-get install` of `shaloms-doggy` the Pi has:

```
/usr/bin/doggy                         firmware (systemd ExecStart)
/usr/bin/doggy-lora                    LoRa sidecar (robot mode on the Pi)
/usr/share/doggy/index.html            dog web UI
/usr/share/doggy/rover.html            rover web UI
/usr/share/doggy/lora.html             LoRa setup UI
/etc/systemd/system/doggy.service      unit (User=doggy)
/etc/systemd/system/doggy-lora.service sidecar unit (User=doggy, dialout)
/etc/doggy/                            created at install (User=doggy, mode 0755)
/etc/doggy/doggy.json                  firmware + robot-side LoRa configuration
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

## Laptop LoRa operator (Ubuntu + Windows)

x86_64 only. Does not build Pi firmware. Needs `golang-go`; Windows NSIS also needs `nsis` and `nsis-common` (stubs and plugins such as MUI2 and nsExec — `./install-prereqs.sh --mode windows`).

```sh
./build-operator.sh                # Ubuntu DEB and Windows NSIS
./build-operator.sh linux         # shaloms-doggy-lora-operator_*.amd64.deb
./build-operator.sh windows        # doggy-lora-operator-*-win64.exe
./build-operator.sh --dry-run
```

CMake presets: `operator-linux-amd64`, `operator-windows-amd64` (`--target package`). The Windows binary is `GOOS=windows GOARCH=amd64` with `CGO_ENABLED=0` (no MinGW). The installer creates the `DoggyLoraOperator` service (LocalSystem, localhost HTTP) using CPack NSIS, same idea as other CPack NSIS projects.

## Tests

CTest covers installer scripts, the SSH helper, PWM math, HTTP API, and log rotation (no I2C):

```sh
cmake --preset native-debug
ctest --preset native-debug
```

Or: `bash tests/install-prereqs.test.sh`

## Source layout

```
proto/doggy.proto             protobuf source of truth for all wire models
src/                          C++ firmware, hardware drivers, HTTPS API, config
lora/cmd/doggy-lora/          Go sidecar/operator executable and mode startup
lora/internal/air/            encrypted protobuf air session, frames, COBS, CRC
lora/internal/atcmd/          Waveshare command generation and reply handling
lora/internal/hop/            robot/operator air-hop runtime
lora/internal/operator/       localhost operator HTTP server and API proxy
lora/internal/pb/             generated Go protobuf package
lora/internal/radio/          serial radio ownership and apply loop
lora/internal/serialport/     Linux/Windows serial configuration
lora/internal/settings/       shared LoRa JSON settings
web/                          dog, rover, and LoRa setup pages
packaging/                    systemd, desktop, polkit, Debian, and NSIS assets
cmake/                        protobuf generation, packaging, and toolchains
tests/                        C++ and shell integration/regression tests
CMakeLists.txt                firmware, sidecar, generated models, tests, install
CMakePresets.json             native, cross, Linux operator, Windows operator
build.sh / build-operator.sh  firmware and laptop package entry points
install.sh                    newest firmware-DEB remote installer
install-watch-s3.sh / -c6.sh  USB flash for each watch board
```

CMake stays at the repository root. Generated files belong under `build/` or
`lora/internal/pb/`; edit `proto/doggy.proto`, not generated `doggy.pb.*` files.
Installed paths are listed under **Target on-disk layout**.

## LoRa setup

The robot and operator each need a Waveshare USB-TO-LoRa-xF device. Configure
the Pi at `https://<pi-ip>/` and the laptop at
`http://127.0.0.1:8765/`:

1. Select **Enabled** and enter the local serial device
   (`/dev/ttyACM0`, `/dev/ttyUSB0`, or `COM3`) and host baud (normally
   `115200`, 8N1).
2. Select the module family: **HF** (850–930 MHz) or **LF** (410–490 MHz).
3. Set TXCH and RXCH on both ends so the radios agree. HF frequency in MHz is
   `850 + channel`; LF frequency is `410 + channel`.
4. Set **LBT** to the module's unsigned 8-bit value (0–255). Start with `0`,
   the factory default. The module accepts the full range but does not document
   its units; do not assume that `1` means simply “enabled”. Apply another value
   only when you have vendor or local regulatory guidance for that radio.
5. Enter the same strong 8–128 byte air passphrase on both ends. It is
   write-only in HTTP responses and is hashed with SHA-256 to derive the
   32-byte AES-256-GCM key.
6. Save both ends. The helper enters command mode with `+++`, waits 200 ms,
   sends `AT+LBT`, `AT+TXCH`, `AT+RXCH`, and `AT+EXIT` sequentially, and waits
   for `OK` before each next command.

Migration: configurations created with the experimental boolean setting must
replace `"listen_before_talk": true|false` with `"lbt": 0`. Legacy fields are
rejected so a stale configuration cannot silently apply the problematic value 1.
Edit `/etc/doggy/doggy.json` on the Pi and `/var/lib/doggy-lora/lora.json` on a
Linux operator host. Startup names the file and the rejected field, for example
`/etc/doggy/doggy.json: invalid config JSON: INVALID_ARGUMENT:(lora)
listen_before_talk: Cannot find field.`

On the Pi, verify `doggy.service` and `doggy-lora.service`; on Linux operator
packages verify `doggy-lora-operator.service`. Service logs show AT commands
and replies but never the air passphrase. If diagnosing a module manually,
enter command mode and use `AT+HELP`, `AT+LBT?`, `AT+TXCH?`, and `AT+RXCH?`,
then finish with `AT+EXIT`.

The HF module can power up with generalized 915 MHz settings. For the Israeli
installation described here, constrain operation to 917–920 MHz (HF channels
67–70). Confirm current LBT and other regulatory requirements for the deployment
location; this README is not a substitute for radio certification.