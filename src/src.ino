/*
   Servotester Deluxe
   Ziege-One (Der RC-Modelbauer)
   https://www.youtube.com/watch?v=YNLPCft2qjg&list=PLS6SFYu711FpxCNO_j4_ig4ey0NwVQPcW

   Modified by TheDIYGuy999
   https://www.youtube.com/channel/UCqWO3PNCSjHmYiACDMLr23w

   It is recommended to use VS Code instead of Arduino IDE, because board and library management is way easier this way!
   Requirements:
   - git is installed (allows to download libraries and boards automatically): https://git-scm.com/downloads
   - PlatformIO plugin is installed in VS Code
   - Espressif32 platform is up to date in Platformio > Platforms > Updates

   If you are using Arduino IDE: Select the following board: "ESP32 Dev Module"

 ESP32 + Encoder + OLED

 /////Pin Belegung////
 GPIO 4: active piezo buzzer

 GPIO 13: Servo1
 GPIO 14: Servo2
 GPIO 27: Servo3
 GPIO 33: Servo4
 GPIO 32: Servo5 & BUS Input
 GPIO 15: Ecoder Taster
 GPIO 16: Ecoder Richtung 1
 GPIO 17: Ecoder Richtung 2

 GPIO 21: SDA OLED
 GPIO 22: SDL OLED

 GPIO 2: Encoder click LED (mounted next to the power LED, flashes on every detent)
 GPIO 0: Onboard BOOT button, repurposed as a "next channel" shortcut

 GPIO 34: Joystick X axis (ADC1, input-only)
 GPIO 35: Joystick Y axis (ADC1, input-only)
 GPIO 26: Joystick click button
 */

char codeVersion[] = "0.46"; // Software revision.

//
// =======================================================================================================
// ! ! I M P O R T A N T ! !   ALL USER SETTINGS ARE DONE IN THE FOLLOWING TABS, WHICH ARE DISPLAYED ABOVE
// (ADJUST THEM BEFORE CODE UPLOAD), DO NOT CHANGE ANYTHING IN THIS TAB
// =======================================================================================================
//

// All the required user settings are done in the following .h files:
#include "0_generalSettings.h" // <<------- general settings

//
// =======================================================================================================
// LIRBARIES & HEADER FILES, REQUIRED ESP32 BOARD DEFINITION
// =======================================================================================================
//

// Libraries (you have to install all of them in the "Arduino sketchbook"/libraries folder)
// !! Do NOT install the libraries in the sketch folder.
// No manual library download is required in Visual Studio Code IDE (see platformio.ini)

/* Boardversion
ESP32                                         2.0.5 or 2.0.6 <<--- (make sure your Espressif32 platform is up to date in Platformio > Platforms > Updates)
 */

/* Required Libraries / Benötigte Bibliotheken
ESP32Encoder                                  0.10.1
IBusBM                                        1.1.5
ESP8266 and ESP OLED driver SSD1306 displays  4.3.0
ESP32AnalogRead                               0.2.1
Array                                         1.0.0
 */

#include <ESP32AnalogRead.h> // https://github.com/madhephaestus/ESP32AnalogRead <<------- required for calibrated battery voltage measurement
#include <ESP32Encoder.h>    // https://github.com/madhephaestus/ESP32Encoder/archive/refs/tags/0.10.1.tar.gz
#include <IBusBM.h>          // https://github.com/bmellink/IBusBM/archive/refs/tags/v1.1.5.tar.gz
#include <SH1106Wire.h>      //1.3"
#include <SSD1306Wire.h>     //0.96" https://github.com/ThingPulse/esp8266-oled-ssd1306/archive/refs/tags/4.3.0.tar.gz
#include <Array.h>           //https://github.com/TheDIYGuy999/arduino-array

// No need to install these, they come with the ESP32 board definition
#include <WiFi.h>
#include <esp_wifi.h>      // for esp_wifi_set_country() - default region caps WiFi to channels 1-11, blocking channel 12/13 networks
#include <ESPmDNS.h>       // Reachable as http://servotester.local when joined to an existing network (Station mode)
#include <WebSocketsServer.h> // Low-latency channel for live servo position updates while dragging a web slider
#include <EEPROM.h>          // for non volatile storage
#include <Esp.h>             // for displaying memory information
#include "rom/rtc.h"         // for displaying reset reason
#include "driver/mcpwm.h"    // for servo PWM output
#include "soc/rtc_wdt.h"     // for watchdog timer
#include <soc/sens_reg.h>    // for custom ADC function
#include <soc/sens_struct.h> // for custom ADC function
#include <string>            // std::string, std::stof
using namespace std;

// Project specific includes
#include "src/sbus.h"      // For SBUS interface
#include "src/languages.h" // Menu language ressources

// EEPROM
#define NUM_SERVO_CHANNELS 5
#define NUM_SERVO_MODES 6 // STD, NOR, SHR, SSR, SUR, SXR
#define SERVO_CHANNEL_DATA_START 48
#define SERVO_CHANNEL_STRIDE (NUM_SERVO_MODES * 3 * 4) // 3 values (Max/Min/Center) x 6 modes x 4 bytes = 72 bytes per channel
#define SERVO_CHANNEL_DATA_END (SERVO_CHANNEL_DATA_START + NUM_SERVO_CHANNELS * SERVO_CHANNEL_STRIDE)
#define WIFI_STA_DATA_START (SERVO_CHANNEL_DATA_END + NUM_SERVO_CHANNELS * 4) // + 4 bytes (1 int, degree range) per servo channel, appended after so existing channel data never shifts
#define JOYSTICK_DATA_START (WIFI_STA_DATA_START + 34 + 66) // + Station SSID (34 bytes) and password (66 bytes), appended after so existing data never shifts
#define SERVO_MODE_GROUP_DATA_START (JOYSTICK_DATA_START + 8) // + Joystick X/Y channel mapping (2 ints), appended after so existing data never shifts
#define EEPROM_SIZE (SERVO_MODE_GROUP_DATA_START + NUM_SERVO_TIMER_GROUPS * 4) // + per-timer-group mode (3 ints), appended after so existing data never shifts

int RESET_EEPROM; // WIFI 1 = Reset 0 = No Reset

#define adr_eprom_WIFI_ON 0             // WIFI 1 = Ein 0 = Aus
#define adr_eprom_WIFI_MODE 4           // Reused from the old deprecated SERVO_STEPS address; 0 = Access Point, 1 = Station
#define adr_eprom_LAYOUT_VERSION 8      // Reused from the old deprecated SERVO_MAX scalar address, nothing else writes here anymore
#define EEPROM_LAYOUT_VERSION 7         // Bump this whenever a field is added/moved, so eepromRead() knows to fill in sane defaults for it
#define adr_eprom_STA_SSID WIFI_STA_DATA_START         // Up to 32 chars + null terminator, 34 bytes reserved
#define adr_eprom_STA_PASSWORD (WIFI_STA_DATA_START + 34) // Up to 64 chars + null terminator, 66 bytes reserved
#define adr_eprom_JOYSTICK_X_CHANNEL JOYSTICK_DATA_START
#define adr_eprom_JOYSTICK_Y_CHANNEL (JOYSTICK_DATA_START + 4)
#define adr_eprom_SERVO_MODE_GROUP(g) (SERVO_MODE_GROUP_DATA_START + (g)*4)
// Addresses 12, 16, 20 used to hold a single deprecated SERVO_MIN/CENTER/Hz scalar - unused, free
#define adr_eprom_POWER_SCALE 24        // Skalierung für Akkuspannungs-Messung
#define adr_eprom_SBUS_INVERTED 28      // SBUS inverted
#define adr_eprom_ENCODER_INVERTED 32   // Encoder inverted
#define adr_eprom_LANGUAGE 36           // Gewählte Sprache
#define adr_eprom_SPEED_CURVE 40        // Encoder speed curve exponent x10 (reused from the old removed PONG_BALL_RATE address)
#define adr_eprom_SERVO_MODE 44         // Old single shared mode value, read only once during the layout-version-6 migration

// SERVO µs Max/Min/Center per servo channel (0-4) AND per mode (0-5: STD/NOR/SHR/SSR/SUR/SXR),
// 72 bytes (18 ints) per channel - each mode remembers its own calibration independently now.
#define adr_eprom_SERVO_MAX(ch, mode) (SERVO_CHANNEL_DATA_START + (ch)*SERVO_CHANNEL_STRIDE + (mode)*12 + 0)
#define adr_eprom_SERVO_MIN(ch, mode) (SERVO_CHANNEL_DATA_START + (ch)*SERVO_CHANNEL_STRIDE + (mode)*12 + 4)
#define adr_eprom_SERVO_CENTER(ch, mode) (SERVO_CHANNEL_DATA_START + (ch)*SERVO_CHANNEL_STRIDE + (mode)*12 + 8)
// Full rotation range in degrees per servo channel (e.g. 90/180/360), appended after the block above
#define adr_eprom_SERVO_DEGREES(ch) (SERVO_CHANNEL_DATA_END + (ch)*4)

// Old (pre layout version 7) addresses, only used to migrate existing calibration data forward -
// each channel used to have just 2 shared families (Std/NOR/SHR and SSR/SUR/SXR) in 24 bytes.
#define OLD_SERVO_CHANNEL_DATA_START 48
#define OLD_SERVO_CHANNEL_STRIDE 24
#define OLD_SERVO_CHANNEL_DATA_END (OLD_SERVO_CHANNEL_DATA_START + NUM_SERVO_CHANNELS * OLD_SERVO_CHANNEL_STRIDE)
#define adr_eprom_OLD_SERVO_MAX_STD(ch) (OLD_SERVO_CHANNEL_DATA_START + (ch)*OLD_SERVO_CHANNEL_STRIDE + 0)
#define adr_eprom_OLD_SERVO_MIN_STD(ch) (OLD_SERVO_CHANNEL_DATA_START + (ch)*OLD_SERVO_CHANNEL_STRIDE + 4)
#define adr_eprom_OLD_SERVO_CENTER_STD(ch) (OLD_SERVO_CHANNEL_DATA_START + (ch)*OLD_SERVO_CHANNEL_STRIDE + 8)
#define adr_eprom_OLD_SERVO_MAX_SANWA(ch) (OLD_SERVO_CHANNEL_DATA_START + (ch)*OLD_SERVO_CHANNEL_STRIDE + 12)
#define adr_eprom_OLD_SERVO_MIN_SANWA(ch) (OLD_SERVO_CHANNEL_DATA_START + (ch)*OLD_SERVO_CHANNEL_STRIDE + 16)
#define adr_eprom_OLD_SERVO_CENTER_SANWA(ch) (OLD_SERVO_CHANNEL_DATA_START + (ch)*OLD_SERVO_CHANNEL_STRIDE + 20)
#define adr_eprom_OLD_SERVO_DEGREES(ch) (OLD_SERVO_CHANNEL_DATA_END + (ch)*4)
// Widening the calibration block above also pushed every field after it to a new address - these
// fields' own content didn't change, only where they live, but they still must be read from here
// on the version-7 migration boot, or the data silently reads as blank/garbage from the new location.
#define OLD_WIFI_STA_DATA_START (OLD_SERVO_CHANNEL_DATA_END + NUM_SERVO_CHANNELS * 4)
#define OLD_JOYSTICK_DATA_START (OLD_WIFI_STA_DATA_START + 34 + 66)
#define OLD_SERVO_MODE_GROUP_DATA_START (OLD_JOYSTICK_DATA_START + 8)
#define adr_eprom_OLD_STA_SSID OLD_WIFI_STA_DATA_START
#define adr_eprom_OLD_STA_PASSWORD (OLD_WIFI_STA_DATA_START + 34)
#define adr_eprom_OLD_JOYSTICK_X_CHANNEL OLD_JOYSTICK_DATA_START
#define adr_eprom_OLD_JOYSTICK_Y_CHANNEL (OLD_JOYSTICK_DATA_START + 4)
#define adr_eprom_OLD_SERVO_MODE_GROUP(g) (OLD_SERVO_MODE_GROUP_DATA_START + (g)*4)

