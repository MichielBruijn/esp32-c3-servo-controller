# ESP32 Servo Tester

A standalone servo tester/sweeper built around an ESP32 and a small OLED display, controlled entirely with a single rotary encoder + push button. No app or PC required.

## Credits

This project did not start from scratch — it stands on the work of others:

- Originally designed by **"Der RC Modellbauer"** — [Ziege-One/Servotester_Deluxe](https://github.com/Ziege-One/Servotester_Deluxe)
- Extended into **Servotester Deluxe** by **TheDIYGuy999** — [TheDIYGuy999/Servotester_Deluxe](https://github.com/TheDIYGuy999/Servotester_Deluxe)
- This repository is a **heavily modified fork** of TheDIYGuy999's version. See [Changes in this fork](#changes-in-this-fork) below for what's different.

All credit for the original hardware design, PCB and the bulk of the firmware architecture (MCPWM servo generation, oscilloscope, signal reading, web interface, menu system) goes to the two authors above.

## Features

- 5-channel PWM signal generator, multiple servo modes: STD, NOR, SHR, SSR, SUR, SXR
- Manual position control or automatic sweep (auto-oscillate) mode
- Per-channel Min / Center / Max calibration, saved to EEPROM
- Per-channel adjustable rotation angle (e.g. 90° / 180° / 360°) for the on-screen degree readout
- PWM signal read function (pulse width + frequency)
- PPM (Multiswitch), SBUS and IBUS read function
- Oscilloscope for 0-3.3V RC signals
- Signal generator output on GPIO 26
- Operation via a single rotary encoder with push button (short press / long press / double click)
- Powered via USB (small servos) or XT-60 (larger loads)
- 0.96" (SSD1306) or 1.3" (SH1106) OLED display
- Optional web interface for a subset of functions

## Changes in this fork

Compared to the upstream [TheDIYGuy999/Servotester_Deluxe](https://github.com/TheDIYGuy999/Servotester_Deluxe):

- **Per-channel calibration**: Min/Max/Center (and now rotation angle) used to be shared across all 5 channels. Each channel now has its own values, selectable via a new "Servo Channel" item in the Settings menu.
- **Adjustable servo rotation angle**: the degree readout was hardcoded to ±45° (i.e. a 90° servo). It's now a per-channel setting, so 180° and 360° servos read out correctly too.
- **Wider adjustable pulse range**: Min/Center/Max (standard modes) can now be set anywhere from 200-3000µs, with no built-in safety margin — some servos may hit their mechanical end stop if pushed too far, by design.
- **Encoder acceleration on calibration values**: turning the encoder faster now moves Min/Max/Center in bigger steps, matching the acceleration already used for normal servo positioning.
- **Passive buzzer support**: the buzzer output now drives a PWM tone (via LEDC) instead of a flat digitalWrite, so a passive piezo buzzer works too, not just an active one.
- **Crash fixes**: fixed several integer divide-by-zero crashes (frequency counter on a floating/unconnected input pin, progress bar calculations when a channel's Min equals its Max) and an EEPROM layout bug that left channels 2-5 uninitialized after upgrading from the single-channel version.
- **Long press fires immediately**: the "back" action now triggers as soon as the long-press threshold is reached, instead of waiting for the button to be released.
- **Double-click always switches channel** in the Settings menu, regardless of which item is currently selected.
- **Boot screen**: the control-help screen now dismisses on a button press or after a 10s timeout (was a fixed 4s delay); WiFi SSID/password are no longer shown as a boot popup and are instead available on demand in the Settings menu; the splash screen is now neutral (no logo/branding).
- **Removed the Pong and Flappy Bird games and the Calculator** to simplify the firmware and free up flash space.

## How to program it

- With Arduino IDE
- With Visual Studio Code + PlatformIO (recommended) — board and library versions are pinned in `platformio.ini` and downloaded automatically
- With ESPHome-Flasher (for pre-compiled `.bin` files, depending on your display)

## Hardware / Schematic

The pictures below document the original custom PCB design (with battery input, resistor network, etc.). A simple breadboard build also works fine: ESP32 DevKit + I2C OLED + a 5-pin rotary encoder with push button, wired directly to the GPIOs used in `src.ino`.

![](documentation/pictures/schematic.png)

![](documentation/pictures/resistorValues.JPG)

![](documentation/pictures/capacitor.JPG)

## Menu

![](documentation/pictures/menu.png)

## Oscilloscope 0 - 3.3V

![](documentation/pictures/scope.jpg)

![](documentation/pictures/ppm_scope.jpg)
