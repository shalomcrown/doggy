# Doggy watch firmware

PlatformIO firmware for:

- LilyGO T-Watch S3 (`twatch-s3`)
- Waveshare ESP32-C6 Touch AMOLED 2.06 (`waveshare-c6`)

Both environments compile the same LVGL 9 clock, settings, ESP-Touch v2, NTP,
and NVS code. `src/board_*.cpp` is the hardware boundary for display and touch.
LoRa is not on the watch yet; rover control lives on the Control screen (see
Navigation below).

## Build and flash

Install PlatformIO Core 6.2.0 or newer once with the rest of the repo tools:

```sh
./install-prereqs.sh
```

Debian/Ubuntu `apt` still ships PlatformIO 4.x, which cannot load the pinned
Arduino-ESP32 3.3 platform. `install-prereqs.sh` therefore uses `pipx`.

From the repository root:

```sh
./build-watch.sh all
./build-watch.sh twatch-s3
./build-watch.sh waveshare-c6

./install-watch-s3.sh                 # LilyGO T-Watch S3
./install-watch-c6.sh                 # Waveshare ESP32-C6
./install-watch-s3.sh --port /dev/ttyACM0
```

USB-Serial/JTAG boards (T-Watch S3 and the C6) enter the bootloader when
esptool opens the port. Holding BOOT while plugging in USB is not required.

With no `--port`, the installers pick the watch by its `/dev/serial/by-id` name,
which is built from the chip's serial number and therefore survives the
re-enumeration that entering the bootloader causes. A bare `/dev/ttyACM` number
does not: the kernel assigns those in enumeration order, so with two watches
attached the number can point at the other board after a reset. When both are
attached the installer stops and prints both names — pass one to `--port`.

A watch already running this firmware can be flashed at any time. Older
firmware light sleeps after its idle timeout, and the flash will fail with
`OSError: [Errno 71] Protocol error` until the screen is touched, because light
sleep takes the USB-Serial/JTAG peripheral down while leaving the port
enumerated.

Watch flash is pinned to pioarduino `tool-esptoolpy` 5.3.1. Version 5.4.0
raises `AttributeError: EsptoolLogger has no attribute _get_progress_print_file`
after connecting, so the original firmware stays on the device.

The equivalent CMake targets are `watch-s3` and `watch-c6`. They are explicit
targets and are not dependencies of the Raspberry Pi firmware or DEB.

The first `pio run` downloads the Arduino-ESP32 3.3 toolchain, LVGL 9.3,
LilyGoLib, and Arduino_GFX 1.6.8. That can take several minutes with no compiler
output. Later builds reuse `watch/.pio/`.

## First boot

1. The watch tries its own saved networks first, most recently joined first, up
   to five of them at 8 seconds each.
2. It then asks the ESP32 Wi-Fi stack to reconnect with whatever credentials the
   core itself stored, which is what a watch upgraded from an earlier build has.
3. Only when all of those fail does it start **ESP-Touch v2**. Use an
   ESP-Touch v2-compatible phone app to send the 2.4 GHz SSID and password.
   Provisioning is unencrypted, so leave the app's AES key field empty. The
   phone must be associated to the 2.4 GHz band; the watch has no 5 GHz radio
   and ESP-Touch reads the packets the phone sends through its own AP.
   **Pair Wi-Fi** in settings starts the same thing immediately, without waiting
   out the roster.
4. Once connected, SNTP uses `pool.ntp.org` and `time.nist.gov`. The clock
   explicitly says it is waiting until a plausible UTC value is available.

ESP-Touch custom data is ignored. Every successful join is written to a
five-entry roster in NVS, newest first, and rejoining the network already at the
front costs no flash write. The ESP32 Wi-Fi stack keeps its own copy of the most
recent credentials as well.