// EEPROM storage for settings
int WIFI_ON;            // WIFI 1 = Ein 0 = Aus
enum WifiModeEnum
{
  WIFI_AP_MODE = 0,      // Own access point, e.g. "ServoTester" - always reachable, but disconnects you from your own WiFi
  WIFI_STATION_MODE = 1  // Join an existing WiFi network (STA_SSID/STA_PASSWORD below), reachable via http://servotester.local
};
int WIFI_MODE;           // 0 = Access Point, 1 = Station, see WifiModeEnum above
String STA_SSID = "";     // Home WiFi network SSID to join in Station mode, entered via the web interface
String STA_PASSWORD = ""; // Home WiFi network password to join in Station mode, entered via the web interface
bool wifiStaFallback;     // True when Station mode was requested but joining failed, and we fell back to Access Point
int JOYSTICK_X_CHANNEL;  // Which servo channel (0-4) the joystick's X axis drives
int JOYSTICK_Y_CHANNEL;  // Which servo channel (0-4) the joystick's Y axis drives
// Learned raw ADC extremes per axis (see the self-widening calibration in Joystick_Menu), exposed
// globally too so the web interface's Joystick page can show them for diagnosis. Initialized in setup().
int joystickXRawMin, joystickXRawMax, joystickYRawMin, joystickYRawMax;
String wifiIpString = ""; // AP/Station IP address, filled in wifiSetup(), shown in the Wifi Info screen
int SERVO_STEPS;        // Deprecated, calculated automaticallly
int SERVO_MAX;          // Deprecated, controlled by servoModes.h
int SERVO_MIN;          // Deprecated, controlled by servoModes.h
int SERVO_CENTER;       // Deprecated, controlled by servoModes.h
int SERVO_Hz;           // Deprecated, controlled by servoModes.h
int POWER_SCALE;        // Skalierung für Akkuspannungs-Messung
int SBUS_INVERTED;      // SBUS inverted
int ENCODER_INVERTED;   // Encoder inverted
int LANGUAGE;           // Gewählte Sprache
int SPEED_CURVE;        // Encoder speed curve exponent x10 (e.g. 19 = 1.9)
int SERVO_MODE;         // Legacy scalar: always refreshed to reflect selectedServo's own group (see servoModes())
#define NUM_SERVO_TIMER_GROUPS 3 // ESP32 MCPWM only has 3 independent timers for 5 channels: {CH1,CH2}, {CH3,CH4}, {CH5}
int SERVO_MODE_PER_GROUP[NUM_SERVO_TIMER_GROUPS]; // Mode (and therefore Hz) is stored per timer group, not per channel - channels sharing a timer physically can't run different Hz at once
// Which timer group a channel belongs to (0-indexed channel number in, 0-2 group index out)
uint8_t servoTimerGroup(uint8_t ch)
{
  if (ch <= 1)
    return 0; // Servo 1+2 - MCPWM_UNIT_0/TIMER_0
  if (ch <= 3)
    return 1; // Servo 3+4 - MCPWM_UNIT_0/TIMER_1
  return 2;   // Servo 5   - MCPWM_UNIT_1/TIMER_0 (the only channel with a genuinely independent timer)
}
// Min/Max/Center in µs, per servo channel AND per mode (STD/NOR/SHR/SSR/SUR/SXR) - each mode
// remembers its own calibration independently for a given channel.
int SERVO_MAX_BY_MODE[NUM_SERVO_CHANNELS][NUM_SERVO_MODES];
int SERVO_MIN_BY_MODE[NUM_SERVO_CHANNELS][NUM_SERVO_MODES];
int SERVO_CENTER_BY_MODE[NUM_SERVO_CHANNELS][NUM_SERVO_MODES];
int SERVO_DEGREES[NUM_SERVO_CHANNELS];      // Volledige draaihoek in graden (bv. 90/180/360), pro Kanal

bool WiFiChanged;
bool webArcadeMode; // Web interface: two-thumb touch-slider control page instead of the normal per-channel sliders

// Encoder + button
ESP32Encoder encoder;

#define BUTTON_PIN 15         // Hardware Pin Button
#define BOOT_BUTTON_PIN 0     // Onboard BOOT button, repurposed at runtime as a channel++ shortcut

// Optional analog joystick - X/Y axes each drive a configurable servo channel directly (position
// control, like an RC stick), click button re-centers both mapped channels.
#define JOYSTICK_X_PIN 34        // ADC1 channel, input-only
#define JOYSTICK_Y_PIN 35        // ADC1 channel, input-only
#define JOYSTICK_BUTTON_PIN 26
#define JOYSTICK_ADC_MAX 4095    // 12-bit ADC
#define JOYSTICK_ADC_CENTER 2048
#define JOYSTICK_DEADZONE 150    // +/- around center that snaps to the channel's calibrated Center, absorbs mechanical/ADC noise at rest
#define ENCODER_PIN_1 16      // Hardware Pin1 Encoder
#define ENCODER_PIN_2 17      // Hardware Pin2 Encoder
long prev1 = 0;               // Zeitspeicher für Taster
long prev2 = 0;               // Zeitspeicher für Taster
int buttonState = 0;          // 0 = Taster nicht betätigt; 1 = Taster langer Druck; 2 = Taster kurzer Druck; 3 = Taster Doppelklick
int encoderState = 0;         // 1 = Drehung nach Links (-); 2 = Drehung nach Rechts (+)
int Duration_long = 600;      // Zeit für langen Druck
int Duration_double = 200;    // Zeit für Doppelklick
int bouncing = 50;            // Zeit für Taster Entprellung
int encoder_last;             // Speicher letzer Wert Encoder
int encoder_read;             // Speicher aktueller Wert Encoder
int encoderSpeed;             // Speicher aktuelle Encoder Geschwindigkeit für Beschleunigung
bool disableButtonRead;       // In gewissen Situationen soll der Encoder Button nicht von der blockierenden Funktion gelesen werden

// 3 pin connectors, used as input and output
#define SERVO_CONNECTOR_1 13
#define SERVO_CONNECTOR_2 14
#define SERVO_CONNECTOR_3 27
#define SERVO_CONNECTOR_4 33
#define SERVO_CONNECTOR_5 32

// Servo
uint8_t servopin[5] = {SERVO_CONNECTOR_1, SERVO_CONNECTOR_2, SERVO_CONNECTOR_3, SERVO_CONNECTOR_4, SERVO_CONNECTOR_5}; // Pins Servoausgang 1 - 5                                                                                                                     // Servo Objekte erzeugen
int servo_pos[5];                                                                                                      // Speicher für Servowerte
int selectedServo = 0;                                                                                                 // Das momentan angesteuerte Servo
String servoMode = "";                                                                                                 // Servo operation mode text

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

// Sound
#define BUZZER_PIN 4 // Passive buzzer, driven via LEDC tone
#define BUZZER_LEDC_CHANNEL 4  // LEDC channel 2 is already used by the signal generator on GPIO 26
#define BUZZER_TONE_HZ 2700    // Audible tone frequency for the passive buzzer
int beepDuration;    // how long the beep will be
bool pewPewTrigger;  // Set true to start the joystick button's "pew pew" laser sound

// Encoder click LED, next to the power LED
#define ENCODER_LED_PIN 2 // Flashes on every encoder detent
#define ENCODER_LED_FLASH_MS 15 // Flash duration per click
int encoderLedDuration; // ms remaining for the current flash, 0 = off

// Oscilloscope + Signal Generator pins - restored from an earlier version of this fork, moved off
// their original pins (GPIO32, GPIO26) since those are now Servo5/SBUS and the joystick button
#define OSCILLOSCOPE_PIN 39     // ADC1 pin only, input-only! Don't change without also updating oscilloscope.h's hardcoded ADC1_CHANNEL_3
#define SIGNAL_GENERATOR_PIN 25 // The other DAC-capable pin (DAC_CHANNEL_1)

// Serial command pins for SBUS, IBUS -----
#define COMMAND_RX 32 // pin 13
#define COMMAND_TX -1 // -1 is just a dummy

// SBUS
bfs::SbusRx sBus(&Serial2);
std::array<int16_t, bfs::SbusRx::NUM_CH()> SBUSchannels;

// IBUS
IBusBM IBus; // IBus object

// Externe Spannungsversorgung
#define BATTERY_DETECT_PIN 36  // Hardware Pin Externe Spannung in. ADC channel 2 only!
bool batteryDetected;          // Akku vorhanden
int numberOfBatteryCells;      // Akkuzellen
float batteryVoltage;          // Akkuspannung in V
float batteryChargePercentage; // Akkuspannung in Prozent

// Menüstruktur
/*
 * 1 = Servotester_Select       Selection -> 51 Servotester_Menu
 * 2 = AutoMode_Select   Selection -> 52 AutoMode_Menu
 * 3 = ReadPulse_Select      Selection -> 53 ReadPulse_Menu
 * 4 = ReadMultiswitch_Select Selection -> 54 ReadMultiswitch_Menu
 * 5 = ReadSbus_Select        Selection -> 55 ReadSbus_Menu
 * 6 = ReadIbus_Select        Selection -> 56 ReadIbus_Menu
 * 7 = Info_Select              Selection -> 57 Info_Menu (3 pages, left/right to page through: Controls, Wifi, Firmware)
 * 8 = Joystick_Select          Selection -> 60 Joystick_Menu
 * 9 = Oscilloscope_Select      Selection -> 61 Oscilloscope_Menu
 * 10 = SignalGenerator_Select  Selection -> 62 SignalGenerator_Menu
 * 11 = Settings_Select      Selection -> 58 Settings_Menu (last item)
 * etc.
 */
enum
{
  Servotester_Select = 1,
  AutoMode_Select = 2,
  ReadPulse_Select = 3,
  ReadMultiswitch_Select = 4,
  ReadSbus_Select = 5,
  ReadIbus_Select = 6,
  Info_Select = 7,
  Joystick_Select = 8,
  Oscilloscope_Select = 9,
  SignalGenerator_Select = 10,
  Settings_Select = 11,
  //
  Servotester_Menu = 51,
  AutoMode_Menu = 52,
  ReadPulse_Menu = 53,
  ReadMultiswitch_Menu = 54,
  ReadSbus_Menu = 55,
  ReadIbus_Menu = 56,
  Info_Menu = 57,
  Settings_Menu = 58,
  Joystick_Menu = 60,
  Oscilloscope_Menu = 61,
  SignalGenerator_Menu = 62
};

//-Menu 52 Automatik Modus
int Autopos[5];      // Speicher
bool Auto_Pause = 0; // Pause im Auto Modus

//-Menu 53 Impuls lesen
int PulseMin = 1000;
int PulseMax = 2000;
int pwmFreq = 0;

//-Menu 54 Multiwitch Futaba lesen
#define kanaele 9    // Anzahl der Multiswitch Kanäle
int value1[kanaele]; // Speicher Multiswitch Werte

//-Menu
int Menu = Servotester_Select; // Active menu
bool SetupMenu = false;         // Setup-menu state
int SettingsItem = 0;            // Active settings item
int InfoPage = 0;               // Which page of the Info menu is shown (0=Wifi, 1=Controls, 2=Firmware)
bool Edit = false;              // Settings item selected for editing

// Battery voltage
ESP32AnalogRead battery;

// OLED
#ifdef OLED1306
SSD1306Wire display(0x3c, SDA, SCL); // Oled Hardware an SDA 21 und SCL 22
#else
SH1106Wire display(0x3c, SDA, SCL); // Oled Hardware an SDA 21 und SCL 22
#endif

// Webserver auf Port 80
WiFiServer server(80);

// Low-latency WebSocket channel (port 81), used only for live servo position updates while
// dragging a slider in the web interface - a plain HTTP request per tick (even throttled) still
// pays a fresh TCP handshake and full header parse each time, which is what made a sustained drag
// feel laggy compared to a real RC stick. Everything else (page navigation, settings) stays on the
// existing HTTP server.
WebSocketsServer webSocket(81);

// Handle incoming WebSocket frames - the only message this expects is "Pos<ch>=<value>",
// e.g. "Pos0=1500", mirroring the /?Pos0= HTTP query key. The browser already clamps to that
// channel's calibrated Min/Max via the slider's own min/max attributes, same as the HTTP path.
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (type != WStype_TEXT)
    return;

  String msg;
  msg.reserve(length);
  for (size_t i = 0; i < length; i++)
    msg += (char)payload[i];

  if (msg.startsWith("Pos") && msg.length() > 4)
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

// Speicher HTTP request
String header;

// Für HTTP GET value
String valueString = String(5);
int pos1 = 0;
int pos2 = 0;

// These are used to print the reset reason on startup
const char *RESET_REASONS[] = {"POWERON_RESET", "NO_REASON", "SW_RESET", "OWDT_RESET", "DEEPSLEEP_RESET", "SDIO_RESET", "TG0WDT_SYS_RESET", "TG1WDT_SYS_RESET", "RTCWDT_SYS_RESET", "INTRUSION_RESET", "TGWDT_CPU_RESET", "SW_CPU_RESET", "RTCWDT_CPU_RESET", "EXT_CPU_RESET", "RTCWDT_BROWN_OUT_RESET", "RTCWDT_RTC_RESET"};

