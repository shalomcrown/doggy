# Electronics of doggy / rover


## Design of Pi hat for doggy/rover

A Raspberry Pi HAT for **Pi 3 Model B+** and later 40-pin boards (Pi 3B+, Pi 4, Pi 5). It uses the HAT mounting holes, a stacking 40-pin header, and an ID EEPROM. The board may extend past the 65 mm × 56.5 mm HAT outline. Sixteen servo connectors do not fit inside that outline, so the extension is expected. Keep the Pi 3 CSI ribbon, USB, Ethernet, HDMI, and the SD card usable.

The device bus is I2C1 on GPIO2/GPIO3. GPIO0/GPIO1 stay the HAT ID bus.

### Requirements

* One battery input: a screw-terminal pair. The Pi is powered only through the header.
* Pack voltage and total input current, one INA228. Cutoff is a software setting (cell count or minimum voltage). The pack is 4S today and may be up to 8S.
* 16 servo channels for SG92R-class servos, on a regulated 5 V rail.
* 4 DC motor channels, 1 A stall each, motor voltage set in the 5–12 V range.
* IMU, compass, and barometer.
* One sensor per wheel, wired for either a single channel or a quadrature pair. The mode is a firmware setting.
* I2C1 uses the pull-ups already fitted on the Pi. The ID bus has its own pull-ups.

### Power

* **Input:** one screw-terminal pair, battery positive and negative. No second power connector. The Pi's own USB power socket stays unused while the hat is fitted.
* **Range:** the input path is rated for a 4S pack at cutoff through an 8S pack at full charge. Electrical window is 10 V to 36 V operating, 40 V absolute, so 8 × 4.2 V (33.6 V) still has margin. Software may set the cutoff higher. It cannot rely on the regulators below 10 V.
* **INA228** high-side shunt on this input, ahead of every regulator. Terminals, shunt, and input copper are sized for 15 A, which covers the Pi, sixteen servos at stall, and four motors at stall while the pack is at the low end of the window.
* Reverse-polarity protection on the pack input. A fuse on the pack input.
* **Pi rail:** buck to 5 V at 5 A, into header pins 2 and 4, with ground on the header ground pins. An ideal diode on this feed so the hat and a USB supply on the Pi cannot back-feed each other. The hat's fuse is the protection: back-powering bypasses the Pi 3 polyfuse. The hat does not draw logic power from the Pi 3.3 V pin.
* **Hat 3.3 V:** its own regulator from the Pi 5 V rail, for the PCA9685s, IMU, barometer, INA228, encoder microcontroller, and the ID-bus pull-ups.
* **Servo rail:** buck to 5.0 V. SG92R-class servos run from 4.8 V to 6.0 V. Tower Pro quotes torque at 4.8 V and does not state stall current; allow 0.75 A stall per servo. The rail, its fuse, the connectors, and the copper are sized for 16 × 0.75 A (12 A).
* **Motor rail:** a regulator set by resistors to a fixed voltage in the 5–12 V range, at 4 A continuous (4 × 1 A stall). It has to hold that voltage across the whole pack window, including a 12 V rail while a 4S pack is near cutoff, so this regulator is a buck-boost. The chosen voltage is an assembly setting, recorded on the board.
* Servo and motor current return to their own rails. Header ground is the logic reference. The three rails join the pack at one point.
* Bulk capacitance at the motor drivers.
* **E-stop:** a switch, independent of the Pi, drops the servo rail and the motor rail. The Pi rail stays up.
* **Pack cutoff:** firmware writes the INA228 alert from the software setting, then arms a latch. The latch drops the Pi rail and both actuator rails, and it stays off until a manual reset. The alert covers low pack voltage and overcurrent. Actuator rails stay off until the firmware has armed the latch and the pack is above the cutoff.

### Software settings

These are firmware configuration, not resistors:

* Pack cutoff, defaulting to the present 4S pack. Either a cell count from 4 to 8, turned into a voltage with a per-cell floor, or an explicit minimum pack voltage. The INA228 alert uses that voltage.
* Wheel sensors: quadrature, or single channel. Single channel counts pulses on A and takes direction from the motor command.

### Servos and motors

The PCA9685 produces 3.3 V PWM. It does not switch motor current. One PCA9685 has 16 channels, and the servos use all of them, so the motors need a second PCA9685. The Pi has two hardware PWM channels, which is not enough for four motors.

* **Servo PCA9685** at `0x40`. Sixteen 3-pin headers (signal, 5 V servo power, ground), grouped to match the leg harness. Series resistor on each signal. `OE` pulled inactive (outputs off) until software enables them.
* **Motor PCA9685** at `0x60`. Four H-bridge channels, each rated for at least 1 A continuous. Each motor uses three logic signals: `PWM`, `IN1`, `IN2`.
  * `IN1`/`IN2` opposite: forward or reverse, speed from `PWM`
  * both high: short brake
  * both low: coast
* Driver standby/enable is pulled off at power-up, so the wheels coast until software enables them. Outputs return to that off state if the Pi stops refreshing within the motor watchdog (about 100 ms).