**The roster is stored in plaintext.** NVS is not encrypted on either board, so
anyone who can read the flash can read the saved passwords — the same exposure
the Wi-Fi stack's own stored credentials already carry. Enabling flash
encryption is the fix, and it is out of scope for this slice.

## Sleep and timekeeping

After a minute without a touch the watch blanks its panel, drops the radio, and
halts the CPU in light sleep. Touching the screen wakes it. The T-Watch S3 also
wakes on a wrist raise (the BMA423 tilt gesture; the step, activity, and
motion interrupts are masked during sleep so an arm swing does not wake it).
The Waveshare C6 wakes on QMI8658 wake-on-motion (INT1, GPIO16) at a 250 mg
threshold — that is not a tilt gesture, so walking can still wake it. That line
toggles on each motion event rather than resting at a level, so the C6 samples
it as sleep begins and arms the opposite level; arming a fixed level would end
the sleep the moment it started. Each wake prints the GPIO mask that ended it to
the USB serial monitor at 115200 baud, so a wake nobody asked for can be traced
to a pin (a `0x0` mask means neither line woke it — suspect the attached USB
monitor itself).

A watch plugged into a computer blanks its panel at the same deadline but does
not light sleep, because light sleep stops clocking the USB-Serial/JTAG
peripheral: the port stays enumerated while nothing answers it, and neither
`pio device monitor` nor a flash can reach the chip until someone touches the
screen. The wake lines are polled instead, which costs run current the cable is
already paying for. On battery, or on a charger with no host, the watch light
sleeps exactly as before.

The timeout is set on the settings page — 15 s, 30 s, 1 min, 2 min, 5 min, or
**Never**. Waking restarts the network walk from the saved roster, so the watch
reconnects without ESP-Touch. Sleep is held off entirely while ESP-Touch is
listening, since the phone needs both the screen and the radio up.

Both watches have a PCF85063 calendar chip. On the Waveshare C6 it shares I2C
with touch, the AXP2101, and the IMU (SDA GPIO8, SCL GPIO7). Every NTP sync is
copied to the board's chip, a cold boot seeds the clock from it, and every wake
re-anchors the ESP system clock from it before Wi-Fi restarts. This avoids the
ESP sleep clock accumulating seconds of error during a long sleep. The chip
holds UTC regardless of the configured offset. The calendar write happens in
`loop()` rather than in the SNTP callback because the chip shares that bus with
the power gauge the UI polls.

The C6 releases Arduino `Wire` immediately before real light sleep and restores
it at 400 kHz immediately after wake. This prevents the first post-wake shared
bus transaction from reaching an inactive ESP-IDF I2C driver.

`timegm` is missing from the ESP32 C library and `mktime` would fold in the
local zone, so `watch_utc_time_from_civil()` in `time_offset.cpp` does the
calendar-to-epoch conversion and is host-tested against `gmtime_r`.

## I2C map

LilyGoLib's `instance.begin()` on the T-Watch S3 prints two bus scans at boot
(and a `Listing directory: /` dump of the onboard filesystem). Those scans are
not discovery. A live S3 capture on 2026-09-24:

| Bus (boot order) | Address | Device |
|------------------|---------|--------|
| First (PMU / RTC / IMU / haptic) | `0x19` | BMA423 accelerometer |
| First | `0x34` | AXP2101 PMIC |
| First | `0x51` | PCF85063-class calendar |
| First | `0x5A` | DRV2605 haptic |
| Second (touch) | `0x38` | FT6336U touch |

The Waveshare C6 shares one bus (SDA GPIO8, SCL GPIO7). Addresses come from the
board HAL, not from that S3 log:

| Address | Device |
|---------|--------|
| `0x34` | AXP2101 PMIC |
| `0x38` | FT3168 touch |
| `0x51` | PCF85063 calendar |
| `0x6B` | QMI8658 IMU |

`[E][Wire.cpp:135] setPins(): bus already initialized` is LilyGoLib probing a
bus it already started. It is not an mDNS failure.

