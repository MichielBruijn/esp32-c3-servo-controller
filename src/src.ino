/*
   Servotester Deluxe - "C3 Mini" variant
   Original: Ziege-One (Der RC-Modelbauer) / TheDIYGuy999 - see README for full credits
   This branch: fork for the ESP32-C3 SuperMini, stripped down to the smallest possible servo
   controller - WiFi web interface only, no OLED, no rotary encoder, no local UI at all.

   Removed compared to the classic ESP32 board this was forked from: OLED display, rotary
   encoder + click LED, piezo buzzer, battery voltage measurement, oscilloscope, signal
   generator, SBUS/IBUS/PPM/PWM-impulse reading. Manual Mode, Sweep Mode, Joystick Mode
   (touch-drag + USB serial), per-channel calibration and GitHub-release firmware updates all
   stayed - the web interface is now the ONLY control surface.

   Why LEDC instead of MCPWM: the ESP32-C3 (RISC-V) has no MCPWM peripheral - that's a classic
   ESP32/S3-only block. All 5 servo outputs are generated with LEDC instead, one channel each,
   no hardware-timer-sharing constraint like the old MCPWM setup had.

 /////Pin Belegung (ESP32-C3 SuperMini) ////
 GPIO 0: Servo1
 GPIO 1: Servo2
 GPIO 3: Servo3
 GPIO 4: Servo4
 GPIO 5: Servo5

 Deliberately avoids GPIO2/8/9 (strapping pins - GPIO9 is also the onboard BOOT button) and
 GPIO6/7/10/20/21 (left free for future use). Power servos from their own 5V supply, not the
 board's own 5V/3V3 pin - shared only GND - to avoid voltage-drop jitter on the PWM signal.
 */

char codeVersion[] = "0.1-c3mini"; // Software revision.

//
// =======================================================================================================
// LIBRARIES & HEADER FILES
// =======================================================================================================
//

/* Required Libraries / Benötigte Bibliotheken
arduinoWebSockets                             2.4.1
 */

// No need to install these, they come with the ESP32 board definition
#include <WiFi.h>
#include <esp_wifi.h>      // for esp_wifi_set_country() - default region caps WiFi to channels 1-11, blocking channel 12/13 networks
#include <ESPmDNS.h>       // Reachable as http://servotester.local when joined to an existing network (Station mode)
#include <WebSocketsServer.h> // Low-latency channel for live servo position updates while dragging a web slider
#include <HTTPClient.h>       // for checking/downloading firmware releases from GitHub
#include <WiFiClientSecure.h> // HTTPS transport for the GitHub API and release asset download
#include <Update.h>           // for writing a downloaded firmware image to the OTA partition
#include <EEPROM.h>          // for non volatile storage
#include <Esp.h>             // for displaying memory information
#include "rom/rtc.h"         // for displaying reset reason
#include "soc/rtc_wdt.h"     // for watchdog timer
#include <string>            // std::string, std::stof
using namespace std;

// All the required user settings are done in the following .h file:
#include "0_generalSettings.h" // <<------- general settings

// EEPROM layout - brand new, fresh device (no migration from the classic-board firmware, which
// used a different, much larger layout for features that don't exist here).
#define NUM_SERVO_CHANNELS 5
#define NUM_SERVO_MODES 6 // STD, NOR, SHR, SSR, SUR, SXR
#define NUM_SERVO_TIMER_GROUPS 3 // Kept as a software grouping only now (LEDC has no hardware pairing constraint) - {CH1,CH2},{CH3,CH4},{CH5}, same as the classic board, so Mode still applies per group of channels

#define EEPROM_LAYOUT_VERSION 1 // Bump whenever a field is added/moved, so eepromRead() knows to fill in sane defaults for it
#define adr_eprom_LAYOUT_VERSION 0
#define adr_eprom_WIFI_MODE 4
#define adr_eprom_STA_SSID 8               // Up to 32 chars + null terminator, 34 bytes reserved
#define adr_eprom_STA_PASSWORD 42          // Up to 64 chars + null terminator, 66 bytes reserved
#define adr_eprom_JOYSTICK_X_CHANNEL 108
#define adr_eprom_JOYSTICK_Y_CHANNEL 112
#define adr_eprom_JOYSTICK_X_LINK_MASK 116 // Bitmask: additional channels mirroring Steer, beyond JOYSTICK_X_CHANNEL itself
#define adr_eprom_JOYSTICK_Y_LINK_MASK 120 // Same, for Throttle
#define adr_eprom_STEERING_LIMIT 124       // 0-100%: how much Steer is progressively cut as Throttle deflection increases
#define adr_eprom_STEERING_LIMIT_ENABLED 128
#define SERVO_MODE_GROUP_DATA_START 132
#define adr_eprom_SERVO_MODE_GROUP(g) (SERVO_MODE_GROUP_DATA_START + (g)*4) // NUM_SERVO_TIMER_GROUPS ints
#define SERVO_CHANNEL_DATA_START (SERVO_MODE_GROUP_DATA_START + NUM_SERVO_TIMER_GROUPS * 4)
#define SERVO_CHANNEL_STRIDE (NUM_SERVO_MODES * 3 * 4) // 3 values (Max/Min/Center) x 6 modes x 4 bytes = 72 bytes per channel
#define SERVO_CHANNEL_DATA_END (SERVO_CHANNEL_DATA_START + NUM_SERVO_CHANNELS * SERVO_CHANNEL_STRIDE)
// SERVO µs Max/Min/Center per servo channel (0-4) AND per mode (0-5: STD/NOR/SHR/SSR/SUR/SXR)
#define adr_eprom_SERVO_MAX(ch, mode) (SERVO_CHANNEL_DATA_START + (ch)*SERVO_CHANNEL_STRIDE + (mode)*12 + 0)
#define adr_eprom_SERVO_MIN(ch, mode) (SERVO_CHANNEL_DATA_START + (ch)*SERVO_CHANNEL_STRIDE + (mode)*12 + 4)
#define adr_eprom_SERVO_CENTER(ch, mode) (SERVO_CHANNEL_DATA_START + (ch)*SERVO_CHANNEL_STRIDE + (mode)*12 + 8)
// Full rotation range in degrees per servo channel (e.g. 90/180/360), appended after the block above
#define adr_eprom_SERVO_DEGREES(ch) (SERVO_CHANNEL_DATA_END + (ch)*4)
#define EEPROM_SIZE (SERVO_CHANNEL_DATA_END + NUM_SERVO_CHANNELS * 4)

