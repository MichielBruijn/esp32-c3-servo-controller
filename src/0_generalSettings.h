#include <Arduino.h>
#include <WiFi.h>

/* GENERAL SETTINGS ************************************************************************************************
 *  Have a look at the serial monitor in order to check the current settings!
 *  This is the ESP32-C3 SuperMini "mini" variant: no OLED, no encoder, WiFi web interface only.
 */

// WiFi settings -------------------------------------------------------------------------------------------------
const char* ssid = "ServoTester";  // SSID
const char* password = "123456789";       // Password

/* Wifi transmission power: less power = longer battery life. Valid options are:
WIFI_POWER_19_5dBm = 78     // full power
WIFI_POWER_19dBm = 76
WIFI_POWER_18_5dBm = 74
WIFI_POWER_17dBm = 68
WIFI_POWER_15dBm = 60
WIFI_POWER_13dBm = 52       // recommended setting
WIFI_POWER_11dBm = 44
WIFI_POWER_8_5dBm = 34
WIFI_POWER_7dBm = 28
WIFI_POWER_5dBm = 20
WIFI_POWER_2dBm = 8          // lowest setting, WiFi may become weak
*/
wifi_power_t cpType = WIFI_POWER_13dBm; // Only use values from above!
