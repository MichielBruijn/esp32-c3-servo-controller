# ESP32 Servo Tester

A standalone servo tester/sweeper built around an ESP32 and a small OLED display, controlled entirely with a single rotary encoder + push button. No app or PC required.

## Credits

This project did not start from scratch — it stands on the work of others:

- Originally designed by **"Der RC Modellbauer"** — [Ziege-One/Servotester_Deluxe](https://github.com/Ziege-One/Servotester_Deluxe)
- Extended into **Servotester Deluxe** by **TheDIYGuy999** — [TheDIYGuy999/Servotester_Deluxe](https://github.com/TheDIYGuy999/Servotester_Deluxe)
- This repository is a **heavily modified fork** of TheDIYGuy999's version, built on custom hardware (not the original PCB).

All credit for the original firmware architecture (MCPWM servo generation, signal reading, web interface, menu system) goes to the two authors above.

## Features

- 5-channel PWM signal generator, multiple servo modes: STD, NOR, SHR, SSR, SUR, SXR
- Manual position control or automatic sweep (auto-oscillate) mode
- Per-channel Min / Center / Max calibration, per servo mode, saved to EEPROM
- Per-channel adjustable rotation angle (e.g. 90° / 180° / 360°) for the on-screen degree readout
- PWM signal read function (pulse width + frequency)
- PPM (Multiswitch), SBUS and IBUS read function
- Operation via a single rotary encoder with push button (short press / long press / double click), plus the onboard BOOT button as a physical "next channel" shortcut
- LED flash on every encoder click and button press, next to the power LED
- Powered via USB (small servos) or a LiPo battery, 2S-6S (larger loads)
- 0.96" (SSD1306) or 1.3" (SH1106) OLED display
- Web interface with the same settings and channel calibration as the OLED menu, reachable either via the device's own WiFi access point or, once configured, by joining your home WiFi network (`http://servotester.local`) — servo position sliders track a drag in real time (low-latency WebSocket channel), close to RC-stick feel
- Optional built-in oscilloscope with 2 switchable probe inputs (BOOT button or double-click to swap live), 0-3.3V RC signals natively, up to 5V per probe with its own external 16.2k/24.9k voltage divider. Trigger level auto-recenters on the signal every frame, so frequency/pulsewidth locks regardless of amplitude or DC offset. Per-probe voltage calibration lives in Settings ("Scope" group): a manual trim, or apply a clean 5.00Vpp reference and turn the knob once on the Auto-cal item - no multimeter needed - plus a 3-waveform (sine/triangle/rectangle) signal generator
- Web "Joystick Mode": two big touch-drag controls for driving from a phone in landscape - a horizontal steering bar and a vertical throttle bar, side by side, like holding a game controller - both spring back to center on release, driving any 2 servo channels you assign to them
- Joystick channel linking: extra channels can be driven in parallel with Steer or Throttle (e.g. two steering servos), each remapped to its own Min/Center/Max so it stays symmetric even with different calibration
- Joystick Steering Limit: an optional 0-100% setting that progressively reduces Steer deflection as Throttle moves away from center (forward or reverse), so full steering lock is available at parking speed but backed off under throttle - with a quick on/off checkbox right on the Joystick Mode driving page, independent of the configured strength
- Firmware update check: while in WiFi Station mode with internet access, the device checks this GitHub repo's latest release against its own version (at boot and every 6h) and shows an "Update available" notice on both the OLED Info page and the web interface, with a one-press download-and-flash from there
- Firmware backup/restore with no internet needed: download the currently-running firmware as a `.bin` from the web Info page, then upload it to another device's web interface to flash it directly - handy for cloning a known-good version across devices, or without WiFi Station/internet access at all

## How to program it

- With Arduino IDE
- With Visual Studio Code + PlatformIO (recommended) — board and library versions are pinned in `platformio.ini` and downloaded automatically
- With ESPHome-Flasher (for pre-compiled `.bin` files, depending on your display)

## Wiring

Any ESP32 DevKit board works — connect an I2C OLED, a 5-pin rotary encoder with push button, and up to 5 servo/signal connectors to the GPIOs below (see `src/src.ino` for the authoritative pin definitions).

| Function | GPIO | Notes |
|---|---|---|
| Servo 1 output | 13 | |
| Servo 2 output | 14 | |
| Servo 3 output | 27 | |
| Servo 4 output | 33 | |
| Servo 5 output / SBUS + IBUS receiver input | 32 | Shared pin: PWM output normally, switches to UART RX when reading SBUS/IBUS |
| Encoder push button | 15 | `INPUT_PULLUP`, button to GND |
| Encoder direction pin A | 16 | |
| Encoder direction pin B | 17 | |
| OLED SDA (I2C) | 21 | |
| OLED SCL (I2C) | 22 | |
| Passive piezo buzzer | 4 | Driven via LEDC PWM tone, not a flat digitalWrite |
| BOOT button | 0 | Onboard button on most ESP32 DevKit boards, no extra wiring needed. "Next channel" shortcut everywhere, except in the Oscilloscope screen where it switches probe 1/2 instead |
| Encoder click LED | 2 | LED + ~220-330Ω series resistor to GND, mounted next to the power LED |
| Battery voltage sense | 36 | Input-only ADC pin; needs an external resistor divider to bring pack voltage (up to 6S/~25.2V) under 3.3V — the divider ratio is calibrated in software via the "Power Scale" setting, no fixed resistor values required |
| Oscilloscope probe 1 input | 39 | Input-only ADC pin, optional; 0-3.3V RC signals natively. For up to 5V, add a 16.2k/24.9k resistor divider (top/bottom, 5V -> ~3.03V) and set `voltMaxByChannel[0] = 5.45` in `oscilloscope.h` so the vPP readout scales back up correctly |
| Oscilloscope probe 2 input | 34 | Input-only ADC pin, optional. Same 16.2k/24.9k divider, `voltMaxByChannel[1] = 5.45`. BOOT button or double-click switches the live oscilloscope view between probe 1 and 2 |
| Signal Generator output | 25 | Optional; the ESP32's other DAC-capable pin, 0-3.3V |

Power: USB 5V is enough for small servos. For anything drawing more current, feed the servos from a separate 2S-6S LiPo/BEC rather than the ESP32's own 5V pin — powering servos directly off the ESP32 board's regulator causes voltage-drop jitter on the PWM signal.

## Menu

Navigate with the rotary encoder (turn to move, short press to select, long press to go back, double-click to jump between servo channels; the BOOT button also jumps to the next channel). See `src/src.ino` for the full menu tree: Manual Mode, Sweep Mode, Expert Functions, Info, and Settings (last). Expert Functions is its own submenu (turn to browse, short press to enter, long press to go back up) holding Read PWM Impulse, Read PPM Multiswitch, Read SBUS, Read IBUS, Oscilloscope and Signal Generator. Info is a single item with 3 pages you page through left/right: on-screen controls help, Wifi status, and firmware version/source.