int RESET_EEPROM; // 1 = Reset 0 = No Reset

// EEPROM storage for settings
enum WifiModeEnum
{
  WIFI_AP_MODE = 0,      // Own access point, e.g. "ServoTester" - always reachable, but disconnects you from your own WiFi
  WIFI_STATION_MODE = 1  // Join an existing WiFi network (STA_SSID/STA_PASSWORD below), reachable via http://servotester.local
};
int WIFI_MODE;           // 0 = Access Point, 1 = Station, see WifiModeEnum above. No WiFi on/off toggle on this board -
                          // it has no OLED/encoder, WiFi is the only control surface, so it's always on.
String STA_SSID = "";     // Home WiFi network SSID to join in Station mode, entered via the web interface
String STA_PASSWORD = ""; // Home WiFi network password to join in Station mode, entered via the web interface
bool wifiStaFallback;     // True when Station mode was requested but joining failed, and we fell back to Access Point

// Firmware update check (GitHub Releases) - see src/firmwareUpdate.h
bool updateAvailable = false;      // True once a newer release than codeVersion has been found
String latestFirmwareVersion = ""; // Version string of the latest GitHub release, e.g. "0.63"
String latestFirmwareUrl = "";     // browser_download_url of that release's "firmware.bin" asset
String updateErrorMessage = "";    // Set when a check or install attempt fails, shown once on the next page render
bool updateInProgress = false;     // True while a download+flash is running (blocks re-entry)
unsigned long lastUpdateCheckMillis = 0;
const unsigned long UPDATE_CHECK_INTERVAL_MS = 6UL * 60 * 60 * 1000; // Re-check every 6 hours while connected

// Which servo channel (0-4) each Joystick Mode control drives (web interface)
int JOYSTICK_X_CHANNEL;
int JOYSTICK_Y_CHANNEL;
int JOYSTICK_X_LINK_MASK; // Bitmask (bit n = channel n) of ADDITIONAL channels driven in parallel with Steer, beyond JOYSTICK_X_CHANNEL itself
int JOYSTICK_Y_LINK_MASK; // Same, for Throttle
int STEERING_LIMIT;       // 0-100%: how much Steer deflection is progressively cut as Throttle deflection increases (either direction). 0 = off
int STEERING_LIMIT_ENABLED; // Quick on/off for the above (Joystick Mode web page checkbox), independent of the configured strength above
int lastRawSteerTarget = -1; // Last raw (pre-limit) Steer target in µs, -1 = none yet (Throttle-only updates don't reapply the limit until Steer has been touched once)
String wifiIpString = ""; // AP/Station IP address, filled in wifiSetup(), shown on the Info page
int SERVO_STEPS;        // Calculated automatically by servoModes(), for whichever channel is selectedServo
int SERVO_MAX;          // Legacy scalars, controlled by servoModes.h - always reflect selectedServo's own group/mode
int SERVO_MIN;
int SERVO_CENTER;
int SERVO_Hz;
int SERVO_MODE;          // Legacy scalar: always refreshed to reflect selectedServo's own group (see servoModes())
int SERVO_MODE_PER_GROUP[NUM_SERVO_TIMER_GROUPS]; // Mode (and therefore Hz) is stored per group of channels, not per channel

// Which group a channel belongs to (0-indexed channel number in, 0-2 group index out). Purely a
// software grouping on this board (LEDC has no hardware-timer-sharing constraint like the classic
// board's MCPWM did) - kept anyway so calibration data/UI/EEPROM layout stay simple and familiar.
uint8_t servoTimerGroup(uint8_t ch)
{
  if (ch <= 1)
    return 0; // Servo 1+2
  if (ch <= 3)
    return 1; // Servo 3+4
  return 2;   // Servo 5
}

// Min/Max/Center in µs, per servo channel AND per mode (STD/NOR/SHR/SSR/SUR/SXR) - each mode
// remembers its own calibration independently for a given channel.
int SERVO_MAX_BY_MODE[NUM_SERVO_CHANNELS][NUM_SERVO_MODES];
int SERVO_MIN_BY_MODE[NUM_SERVO_CHANNELS][NUM_SERVO_MODES];
int SERVO_CENTER_BY_MODE[NUM_SERVO_CHANNELS][NUM_SERVO_MODES];
int SERVO_DEGREES[NUM_SERVO_CHANNELS];      // Full rotation range in degrees (e.g. 90/180/360), per channel

bool WiFiChanged;
bool webJoystickMode; // Web interface: two-thumb touch-slider control page instead of the normal per-channel sliders
unsigned long lastJoystickMsgMillis; // millis() of the last PosJ websocket message - drives the failsafe below

// Webserver on port 80 - the only control surface on this board
WiFiServer server(80);

// Low-latency WebSocket channel (port 81), used only for live servo position updates while
// dragging a slider in the web interface - a plain HTTP request per tick (even throttled) still
// pays a fresh TCP handshake and full header parse each time, which is what made a sustained drag
// feel laggy compared to a real RC stick. Everything else (page navigation, settings) stays on the
// existing HTTP server.
WebSocketsServer webSocket(81);

// 3 pin connectors, used as servo PWM output only (no receiver/BUS input on this board)
#define SERVO_CONNECTOR_1 0
#define SERVO_CONNECTOR_2 1
#define SERVO_CONNECTOR_3 3
#define SERVO_CONNECTOR_4 4
#define SERVO_CONNECTOR_5 5