unsigned long currentTime = millis();     // Aktuelle Zeit
unsigned long currentTimeAuto = millis(); // Aktuelle Zeit für Auto Modus
unsigned long currentTimeSpan = millis(); // Aktuelle Zeit für Externe Spannung
unsigned long previousTime = 0;           // Previous time
unsigned long previousTimeAuto = 0;       // Previous time für Auto Modus
unsigned long previousTimeSpan = 0;       // Previous time für Externe Spannung
int TimeAuto = 50;                        // Auto time
const long timeoutTime = 2000;            // Define timeout time in milliseconds (example: 2000ms = 2s)

//
// =======================================================================================================
// SUB FUNCTIONS & ADDITIONAL HEADERS
// =======================================================================================================
//

// Map as Float --------------------------------------------------------------------------------
float map_float(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Convert µs to degrees (°)
float us2degree(uint16_t value)
{
  float halfRange = SERVO_DEGREES[selectedServo] / 2.0;
  return map_float(float(value), float(SERVO_MIN), float(SERVO_MAX), -halfRange, halfRange);
}

// buzzer control ------------------------------------------------------------------------------
void beep()
{
  static unsigned long buzzerTriggerMillis;
  static bool buzzerOn; // Track state in software, don't rely on reading the driven pin back
  if (beepDuration > 0 && !buzzerOn)
  {
    ledcWrite(BUZZER_LEDC_CHANNEL, 128); // 50% duty at BUZZER_TONE_HZ = audible tone on a passive buzzer
    buzzerOn = true;
    buzzerTriggerMillis = millis();
  }

  if (buzzerOn && millis() - buzzerTriggerMillis >= beepDuration)
  {
    ledcWrite(BUZZER_LEDC_CHANNEL, 0); // Silence
    buzzerOn = false;
    beepDuration = 0;
  }
}

// "Pew pew" laser sound for the joystick click button --------------------------------------------
// Non-blocking descending frequency sweep on the same buzzer channel as beep(), so it never runs
// at the same time as a plain beep (pewPewTrigger and beepDuration are never both set together).
void pewPew()
{
  static unsigned long pewPewStartMillis;
  static bool pewPewActive;
  const unsigned long pewPewDuration = 150; // ms, total sweep length

  if (pewPewTrigger)
  {
    pewPewTrigger = false;
    pewPewActive = true;
    pewPewStartMillis = millis();
  }

  if (pewPewActive)
  {
    unsigned long elapsed = millis() - pewPewStartMillis;
    if (elapsed >= pewPewDuration)
    {
      ledcWriteTone(BUZZER_LEDC_CHANNEL, 0); // Silence
      pewPewActive = false;
    }
    else
    {
      int freq = map(elapsed, 0, pewPewDuration, 3000, 400); // High pitch down to low = laser zap
      ledcWriteTone(BUZZER_LEDC_CHANNEL, freq);
    }
  }
}

// encoder click LED control ---------------------------------------------------------------------
void flashEncoderLed()
{
  static unsigned long ledTriggerMillis;
  static bool ledOn; // Track state in software, don't rely on reading the driven pin back

  if (encoderLedDuration > 0 && !ledOn)
  {
    digitalWrite(ENCODER_LED_PIN, HIGH);
    ledOn = true;
    ledTriggerMillis = millis();
  }

  if (ledOn && millis() - ledTriggerMillis >= encoderLedDuration)
  {
    digitalWrite(ENCODER_LED_PIN, LOW);
    ledOn = false;
    encoderLedDuration = 0;
  }
}

// Loop time measurement -----------------------------------------------------------------------
unsigned long loopDuration()
{
  static unsigned long timerOld;
  unsigned long loopTime;
  unsigned long timer = millis();
  loopTime = timer - timerOld;
  timerOld = timer;
  return loopTime;
}

// Frequency counter ---------------------------------------------------------------------------
#define WAIT_FOR_PIN_STATE(state)                                       \
  while (digitalRead(pin) != (state))                                   \
  {                                                                     \
    if (cpu_hal_get_cycle_count() - start_cycle_count > timeout_cycles) \
    {                                                                   \
      return 0;                                                         \
    }                                                                   \
  }

unsigned long readFreq(uint8_t pin, uint8_t state, unsigned long timeout)
{
  const uint32_t max_timeout_us = clockCyclesToMicroseconds(UINT_MAX);
  if (timeout > max_timeout_us)
  {
    timeout = max_timeout_us;
  }
  const uint32_t timeout_cycles = microsecondsToClockCycles(timeout);
  const uint32_t start_cycle_count = cpu_hal_get_cycle_count();
  WAIT_FOR_PIN_STATE(!state);
  WAIT_FOR_PIN_STATE(state);                                          // Signal going high
  const uint32_t pulse_start_cycle_count = cpu_hal_get_cycle_count(); // Store start time
  WAIT_FOR_PIN_STATE(!state);                                         // Signal going low
  WAIT_FOR_PIN_STATE(state);                                          // Signal going high again

  uint32_t periodUs = clockCyclesToMicroseconds(cpu_hal_get_cycle_count() - pulse_start_cycle_count);
  if (periodUs == 0) // Noise on a floating/unconnected pin can produce back-to-back edges within 1µs
    return 0;
  return 1000000 / periodUs;
}

// Super fast analogRead() alternative, used by the oscilloscope ------------------------------
// See: https://www.toptal.com/embedded/esp32-audio-sampling
int IRAM_ATTR local_adc1_read(int channel)
{
  uint16_t adc_value;
  SENS.sar_meas_start1.sar1_en_pad = (1 << channel); // only one channel is selected
  while (SENS.sar_slave_addr1.meas_status != 0)
    ;
  SENS.sar_meas_start1.meas1_start_sar = 0;
  SENS.sar_meas_start1.meas1_start_sar = 1;
  while (SENS.sar_meas_start1.meas1_done_sar == 0)
    ;
  adc_value = SENS.sar_meas_start1.meas1_data_sar;
  return adc_value;
}

// Additional headers --------------------------------------------------------------------------
#include "src/servoModes.h"      // Servo operation profiles
#include "src/webInterface.h"    // Configuration website
#include "src/oscilloscope.h"    // A handy oscilloscope
#include "src/signalGenerator.h" // A handy signal generator
#include "src/systemImages.h"    // Symbols

//
// =======================================================================================================
// mcpwm unit SETUP for servos (1x during startup)
// =======================================================================================================
//
// See: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/mcpwm.html#configure

void setupMcpwm()
{

  // Unit 0 ---------------------------------------------------------------------
  // 1. set our servo 1 - 4 output pins
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, SERVO_CONNECTOR_1); // Set steering as PWM0A
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, SERVO_CONNECTOR_2); // Set shifting as PWM0B
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1A, SERVO_CONNECTOR_3); // Set coupling as PWM1A
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1B, SERVO_CONNECTOR_4); // Set winch  or beacon as PWM1B

  // 2. configure MCPWM parameters
  // Each timer group has its own stored mode/Hz (see SERVO_MODE_PER_GROUP) - Servo1+2 and Servo3+4
  // physically share one timer each, so within a pair both channels always run at the same Hz;
  // only Servo5 has a genuinely independent timer.
  mcpwm_config_t pwm_config;
  pwm_config.frequency = servoHzForMode(SERVO_MODE_PER_GROUP[0]); // Servo 1+2
  pwm_config.cmpr_a = 0;           // duty cycle of PWMxa = 0
  pwm_config.cmpr_b = 0;           // duty cycle of PWMxb = 0
  pwm_config.counter_mode = MCPWM_UP_COUNTER;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0; // 0 = not inverted, 1 = inverted
  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config); // Configure PWM0A & PWM0B

  mcpwm_config_t pwm_config2 = pwm_config;
  pwm_config2.frequency = servoHzForMode(SERVO_MODE_PER_GROUP[1]); // Servo 3+4
  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_1, &pwm_config2); // Configure PWM1A & PWM1B

  // Unit 1 ---------------------------------------------------------------------
  // 1. set our servo 5 output pin
  mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0A, SERVO_CONNECTOR_5); // Set ESC as PWM0A

  // 2. configure MCPWM parameters
  mcpwm_config_t pwm_config1;
  pwm_config1.frequency = servoHzForMode(SERVO_MODE_PER_GROUP[2]); // Servo 5, independent timer
  pwm_config1.cmpr_a = 0;           // duty cycle of PWMxa = 0
  pwm_config1.cmpr_b = 0;           // duty cycle of PWMxb = 0
  pwm_config1.counter_mode = MCPWM_UP_COUNTER;
  pwm_config1.duty_mode = MCPWM_DUTY_MODE_0; // 0 = not inverted, 1 = inverted

  // 3. configure channels with settings above
  mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_0, &pwm_config1); // Configure PWM0A & PWM0B
}

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
// to 10s) so setup() can kick it off before the splash/help screens and only wait out whatever's
// left of the timeout afterward - the connect attempt and those screens then overlap instead of
// stacking, which used to make Station mode the single biggest chunk of boot time.
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

  if (MDNS.begin("servotester"))
  {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: http://servotester.local");
  }

  digitalWrite(BUZZER_PIN, LOW); // Buzzer off
  Serial.printf("\nWiFi Tx Power Level: %u", WiFi.getTxPower());
  WiFi.setTxPower(cpType); // WiFi and ESP-Now power according to "0_generalSettings.h"
  Serial.printf("\nWiFi Tx Power Level changed to: %u\n\n", WiFi.getTxPower());

  server.begin(); // Start Webserver
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void wifiStartAccessPoint()
{
  Serial.println(connectingAccessPointString[LANGUAGE]);
  WiFi.mode(WIFI_STA);
  setWifiChannelRange(); // allow channels 12/13, not just the default-region 1-11
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print(apIpAddressString[LANGUAGE]);
  Serial.println(IP);
  wifiIpString = IP.toString();

  digitalWrite(BUZZER_PIN, LOW); // Buzzer off

  // SSID, password and IP are shown on demand in the Settings menu (Wifi item) instead of a boot popup

  Serial.printf("\nWiFi Tx Power Level: %u", WiFi.getTxPower());
  WiFi.setTxPower(cpType); // WiFi and ESP-Now power according to "0_generalSettings.h"
  Serial.printf("\nWiFi Tx Power Level changed to: %u\n\n", WiFi.getTxPower());

  server.begin(); // Start Webserver
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void wifiSetup()
{
  MDNS.end(); // Clear any previous responder before (re)configuring WiFi, safe even if never started
  wifiStaFallback = false;

  if (WIFI_ON == 1)
  { // Wifi Ein
    if (WIFI_MODE == WIFI_STATION_MODE && STA_SSID.length() > 0)
    {
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_10);
      display.drawString(64, 20, "Connecting to");
      display.drawString(64, 34, STA_SSID);
      display.display();

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

  // WiFi off
  else
  {
    server.end();
    webSocket.close();
    WiFi.mode(WIFI_OFF);
    Serial.println("");
    Serial.println(WiFiOffString[LANGUAGE]);

    digitalWrite(BUZZER_PIN, LOW); // Buzzer off

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 10, "WiFi");
    display.drawString(0, 26, offString[LANGUAGE]);
    display.display();
    delay(700);
  }
}

//
// =======================================================================================================
// MAIN ARDUINO SETUP (1x during startup)
// =======================================================================================================
//
void setup()
{

  // Watchdog timers need to be disabled, if task 1 is running without delay(1)
  // disableCore0WDT();

  // Serial setup
  Serial.begin(115200); // USB serial (for DEBUG)

  // Print some system and software info to serial monitor
  delay(1000); // Give serial port/connection some time to get ready
  Serial.printf("\n**************************************************************************************************\n");
  Serial.printf("Sevotester Deluxe for ESP32 software version %s\n", codeVersion);
  Serial.printf("Original version: https://github.com/Ziege-One/Servotester_Deluxe\n");
  Serial.printf("Modified version: https://github.com/TheDIYGuy999/Servotester_Deluxe\n");
  Serial.printf("XTAL Frequency: %i MHz, CPU Clock: %i MHz, APB Bus Clock: %i Hz\n", getXtalFrequencyMhz(), getCpuFrequencyMhz(), getApbFrequency());
  Serial.printf("Internal RAM size: %i Byte, Free: %i Byte\n", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("WiFi MAC address: %s\n", WiFi.macAddress().c_str());
  for (uint8_t coreNum = 0; coreNum < 2; coreNum++)
  {
    uint8_t resetReason = rtc_get_reset_reason(coreNum);
    if (resetReason <= (sizeof(RESET_REASONS) / sizeof(RESET_REASONS[0])))
    {
      Serial.printf("Core %i reset reason: %i: %s\n", coreNum, rtc_get_reset_reason(coreNum), RESET_REASONS[resetReason - 1]);
    }
  }

  // EEPROM
  EEPROM.begin(EEPROM_SIZE);

  eepromRead(); // Read EEPROM

  eepromInit(); // Initialize EEPROM (store defaults, if new or erased EEPROM is detected)

  // Setup Encoder
  ESP32Encoder::useInternalWeakPullResistors = UP;
  encoder.attachHalfQuad(ENCODER_PIN_1, ENCODER_PIN_2);
  encoder.setFilter(1023);
  pinMode(BUTTON_PIN, INPUT_PULLUP); // BUTTON_PIN = Eingang
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP); // BOOT button, only read after boot completes - GPIO0's strapping role is over by then
  pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP); // Read globally now (channel-change works from any menu), not just while inside Joystick_Menu
  joystickXRawMin = joystickXRawMax = joystickYRawMin = joystickYRawMax = JOYSTICK_ADC_CENTER;
  // 11dB is already this core's default (full 0-3.3V ADC range), set explicitly for certainty
  analogSetPinAttenuation(JOYSTICK_X_PIN, ADC_11db);
  analogSetPinAttenuation(JOYSTICK_Y_PIN, ADC_11db);

  // Speaker setup (passive buzzer, needs a PWM tone rather than a flat digitalWrite)
  ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_TONE_HZ, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);

  // Encoder click LED setup
  pinMode(ENCODER_LED_PIN, OUTPUT);
  digitalWrite(ENCODER_LED_PIN, LOW);

  // Battery
  battery.attach(BATTERY_DETECT_PIN);

  // Setup OLED
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.display(); // Blank rather than garbled/uninitialized while Station connect starts below

  // Kick off a WiFi Station connect attempt now (if configured), so it runs in the background
  // while the splash/help screens below are shown, instead of waiting for it (up to 10s)
  // afterward - the two now overlap instead of stacking, since that connect attempt used to be
  // the single biggest chunk of boot time.
  bool attemptingStationBoot = (WIFI_ON == 1 && WIFI_MODE == WIFI_STATION_MODE && STA_SSID.length() > 0);
  if (attemptingStationBoot)
  {
    MDNS.end();
    wifiStaFallback = false;
    wifiStationBegin();
  }

  // Show splash screen
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 22, "Servo Tester");
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 48, String(codeVersion));
  display.display();
  delay(1250);

  // Show manual
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, operationString[LANGUAGE]);
  display.drawString(0, 12, shortPressString[LANGUAGE]);
  display.drawString(0, 24, longPressString[LANGUAGE]);
  display.drawString(0, 36, doubleclickString[LANGUAGE]);
  display.drawString(0, 48, RotateKnobString[LANGUAGE]);
  display.display();

  unsigned long helpScreenStart = millis();
  while (digitalRead(BUTTON_PIN) && millis() - helpScreenStart < 10000)
  {
    // Wait for a button press or a 10s timeout, whichever comes first
  }
  while (!digitalRead(BUTTON_PIN))
  {
    // Wait for button release, so the press doesn't leak into the main menu
  }

  if (attemptingStationBoot)
  {
    // Only waits out whatever's left of the 10s budget - most of it was likely already spent
    // above, during the splash/help screens.
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
    wifiSetup(); // Access Point mode, or WiFi off - nothing worth overlapping, just set it up now
  }

  encoder.setCount(Menu);
  servo_pos[0] = 1500;
  servo_pos[1] = 1500;
  servo_pos[2] = 1500;
  servo_pos[3] = 1500;
  servo_pos[4] = 1500;

  beepDuration = 10; // Short beep = device ready

  /*
    // Task 1 setup (running on core 0) TODO, testing only
    TaskHandle_t Task1;
    // create a task that will be executed in the Task1code() function, with priority 1 and executed on core 0
    xTaskCreatePinnedToCore(
        Task1code, // Task function
        "Task1",   // name of task
        8192,      // Stack size of task (8192)
        NULL,      // parameter of the task
        1,         // priority of the task (1 = low, 3 = medium, 5 = highest)
        &Task1,    // Task handle to keep track of created task
        0);        // pin task to core 0
        */
}