This split matches the firmware: the dog uses a PCA9685 at `0x40` for servos, and the rover uses `0x60` with `pwm` / `in1` / `in2` per motor.

### INA228

Address `0x45` (`A1` and `A0` tied to VS), clear of both PCA9685 addresses. The alert pin drives the cutoff latch. The Pi also reads pack voltage and current over I2C.

### Sensors

* **IMU** at `0x68` on the hat (the body MPU6050 the firmware already reads). Data-ready pin to a Pi GPIO.
* **Barometer** at `0x76` on the hat.
* **Compass** on a small board at the end of an I2C cable, away from motor current and the pack. On this PCB it would read the motors. Address `0x0D` (QMC5883L) or `0x1E` (HMC5883L). No pull-ups on that board. Cable length under 30 cm.
* **Wheels:** one connector per wheel, with A, B, 3.3 V, and ground. A single-channel sensor uses A and leaves B open. A quadrature sensor uses A and B in the same package. Four LM393 channels cover the single-channel case; quadrature uses eight comparator channels into the microcontroller. Open-collector outputs are pulled up to hat 3.3 V, then a 3.3 V Schmitt trigger into the microcontroller. The microcontroller publishes counts and a reset over I2C. The Pi reads counts over I2C. Interrupt to a Pi GPIO.

### I2C

GPIO2 and GPIO3 already have about 1.8 kΩ pull-ups to 3.3 V on the Pi. A second pair on the hat would push the sink current past the 3 mA I2C limit, so I2C1 has no pull-ups on this board.

* Run the device bus at 400 kHz. With the Pi's pull-ups and the devices on this hat, rise time stays inside Fast-mode for a compass cable under 30 cm.
* Series resistors of 47 Ω in SDA and SCL next to the GPIO header, to damp the edges.
* The compass board adds capacitance and no pull-ups.

The ID bus is separate. ID_SD and ID_SC connect only to a 3.3 V, 16-bit-address, non-paged EEPROM (CAT24C32 or equivalent) at `0x50`, plus 3.9 kΩ pull-ups to hat 3.3 V. Write-protect goes to a test point, pulled up. The EEPROM is specified for 100 kHz and does not clock-stretch. Nothing else connects to those pins.

### I2C map

| Device | Bus | Address |
|---|---|---|
| HAT EEPROM (CAT24C32) | ID (GPIO0/GPIO1) | `0x50` |
| PCA9685, servos | I2C1 | `0x40` |
| PCA9685, motors | I2C1 | `0x60` |
| INA228 | I2C1 | `0x45` |
| IMU | I2C1 | `0x68` |
| Barometer | I2C1 | `0x76` |
| Compass (off-board) | I2C1 | `0x0D` or `0x1E` |
| RP2040 encoder microcontroller | I2C1 | `0x33` |

### Header pins left for later

* UART on GPIO14/GPIO15 and a PPS input, so GPS can leave USB later. The receiver stays off this board. On Pi 3 this UART is shared with Bluetooth, so Bluetooth has to move if both are on.
* LoRa stays on USB. No radio on the hat.

### Bring-up

* LEDs and test points for pack input, Pi 5 V, hat 3.3 V, servo 5 V, and the motor rail.
* Test points on servo `OE`, motor enable, the cutoff latch, and EEPROM write-protect.

### Datasheets

Before a chip is placed in the schematic, its manufacturer datasheet is in `datasheets/` at the repo root. Use a file already there when it matches the part. Otherwise download it as part of implementation. Symbols, footprints, and pin wiring come from that datasheet.

Already present: `PCA9685.pdf`, `TB6612FNG_datasheet_en_20121101.pdf`, `DS_SX1261-2_V1.2.pdf`, `raspberry_pi_schem1.png`.

### Encoder microcontroller

**RP2040.** The encoders are low resolution, and the board has to be programmed with a normal USB cable. The RP2040 bootloader is in mask ROM: hold BOOTSEL, plug in USB, and copy a UF2 file. A blank flash chip is enough. No UPDI, SWD, or ISP adapter.

* QFN-56 on this board, with a USB-C device receptacle on the board edge, a BOOTSEL button, a 12 MHz crystal, and a QSPI flash (2 MB is enough). USB data goes only to the RP2040. It is not wired to the Pi.
* Follow the Pico reference for the flash, crystal, and BOOTSEL wiring. Those parts come from the RP2040 and flash datasheets at implementation.
* I2C slave at `0x33`. One interrupt output to the Pi. Quadrature or single-channel is the firmware setting. PIO counts the edges.
* The slave must not clock-stretch. Pi 3 I2C loses the bus when a slave holds the clock. Counts stay in memory and the transfer only shifts those bytes out.
* Power: the hat 3.3 V rail runs the chip when the pack is on. USB VBUS can power the RP2040 alone, through its own regulator, so the chip can be programmed with no battery connected. The rest of the hat stays off in that case.
* An ESP32 is not used. Its radio would sit next to the compass and the motor PWM.