// Servo
uint8_t servopin[NUM_SERVO_CHANNELS] = {SERVO_CONNECTOR_1, SERVO_CONNECTOR_2, SERVO_CONNECTOR_3, SERVO_CONNECTOR_4, SERVO_CONNECTOR_5}; // Pins Servoausgang 1 - 5
int servo_pos[NUM_SERVO_CHANNELS];                                                                                                      // Speicher für Servowerte
int selectedServo = 0;                                                                                                                  // Channel currently shown/calibrated in Sweep Mode/Settings
String servoMode = "";                                                                                                                  // Servo operation mode text

// Servo operation modes (see servoModes.h)
enum
{
  STD, // Std = 50Hz   1000 - 1500 - 2000µs = gemäss ursprünglichem Standard
  NOR, // NOR = 100Hz  1000 - 1500 - 2000µs = normal = für die meisten analogen Servos
  SHR, // SHR = 333Hz  1000 - 1500 - 2000µs = Sanwa High Response = für alle Digitalservos
  SSR, // SSR = 400Hz   130 -  300 - 470µs  = Sanwa Super Response = nur für Sanwa Servos der SRG-Linie
  SUR, // SUR = 800Hz   130 -  300 - 470µs  = Sanwa Ultra Response
  SXR  // SXR = 1600Hz  130 -  300 - 470µs  = Sanwa Xtreme Response
};

// LEDC servo PWM output ------------------------------------------------------------------------
// 14-bit duty resolution is valid for the whole 50-1600Hz range this board uses, given the
// ESP32-C3's 80MHz APB LEDC clock (freq * 2^resolution must stay under that): even the worst
// case, 1600Hz * 2^14 = ~26.2M, comfortably fits. One LEDC channel per servo channel (same
// number), no hardware-timer-sharing constraint like the classic board's MCPWM setup had - Mode
// is still stored per group of channels (see servoTimerGroup()) purely to keep the calibration
// data/UI/EEPROM layout the same shape as the classic board's.
#define SERVO_LEDC_RESOLUTION_BITS 14
#define SERVO_LEDC_DUTY_STEPS (1UL << SERVO_LEDC_RESOLUTION_BITS)

#include "src/servoModes.h" // Servo operation profiles - needed here already for servoHzForMode()

void configureServoGroupFrequency(uint8_t group)
{
  int hz = servoHzForMode(SERVO_MODE_PER_GROUP[group]);
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    if (servoTimerGroup(ch) == group)
      ledcSetup(ch, hz, SERVO_LEDC_RESOLUTION_BITS);
  }
}

void writeAllServos()
{
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    int hz = servoHzForMode(SERVO_MODE_PER_GROUP[servoTimerGroup(ch)]);
    uint32_t duty = (uint32_t)((double)servo_pos[ch] * hz * SERVO_LEDC_DUTY_STEPS / 1000000.0 + 0.5);
    ledcWrite(ch, duty);
  }
}

// Sweep Mode (web interface only, replaces the classic board's OLED "Auto Mode" screen) --------
bool sweepPaused = true; // Starts paused, so you can pick the right channel before it starts moving
int sweepAutopos[NUM_SERVO_CHANNELS]; // Which end (Min/Max) each channel is currently heading towards
unsigned long previousTimeAuto = 0;
int TimeAuto = 50; // ms between steps

// Web page navigation state (server-side "which page" - this board only ever expects one
// controlling browser at a time, same single-user assumption the classic board's OLED menu had)
enum
{
  Home = 0,
  ManualPage,
  SweepPage,
  InfoPage,
  SettingsPage
};
int Menu = Home;

// Which timer group a channel belongs to (0-indexed channel number in, 0-2 group index out)
int remapServoPos(int value, uint8_t fromCh, uint8_t toCh)
{
  // Note: servoCenterForChannel() (servoModes.h) isn't usable here yet - that header is #included
  // further down the sketch, so its function isn't declared this early in the translation unit.
  int fromMode = SERVO_MODE_PER_GROUP[servoTimerGroup(fromCh)];
  int fromCenter = SERVO_CENTER_BY_MODE[fromCh][fromMode];
  int toMode = SERVO_MODE_PER_GROUP[servoTimerGroup(toCh)];
  int toCenter = SERVO_CENTER_BY_MODE[toCh][toMode];
  int toMin = SERVO_MIN_BY_MODE[toCh][toMode];
  int toMax = SERVO_MAX_BY_MODE[toCh][toMode];

  if (value >= fromCenter)
  {
    int fromMax = SERVO_MAX_BY_MODE[fromCh][fromMode];
    float frac = (fromMax != fromCenter) ? (float)(value - fromCenter) / (fromMax - fromCenter) : 0;
    return constrain((int)round(toCenter + frac * (toMax - toCenter)), toMin, toMax);
  }
  else
  {
    int fromMin = SERVO_MIN_BY_MODE[fromCh][fromMode];
    float frac = (fromCenter != fromMin) ? (float)(fromCenter - value) / (fromCenter - fromMin) : 0;
    return constrain((int)round(toCenter - frac * (toCenter - toMin)), toMin, toMax);
  }
}

// How far (0-1) the Throttle channel is currently deflected from its center, in either direction.
float throttleDeflectionFraction()
{
  int mode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_Y_CHANNEL)];
  int center = SERVO_CENTER_BY_MODE[JOYSTICK_Y_CHANNEL][mode];
  int pos = servo_pos[JOYSTICK_Y_CHANNEL];
  if (pos >= center)
  {
    int max = SERVO_MAX_BY_MODE[JOYSTICK_Y_CHANNEL][mode];
    return (max != center) ? constrain((float)(pos - center) / (max - center), 0.0f, 1.0f) : 0;
  }
  else
  {
    int min = SERVO_MIN_BY_MODE[JOYSTICK_Y_CHANNEL][mode];
    return (center != min) ? constrain((float)(center - pos) / (center - min), 0.0f, 1.0f) : 0;
  }
}

