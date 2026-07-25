# Lámpara Corazón

Touch-controlled lamp using a LOLIN ESP32-S2 Mini and three daisy-chained
8-pixel WS2812B blocks (24 pixels total). Each tap on an isolated aluminum
touch plate increases the brightness; after maximum brightness, the next tap
turns the lamp off.

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
5. After the two-second calibration, compare `touch=...` while released and
   touched. A touch should clearly exceed `threshold=...`.
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
