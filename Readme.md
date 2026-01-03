# Marcel MRI Lab (RP2040)

An RP2040-based MRI lab prop with an OLED menu, PWM audio playback, LED effects,
and a hidden dino game. Built for a Waveshare RP2040 Zero using PlatformIO.

## Demo

- https://youtu.be/FraazRMqyzU?si=6j2MVJFrNLZ4VyAl

## Features

- OLED menu with animated diagnostics, image wipes, and text sequences.
- PWM audio playback of stitched WAV clips over DMA.
- WS2812 LED palette effects plus discrete alarm/on LEDs.
- "Relax" mode launches a Dino-style game on the OLED.
- Button-only UI (scroll/select and game controls).

## Hardware

- Board: Waveshare RP2040 Zero (Earle Philhower Arduino core).
- OLED: SSD1306 128x64 I2C at 0x3C.
- Buttons: two momentary switches with internal pull-ups.
- Audio: PWM output on GPIO 0 (feed through an RC filter and amp/speaker).
- LEDs:
  - WS2812 on GPIO 16.
  - Discrete alarm LED on GPIO 11.
  - Discrete power/on LED on GPIO 10.

## Wiring

Pin assignments are defined in `src/main.cpp` and `platformio.ini`:

- OLED SDA on GPIO 8, OLED SCL on GPIO 9 (I2C).
- Button "Down" on GPIO 13 (INPUT_PULLUP to GND).
- Button "Enter" on GPIO 12 (INPUT_PULLUP to GND).
- PWM audio out on GPIO 0.
- WS2812 data on GPIO 16.
- Alarm LED on GPIO 11, On LED on GPIO 10.

LED wiring used on this build:

- Two red LEDs on 5V, each with its own 270 ohm resistor.
- Both red LED cathodes are switched low-side by a BC337 NPN.
- BC337 base uses a 1.5k resistor from GPIO 11, emitter to GND.
- Green LED on GPIO 10 with a 680 ohm series resistor.
- Share ground between the RP2040 and the 5V LED supply.

## Controls and Modes

- Down button: scrolls the menu.
- Enter button: selects a menu item or cancels playback.

Menu mapping (see `src/main.cpp`):

- Localizer: plays the "second try" image wipe sequence.
- Debug Code: plays the diagnostics scrolling sequence.
- rEPSI: plays the "first try" dial animation sequence.
- rEPSI, more averages: runs the alarm "don't try" sequence.
- Relax: launches the Dino game on the OLED.

In the Dino game:

- Enter: jump.
- Down: duck.

## Build and Flash

1. Install PlatformIO.
2. Build:

```sh
pio run -e waveshare_rp2040_zero
```

3. Upload:

```sh
pio run -e waveshare_rp2040_zero -t upload
```

Optional serial monitor:

```sh
pio device monitor -b 115200
```

The OLED I2C pins can be overridden via `platformio.ini` build flags
(`OLED_SDA_PIN` / `OLED_SCL_PIN`).

## Assets

### Audio clips

Audio clips are stored as PWM-friendly arrays in `src/WAVData.h` (or
`src/WAVData2.h` when `-DUSE_WAVDATA2` is enabled).

Note: audio assets are generated locally using the scripts in `wav2pwm`.

### Bitmap images

OLED bitmaps live in `src/PICsData.h` and are generated from the BMPs in
`pics/` 

## Repo Layout

- `src/`: firmware source, OLED UI, audio playback, Dino game.
- `wav2pwm/`: audio conversion tools, WAV sources, generated headers (not sync due toi the size).
- `pics/`: bitmap sources and conversion scripts.

## Credits and Notes

- `src/WavPwmAudio.*` and `wav2pwm/WavConverter.*` are based on work by
  Jason Birch (see headers for license details).
- The Dino game is refactored from https://github.com/tech-nickk/Chrome-Dino-Game-on-Arduino-and-OLED-/tree/master