// Apply the Steering Limit (reduces Steer deflection as Throttle deflection increases, symmetric
// forward/reverse) to a raw Steer target, then write the result to the primary Steer channel plus
// any linked channels.
void applySteerOutput(int rawSteerValue)
{
  lastRawSteerTarget = rawSteerValue;

  int mode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_X_CHANNEL)];
  int center = SERVO_CENTER_BY_MODE[JOYSTICK_X_CHANNEL][mode];
  int effectiveLimit = STEERING_LIMIT_ENABLED ? STEERING_LIMIT : 0;
  float scale = 1.0f - (effectiveLimit / 100.0f) * throttleDeflectionFraction();
  int limited = constrain((int)round(center + (rawSteerValue - center) * scale),
                          SERVO_MIN_BY_MODE[JOYSTICK_X_CHANNEL][mode], SERVO_MAX_BY_MODE[JOYSTICK_X_CHANNEL][mode]);

  servo_pos[JOYSTICK_X_CHANNEL] = limited;
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    if (ch != JOYSTICK_X_CHANNEL && (JOYSTICK_X_LINK_MASK & (1 << ch)))
    {
      servo_pos[ch] = remapServoPos(limited, JOYSTICK_X_CHANNEL, ch);
    }
  }
}

// Mirror a raw Throttle target to the primary channel plus any linked channels, then re-apply the
// Steering Limit using the last known Steer target - so Steer re-scales immediately when only
// Throttle changes, without needing to touch the Steer control too.
void applyThrottleOutput(int rawThrottleValue)
{
  int mode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_Y_CHANNEL)];
  int limited = constrain(rawThrottleValue,
      SERVO_MIN_BY_MODE[JOYSTICK_Y_CHANNEL][mode], SERVO_MAX_BY_MODE[JOYSTICK_Y_CHANNEL][mode]);
  servo_pos[JOYSTICK_Y_CHANNEL] = limited;
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    if (ch != JOYSTICK_Y_CHANNEL && (JOYSTICK_Y_LINK_MASK & (1 << ch)))
    {
      servo_pos[ch] = remapServoPos(limited, JOYSTICK_Y_CHANNEL, ch);
    }
  }

  if (lastRawSteerTarget >= 0)
  {
    applySteerOutput(lastRawSteerTarget);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (type != WStype_TEXT)
    return;

  String msg;
  msg.reserve(length);
  for (size_t i = 0; i < length; i++)
    msg += (char)payload[i];

  // Same quick on/off as GET /?SteerLimitOn= (webInterface.h) and the USB-serial
  // path's usbJoystickLoop() - lets an app connected over the wifi websocket
  // toggle Steering Limit too, not just USB or the physical web page.
  if (msg.startsWith("SteerLimitOn=") && msg.length() > 13)
  {
    STEERING_LIMIT_ENABLED = constrain(msg.substring(13).toInt(), 0, 1);
    return;
  }

  // "PosJ{ch}={value}" comes from the Joystick Mode touch tracks specifically - only those go
  // through channel linking/Steering Limit. Plain "Pos{ch}={value}" (Manual Mode's sliders, which
  // share the same underlying sendPos()/websocket) always writes the channel raw, even if it
  // happens to be whichever channel is currently configured as Joystick Steer/Throttle - dragging
  // that channel's own slider in Manual Mode shouldn't also move its linked partner channels.
  if (msg.startsWith("PosJ") && msg.length() > 5)
  {
    int equalsPos = msg.indexOf('=');
    if (equalsPos > 4)
    {
      int ch = msg.substring(4, equalsPos).toInt();
      int value = msg.substring(equalsPos + 1).toInt();
      if (ch >= 0 && ch < NUM_SERVO_CHANNELS)
      {
        lastJoystickMsgMillis = millis(); // Feeds the loop() failsafe - a dropped connection must not leave a stale command applied
        if (ch == JOYSTICK_X_CHANNEL)
        {
          applySteerOutput(value);
        }
        else if (ch == JOYSTICK_Y_CHANNEL)
        {
          applyThrottleOutput(value);
        }
        else
        {
          servo_pos[ch] = value; // Shouldn't normally happen - Joystick Mode only ever drives its 2 configured channels
        }
      }
    }
  }
  else if (msg.startsWith("Pos") && msg.length() > 4)
  {
    int equalsPos = msg.indexOf('=');
    if (equalsPos > 3)
    {
      int ch = msg.substring(3, equalsPos).toInt();
      int value = msg.substring(equalsPos + 1).toInt();
      if (ch >= 0 && ch < NUM_SERVO_CHANNELS)
      {
        servo_pos[ch] = value;
      }
    }
  }
}

// Defined in src/firmwareUpdate.h, included further down - needs a forward declaration here since
// usbJoystickLoop() below (which calls it) is defined earlier in this file than that #include.
bool usbFirmwareUpdate(size_t contentLength, const String &expectedMd5);

