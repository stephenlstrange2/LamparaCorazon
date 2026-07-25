# Lámpara Corazón

Touch-controlled and Wi-Fi-controlled lamp using a LOLIN ESP32-S2 Mini and
three daisy-chained 8-pixel WS2812B blocks (24 pixels total). Each tap on an
isolated aluminum touch plate increases the brightness; after maximum
brightness, the next tap turns the lamp off.

The firmware also provides:

- Live capacitive readings through USB serial and a web dashboard
- A password-protected captive access point
- Optional connection to a home Wi-Fi network
- Brightness and RGB color controls
- Browser-based firmware updates (OTA)

## Important electrical notes

- The ESP32-S2 GPIOs are **3.3 V only**.
- Do not power 24 WS2812B pixels from the ESP32-S2 `3V3` pin.
- At full white, 24 WS2812B pixels can theoretically draw about **1.44 A**
  (24 × 60 mA). Use a regulated **5 V, 2 A or larger** supply for the LEDs.
- Join LED-supply GND and ESP32-S2 GND. Without a common ground, the data
  signal will not work reliably.
- A 74AHCT125 or 74HCT14 level shifter from 3.3 V to 5 V is recommended,
  especially if the wire from the controller to the first pixel is long.
- Add a 330–470 ohm resistor in series with the WS2812B data wire and a
  500–1000 µF capacitor across 5 V/GND near the first LED block.
- The touch plate must be electrically isolated from GND and from any metal
  lamp chassis connected to earth. Never connect the touch plate to mains.

## Wiring

| ESP32-S2 Mini | Connect to | Notes |
|---|---|---|
| GPIO4 / T4 | 510 ohm resistor, then aluminum plate | Touch electrode |
| GPIO16 | Level shifter input, then first block DIN | LED data |
| GND | LED supply GND and all block GND pins | Common ground |
| USB-C | Computer | Programming and serial monitor |

Power all three blocks from the external 5 V supply in parallel. Daisy-chain
data only:

`GPIO16 -> block 1 DIN`, `block 1 DOUT -> block 2 DIN`,
`block 2 DOUT -> block 3 DIN`.

Do not use GPIO19 or GPIO20 for the lamp: they are USB D- and D+ on the
ESP32-S2. GPIO15 is avoided because it drives the S2 Mini's onboard LED.

If USB and the external LED supply are connected at the same time, keep the
grounds common but do not connect the external 5 V output to the computer's
USB 5 V/VBUS unless the power design explicitly prevents back-feeding.

## How capacitive touch works

GPIO4 repeatedly measures the capacitance of the aluminum plate. A nearby
finger increases that capacitance, so on the ESP32-S2 the `touchRead()` value
rises. Firmware averages samples, calibrates the untouched baseline for two
seconds at boot, and regards a roughly 20% rise as a touch. It uses hysteresis
and debounce to prevent a single press from registering multiple times.

Keep the plate wire short and away from LED power/data wiring. Espressif
recommends a 470 ohm–2 kohm series resistor close to the ESP32-S2; 510 ohm is
a good starting value. Plate shape, insulation, wiring, and the power supply
all affect readings, so confirm the values in the serial monitor after final
assembly.

## Build, upload, and test

This repository uses PlatformIO and already declares Adafruit NeoPixel.

1. Connect the board to the computer using a **USB data cable**.
2. Leave the aluminum plate untouched, then upload with PlatformIO.
3. If upload cannot find the board, hold button `0`/`BOOT`, press and release
   `RST`, release `0`, and upload again. This enters the ROM USB bootloader.
4. Open the serial monitor at 115200 baud.
5. After the two-second calibration, compare `raw=...` while released and
   touched. A touch should clearly exceed `press=...`.
6. Tap and release the plate. Brightness steps through
   `0, 24, 64, 128, 192, 255`, then returns to `0`.

Command-line equivalents (when PlatformIO is on `PATH`):