//
// =======================================================================================================
// ENCODER & BUTTON
// =======================================================================================================
//
void ButtonRead()
{
  static bool longPressFired; // Makes sure the long-press action fires only once per physical hold, so releasing afterwards isn't misread as a fresh short press
  if (!disableButtonRead)
  {
    buttonState = 0;
    if (!(digitalRead(BUTTON_PIN)))
    {                  // Button gedrückt 0
      if (!longPressFired)
      {
        delay(bouncing); // Taster entprellen
        prev1 = millis();
        buttonState = 1;
        while ((millis() - prev1) <= Duration_long)
        {
          if (digitalRead(BUTTON_PIN))
          {                  // Button losgelassen 1 innerhalb Zeit
            delay(bouncing); // Taster entprellen
            buttonState = 2;
            prev2 = millis();
            while ((millis() - prev2) <= Duration_double)
            { // Doppelkick abwarten
              if (!(digitalRead(BUTTON_PIN)))
              {                  // Button gedrückt 0 innerhalb Zeit Doppelklick
                delay(bouncing); // Taster entprellen
                buttonState = 3;
                if (digitalRead(BUTTON_PIN))
                { // Button losgelassen 1
                  break;
                }
              }
            }
            break;
          }
        }

        if (buttonState == 1) // Long press timed out while still held: act on it now, don't wait for release
        {
          longPressFired = true;
        }
        else
        {
          while (!(digitalRead(BUTTON_PIN)))
          { // Warten bis Button nicht gedückt ist = 1
          }
        }

        Serial.print("Buttonstate: ");
        Serial.println(buttonState);

        if (buttonState == 1)
          beepDuration = 20;
        if (buttonState == 2)
          beepDuration = 10;
        if (buttonState == 3)
          beepDuration = 30;

        encoderLedDuration = ENCODER_LED_FLASH_MS; // Flash the click LED on every button press too
      }
    }
    else
    {
      longPressFired = false; // Button released, ready to detect a fresh press next time
    }
  }

  // Encoder -------------------------------------------------------------------------------------------------
  encoder_read = encoder.getCount(); // Read encoder --------------
  // Note: no software debounce/throttle here on purpose. The hardware pulse counter is already
  // glitch-filtered (encoder.setFilter() in setup()), and a time-based software debounce previously
  // here was re-syncing encoder_last to encoder_read within its window, silently dropping any tick
  // that followed within 10ms of the last one - exactly the fast turns we want to register.

  static unsigned long encoderSpeedMillis;
  static int lastEncoderSpeed;

  if (millis() - encoderSpeedMillis > 150) // Encoder speed detection (150 was 100) -----------------
  {
    encoderSpeedMillis = millis();
    encoderSpeed = abs(encoder_read - lastEncoderSpeed);
    encoderSpeed = constrain(encoderSpeed, 1, 15);
    encoderSpeed = (int)round(pow(encoderSpeed, SPEED_CURVE / 10.0)); // Power-law ramp, exponent adjustable in Settings ("Speed Curve")

    // Serial.println(encoderSpeed); // For encoder speed debuggging

    lastEncoderSpeed = encoder_read;
  }

  // This encoder's mechanical detent (the "click" you feel) reports as 2 raw quadrature counts,
  // with no reliable timing gap between them (it varies with turning speed and even direction
  // reversal), so a fixed settle-timeout can't tell "both halves of one click" apart from "a
  // lone stray count". Comparing counts in whole-detent units (raw count / 2) sidesteps the
  // timing question entirely: a physical click always moves this value by exactly 1, however
  // its 2 raw counts are spaced out in time.
  int currentDetent = encoder_read / 2;
  int lastDetent = encoder_last / 2;

  encoderState = 0;
  if (currentDetent > lastDetent)
  {
    encoderState = ENCODER_INVERTED ? 1 : 2; // right (or left if inverted)
    encoder_last = encoder_read;
    encoderLedDuration = ENCODER_LED_FLASH_MS; // Flash the click LED for this detent
  }
  else if (currentDetent < lastDetent)
  {
    encoderState = ENCODER_INVERTED ? 2 : 1; // left (or right if inverted)
    encoder_last = encoder_read;
    encoderLedDuration = ENCODER_LED_FLASH_MS; // Flash the click LED for this detent
  }

  // BOOT button ---------------------------------------------------------------------------------
  // Extra physical shortcut for "next channel", so you don't need the encoder's double-click for it.
  static bool lastBootButtonState = HIGH;
  static unsigned long bootButtonMillis;
  bool bootButtonState = digitalRead(BOOT_BUTTON_PIN);
  if (bootButtonState == LOW && lastBootButtonState == HIGH && millis() - bootButtonMillis > bouncing)
  {
    bootButtonMillis = millis();
    selectedServo++;
    if (selectedServo > NUM_SERVO_CHANNELS - 1)
      selectedServo = 0;
    encoderLedDuration = ENCODER_LED_FLASH_MS; // Same click feedback as the encoder button/detent
    beepDuration = 10; // Same short beep as a normal button click
  }
  lastBootButtonState = bootButtonState;

  // Joystick click button -------------------------------------------------------------------------
  // Always changes channel too (same as the BOOT button), from any menu - including Joystick_Menu,
  // where it does NOT affect the joystick's own X/Y channel mapping, only which channel is
  // "selected" for when you leave the Joystick menu. Uses the "pew pew" sound instead of a plain
  // beep so it stays a distinct, fun button in live mode.
  static bool lastJoystickButtonState = HIGH;
  static unsigned long joystickButtonMillis;
  bool joystickButtonState = digitalRead(JOYSTICK_BUTTON_PIN);
  if (joystickButtonState == LOW && lastJoystickButtonState == HIGH && millis() - joystickButtonMillis > bouncing)
  {
    joystickButtonMillis = millis();
    selectedServo++;
    if (selectedServo > NUM_SERVO_CHANNELS - 1)
      selectedServo = 0;
    encoderLedDuration = ENCODER_LED_FLASH_MS;
    pewPewTrigger = true;
  }
  lastJoystickButtonState = joystickButtonState;
}