// Reads "PosJ{ch}=<value>" and "SteerLimitOn=<0|1>" lines from the USB serial
// port - an alternative to the websocket path above for a phone connected via
// USB-OTG cable instead of joining this board's wifi (avoids that phone having
// to juggle wifi-to-here plus mobile data for its own uplink at the same time).
// Deliberately mirrors webSocketEvent's PosJ branch and the HTTP SteerLimitOn
// handler exactly (same channel-linking/Steering-Limit path via
// applySteerOutput/applyThrottleOutput, same lastJoystickMsgMillis feed into the
// existing >500ms failsafe, same immediate non-persisted toggle) so all
// transports behave identically. Runs unconditionally every loop() iteration -
// an external controller can't depend on someone also being at a physical menu
// on a board that no longer has one. Only forces Menu/webJoystickMode on an
// actually-valid PosJ line, not on any stray serial input (e.g. someone poking
// at Serial Monitor for debugging).
void usbJoystickLoop()
{
  if (!Serial.available())
    return;

  String msg = Serial.readStringUntil('\n');
  msg.trim();

  if (msg.startsWith("SteerLimitOn=") && msg.length() > 13)
  {
    STEERING_LIMIT_ENABLED = constrain(msg.substring(13).toInt(), 0, 1);
    return;
  }

  // Lets a phone connected over this same USB-OTG cable flash new firmware
  // without any wifi involved at all - see usbFirmwareUpdate() (firmwareUpdate.h)
  // for the actual transfer/flash logic, which reuses the same Update library
  // as the existing wifi-upload/GitHub-download paths.
  if (msg == "GETVERSION")
  {
    Serial.println("VERSION=" + String(codeVersion));
    return;
  }
  // Lets a phone-side app drive whichever channels this board's own Joystick
  // X/Y are actually configured for (Settings -> Joystick channel mapping),
  // instead of assuming a fixed default that silently stops matching the
  // moment someone changes it in the web interface.
  if (msg == "GETJOYCHANNELS")
  {
    Serial.println("JOYCHANNELS=" + String(JOYSTICK_X_CHANNEL) + "," + String(JOYSTICK_Y_CHANNEL));
    return;
  }
  // Lets a phone-side app map its -1.0..1.0 input onto THIS board's own
  // calibrated µs range per channel, instead of assuming the generic
  // 1000/1500/2000 RC default - applySteerOutput()/applyThrottleOutput() above
  // already constrain() to these same per-channel, per-mode values, so a
  // mismatched assumption on the app side just silently lost whatever range
  // sits outside the app's hardcoded guess.
  if (msg == "GETCALIBRATION")
  {
    int xMode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_X_CHANNEL)];
    int yMode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_Y_CHANNEL)];
    Serial.println("CALIBRATION=" +
                    String(SERVO_MIN_BY_MODE[JOYSTICK_X_CHANNEL][xMode]) + "," +
                    String(SERVO_CENTER_BY_MODE[JOYSTICK_X_CHANNEL][xMode]) + "," +
                    String(SERVO_MAX_BY_MODE[JOYSTICK_X_CHANNEL][xMode]) + "," +
                    String(SERVO_MIN_BY_MODE[JOYSTICK_Y_CHANNEL][yMode]) + "," +
                    String(SERVO_CENTER_BY_MODE[JOYSTICK_Y_CHANNEL][yMode]) + "," +
                    String(SERVO_MAX_BY_MODE[JOYSTICK_Y_CHANNEL][yMode]));
    return;
  }
  if (msg.startsWith("OTAUPDATE=") && msg.length() > 10)
  {
    // "OTAUPDATE=<size>:<md5hex>" - the MD5 lets usbFirmwareUpdate() verify the image
    // actually arrived intact over this raw, unchecked serial byte stream (see its doc).
    String rest = msg.substring(10);
    int colonPos = rest.indexOf(':');
    if (colonPos > 0)
    {
      usbFirmwareUpdate((size_t)rest.substring(0, colonPos).toInt(), rest.substring(colonPos + 1));
    }
    return;
  }

  if (!msg.startsWith("PosJ") || msg.length() <= 5)
    return;

  int equalsPos = msg.indexOf('=');
  if (equalsPos <= 4)
    return;

  int ch = msg.substring(4, equalsPos).toInt();
  int value = msg.substring(equalsPos + 1).toInt();
  if (ch < 0 || ch >= NUM_SERVO_CHANNELS)
    return;

  if (!webJoystickMode)
  {
    Menu = ManualPage;
    webJoystickMode = true;
  }
  lastJoystickMsgMillis = millis();

  if (ch == JOYSTICK_X_CHANNEL)
  {
    applySteerOutput(value);
  }
  else if (ch == JOYSTICK_Y_CHANNEL)
  {
    applyThrottleOutput(value);
  }
  else
  {
    servo_pos[ch] = value; // Shouldn't normally happen - Joystick Mode only ever drives its 2 configured channels
  }
}

// Speicher HTTP request
String header;

// Für HTTP GET value
String valueString = String(5);
int pos1 = 0;
int pos2 = 0;

// These are used to print the reset reason on startup
const char *RESET_REASONS[] = {"POWERON_RESET", "NO_REASON", "SW_RESET", "OWDT_RESET", "DEEPSLEEP_RESET", "SDIO_RESET", "TG0WDT_SYS_RESET", "TG1WDT_SYS_RESET", "RTCWDT_SYS_RESET", "INTRUSION_RESET", "TGWDT_CPU_RESET", "SW_CPU_RESET", "RTCWDT_CPU_RESET", "EXT_CPU_RESET", "RTCWDT_BROWN_OUT_RESET", "RTCWDT_RTC_RESET"};

unsigned long currentTime = millis();     // Aktuelle Zeit
unsigned long previousTime = 0;           // Previous time
const long timeoutTime = 2000;            // Define timeout time in milliseconds (example: 2000ms = 2s)

//
// =======================================================================================================
// Additional headers
// =======================================================================================================
//
#include "src/firmwareUpdate.h"  // GitHub Releases update check + install
#include "src/webInterface.h"    // Configuration website - the only control surface on this board

//
// =======================================================================================================
// WiFi SETUP
// =======================================================================================================
//
void setWifiChannelRange()
{
  // By default the ESP32 uses the "01" (world safe) regulatory domain, which only permits
  // channels 1-11. Channels 12/13 are legal (and commonly used) in most of Europe, so without
  // this the device simply refuses to see/join networks broadcasting on them.
  wifi_country_t country = {
      .cc = "NL",
      .schan = 1,
      .nchan = 13,
      .max_tx_power = 20,
      .policy = WIFI_COUNTRY_POLICY_MANUAL};
  esp_wifi_set_country(&country);
}

// Station connect is split into begin/await/finish (instead of one function that blocks for up
// to 10s) so setup() can kick it off before other boot work and only wait out whatever's left of
// the timeout afterward.
unsigned long wifiConnectStartMillis;