```sh
pio run
pio run --target upload
pio device monitor --baud 115200
```

The serial port may disconnect and reappear during flashing because the S2
Mini uses the ESP32-S2's native USB connection rather than a separate
USB-to-serial converter.

To see USB logs in VS Code:

1. Wait for the upload to finish and the ESP32-S2 to restart.
2. Run **PlatformIO: Monitor** separately.
3. If it opens the old port, close it, press `RST`, and run the monitor again.

Typical sensor output is:

```text
[4.5s] CAP raw=12345 baseline=12100 press=14520 state=released
[7.1s] TOUCH reading=17320 baseline=12120 -> brightness=64
```

On ESP32-S2, touching the plate should make `raw` increase above `press`.

## Web dashboard and captive portal

After every boot, the lamp creates this Wi-Fi network:

| Setting | Value |
|---|---|
| Network | `LamparaCorazon` |
| Wi-Fi password | `corazon24` |
| Dashboard | `http://192.168.4.1` |

Connect a phone or laptop to `LamparaCorazon`. The dashboard may open
automatically as a captive portal. If it does not, browse directly to
`http://192.168.4.1`.

The dashboard displays live `raw`, `baseline`, threshold, touch events, Wi-Fi
status, and other recent logs. It can also set lamp brightness and color.

### Connect the lamp to home Wi-Fi

Enter the home network name and password on the dashboard. When asked for
administrator credentials, use:

| Setting | Value |
|---|---|
| User | `admin` |
| Password | `corazon32` |

The credentials are stored in ESP32 nonvolatile memory. The dashboard reports
the home-network IP after it connects. The lamp access point remains active,
so `192.168.4.1` remains available even when home Wi-Fi is unavailable.

Change `AP_PASSWORD` and `ADMIN_PASSWORD` near the top of `src/main.cpp`
before treating the lamp as a finished device.

## Browser OTA update

USB is required for the first installation. Later updates can be installed
from the dashboard:

1. Compile with **PlatformIO: Build** or `pio run`.
2. Open the lamp dashboard.
3. Under **Firmware update**, select:
   `.pio/build/lolin_s2_mini/firmware.bin`
4. Submit the form and enter `admin` / `corazon32` when requested.
5. Keep the lamp powered and wait for the completion message and restart.

Only upload `firmware.bin`, not `firmware.elf` or `bootloader.bin`. If an OTA
update is interrupted, the currently installed firmware should remain
bootable; use USB recovery if necessary.

### Linux `Errno 71: Protocol error`

The project disables esptool's DTR/RTS reset because some Linux USB stacks do
not support that operation on the S2's native `/dev/ttyACM*` port. PlatformIO
first performs the supported 1200-baud USB reset, then esptool connects without
resetting a second time.

If automatic upload still does not enter the bootloader:

1. Close every serial monitor using `/dev/ttyACM0`.
2. Hold the board's `0` button.
3. Press and release `RST`.
4. Release `0`.
5. Start **PlatformIO: Upload**.

After a successful upload, press `RST` once if the application does not begin
automatically. The `/dev/ttyACM` number can change after a reset.

## Troubleshooting touch

- **Touch never triggers:** watch the serial values. If touch rises by less
  than 20%, enlarge the plate, shorten its wire, or reduce the percentage in
  `pressThreshold`.
- **Triggers by itself:** route the touch wire away from LED wiring and the
  switching supply, verify the common ground, or raise the threshold.
- **Multiple steps per touch:** ensure the finger is fully released; the
  firmware changes brightness only on a debounced press edge.
- **Unstable after installation:** reset the board with the plate untouched
  so it recalibrates in its final environment.

## Source references

- [LOLIN S2 Mini board page and schematic](https://www.wemos.cc/en/latest/s2/s2_mini.html)
- [Espressif ESP32-S2 touch sensor design guidance](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s2/schematic-checklist.html#touch-sensor)
- [Espressif USB CDC flashing guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/cdc_dfu_flash.html)