`watch_network.cpp` calls `esp_smartconfig_start()` rather than
`WiFi.beginSmartConfig()`: the Arduino wrapper turns on
`esp_touch_v2_enable_crypt` with a NULL key whenever the type is v2, so the
watch silently waits for AES packets that no phone app sends.

## Clock and settings

Both pages draw white on black. The clock page has a status bar across the top:
the joined SSID on the left (truncated with an ellipsis when it is long, or
`No Wi-Fi` when disconnected), then the battery symbol with its percentage, then
a four-bar Wi-Fi meter driven by RSSI (−55/−65/−75/−85 dBm thresholds).

Below the clock, the date reads `Mon 2026-09-21`, and a smaller line reports
`Time synchronized HH:MM:SS ago`. That age comes from the SNTP sync callback, so
it measures the last real NTP update rather than the Wi-Fi connection. It reads
`Time not synchronized` until the first update and clamps at `99:59:59`.

A separate line near the bottom reports the network path only: `Connecting to
Wi-Fi`, `Waiting for ESP-Touch v2`, or `Waiting for NTP`. It is blank while the
clock is good, because the synchronized-age line above already says so. If Wi-Fi
drops later it reappears while the age line keeps counting.

The C6 panel is a rounded rectangle, so anything drawn against a screen edge
disappears under the corner mask. `watch_board_safe_inset()` is the board's
answer to "how many pixels of every edge can this panel eat" — 48 on the C6, 0
on the square T-Watch S3 LCD — and both pages pad their content by that much.
A corner of radius R needs an inset of at least `R * (1 - 1 / sqrt(2))`, so 48
covers a radius up to roughly 160 px. Retune that one constant if the C6 status
bar still clips or the margin looks too generous.

Both boards report battery percentage through an AXP2101 gauge and show a
charging bolt while USB power is charging the pack. If the C6 cannot see a
battery, the status bar still shows `--` rather than a fabricated level.

The factory offset is UTC+3:00. Swipe **right** from the clock (or tap
**< Control**) for **Rover** control; tile swipes are disabled on that page so
the joystick does not drag you back to the clock — tap **< Clock** to leave.
Swipe **left**, or tap **Settings >**, for settings. The page scrolls vertically: UTC offset (whole hours from −12 through
+14, minutes from 00, 15, 30, or 45), the sleep timeout, **Pair Wi-Fi**, then
**Save** and **Cancel**. Swipe left twice more for **Doggys**: **Refresh** runs
a bounded LAN browse of `_doggy._tcp`, and tapping a result stores that robot
for control. With no doggy selected, the control page says so. With a rover
selected, the joystick matches the web page (15% dead zone, forward/back and
left/right). While that page is visible the watch posts `POST /api/heartbeat`
every 750 ms and `POST /api/drive` when speed or turn change (about every 50 ms
while the stick moves, immediately on release). HTTPS runs on a dedicated
FreeRTOS task so the UI keeps swiping when the Pi is offline; one TLS session is
reused per selected doggy. Leaving the page stops both and sends a zero drive.
HTTPS uses the self-signed Pi cert via `setInsecure()` on the LAN only. Settings, Doggys, and Control keep
SSID and battery in the status bar and put local `HH:MM:SS` in the middle so
the clock stays visible while discovery or control runs.

Nothing on that page reaches storage until **Save**. Moving a roller only edits
a draft, and the draft is thrown away by **Cancel**, by **< Clock**, by swiping
back, and by falling asleep — so a pocket touch cannot quietly change the clock.

This is a fixed UTC offset. It does not include IANA tzdata and does not change
automatically for daylight saving time.

## Hardware verification

Hardware is not available to CI. Before relying on a build:

1. Flash each environment and verify display orientation and touch coordinates.
2. Provision with ESP-Touch v2 and reboot; it should reconnect without
   provisioning again.