void wifiStationBegin()
{
  // Try to join the configured home network first, so the device stays on the same
  // network as the phone/laptop already using it - no need to switch WiFi to reach it.
  Serial.print("Connecting to WiFi network: ");
  Serial.println(STA_SSID);

  // Must be set before WiFi.mode(), which applies the hostname to the network interface
  // at that point in time - setting it after mode() is a no-op for the netif already created.
  WiFi.setHostname("servotester"); // else the DHCP hostname defaults to "esp32-<MAC suffix>"
  WiFi.mode(WIFI_STA);
  setWifiChannelRange(); // allow channels 12/13, not just the default-region 1-11

  wifi_country_t currentCountry;
  esp_wifi_get_country(&currentCountry);
  Serial.printf("WiFi country: %s, channels %u-%u, policy %d\n",
                 currentCountry.cc, currentCountry.schan,
                 currentCountry.schan + currentCountry.nchan - 1, currentCountry.policy);

  // Multi-AP networks (e.g. mesh/UniFi setups) broadcast the same SSID from several access
  // points on different channels. WiFi.begin() alone connects to whichever one it happens to
  // find first while scanning channel-by-channel, which is often not the strongest one. So
  // scan ourselves and explicitly pin the connection to the strongest matching AP.
  Serial.println("Scanning...");
  int scanCount = WiFi.scanNetworks();
  int bestIndex = -1;
  for (int i = 0; i < scanCount; i++)
  {
    Serial.printf("  [%2d] ch%2d  %4ddBm  %s\n", i, WiFi.channel(i), WiFi.RSSI(i), WiFi.SSID(i).c_str());
    if (WiFi.SSID(i) == STA_SSID && (bestIndex == -1 || WiFi.RSSI(i) > WiFi.RSSI(bestIndex)))
    {
      bestIndex = i;
    }
  }

  int32_t bestChannel = 0;
  const uint8_t *bestBssid = NULL;
  if (bestIndex != -1)
  {
    bestChannel = WiFi.channel(bestIndex);
    bestBssid = WiFi.BSSID(bestIndex);
    Serial.printf("Strongest match: ch%d %ddBm\n", bestChannel, WiFi.RSSI(bestIndex));
  }
  WiFi.scanDelete();

  WiFi.begin(STA_SSID.c_str(), STA_PASSWORD.c_str(), bestChannel, bestBssid);
  wifiConnectStartMillis = millis();
}

