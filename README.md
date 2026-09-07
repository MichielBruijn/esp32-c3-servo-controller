# ESP32 Servo Tester

A standalone servo tester/sweeper built around an ESP32 and a small OLED display, controlled entirely with a single rotary encoder + push button. No app or PC required.

## Credits

This project did not start from scratch — it stands on the work of others:

- Originally designed by **"Der RC Modellbauer"** — [Ziege-One/Servotester_Deluxe](https://github.com/Ziege-One/Servotester_Deluxe)
- Extended into **Servotester Deluxe** by **TheDIYGuy999** — [TheDIYGuy999/Servotester_Deluxe](https://github.com/TheDIYGuy999/Servotester_Deluxe)
- This repository is a **heavily modified fork** of TheDIYGuy999's version, built on custom hardware (not the original PCB). See [Changes in this fork](#changes-in-this-fork) below for what's different.

All credit for the original firmware architecture (MCPWM servo generation, signal reading, web interface, menu system) goes to the two authors above.

## Features

- 5-channel PWM signal generator, multiple servo modes: STD, NOR, SHR, SSR, SUR, SXR
- Manual position control or automatic sweep (auto-oscillate) mode
- Per-channel Min / Center / Max calibration, saved to EEPROM
- Per-channel adjustable rotation angle (e.g. 90° / 180° / 360°) for the on-screen degree readout
- PWM signal read function (pulse width + frequency)
- PPM (Multiswitch), SBUS and IBUS read function
- Operation via a single rotary encoder with push button (short press / long press / double click)
- LED flash on every encoder click and button press, next to the power LED
- Powered via USB (small servos) or a LiPo battery, 2S-6S (larger loads)
- 0.96" (SSD1306) or 1.3" (SH1106) OLED display
- Web interface with the same settings and channel calibration as the OLED menu, reachable either via the device's own WiFi access point or, once configured, by joining your home WiFi network (`http://servotester.local`) — servo position sliders track a drag in real time (low-latency WebSocket channel), close to RC-stick feel
- Optional analog joystick: X and Y axes each drive any servo channel you assign to them (direct position control, deadzone around center), click button re-centers both and fires a "pew pew" sound

## Changes in this fork

Compared to the upstream [TheDIYGuy999/Servotester_Deluxe](https://github.com/TheDIYGuy999/Servotester_Deluxe):

- **Per-channel calibration**: Min/Max/Center (and now rotation angle) used to be shared across all 5 channels. Each channel now has its own values, selectable via a new "Servo Channel" item in the Settings menu.
- **Adjustable servo rotation angle**: the degree readout was hardcoded to ±45° (i.e. a 90° servo). It's now a per-channel setting, so 180° and 360° servos read out correctly too.
- **Wider adjustable pulse range**: Min/Center/Max (standard modes) can now be set anywhere from 200-3000µs, with no built-in safety margin — some servos may hit their mechanical end stop if pushed too far, by design.
- **Encoder acceleration on calibration values**: turning the encoder faster now moves Min/Max/Center in bigger steps, matching the acceleration already used for normal servo positioning.
- **Passive buzzer support**: the buzzer output now drives a PWM tone (via LEDC) instead of a flat digitalWrite, so a passive piezo buzzer works too, not just an active one.
- **Encoder click LED**: an extra LED, next to the power LED, flashes briefly on every encoder detent and every button press.
- **Crash fixes**: fixed several integer divide-by-zero crashes (frequency counter on a floating/unconnected input pin, progress bar calculations when a channel's Min equals its Max) and an EEPROM layout bug that left channels 2-5 uninitialized after upgrading from the single-channel version.
- **Long press fires immediately**: the "back" action now triggers as soon as the long-press threshold is reached, instead of waiting for the button to be released.
- **Double-click always switches channel** in the Settings menu, regardless of which item is currently selected.
- **Boot screen**: the control-help screen now dismisses on a button press or after a 10s timeout (was a fixed 4s delay); WiFi SSID/password are no longer shown as a boot popup and are instead available on demand in the Settings menu; the splash screen is now neutral (no logo/branding).
- **Web interface rewritten**: fully translated to English (was German) and rewritten to match the OLED menu — same per-channel Min/Max/Center/Angle, servo mode, power scale, SBUS/encoder direction and speed curve settings, plus factory reset. The WiFi access point's IP address is now also shown on the OLED's Wifi Info screen.
- **Real-time web servo control**: the position sliders update live while dragging (not just on release), over a dedicated low-latency WebSocket channel (port 81) instead of one HTTP request per tick — close to RC-stick feel instead of a laggy, catch-up motion. Falls back to plain HTTP automatically if the socket isn't connected. The Center button and page navigation are instant too (no more full-page reload for a single click).
- **BOOT button repurposed**: the ESP32 board's onboard BOOT button (unused once the device has booted) now works as a "next channel" shortcut, with the same click LED/beep feedback as the encoder.
- **Info menu + link**: a new "Info" item in the OLED menu, and a link at the bottom of every web page, point back to this repository - so anyone who finds the device can find the source and build their own.
- **Optional analog joystick**: a new "Joystick" menu drives two servo channels directly from a 2-axis analog stick (deadzone snaps to the calibrated Center so it doesn't twitch at rest). Which channel is X and which is Y is configurable, from the OLED Settings menu or the web Settings page. The stick's click button re-centers both mapped channels and plays a "pew pew" sound.
- **WiFi Station mode**: besides the device's own Access Point, it can now join an existing WiFi network instead (Settings menu or web Settings page — SSID/password are entered via the web interface). Once joined, it's reachable at `http://servotester.local` (mDNS) from any computer on that network, without disconnecting from your own WiFi/internet to reach it. If the configured network can't be joined within 10s, it automatically falls back to its own Access Point so it's never left unreachable.
- **Removed the oscilloscope function, the Pong and Flappy Bird games, and the Calculator** to simplify the firmware and free up flash space.
- **Custom hardware**: this fork is not built on the original PCB — see [Wiring](#wiring) below for the GPIO pinout, which works with any ESP32 DevKit + I2C OLED + 5-pin rotary encoder breadboard build.

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
| BOOT button ("next channel" shortcut) | 0 | Onboard button on most ESP32 DevKit boards, no extra wiring needed |
| Encoder click LED | 2 | LED + ~220-330Ω series resistor to GND, mounted next to the power LED |
| Battery voltage sense | 36 | Input-only ADC pin; needs an external resistor divider to bring pack voltage (up to 6S/~25.2V) under 3.3V — the divider ratio is calibrated in software via the "Power Scale" setting, no fixed resistor values required |
| Joystick X axis (VRx) | 34 | Input-only ADC pin, optional |
| Joystick Y axis (VRy) | 35 | Input-only ADC pin, optional |
| Joystick click button (SW) | 26 | Optional; most joystick breakout modules already have their own pull-up |

Power: USB 5V is enough for small servos. For anything drawing more current, feed the servos from a separate 2S-6S LiPo/BEC rather than the ESP32's own 5V pin — powering servos directly off the ESP32 board's regulator causes voltage-drop jitter on the PWM signal.

## Menu

Navigate with the rotary encoder (turn to move, short press to select, long press to go back, double-click to jump between servo channels). See `src/src.ino` for the full menu tree: Servo Tester, Auto Mode, Pulse Read, Multiswitch Read, SBUS Read, IBUS Read, Wifi Info, Settings, Info, and Joystick.