3. Verify the waiting state changes to the correct UTC+3 clock after NTP.
4. Change both offset controls, reboot, and confirm the setting persists.
5. Confirm the status bar fits: SSID, battery, and Wi-Fi bars must not overlap
   on the 240×240 S3, and the synchronized-age line must not wrap.
5b. On the C6, confirm the whole status bar clears the rounded corner mask —
   the SSID on the left and the Wi-Fi bars on the right are the first things to
   disappear. Adjust `kSafeInset` in `board_waveshare_c6.cpp` if it still clips.
6. Confirm the battery percentage tracks the charger (bolt appears on USB) and
   that the Wi-Fi bars drop as you walk away from the access point. On the C6
   the gauge is the onboard AXP2101; `--` is only correct when no pack is
   connected.
7. Leave the watch untouched for the configured timeout: the panel must blank,
   and a touch must bring it back on the clock page with the screen fully
   redrawn. On the S3, also confirm a wrist raise wakes it and that walking does
   not. On the C6, a firm wrist flick should wake it via the QMI8658; walking
   may also wake it. After a one-hour sleep, compare the displayed time with a
   synchronized clock; it should be within the PCF85063's one-second display
   resolution. The C6 serial log must not contain post-wake
   `ESP_ERR_INVALID_STATE` I2C errors.
8. Set the timeout to **Never** and confirm the watch stays on.
9. Move a roller, then leave the page three ways — Cancel, swipe, and letting it
   sleep — and confirm the stored offset never moved. Then move it again and
   press **Save**, reboot, and confirm it persisted.
10. Join a second access point, then power both off and on in turn: the watch
    must rejoin each without ESP-Touch, and the one joined most recently must be
    tried first.
11. Let NTP sync, then power the watch down completely and boot it with Wi-Fi
    unavailable — the clock must come up on RTC time rather than the waiting
    state (PCF85063 on both boards).
12. With a packaged doggy on the LAN, open **Doggys**, tap **Refresh**, and
    confirm the large clock on the first page and the centered status-bar time
    on Settings/Doggys keep advancing during the search. Select a result,
    reboot, and confirm the same name still shows as selected. With Wi-Fi off,
    the page must say to connect Wi-Fi rather than freeze.

### Reading a failed discovery

Every search reports itself over USB serial (`pio device monitor` at 115200).
Each `discovery:` line ends with CR+LF so the monitor does not stair-step.
Confirm the robot is advertising first — `avahi-browse -r _doggy._tcp` on a
laptop on the same network must resolve a hostname, port 443, and the
`proto=https` TXT record. Then tap **Refresh** and read the log:

| Log line | Meaning |
|----------|---------|
| `mDNS begin … FAILED` | The mDNS stack did not start; nothing was ever sent. |
| `query start FAILED` | mDNS is up but refused the query. |
| `doggy search raw=0` `attempt=1/3` then later `accepted=1` | The first query raced `MDNS.begin()`. Retries are expected; this is not an empty LAN. |
| `doggy search raw=0` on `attempt=3/3` then `probe raw=0` | No mDNS answers of any kind reach the watch after three tries. Suspect the access point (band steering between 2.4/5 GHz, client isolation, multicast/IGMP filtering), not the query parser. |
| `doggy search raw=0` on `attempt=3/3` then `probe raw=N` listing `_doggy._tcp` | The watch hears the network and the robot advertises, but the doggy query itself came back empty. |
| `doggy search raw=N accepted=0` | Answers arrived and were rejected. Read the `result …` lines: `port=0` or `hostname=(none)` means the SRV record did not arrive with the PTR answer. |
| `search did not finish in time` | The query never completed inside its window plus grace period. |

Host CTest always covers UTC-offset math and `build-watch.sh`. Firmware compile
is opt-in (`DOGGY_TEST_WATCH_FIRMWARE=1 ctest --preset native-debug -R watch-`)
so a normal Pi `ctest` stays fast after PlatformIO is installed.