// Waits up to timeoutMs *measured from wifiConnectStartMillis* (not from when this is called) -
// so calling it later, after other work already spent part of that budget, only waits out
// whatever's left instead of a fresh full timeout.
bool wifiStationAwaitConnected(unsigned long timeoutMs)
{
  while (WiFi.status() != WL_CONNECTED && millis() - wifiConnectStartMillis < timeoutMs)
  {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void wifiStationFinishConnected()
{
  wifiIpString = WiFi.localIP().toString();
  Serial.print("Connected, IP: ");
  Serial.println(wifiIpString);

  // Default WiFi power-save (modem sleep) periodically powers the radio down between beacons -
  // fine for a quick page load, but a sustained transfer (firmware download/upload, or dragging
  // a joystick control) can stall hard for seconds at a time while the radio is asleep and miss
  // its window to recover, instead of just slowing down. This device is never battery-life
  // constrained enough for that trade-off to be worth it.
  WiFi.setSleep(false);

  if (MDNS.begin("servotester"))
  {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: http://servotester.local");
  }

  Serial.printf("\nWiFi Tx Power Level: %u", WiFi.getTxPower());
  WiFi.setTxPower(cpType); // WiFi power according to "0_generalSettings.h"
  Serial.printf("\nWiFi Tx Power Level changed to: %u\n\n", WiFi.getTxPower());

  server.begin(); // Start Webserver
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  checkForFirmwareUpdate();
  lastUpdateCheckMillis = millis();
}

void wifiStartAccessPoint()
{
  Serial.println("Starting Access Point...");
  WiFi.mode(WIFI_AP); // pure AP - no STA component trying to (re)connect to a stored home network in the background
  setWifiChannelRange(); // allow channels 12/13, not just the default-region 1-11
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  wifiIpString = IP.toString();

  // SSID, password and IP are shown on demand on the Info page instead of a boot popup

  Serial.printf("\nWiFi Tx Power Level: %u", WiFi.getTxPower());
  WiFi.setTxPower(cpType); // WiFi power according to "0_generalSettings.h"
  Serial.printf("\nWiFi Tx Power Level changed to: %u\n\n", WiFi.getTxPower());

  server.begin(); // Start Webserver
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void wifiSetup()
{
  MDNS.end(); // Clear any previous responder before (re)configuring WiFi, safe even if never started
  wifiStaFallback = false;

  if (WIFI_MODE == WIFI_STATION_MODE && STA_SSID.length() > 0)
  {
    wifiStationBegin();
    if (wifiStationAwaitConnected(10000))
    {
      wifiStationFinishConnected();
      return;
    }

    // Could not join within the timeout: fall back to Access Point, so the device is
    // never left unreachable just because the configured home network is out of range.
    Serial.println("Could not join WiFi network, falling back to Access Point");
    wifiStaFallback = true;
  }

  // Access Point mode (selected directly, or a failed Station join falling back to it)
  wifiStartAccessPoint();
}

//
// =======================================================================================================
// MAIN ARDUINO SETUP (1x during startup)
// =======================================================================================================
//
void setup()
{
  // Serial setup
  Serial.begin(115200); // USB serial (for DEBUG)

  // Print some system and software info to serial monitor
  delay(1000); // Give serial port/connection some time to get ready
  Serial.printf("\n**************************************************************************************************\n");
  Serial.printf("Servotester Deluxe C3 Mini for ESP32-C3 software version %s\n", codeVersion);
  Serial.printf("Original version: https://github.com/Ziege-One/Servotester_Deluxe\n");
  Serial.printf("Classic-ESP32 version: https://github.com/MichielBruijn/esp32-servo-tester\n");
  Serial.printf("XTAL Frequency: %i MHz, CPU Clock: %i MHz, APB Bus Clock: %i Hz\n", getXtalFrequencyMhz(), getCpuFrequencyMhz(), getApbFrequency());
  Serial.printf("Internal RAM size: %i Byte, Free: %i Byte\n", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("WiFi MAC address: %s\n", WiFi.macAddress().c_str());
  // ESP32-C3 is single-core (unlike the classic ESP32 this was forked from) - only core 0 exists.
  uint8_t resetReason = rtc_get_reset_reason(0);
  if (resetReason >= 1 && resetReason <= (sizeof(RESET_REASONS) / sizeof(RESET_REASONS[0])))
  {
    Serial.printf("Reset reason: %i: %s\n", resetReason, RESET_REASONS[resetReason - 1]);
  }

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);

  eepromRead(); // Read EEPROM
  eepromInit(); // Initialize EEPROM (store defaults, if new or erased EEPROM is detected)

  // Servo LEDC outputs
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    ledcAttachPin(servopin[ch], ch);
  }
  for (uint8_t group = 0; group < NUM_SERVO_TIMER_GROUPS; group++)
  {
    configureServoGroupFrequency(group);
  }
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    servo_pos[ch] = servoCenterForChannel(ch); // Each channel starts at its own calibrated center, not a generic 1500 - outputs are live from boot, there's no menu gate to correct it later
    sweepAutopos[ch] = SERVO_MAX_BY_MODE[ch][SERVO_MODE_PER_GROUP[servoTimerGroup(ch)]];
  }
  writeAllServos();

  // Kick off a WiFi Station connect attempt now (if configured), so it runs in the background
  // instead of blocking boot for up to 10s.
  bool attemptingStationBoot = (WIFI_MODE == WIFI_STATION_MODE && STA_SSID.length() > 0);
  if (attemptingStationBoot)
  {
    MDNS.end();
    wifiStaFallback = false;
    wifiStationBegin();
    if (wifiStationAwaitConnected(10000))
    {
      wifiStationFinishConnected();
    }
    else
    {
      Serial.println("Could not join WiFi network, falling back to Access Point");
      wifiStaFallback = true;
      wifiStartAccessPoint();
    }
  }
  else
  {
    wifiSetup(); // Access Point mode
  }
}

//
// =======================================================================================================
// MAIN LOOP
// =======================================================================================================
//
void loop()
{
  static int previousMenu = -1;
  bool enteringManual = (Menu == ManualPage && previousMenu != ManualPage);
  bool enteringSweep = (Menu == SweepPage && previousMenu != SweepPage);
  previousMenu = Menu;

  servoModes(); // Keep the legacy SERVO_MODE/MAX/MIN/CENTER/Hz scalars in sync with selectedServo every iteration

  if (enteringManual)
  {
    for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
      servo_pos[ch] = servoCenterForChannel(ch); // Each channel centers on its own calibrated value
  }
  if (enteringSweep)
  {
    for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
      servo_pos[ch] = servoCenterForChannel(ch);
    sweepPaused = true; // Start paused, so you can pick the right channel before it starts moving
    previousTimeAuto = millis();
  }

  webInterface();
  webSocket.loop();
  usbJoystickLoop();

  // Failsafe: an external Joystick Mode controller (app/browser) that goes silent - dropped
  // connection, crashed client, etc. - must not leave the last received command applied
  // indefinitely. Force both channels back to center until fresh PosJ traffic resumes.
  if (webJoystickMode && millis() - lastJoystickMsgMillis > 500)
  {
    applySteerOutput(SERVO_CENTER_BY_MODE[JOYSTICK_X_CHANNEL][SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_X_CHANNEL)]]);
    applyThrottleOutput(SERVO_CENTER_BY_MODE[JOYSTICK_Y_CHANNEL][SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_Y_CHANNEL)]]);
  }

  if (Menu == SweepPage && !sweepPaused)
  {
    if (millis() - previousTimeAuto > TimeAuto)
    {
      previousTimeAuto = millis();
      if (sweepAutopos[selectedServo] > ((SERVO_MIN + SERVO_MAX) / 2))
      {
        servo_pos[selectedServo] = servo_pos[selectedServo] + SERVO_STEPS;
      }
      else
      {
        servo_pos[selectedServo] = servo_pos[selectedServo] - SERVO_STEPS;
      }

      if (servo_pos[selectedServo] < SERVO_MIN)
      {
        sweepAutopos[selectedServo] = SERVO_MAX;
        servo_pos[selectedServo] = SERVO_MIN;
      }
      if (servo_pos[selectedServo] > SERVO_MAX)
      {
        sweepAutopos[selectedServo] = SERVO_MIN;
        servo_pos[selectedServo] = SERVO_MAX;
      }
    }
  }

  writeAllServos();

  // Periodic re-check while connected - a short (typically well under a second) blocking pause
  // in the main loop every 6 hours is an acceptable trade-off against added complexity here.
  if (!updateInProgress && millis() - lastUpdateCheckMillis > UPDATE_CHECK_INTERVAL_MS)
  {
    lastUpdateCheckMillis = millis();
    checkForFirmwareUpdate();
  }
}

//
// =======================================================================================================
// EEPROM
// =======================================================================================================
//
void eepromInit()
{
  bool anyChannelInvalid = false;
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    if (SERVO_MIN_BY_MODE[ch][STD] < 50)
      anyChannelInvalid = true;
  }

  if (anyChannelInvalid || RESET_EEPROM) // Automatic (any channel still uninitialized) or manual reset
  {
    RESET_EEPROM = 0;

    // Restore defaults
    WIFI_MODE = WIFI_AP_MODE; // Factory reset always drops back to the always-reachable Access Point
    STA_SSID = "";
    STA_PASSWORD = ""; // Factory reset must not leave a saved home WiFi password behind
    JOYSTICK_X_CHANNEL = 0; // CH1
    JOYSTICK_Y_CHANNEL = 1; // CH2
    JOYSTICK_X_LINK_MASK = 0;
    JOYSTICK_Y_LINK_MASK = 0;
    STEERING_LIMIT = 0;
    STEERING_LIMIT_ENABLED = 1;
    for (uint8_t g = 0; g < NUM_SERVO_TIMER_GROUPS; g++)
    {
      SERVO_MODE_PER_GROUP[g] = STD;
    }
    for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
    {
      for (uint8_t m = 0; m < NUM_SERVO_MODES; m++)
      {
        if (m == STD || m == NOR || m == SHR)
        {
          SERVO_MAX_BY_MODE[ch][m] = 2000;
          SERVO_MIN_BY_MODE[ch][m] = 1000;
          SERVO_CENTER_BY_MODE[ch][m] = 1500;
        }
        else
        {
          SERVO_MAX_BY_MODE[ch][m] = 470;
          SERVO_MIN_BY_MODE[ch][m] = 130;
          SERVO_CENTER_BY_MODE[ch][m] = 300;
        }
      }
      SERVO_DEGREES[ch] = 90;
    }
    Serial.println("EEPROM initialized with factory defaults");
    servoModes(); // servoModes() needs to be executed in order to actualize the values
    eepromWrite();
  }
}