//
// =======================================================================================================
// MENU
// =======================================================================================================
// (see the menu structure comment above the enum definition for the full list)
//
void MenuUpdate()
{

  switch (Menu)
  {
    // Servotester Selection *********************************************************
  case Servotester_Select:
    servoModes();   // Refresh servo operation mode
    batteryVolts(); // Read battery voltage
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "  Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, servotesterString[LANGUAGE]);
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "Hz");
    display.drawString(0, 10, String(SERVO_Hz));
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, servoMode);
    drawWiFi();
    if (batteryDetected)
    {
      display.setFont(ArialMT_Plain_16);
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 45, String(batteryVoltage, 2) + "V");
    }
    else
    {
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 50, noBatteryString[LANGUAGE]); // No battery
      display.setTextAlignment(TEXT_ALIGN_LEFT);
    }

    display.display();

    if (encoderState == 1)
    {
      Menu = Servotester_Select;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = Servotester_Menu;
    }
    break;

    // Auto Mode Selection *********************************************************
  case AutoMode_Select:
    servoModes(); // Refresh servo operation mode
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, automaticModeString[LANGUAGE]);
    display.drawString(64, 45, oscillateServoString[LANGUAGE]);
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = AutoMode_Menu;
    }
    break;

  // Read Pulse Selection *********************************************************
  case ReadPulse_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, PwmImpulseString[LANGUAGE]);
    display.drawString(64, 45, readCh1Ch5String[LANGUAGE]);
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = ReadPulse_Menu;
    }
    break;

  // Read Multiswitch Selection *********************************************************
  case ReadMultiswitch_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, "PPM Multiswitch");
    display.drawString(64, 45, readCh5String[LANGUAGE]);
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = ReadMultiswitch_Menu;
    }
    break;

  // Read SBUS Selection *********************************************************
  case ReadSbus_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, readSbusString[LANGUAGE]);
    display.drawString(64, 45, "CH5");
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = ReadSbus_Menu;
    }
    break;

  // Read IBUS Selection *********************************************************
  case ReadIbus_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, readIbusString[LANGUAGE]);
    display.drawString(64, 45, "CH5");
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = ReadIbus_Menu;
    }
    break;

  // Info Selection *********************************************************
  case Info_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, "Info");
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = Info_Menu;
    }
    break;

  // Joystick Selection *********************************************************
  case Joystick_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, "Joystick");
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = Joystick_Menu;
    }
    break;

    // Oscilloscope Selection *********************************************************
  case Oscilloscope_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, readOscilloscopeString[LANGUAGE]);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 45, readOscilloscopeString2[LANGUAGE]);
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = Oscilloscope_Menu;
    }
    break;

    // Signal Generator Selection *********************************************************
  case SignalGenerator_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu >");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, signalGeneratorString[LANGUAGE]);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 45, signalGeneratorString2[LANGUAGE]);
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu++;
    }

    if (buttonState == 2)
    {
      Menu = SignalGenerator_Menu;
    }
    break;

  // Settings Selection (last item in the list) *********************************************************
  case Settings_Select:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_24);
    display.drawString(64, 0, "< Menu  ");
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, settingsString[LANGUAGE]);
    drawWiFi();
    display.display();

    if (encoderState == 1)
    {
      Menu--;
    }
    if (encoderState == 2)
    {
      Menu = Settings_Select;
    }

    if (buttonState == 2)
    {
      Menu = Settings_Menu;
      SettingsItem = 7; // Pre select Servo frequency setting
    }
    break;

  // Untermenus ================================================================

  // Servotester *********************************************************
  case Servotester_Menu:
    servoModes(); // Refresh servo operation mode for the currently selected channel

    // The OLED flush over I2C costs ~20ms+ on its own; redrawing it unconditionally on every single
    // loop() iteration capped the whole loop (and with it, how often incoming web requests could be
    // read byte-by-byte) at that same low rate. Throttling it here - same pattern as the pulse-read
    // screen elsewhere - frees up the loop to service the web interface immediately instead of only
    // between display flushes, which is what made dragging the web slider feel laggy/jerky.
    static unsigned long servoMenuMillis;
    if (millis() - servoMenuMillis > 50) // Every 50ms (~20fps, still smooth to the eye)
    {
      servoMenuMillis = millis();
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 10, "Hz");
      display.drawString(0, 20, String(SERVO_Hz));
      display.drawString(0, 35, servoMode);
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(128, 10, "°");
      display.drawString(128, 20, String(us2degree(servo_pos[selectedServo])));
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_24);
      display.drawString(64, 0, "Servo" + String(selectedServo + 1));
      display.drawString(64, 25, String(servo_pos[selectedServo]) + "µs");
      display.drawProgressBar(8, 50, 112, 10, (SERVO_MAX != SERVO_MIN ? (((servo_pos[selectedServo] - SERVO_MIN) * 100) / (SERVO_MAX - SERVO_MIN)) : 50));
      display.display();
    }
    if (!SetupMenu)
    {
      for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
        servo_pos[ch] = servoCenterForChannel(ch); // Each channel centers on its own calibrated value
      setupMcpwm();
      SetupMenu = true;
    }
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, servo_pos[0]);
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, servo_pos[1]);
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_A, servo_pos[2]);
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_B, servo_pos[3]);
    mcpwm_set_duty_in_us(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, servo_pos[4]);

    if (encoderState == 1) // Left turn
    {
      servo_pos[selectedServo] = servo_pos[selectedServo] - encoderSpeed; // 1us per tick, faster turns cover more ground
    }
    if (encoderState == 2) // Right turn
    {
      servo_pos[selectedServo] = servo_pos[selectedServo] + encoderSpeed;
    }

    if (servo_pos[selectedServo] > SERVO_MAX) // Servo MAX
    {
      servo_pos[selectedServo] = SERVO_MAX;
    }
    else if (servo_pos[selectedServo] < SERVO_MIN) // Servo MIN
    {
      servo_pos[selectedServo] = SERVO_MIN;
    }

    if (buttonState == 1)
    {
      Menu = Servotester_Select;
      SetupMenu = false;
      selectedServo = 0;
    }

    if (buttonState == 2)
    {
      servo_pos[selectedServo] = SERVO_CENTER; // Servo Mitte
    }

    if (buttonState == 3)
    {
      selectedServo++; // Servo +
    }

    if (selectedServo > 4)
    {
      selectedServo = 0;
    }
    break;

  // Automatik Modus *********************************************************
  case AutoMode_Menu:
    servoModes(); // Refresh servo operation mode for the currently selected channel
    static unsigned long autoMenuMillis;
    int autoChange;
    if (millis() - autoMenuMillis > 20)
    { // Every 20ms (slow screen refresh down, servo movement is too slow otherwise!)
      autoMenuMillis = millis();
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 0, delayString[LANGUAGE]);
      display.drawString(0, 10, String(TimeAuto));
      display.drawString(0, 20, "ms");
      display.drawString(0, 35, servoMode);
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(128, 10, "°");
      display.drawString(128, 20, String(us2degree(servo_pos[selectedServo])));
      display.drawString(128, 35, String(SERVO_Hz));
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_24);
      display.drawString(64, 0, "Servo" + String(selectedServo + 1));
      display.drawString(64, 25, String(servo_pos[selectedServo]) + "µs");
      if (Auto_Pause)
      {
        display.setFont(ArialMT_Plain_16);
        display.drawString(64, 48, "Pause");
      }
      else
      {
        display.drawProgressBar(8, 50, 112, 10, (SERVO_MAX != SERVO_MIN ? (((servo_pos[selectedServo] - SERVO_MIN) * 100) / (SERVO_MAX - SERVO_MIN)) : 50));
      }
      display.display();
    }

    if (!SetupMenu)
    {
      for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
        servo_pos[ch] = servoCenterForChannel(ch); // Each channel centers on its own calibrated value
      setupMcpwm();
      TimeAuto = 50;      // Zeit für SERVO Steps +-
      Auto_Pause = false; // Pause aus
      SetupMenu = true;
    }

    currentTimeAuto = millis();
    if (!Auto_Pause)
    {
      if ((currentTimeAuto - previousTimeAuto) > TimeAuto)
      {
        previousTimeAuto = currentTimeAuto;
        if (Autopos[selectedServo] > ((SERVO_MIN + SERVO_MAX) / 2))
        {
          servo_pos[selectedServo] = servo_pos[selectedServo] + SERVO_STEPS;
        }
        else
        {
          servo_pos[selectedServo] = servo_pos[selectedServo] - SERVO_STEPS;
        }
        mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, servo_pos[0]);
        mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, servo_pos[1]);
        mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_A, servo_pos[2]);
        mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_B, servo_pos[3]);
        mcpwm_set_duty_in_us(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, servo_pos[4]);
      }
    }

    if (servo_pos[selectedServo] < SERVO_MIN)
    {
      Autopos[selectedServo] = SERVO_MAX;
      servo_pos[selectedServo] = SERVO_MIN;
    }
    if (servo_pos[selectedServo] > SERVO_MAX)
    {
      Autopos[selectedServo] = SERVO_MIN;
      servo_pos[selectedServo] = SERVO_MAX;
    }
    autoChange = map(TimeAuto, 0, 100, 1, 10); // Calculate adjustment step
    if (encoderState == 1)
    {
      if (Auto_Pause)
      {
        servo_pos[selectedServo] = servo_pos[selectedServo] - SERVO_STEPS;
      }
      else
      {
        TimeAuto -= autoChange;
      }
    }
    if (encoderState == 2)
    {
      if (Auto_Pause)
      {
        servo_pos[selectedServo] = servo_pos[selectedServo] + SERVO_STEPS;
      }
      else
      {
        TimeAuto += autoChange;
      }
    }

    if (TimeAuto > 100) // TimeAuto MAX
    {
      TimeAuto = 100;
    }
    else if (TimeAuto < 0) // TimeAuto MIN
    {
      TimeAuto = 0;
    }

    if (selectedServo > 4)
    {
      selectedServo = 4;
    }
    else if (selectedServo < 0)
    {
      selectedServo = 0;
    }

    if (buttonState == 1) // Long press = back
    {
      Menu = AutoMode_Select;
      SetupMenu = false;
      selectedServo = 0;
    }

    if (buttonState == 2) // Short press = pause
    {
      Auto_Pause = !Auto_Pause;
    }

    if (buttonState == 3) // Doubleclick = Change channel
    {
      selectedServo++; // Servo +
    }

    if (selectedServo > 4)
    {
      selectedServo = 0;
    }
    break;

  // PWM Impuls lesen *********************************************************
  case ReadPulse_Menu:

    bool parameterSet; // See servoModes.h

    if (!SetupMenu)
    {
      pinMode(servopin[0], INPUT);
      pinMode(servopin[1], INPUT);
      pinMode(servopin[2], INPUT);
      pinMode(servopin[3], INPUT);
      pinMode(servopin[4], INPUT);
      SetupMenu = true;
    }

    servo_pos[selectedServo] = pulseIn(servopin[selectedServo], HIGH, 50000); // Read PWM signal
    pwmFreq = readFreq(servopin[selectedServo], HIGH, 50000);                 // Read PWM frequency

    // Switch progress bar range - just a rough display heuristic, not tied to the actual selected
    // mode, so reference the Std (normal-range) and SSR (short-range) calibrations as stand-ins.
    if (servo_pos[selectedServo] > 750) // Normal pulsewidth range
    {
      PulseMin = SERVO_MIN_BY_MODE[selectedServo][STD];
      PulseMax = SERVO_MAX_BY_MODE[selectedServo][STD];
    }
    else // Sanwa pulsewidth range
    {
      PulseMin = SERVO_MIN_BY_MODE[selectedServo][SSR];
      PulseMax = SERVO_MAX_BY_MODE[selectedServo][SSR];
    }

    // Enlarge range, if required
    if (servo_pos[selectedServo] > PulseMax)
      PulseMax = servo_pos[selectedServo];
    if (servo_pos[selectedServo] < PulseMin)
      PulseMin = servo_pos[selectedServo];

    static unsigned long pwmMenuMillis;
    if (millis() - pwmMenuMillis > 100)
    { // Every 100ms (slow screen refresh down, display is too nervous otherwise!)
      pwmMenuMillis = millis();
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_24);
      display.drawString(64, 0, impulseString[LANGUAGE] + String(selectedServo + 1));
      display.drawString(64, 25, String(servo_pos[selectedServo]) + "µs");

      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.setFont(ArialMT_Plain_10);

      display.drawString(0, 25, "Hz");
      display.drawString(0, 35, String(pwmFreq));

      if (pwmFreq > 1) // Only show progress bar, if we have a signal
      {
        display.drawProgressBar(8, 50, 112, 10, (PulseMax != PulseMin ? (((servo_pos[selectedServo] - PulseMin) * 100) / (PulseMax - PulseMin)) : 50));
      }
      else
      {
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.setFont(ArialMT_Plain_10);
        display.drawString(64, 50, impulseSignalString[LANGUAGE]);
      }

      display.display();
    }

    if (encoderState == 1)
    {
      selectedServo--;
    }
    if (encoderState == 2)
    {
      selectedServo++;
    }

    if (selectedServo > 4)
    {
      selectedServo = 4;
    }
    else if (selectedServo < 0)
    {
      selectedServo = 0;
    }

    if (buttonState == 1)
    {
      Menu = ReadPulse_Select;
      SetupMenu = false;
      selectedServo = 0;
    }
    break;

  // Multiswitch lesen *********************************************************
  https: // www.modelltruck.net/showthread.php?54795-Futaba-Robbe-Multiswitch-Decoder-mit-Arduino
  case ReadMultiswitch_Menu:
    static unsigned long multiswitchMenuMillis;
    if (millis() - multiswitchMenuMillis > 20)
    { // Every 20ms (slow screen refresh down)
      multiswitchMenuMillis = millis();
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(20, 0, String(value1[8]));
      display.drawString(64, 0, String(value1[0]) + "  " + String(value1[1]));
      display.drawString(64, 15, String(value1[2]) + "  " + String(value1[3]));
      display.drawString(64, 30, String(value1[4]) + "  " + String(value1[5]));
      display.drawString(64, 45, String(value1[6]) + "  " + String(value1[7]));
      display.drawString(5, 30, "Multiswitch");
      display.display();
    }

    if (!SetupMenu)
    {
      pinMode(servopin[4], INPUT);
      SetupMenu = true;
    }

    value1[8] = 1500;

    while (value1[8] > 1000) // Wait for the beginning of the frame (1000)
    {
      value1[8] = pulseIn(servopin[4], HIGH, 50000);
    }

    for (int x = 0; x <= kanaele - 1; x++) // Loop to store all the channel positions
    {
      value1[x] = pulseIn(servopin[4], HIGH, 50000);
    }

    if (buttonState == 1)
    {
      Menu = ReadMultiswitch_Select;
      SetupMenu = false;
    }

    break;

  // SBUS lesen *********************************************************
  case ReadSbus_Menu:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.setFont(ArialMT_Plain_10);

    if (!SetupMenu)
    {
      pinMode(servopin[0], INPUT);
      pinMode(servopin[1], INPUT);
      pinMode(servopin[2], INPUT);
      pinMode(servopin[3], INPUT);
      pinMode(servopin[4], INPUT);
      sBus.begin(COMMAND_RX, COMMAND_TX, SBUS_INVERTED); // begin SBUS communication with compatible receivers
      SetupMenu = true;
    }

    sBus.read();
    SBUSchannels = sBus.ch();

    display.drawString(32, 0, String(map(SBUSchannels[0], 172, 1811, 1000, 2000)));
    display.drawString(64, 0, String(map(SBUSchannels[1], 172, 1811, 1000, 2000)));
    display.drawString(96, 0, String(map(SBUSchannels[2], 172, 1811, 1000, 2000)));
    display.drawString(128, 0, String(map(SBUSchannels[3], 172, 1811, 1000, 2000)));

    display.drawString(32, 15, String(map(SBUSchannels[4], 172, 1811, 1000, 2000)));
    display.drawString(64, 15, String(map(SBUSchannels[5], 172, 1811, 1000, 2000)));
    display.drawString(96, 15, String(map(SBUSchannels[6], 172, 1811, 1000, 2000)));
    display.drawString(128, 15, String(map(SBUSchannels[7], 172, 1811, 1000, 2000)));

    display.drawString(32, 30, String(map(SBUSchannels[8], 172, 1811, 1000, 2000)));
    display.drawString(64, 30, String(map(SBUSchannels[9], 172, 1811, 1000, 2000)));
    display.drawString(96, 30, String(map(SBUSchannels[10], 172, 1811, 1000, 2000)));
    display.drawString(128, 30, String(map(SBUSchannels[11], 172, 1811, 1000, 2000)));

    display.drawString(32, 45, String(map(SBUSchannels[12], 172, 1811, 1000, 2000)));
    display.drawString(64, 45, String(map(SBUSchannels[13], 172, 1811, 1000, 2000)));
    display.drawString(96, 45, String(map(SBUSchannels[14], 172, 1811, 1000, 2000)));
    display.drawString(128, 45, String(map(SBUSchannels[15], 172, 1811, 1000, 2000)));
    display.display();

    if (buttonState == 1)
    {
      Menu = ReadSbus_Select;
      SetupMenu = false;
    }
    break;

  // IBUS lesen *********************************************************
  case ReadIbus_Menu:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(32, 0, String(IBus.readChannel(0)));
    display.drawString(64, 0, String(IBus.readChannel(1)));
    display.drawString(96, 0, String(IBus.readChannel(2)));
    display.drawString(128, 0, String(IBus.readChannel(3)));

    display.drawString(32, 15, String(IBus.readChannel(4)));
    display.drawString(64, 15, String(IBus.readChannel(5)));
    display.drawString(96, 15, String(IBus.readChannel(6)));
    display.drawString(128, 15, String(IBus.readChannel(7)));

    display.drawString(32, 30, String(IBus.readChannel(8)));
    display.drawString(64, 30, String(IBus.readChannel(9)));
    display.drawString(96, 30, String(IBus.readChannel(10)));
    display.drawString(128, 30, String(IBus.readChannel(11)));

    display.drawString(32, 45, String(IBus.readChannel(12)));
    display.drawString(64, 45, String(IBus.readChannel(13)));
    // display.drawString( 96, 45,  String(IBus.readChannel(14)));
    // display.drawString(128, 45,  String(IBus.readChannel(15)));
    display.display();

    if (!SetupMenu)
    {
      pinMode(servopin[0], INPUT);
      pinMode(servopin[1], INPUT);
      pinMode(servopin[2], INPUT);
      pinMode(servopin[3], INPUT);
      pinMode(servopin[4], INPUT);
      IBus.begin(Serial2, IBUSBM_NOTIMER, COMMAND_RX, COMMAND_TX); // iBUS object connected to serial2 RX2 pin and use timer 1
      SetupMenu = true;
    }

    IBus.loop(); // call internal loop function to update the communication to the receiver

    if (buttonState == 1)
    {
      Menu = ReadIbus_Select;
      SetupMenu = false;
    }
    break;

  // Info - 3 pages, left/right to page through: Wifi, Controls, Firmware *********************
  case Info_Menu:
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);

    switch (InfoPage)
    {
    case 0: // Wifi
      if (WIFI_ON == 1)
      {
        if (WIFI_MODE == WIFI_STATION_MODE && !wifiStaFallback)
        {
          display.drawString(64, 0, "Wifi: Station");
          display.drawString(64, 14, "SSID: " + STA_SSID);
          display.drawString(64, 28, ipAddressString[LANGUAGE] + " " + wifiIpString);
          display.drawString(64, 42, "servotester.local");
        }
        else
        {
          display.drawString(64, 0, wifiStaFallback ? "Wifi: AP (fallback)" : ("Wifi: " + onString[LANGUAGE]));
          display.drawString(64, 14, "SSID: " + String(ssid));
          display.drawString(64, 28, passwordString[LANGUAGE] + " " + String(password));
          display.drawString(64, 42, ipAddressString[LANGUAGE] + " " + wifiIpString);
        }
      }
      else
      {
        display.setFont(ArialMT_Plain_16);
        display.drawString(64, 25, "Wifi");
        display.drawString(64, 45, offString[LANGUAGE]);
      }
      break;
    case 1: // Controls - same content as the boot help screen
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 0, operationString[LANGUAGE]);
      display.drawString(0, 12, shortPressString[LANGUAGE]);
      display.drawString(0, 24, longPressString[LANGUAGE]);
      display.drawString(0, 36, doubleclickString[LANGUAGE]);
      display.drawString(0, 48, RotateKnobString[LANGUAGE]);
      break;
    case 2: // Firmware
      display.drawString(64, 0, "Firmware source:");
      display.drawString(64, 12, "github.com/");
      display.drawString(64, 22, "MichielBruijn/");
      display.drawString(64, 32, "esp32-servo-tester");
      display.drawString(64, 48, "v" + String(codeVersion));
      break;
    }
    display.display();

    if (encoderState == 1)
    {
      InfoPage--;
    }
    if (encoderState == 2)
    {
      InfoPage++;
    }
    InfoPage = constrain(InfoPage, 0, 2);

    if (buttonState == 1)
    {
      Menu = Info_Select;
      InfoPage = 0;
    }
    break;

  // Joystick *********************************************************
  case Joystick_Menu:
  {
    // Self-widening calibration: a joystick pot rarely actually swings its output all the way from
    // 0 to JOYSTICK_ADC_MAX (mechanical end-stop before the electrical rail, plus the ESP32 ADC's
    // own non-linearity near 0V/3.3V), so mapping against the theoretical full range under-uses the
    // stick's real travel and makes the usable middle feel oversensitive. joystickXRawMin/Max/etc.
    // (global, initialized in setup()) remember the widest raw values actually seen on each side of
    // center and use that instead - accurate after the first full deflection each way. Global so the
    // web interface's Joystick page can show them too, for diagnosis.
    static unsigned long joystickMenuMillis;
    if (millis() - joystickMenuMillis > 50) // Same refresh rate as Servotester_Menu
    {
      joystickMenuMillis = millis();
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_10);
      display.drawString(64, 0, "Joystick");
      display.drawString(64, 12, "X->CH" + String(JOYSTICK_X_CHANNEL + 1) + ": " + String(servo_pos[JOYSTICK_X_CHANNEL]) + "us");
      display.drawString(64, 22, "Y->CH" + String(JOYSTICK_Y_CHANNEL + 1) + ": " + String(servo_pos[JOYSTICK_Y_CHANNEL]) + "us");
      display.drawString(64, 36, "Xraw " + String(joystickXRawMin) + "-" + String(joystickXRawMax));
      display.drawString(64, 48, "Yraw " + String(joystickYRawMin) + "-" + String(joystickYRawMax));
      display.display();
    }

    if (!SetupMenu)
    {
      setupMcpwm();
      pinMode(JOYSTICK_BUTTON_PIN, INPUT_PULLUP);
      SetupMenu = true;
    }

    // Mode is per timer group now, so the X and Y channels may each be in a different mode family
    int xMode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_X_CHANNEL)];
    int yMode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_Y_CHANNEL)];
    int xMin = SERVO_MIN_BY_MODE[JOYSTICK_X_CHANNEL][xMode];
    int xMax = SERVO_MAX_BY_MODE[JOYSTICK_X_CHANNEL][xMode];
    int yMin = SERVO_MIN_BY_MODE[JOYSTICK_Y_CHANNEL][yMode];
    int yMax = SERVO_MAX_BY_MODE[JOYSTICK_Y_CHANNEL][yMode];

    // ESP32's ADC has a well-known channel "memory effect": switching to a new ADC1 channel right
    // after reading a different one can carry over some residual charge from the previous channel's
    // sample-and-hold capacitor, biasing the new reading toward it - worse with a high-impedance
    // source like a joystick's potentiometer. Reading each channel twice and keeping only the second
    // (settled) sample avoids this; this is what made moving one axis appear to nudge the other.
    analogRead(JOYSTICK_X_PIN); // Throwaway, lets the S&H capacitor settle after the previous Y read
    int rawX = analogRead(JOYSTICK_X_PIN);
    analogRead(JOYSTICK_Y_PIN); // Throwaway, lets the S&H capacitor settle after the X read above
    int rawY = analogRead(JOYSTICK_Y_PIN);

    joystickXRawMin = min(joystickXRawMin, rawX);
    joystickXRawMax = max(joystickXRawMax, rawX);
    joystickYRawMin = min(joystickYRawMin, rawY);
    joystickYRawMax = max(joystickYRawMax, rawY);

    int xCenterServo = servoCenterForChannel(JOYSTICK_X_CHANNEL);
    int yCenterServo = servoCenterForChannel(JOYSTICK_Y_CHANNEL);

    // Deadzone snaps to the calibrated Center, so mechanical/ADC noise at rest doesn't twitch the servo
    if (abs(rawX - JOYSTICK_ADC_CENTER) < JOYSTICK_DEADZONE)
      servo_pos[JOYSTICK_X_CHANNEL] = xCenterServo;
    else if (rawX < JOYSTICK_ADC_CENTER)
      servo_pos[JOYSTICK_X_CHANNEL] = map(rawX, joystickXRawMin, JOYSTICK_ADC_CENTER, xMin, xCenterServo);
    else
      servo_pos[JOYSTICK_X_CHANNEL] = map(rawX, JOYSTICK_ADC_CENTER, joystickXRawMax, xCenterServo, xMax);

    if (abs(rawY - JOYSTICK_ADC_CENTER) < JOYSTICK_DEADZONE)
      servo_pos[JOYSTICK_Y_CHANNEL] = yCenterServo;
    else if (rawY < JOYSTICK_ADC_CENTER)
      servo_pos[JOYSTICK_Y_CHANNEL] = map(rawY, joystickYRawMin, JOYSTICK_ADC_CENTER, yMin, yCenterServo);
    else
      servo_pos[JOYSTICK_Y_CHANNEL] = map(rawY, JOYSTICK_ADC_CENTER, joystickYRawMax, yCenterServo, yMax);

    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, servo_pos[0]);
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, servo_pos[1]);
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_A, servo_pos[2]);
    mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_B, servo_pos[3]);
    mcpwm_set_duty_in_us(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, servo_pos[4]);

    // Joystick click button is now handled globally in ButtonRead() (always changes channel,
    // same as the BOOT button) - no menu-specific handling here anymore.

    if (buttonState == 1)
    {
      Menu = Joystick_Select;
      SetupMenu = false;
    }
    break;
  }

    // Oscilloscope *********************************************************
  case Oscilloscope_Menu:

    if (!SetupMenu) // This stuff is only executed once
    {
      pinMode(OSCILLOSCOPE_PIN, INPUT);
      oscilloscopeLoop(true); // Init oscilloscope
      SetupMenu = true;
    }
    else
    {
      oscilloscopeLoop(false); // Loop oscilloscope code
    }

    if (buttonState == 1) // Back
    {
      Menu = Oscilloscope_Select;
      SetupMenu = false;
    }
    break;

    // Signal Generator *********************************************************
  case SignalGenerator_Menu:

    if (!SetupMenu) // This stuff is only executed once
    {
      pinMode(SIGNAL_GENERATOR_PIN, OUTPUT);
      signalGeneratorLoop(true); // Init signal generator
      SetupMenu = true;
    }
    else
    {
      signalGeneratorLoop(false); // Loop signal generator
    }

    if (buttonState == 1) // Back
    {
      Menu = SignalGenerator_Select;
      SetupMenu = false;
    }
    break;

  // SettingsItem *********************************************************
  case Settings_Menu:
    batteryVolts(); // Read battery voltage
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 0, settingsString[LANGUAGE]);
    display.setFont(ArialMT_Plain_16);
    switch (SettingsItem)
    {
    case 0:
      display.drawString(64, 25, "Wifi");
      if (WIFI_ON == 1)
      {
        display.drawString(64, 45, onString[LANGUAGE]);
      }
      else
      {
        display.drawString(64, 45, offString[LANGUAGE]);
      }
      break;
    case 1:
      display.drawString(64, 25, factoryResetString[LANGUAGE]);
      if (RESET_EEPROM == 1)
      {
        display.drawString(64, 45, yesString[LANGUAGE]);
      }
      else
      {
        display.drawString(64, 45, noString[LANGUAGE]);
      }
      break;
    case 2:
      display.drawString(64, 25, channelString[LANGUAGE]);
      display.drawString(64, 45, String(selectedServo + 1));
      break;
    case 3:
      display.drawString(64, 25, servoMaxString[LANGUAGE]);
      display.drawString(64, 45, String(SERVO_MAX));
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 50, "CH" + String(selectedServo + 1));
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(128, 50, servoMode);
      break;
    case 4:
      display.drawString(64, 25, servoMinString[LANGUAGE]);
      display.drawString(64, 45, String(SERVO_MIN));
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 50, "CH" + String(selectedServo + 1));
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(128, 50, servoMode);
      break;
    case 5:
      display.drawString(64, 25, servoCenterString[LANGUAGE]);
      display.drawString(64, 45, String(SERVO_CENTER));
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 50, "CH" + String(selectedServo + 1));
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(128, 50, servoMode);
      break;
    case 6:
      display.drawString(64, 25, servoAngleString[LANGUAGE]);
      display.drawString(64, 45, String(SERVO_DEGREES[selectedServo]) + (char)176);
      display.setFont(ArialMT_Plain_10);
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawString(0, 50, "CH" + String(selectedServo + 1));
      break;
    case 7:
      display.drawString(64, 25, servoHzString[LANGUAGE]);
      display.drawString(64, 45, String(SERVO_Hz));
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.setFont(ArialMT_Plain_10);
      display.drawString(0, 27, "µs");
      display.drawString(0, 37, String(SERVO_MIN));
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(128, 27, "µs");
      display.drawString(128, 37, String(SERVO_MAX));
      display.drawString(128, 50, servoMode);
      break;
    case 8:
      display.setFont(ArialMT_Plain_10);
      display.drawString(64, 20, PowerScaleString[LANGUAGE]);
      display.drawString(64, 31, String(POWER_SCALE));
      display.setFont(ArialMT_Plain_16);
      display.drawString(64, 45, String(batteryVoltage, 2) + "V");
      break;
    case 9:
      display.drawString(64, 25, "SBUS");
      if (SBUS_INVERTED == 1)
      {
        display.drawString(64, 45, standardString[LANGUAGE]);
      }
      else
      {
        display.drawString(64, 45, inversedString[LANGUAGE]);
      }
      break;
    case 10:
      display.drawString(64, 25, encoderDirectionString[LANGUAGE]);
      if (ENCODER_INVERTED == 0)
      {
        display.drawString(64, 45, standardString[LANGUAGE]);
      }
      else
      {
        display.drawString(64, 45, inversedString[LANGUAGE]);
      }
      break;
    case 11:
      display.drawString(64, 25, speedCurveString[LANGUAGE]);
      display.drawString(64, 45, String(SPEED_CURVE / 10.0, 1));
      break;
    case 12:
      display.drawString(64, 25, "Wifi Mode");
      if (WIFI_MODE == WIFI_STATION_MODE)
      {
        display.drawString(64, 45, "Station");
      }
      else
      {
        display.drawString(64, 45, "Access Point");
      }
      break;
    case 13:
      display.drawString(64, 25, "Joystick X");
      display.drawString(64, 45, "CH" + String(JOYSTICK_X_CHANNEL + 1));
      break;
    case 14:
      display.drawString(64, 25, "Joystick Y");
      display.drawString(64, 45, "CH" + String(JOYSTICK_Y_CHANNEL + 1));
      break;
    }
    if (Edit)
    {
      display.drawString(10, 50, "->");
    }
    display.display();

    if (!SetupMenu)
    {

      SetupMenu = true;
    }

    if (encoderState == 1) // Encoder left turn -------
    {
      if (!Edit)
      {
        SettingsItem--;
      }
      else
      {
        switch (SettingsItem)
        {
        case 0:
          WIFI_ON--;
          WiFiChanged = true;
          break;
        case 1:
          RESET_EEPROM--;
          break;
        case 2:
          selectedServo--; // Pick which servo channel Max/Min/Center below apply to
          break;
        case 3:
          SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE] -= encoderSpeed;
          break;
        case 4:
          SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE] -= encoderSpeed;
          break;
        case 5:
          SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE] -= encoderSpeed;
          break;
        case 6:
          SERVO_DEGREES[selectedServo] -= encoderSpeed;
          break;
        case 7:
          SERVO_MODE--;
          break;
        case 8:
          POWER_SCALE--;
          break;
        case 9:
          SBUS_INVERTED--;
          break;
        case 10:
          ENCODER_INVERTED--;
          break;
        case 11:
          SPEED_CURVE--;
          break;
        case 12:
          WIFI_MODE--;
          WiFiChanged = true;
          break;
        case 13:
          JOYSTICK_X_CHANNEL--;
          break;
        case 14:
          JOYSTICK_Y_CHANNEL--;
          break;
        }
      }
    }
    if (encoderState == 2) // Encoder right turn ------
    {
      if (!Edit)
      {
        SettingsItem++;
      }
      else
      {
        switch (SettingsItem)
        {
        case 0:
          WIFI_ON++;
          WiFiChanged = true;
          break;
        case 1:
          RESET_EEPROM++;
          break;
        case 2:
          selectedServo++; // Pick which servo channel Max/Min/Center below apply to
          break;
        case 3:
          SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE] += encoderSpeed;
          break;
        case 4:
          SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE] += encoderSpeed;
          break;
        case 5:
          SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE] += encoderSpeed;
          break;
        case 6:
          SERVO_DEGREES[selectedServo] += encoderSpeed;
          break;
        case 7:
          SERVO_MODE++;
          break;
        case 8:
          POWER_SCALE++;
          break;
        case 9:
          SBUS_INVERTED++;
          break;
        case 10:
          ENCODER_INVERTED++;
          break;
        case 11:
          SPEED_CURVE++;
          break;
        case 12:
          WIFI_MODE++;
          WiFiChanged = true;
          break;
        case 13:
          JOYSTICK_X_CHANNEL++;
          break;
        case 14:
          JOYSTICK_Y_CHANNEL++;
          break;
        }
      }
    }

    // Menu range -------------------------------------
    if (SettingsItem > 14)
    {
      SettingsItem = 0;
    }
    else if (SettingsItem < 0)
    {
      SettingsItem = 14;
    }

    // Limits -----------------------------------------
    if (selectedServo < 0)
    { // Servo channel nicht unter 0
      selectedServo = 0;
    }

    if (selectedServo > 4)
    { // Servo channel nicht über 4
      selectedServo = 4;
    }

    SPEED_CURVE = constrain(SPEED_CURVE, 10, 40); // Exponent x10: 1.0 (linear) to 4.0 (very aggressive)
    WIFI_MODE = constrain(WIFI_MODE, WIFI_AP_MODE, WIFI_STATION_MODE);
    JOYSTICK_X_CHANNEL = constrain(JOYSTICK_X_CHANNEL, 0, NUM_SERVO_CHANNELS - 1);
    JOYSTICK_Y_CHANNEL = constrain(JOYSTICK_Y_CHANNEL, 0, NUM_SERVO_CHANNELS - 1);

    if (LANGUAGE < 0)
    { // Language nicht unter 0
      LANGUAGE = 0;
    }

    if (LANGUAGE > noOfLanguages)
    { // Language nicht über 1
      LANGUAGE = noOfLanguages;
    }

    if (ENCODER_INVERTED < 0)
    { // Encoder inverted nicht unter 0
      ENCODER_INVERTED = 0;
    }

    if (ENCODER_INVERTED > 1)
    { // Encoder inverted nicht über 1
      ENCODER_INVERTED = 1;
    }

    if (SBUS_INVERTED < 0)
    { // SBUS inverted nicht unter 0
      SBUS_INVERTED = 0;
    }

    if (SBUS_INVERTED > 1)
    { // SBUS inverted nicht über 1
      SBUS_INVERTED = 1;
    }

    if (WIFI_ON < 0)
    { // Wifi off nicht unter 0
      WIFI_ON = 0;
    }

    if (WIFI_ON > 1)
    { // Wifi on nicht über 1
      WIFI_ON = 1;
    }

    if (RESET_EEPROM < 0)
    { // Reset eeprom nicht unter 0
      RESET_EEPROM = 0;
    }

    if (RESET_EEPROM > 1)
    { // Reset eeprom nicht über 1
      RESET_EEPROM = 1;
    }

    // Full 200-3000µs range allowed: no built-in safety margin, some servos may hit their mechanical end stop.
    // Clamp only the currently active mode - the others keep whatever they were last clamped to.
    if (SERVO_MODE == STD || SERVO_MODE == NOR || SERVO_MODE == SHR)
    {
      SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE] = constrain(SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE], 200, 3000);
      SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE] = constrain(SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE], 200, 3000);
      SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE] = constrain(SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE], 200, 3000);
    }
    else
    {
      SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE] = constrain(SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE], 100, 200);
      SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE] = constrain(SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE], 250, 350);
      SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE] = constrain(SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE], 400, 500);
    }

    SERVO_DEGREES[selectedServo] = constrain(SERVO_DEGREES[selectedServo], 10, 360);

    servoModes(); // Refresh servo operation mode

    // Buttons ---------------------------------------
    if (buttonState == 1)
    {
      Menu = Settings_Select;
      SetupMenu = false;
    }

    if (buttonState == 2)
    {
      if (Edit)
      {
        Edit = false;
        if (RESET_EEPROM)
        {
          eepromInit(); // Restore defaults
        }
        else
        {
          eepromWrite(); // Safe changes
        }
        if (WiFiChanged)
        {
          WiFiChanged = false;
          wifiSetup();
        }
      }
      else
      {
        Edit = true;
      }
    }

    if (buttonState == 3) // Double click: always jump to the next servo channel, regardless of which item is selected
    {
      selectedServo++;
      if (selectedServo > 4)
      {
        selectedServo = 0;
      }
    }
    break;

  default:
    // Tue etwas, im Defaultfall
    // Dieser Fall ist optional
    Menu = Servotester_Select;
    break; // Wird nicht benötigt, wenn Statement(s) vorhanden sind
  }
}