// Write new values to EEPROM ------
void eepromWrite()
{
  EEPROM.writeInt(adr_eprom_LAYOUT_VERSION, EEPROM_LAYOUT_VERSION);
  EEPROM.writeInt(adr_eprom_WIFI_MODE, WIFI_MODE);
  EEPROM.writeString(adr_eprom_STA_SSID, STA_SSID);
  EEPROM.writeString(adr_eprom_STA_PASSWORD, STA_PASSWORD);
  EEPROM.writeInt(adr_eprom_JOYSTICK_X_CHANNEL, JOYSTICK_X_CHANNEL);
  EEPROM.writeInt(adr_eprom_JOYSTICK_Y_CHANNEL, JOYSTICK_Y_CHANNEL);
  EEPROM.writeInt(adr_eprom_JOYSTICK_X_LINK_MASK, JOYSTICK_X_LINK_MASK);
  EEPROM.writeInt(adr_eprom_JOYSTICK_Y_LINK_MASK, JOYSTICK_Y_LINK_MASK);
  EEPROM.writeInt(adr_eprom_STEERING_LIMIT, STEERING_LIMIT);
  EEPROM.writeInt(adr_eprom_STEERING_LIMIT_ENABLED, STEERING_LIMIT_ENABLED);
  for (uint8_t g = 0; g < NUM_SERVO_TIMER_GROUPS; g++)
  {
    EEPROM.writeInt(adr_eprom_SERVO_MODE_GROUP(g), SERVO_MODE_PER_GROUP[g]);
  }
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    for (uint8_t m = 0; m < NUM_SERVO_MODES; m++)
    {
      EEPROM.writeInt(adr_eprom_SERVO_MAX(ch, m), SERVO_MAX_BY_MODE[ch][m]);
      EEPROM.writeInt(adr_eprom_SERVO_MIN(ch, m), SERVO_MIN_BY_MODE[ch][m]);
      EEPROM.writeInt(adr_eprom_SERVO_CENTER(ch, m), SERVO_CENTER_BY_MODE[ch][m]);
    }
    EEPROM.writeInt(adr_eprom_SERVO_DEGREES(ch), SERVO_DEGREES[ch]);
  }

  EEPROM.commit();
  Serial.println("EEPROM written");
}

// Read values from EEPROM ------
void eepromRead()
{
  // Freshly appended EEPROM bytes aren't reliably blank/zero, so a value range check alone can't
  // tell "never written" apart from "genuinely holds this value" - a layout version marker can.
  // On a mismatch, everything below is about to be overwritten by eepromInit() anyway, so just
  // read it as-is (it'll be garbage on a truly blank chip, but harmless - never displayed/used
  // before eepromInit() runs its anyChannelInvalid check right after this).
  WIFI_MODE = EEPROM.readInt(adr_eprom_WIFI_MODE);
  STA_SSID = EEPROM.readString(adr_eprom_STA_SSID);
  STA_PASSWORD = EEPROM.readString(adr_eprom_STA_PASSWORD);

  JOYSTICK_X_CHANNEL = constrain(EEPROM.readInt(adr_eprom_JOYSTICK_X_CHANNEL), 0, NUM_SERVO_CHANNELS - 1);
  JOYSTICK_Y_CHANNEL = constrain(EEPROM.readInt(adr_eprom_JOYSTICK_Y_CHANNEL), 0, NUM_SERVO_CHANNELS - 1);
  JOYSTICK_X_LINK_MASK = EEPROM.readInt(adr_eprom_JOYSTICK_X_LINK_MASK);
  JOYSTICK_Y_LINK_MASK = EEPROM.readInt(adr_eprom_JOYSTICK_Y_LINK_MASK);
  STEERING_LIMIT = constrain(EEPROM.readInt(adr_eprom_STEERING_LIMIT), 0, 100);
  STEERING_LIMIT_ENABLED = EEPROM.readInt(adr_eprom_STEERING_LIMIT_ENABLED);

  for (uint8_t g = 0; g < NUM_SERVO_TIMER_GROUPS; g++)
  {
    SERVO_MODE_PER_GROUP[g] = constrain(EEPROM.readInt(adr_eprom_SERVO_MODE_GROUP(g)), (int)STD, (int)SXR);
  }

  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    for (uint8_t m = 0; m < NUM_SERVO_MODES; m++)
    {
      SERVO_MAX_BY_MODE[ch][m] = EEPROM.readInt(adr_eprom_SERVO_MAX(ch, m));
      SERVO_MIN_BY_MODE[ch][m] = EEPROM.readInt(adr_eprom_SERVO_MIN(ch, m));
      SERVO_CENTER_BY_MODE[ch][m] = EEPROM.readInt(adr_eprom_SERVO_CENTER(ch, m));
    }
    int readDegrees = EEPROM.readInt(adr_eprom_SERVO_DEGREES(ch));
    SERVO_DEGREES[ch] = (readDegrees < 10 || readDegrees > 360) ? 90 : readDegrees;
  }

  servoModes(); // servoModes() needs to be executed in order to actualize the values

  Serial.println("EEPROM read");
}