// =======================================================================================================
// BATTERY MONITOR
// =======================================================================================================
//
void batteryVolts()
{

  currentTimeSpan = millis();
  if ((currentTimeSpan - previousTimeSpan) > 2000)
  {
    previousTimeSpan = currentTimeSpan;

    batteryVoltage = battery.readVoltage() * 0.825; // We want the same POWER_SCALE as before with analogRead()

    float scale_value = POWER_SCALE / 100.0;
    batteryVoltage = batteryVoltage * scale_value;

    Serial.print(eepromVoltageString[LANGUAGE]);
    Serial.println(batteryVoltage, 3);

    batteryDetected = 1;

    if (batteryVoltage > 21.25) // 6s Lipo
    {
      numberOfBatteryCells = 6;
    }
    else if (batteryVoltage > 17.0) // 5s Lipo
    {
      numberOfBatteryCells = 5;
    }
    else if (batteryVoltage > 12.75) // 4s Lipo
    {
      numberOfBatteryCells = 4;
    }
    else if (batteryVoltage > 8.5) // 3s Lipo
    {
      numberOfBatteryCells = 3;
    }
    else if (batteryVoltage > 6.0) // 2s Lipo
    {
      numberOfBatteryCells = 2;
    }
    else // 1s Lipo kein Akku angeschlossen
    {
      numberOfBatteryCells = 1;
      batteryDetected = 0;
    }

    batteryChargePercentage = (batteryVoltage / numberOfBatteryCells); // Prozentanzeige
    batteryChargePercentage = map_float(batteryChargePercentage, 3.5, 4.2, 0, 100);
    if (batteryChargePercentage < 0)
    {
      batteryChargePercentage = 0;
    }
    if (batteryChargePercentage > 100)
    {
      batteryChargePercentage = 100;
    }
  }
}

//
// =======================================================================================================
// EEPROM
// =======================================================================================================
//

// Init new board with the default values you want ------
void eepromInit()
{
  bool anyChannelInvalid = false;
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    Serial.println(SERVO_MIN_BY_MODE[ch][STD]);
    if (SERVO_MIN_BY_MODE[ch][STD] < 50)
      anyChannelInvalid = true;
  }

  if (anyChannelInvalid || RESET_EEPROM) // Automatic (any channel still uninitialized) or manual reset
  {
    RESET_EEPROM = 0;

    // Restore defaults
    WIFI_ON = 1; // Wifi on
    WIFI_MODE = WIFI_AP_MODE; // Factory reset always drops back to the always-reachable Access Point
    STA_SSID = "";
    STA_PASSWORD = ""; // Factory reset must not leave a saved home WiFi password behind
    JOYSTICK_X_CHANNEL = 0; // CH1
    JOYSTICK_Y_CHANNEL = 1; // CH2
    // SERVO_STEPS = 10;
    // SERVO_MAX = 2000;
    // SERVO_MIN = 1000;
    // SERVO_CENTER = 1500;
    // SERVO_Hz = 50;
    POWER_SCALE = 948;
    SBUS_INVERTED = 1; // 1 = Standard signal!
    ENCODER_INVERTED = 0;
    LANGUAGE = 0;
    SPEED_CURVE = 19;
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
    Serial.println(eepromInitString[LANGUAGE]);
    servoModes(); // servoModes() needs to be executed in order to actualize the values TODO
    eepromWrite();
  }
}

// Write new values to EEPROM ------
void eepromWrite()
{
  EEPROM.writeInt(adr_eprom_WIFI_ON, WIFI_ON);
  EEPROM.writeInt(adr_eprom_WIFI_MODE, WIFI_MODE);
  EEPROM.writeString(adr_eprom_STA_SSID, STA_SSID);
  EEPROM.writeString(adr_eprom_STA_PASSWORD, STA_PASSWORD);
  EEPROM.writeInt(adr_eprom_JOYSTICK_X_CHANNEL, JOYSTICK_X_CHANNEL);
  EEPROM.writeInt(adr_eprom_JOYSTICK_Y_CHANNEL, JOYSTICK_Y_CHANNEL);
  EEPROM.writeInt(adr_eprom_POWER_SCALE, POWER_SCALE);
  EEPROM.writeInt(adr_eprom_SBUS_INVERTED, SBUS_INVERTED);
  EEPROM.writeInt(adr_eprom_ENCODER_INVERTED, ENCODER_INVERTED);
  EEPROM.writeInt(adr_eprom_LANGUAGE, LANGUAGE);
  EEPROM.writeInt(adr_eprom_SPEED_CURVE, SPEED_CURVE);
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
  Serial.println(eepromWrittenString[LANGUAGE]);
}

// Read values from EEPROM ------
void eepromRead()
{
  WIFI_ON = EEPROM.readInt(adr_eprom_WIFI_ON);
  POWER_SCALE = EEPROM.readInt(adr_eprom_POWER_SCALE);
  SBUS_INVERTED = EEPROM.readInt(adr_eprom_SBUS_INVERTED);
  ENCODER_INVERTED = EEPROM.readInt(adr_eprom_ENCODER_INVERTED);
  LANGUAGE = EEPROM.readInt(adr_eprom_LANGUAGE);

  // Freshly appended EEPROM bytes aren't reliably blank/zero, so a value range check alone can't tell
  // "never written" apart from "genuinely holds this value" - a layout version marker can.
  int storedLayoutVersion = EEPROM.readInt(adr_eprom_LAYOUT_VERSION);
  bool layoutJustChanged = (storedLayoutVersion != EEPROM_LAYOUT_VERSION);

  // This address used to hold the removed PONG_BALL_RATE setting, so on the migration boot its old
  // value must be discarded rather than reused as SPEED_CURVE.
  SPEED_CURVE = layoutJustChanged ? 19 : EEPROM.readInt(adr_eprom_SPEED_CURVE);

  // Layout version 7 widened the calibration block earlier in EEPROM (2 shared families -> 6
  // independent per-mode slots), which pushed every field below it - Wifi Station credentials,
  // Joystick channel mapping, per-group servo mode - to a new address. Their own content didn't
  // change, only where they live, so on that specific migration boot they must be read from their
  // OLD address instead of the new one, or they'd silently read as blank/garbage.
  bool addressesShiftedAtV7 = (storedLayoutVersion >= 4 && storedLayoutVersion < 7);

  // WIFI_MODE/STA_SSID/STA_PASSWORD were introduced at layout version 4 (WIFI_MODE reusing the old
  // deprecated SERVO_STEPS address, STA_SSID/PASSWORD newly appended). Only default them on a boot
  // that's upgrading from *before* version 4 - using the generic layoutJustChanged here instead would
  // wipe the saved home WiFi network and password on every future, unrelated layout bump too.
  bool wifiFieldsNeedDefaulting = (storedLayoutVersion < 4);
  WIFI_MODE = wifiFieldsNeedDefaulting ? WIFI_AP_MODE : EEPROM.readInt(adr_eprom_WIFI_MODE); // address 4, never moved

  // Freshly appended fields (never written before this firmware version): start blank rather than
  // risk EEPROM.readString() scanning unwritten flash for a null terminator that isn't there.
  if (wifiFieldsNeedDefaulting)
  {
    STA_SSID = "";
    STA_PASSWORD = "";
  }
  else if (addressesShiftedAtV7)
  {
    STA_SSID = EEPROM.readString(adr_eprom_OLD_STA_SSID);
    STA_PASSWORD = EEPROM.readString(adr_eprom_OLD_STA_PASSWORD);
  }
  else
  {
    STA_SSID = EEPROM.readString(adr_eprom_STA_SSID);
    STA_PASSWORD = EEPROM.readString(adr_eprom_STA_PASSWORD);
  }

  // Same reasoning as above: JOYSTICK_X/Y_CHANNEL were introduced at layout version 5, so only
  // default them when upgrading from before that version, not on every later bump.
  bool joystickFieldsNeedDefaulting = (storedLayoutVersion < 5);
  int joystickXChannelRaw = joystickFieldsNeedDefaulting ? 0 : EEPROM.readInt(addressesShiftedAtV7 ? adr_eprom_OLD_JOYSTICK_X_CHANNEL : adr_eprom_JOYSTICK_X_CHANNEL);
  int joystickYChannelRaw = joystickFieldsNeedDefaulting ? 1 : EEPROM.readInt(addressesShiftedAtV7 ? adr_eprom_OLD_JOYSTICK_Y_CHANNEL : adr_eprom_JOYSTICK_Y_CHANNEL);
  JOYSTICK_X_CHANNEL = constrain(joystickXChannelRaw, 0, NUM_SERVO_CHANNELS - 1);
  JOYSTICK_Y_CHANNEL = constrain(joystickYChannelRaw, 0, NUM_SERVO_CHANNELS - 1);

  // Mode (and Hz) used to be one single value shared by every channel, at the now-unused address 44.
  // Split into one value per timer group at layout version 6 - seed all 3 groups from that old shared
  // value on the upgrade boot, so existing servo behavior doesn't change, instead of resetting to Std.
  bool modeFieldsNeedDefaulting = (storedLayoutVersion < 6);
  int legacySharedMode = constrain(EEPROM.readInt(adr_eprom_SERVO_MODE), (int)STD, (int)SXR);
  for (uint8_t g = 0; g < NUM_SERVO_TIMER_GROUPS; g++)
  {
    if (modeFieldsNeedDefaulting)
      SERVO_MODE_PER_GROUP[g] = legacySharedMode;
    else if (addressesShiftedAtV7)
      SERVO_MODE_PER_GROUP[g] = EEPROM.readInt(adr_eprom_OLD_SERVO_MODE_GROUP(g));
    else
      SERVO_MODE_PER_GROUP[g] = EEPROM.readInt(adr_eprom_SERVO_MODE_GROUP(g));
  }

  // Min/Max/Center used to be 2 shared families per channel (Std/NOR/SHR and SSR/SUR/SXR combined,
  // 24 bytes/channel). Split into 6 independent slots per channel (one per mode) at layout version 7,
  // at a new, bigger address range (72 bytes/channel) - seed each new slot from whichever old family
  // it belonged to, so existing calibration doesn't change. Must read the OLD addresses (including
  // the old degrees address, which the new bigger layout now overlaps) before eepromWrite() below
  // writes the new layout - reading here only, no writes yet, so nothing is clobbered prematurely.
  bool calibrationFieldsNeedDefaulting = (storedLayoutVersion < 7);
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    if (calibrationFieldsNeedDefaulting)
    {
      int oldMaxStd = EEPROM.readInt(adr_eprom_OLD_SERVO_MAX_STD(ch));
      int oldMinStd = EEPROM.readInt(adr_eprom_OLD_SERVO_MIN_STD(ch));
      int oldCenterStd = EEPROM.readInt(adr_eprom_OLD_SERVO_CENTER_STD(ch));
      int oldMaxSanwa = EEPROM.readInt(adr_eprom_OLD_SERVO_MAX_SANWA(ch));
      int oldMinSanwa = EEPROM.readInt(adr_eprom_OLD_SERVO_MIN_SANWA(ch));
      int oldCenterSanwa = EEPROM.readInt(adr_eprom_OLD_SERVO_CENTER_SANWA(ch));
      for (uint8_t m = 0; m < NUM_SERVO_MODES; m++)
      {
        bool stdFamily = (m == STD || m == NOR || m == SHR);
        SERVO_MAX_BY_MODE[ch][m] = stdFamily ? oldMaxStd : oldMaxSanwa;
        SERVO_MIN_BY_MODE[ch][m] = stdFamily ? oldMinStd : oldMinSanwa;
        SERVO_CENTER_BY_MODE[ch][m] = stdFamily ? oldCenterStd : oldCenterSanwa;
      }
    }
    else
    {
      for (uint8_t m = 0; m < NUM_SERVO_MODES; m++)
      {
        SERVO_MAX_BY_MODE[ch][m] = EEPROM.readInt(adr_eprom_SERVO_MAX(ch, m));
        SERVO_MIN_BY_MODE[ch][m] = EEPROM.readInt(adr_eprom_SERVO_MIN(ch, m));
        SERVO_CENTER_BY_MODE[ch][m] = EEPROM.readInt(adr_eprom_SERVO_CENTER(ch, m));
      }
    }

    // Sanity-check the read value instead of resetting on every unrelated layout bump (that bug
    // used to silently wipe custom rotation angles back to 90 on every future EEPROM_LAYOUT_VERSION
    // bump, since it isn't actually a new field).
    int readDegrees = calibrationFieldsNeedDefaulting ? EEPROM.readInt(adr_eprom_OLD_SERVO_DEGREES(ch)) : EEPROM.readInt(adr_eprom_SERVO_DEGREES(ch));
    SERVO_DEGREES[ch] = (readDegrees < 10 || readDegrees > 360) ? 90 : readDegrees;
  }

  if (layoutJustChanged)
  {
    // One-time cleanup (only for upgrades from before layout version 2): channel 1 was left with an
    // extreme 362-3000us test range, well past what a typical servo can physically reach. Bring it
    // back to a sane full-range default. Gated to that specific version, not every future bump,
    // otherwise it would keep re-overwriting a legitimately re-calibrated channel 1 forever after.
    if (storedLayoutVersion < 2)
    {
      SERVO_MIN_BY_MODE[0][STD] = 500;
      SERVO_MAX_BY_MODE[0][STD] = 2500;
    }

    EEPROM.writeInt(adr_eprom_LAYOUT_VERSION, EEPROM_LAYOUT_VERSION);
    eepromWrite();
  }

  servoModes(); // servoModes() needs to be executed in order to actualize the values

  Serial.println(eepromReadString[LANGUAGE]);
  Serial.println(WIFI_ON);
  // Serial.println(SERVO_STEPS);
  // Serial.println(SERVO_MAX);
  // Serial.println(SERVO_MIN);
  // Serial.println(SERVO_CENTER);
  Serial.println(POWER_SCALE);
  Serial.println(SBUS_INVERTED);
  Serial.println(ENCODER_INVERTED);
  if (LANGUAGE < 0) // Make sure, language is in correct range, otherwise device will crash!
    LANGUAGE = 0;
  if (LANGUAGE > noOfLanguages)
    LANGUAGE = noOfLanguages;
  Serial.println(LANGUAGE);
  Serial.println(SERVO_MODE);
  for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
  {
    for (uint8_t m = 0; m < NUM_SERVO_MODES; m++)
    {
      Serial.println(SERVO_MAX_BY_MODE[ch][m]);
      Serial.println(SERVO_MIN_BY_MODE[ch][m]);
      Serial.println(SERVO_CENTER_BY_MODE[ch][m]);
    }
  }
}

//
// =======================================================================================================
// MAIN LOOP, RUNNING ON CORE 1
// =======================================================================================================
//

void loop()
{

  ButtonRead();
  beep();
  pewPew();
  flashEncoderLed();
  MenuUpdate();
  webInterface();
  if (WIFI_ON == 1)
    webSocket.loop();
  // Serial.print(loopDuration());
}

//
// =======================================================================================================
// 1st MAIN TASK, RUNNING ON CORE 0 (Interrupts are running on this core as well)
// =======================================================================================================
//
/*
void Task1code(void *pvParameters) // TODO, testing only!
{
  for (;;)
  {
    // vTaskDelay(1);          // REQUIRED TO RESET THE WATCH DOG TIMER IF WORKFLOW DOES NOT CONTAIN ANY OTHER DELAY
  }
}*/