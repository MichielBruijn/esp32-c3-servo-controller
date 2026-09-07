
#include <Arduino.h>

// Decode a percent-encoded query value (encodeURIComponent() on the JS side), so SSID/password
// fields can carry spaces and special characters through a GET query parameter.
String urlDecode(String input)
{
  String output = "";
  char hexBuf[] = "00";
  unsigned int len = input.length();
  for (unsigned int i = 0; i < len; i++)
  {
    char c = input[i];
    if (c == '+')
    {
      output += ' ';
    }
    else if (c == '%' && i + 2 < len)
    {
      hexBuf[0] = input[i + 1];
      hexBuf[1] = input[i + 2];
      output += (char)strtol(hexBuf, NULL, 16);
      i += 2;
    }
    else
    {
      output += c;
    }
  }
  return output;
}

//
// =======================================================================================================
// WEB INTERFACE
// =======================================================================================================
//

void webInterface()
{

  if (WIFI_ON == 1)
  {                                         // WiFi on
    WiFiClient client = server.available(); // Listen for incoming clients

    if (client)
    { // If a new client connects,
      currentTime = millis();
      previousTime = currentTime;
      Serial.println("New Client."); // print a message out in the serial port
      String currentLine = "";       // make a String to hold incoming data from the client
      while (client.connected() && currentTime - previousTime <= timeoutTime)
      { // loop while the client's connected
        currentTime = millis();
        if (client.available())
        {                         // if there's bytes to read from the client,
          char c = client.read(); // read a byte, then
          Serial.write(c);        // print it out the serial monitor
          header += c;
          if (c == '\n')
          { // if the byte is a newline character
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0)
            {
              // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
              // followed by the content type so the client knows what to expect, then a blank line:
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              // Parse incoming requests -----------------------------------------------

              bool inStdMode = (SERVO_MODE == STD || SERVO_MODE == NOR || SERVO_MODE == SHR);

              // GET /?Pos0=1500& HTTP/1.1
              if (header.indexOf("GET /?Pos0=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[0] = (valueString.toInt());
              }
              if (header.indexOf("GET /?Pos1=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[1] = (valueString.toInt());
              }
              if (header.indexOf("GET /?Pos2=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[2] = (valueString.toInt());
              }
              if (header.indexOf("GET /?Pos3=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[3] = (valueString.toInt());
              }
              if (header.indexOf("GET /?Pos4=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[4] = (valueString.toInt());
              }
              if (header.indexOf("GET /?Speed=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                TimeAuto = (valueString.toInt());
              }

              // Settings: channel select
              if (header.indexOf("GET /?Ch=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                selectedServo = constrain(valueString.toInt(), 0, NUM_SERVO_CHANNELS - 1);
              }
              // Settings: per-channel Max/Min/Center/Angle - written straight into the calibration
              // arrays, same as the OLED encoder does, since the legacy SERVO_MAX/MIN/CENTER globals
              // get overwritten every loop by servoModes() and would otherwise silently ignore this.
              if (header.indexOf("GET /?Max=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                if (inStdMode)
                  SERVO_MAX_STD[selectedServo] = valueString.toInt();
                else
                  SERVO_MAX_SANWA[selectedServo] = valueString.toInt();
              }
              if (header.indexOf("GET /?Min=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                if (inStdMode)
                  SERVO_MIN_STD[selectedServo] = valueString.toInt();
                else
                  SERVO_MIN_SANWA[selectedServo] = valueString.toInt();
              }
              if (header.indexOf("GET /?Center=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                if (inStdMode)
                  SERVO_CENTER_STD[selectedServo] = valueString.toInt();
                else
                  SERVO_CENTER_SANWA[selectedServo] = valueString.toInt();
              }
              if (header.indexOf("GET /?Angle=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                SERVO_DEGREES[selectedServo] = valueString.toInt();
              }
              if (header.indexOf("GET /?Mode=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                SERVO_MODE = constrain(valueString.toInt(), (int)STD, (int)SXR);
              }
              if (header.indexOf("GET /?Power=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                POWER_SCALE = valueString.toInt();
              }
              if (header.indexOf("GET /?Sbus=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                SBUS_INVERTED = constrain(valueString.toInt(), 0, 1);
              }
              if (header.indexOf("GET /?Enc=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                ENCODER_INVERTED = constrain(valueString.toInt(), 0, 1);
              }
              if (header.indexOf("GET /?SpeedCurve=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                SPEED_CURVE = constrain(valueString.toInt(), 10, 40);
              }
              if (header.indexOf("GET /?WifiOn=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                int newWifiOn = constrain(valueString.toInt(), 0, 1);
                if (newWifiOn != WIFI_ON)
                {
                  WIFI_ON = newWifiOn;
                  WiFiChanged = true;
                }
              }
              if (header.indexOf("GET /?WifiMode=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                int newWifiMode = constrain(valueString.toInt(), (int)WIFI_AP_MODE, (int)WIFI_STATION_MODE);
                if (newWifiMode != WIFI_MODE)
                {
                  WIFI_MODE = newWifiMode;
                  WiFiChanged = true;
                }
              }
              // Home WiFi credentials for Station mode - URL-decoded and length-capped to fit the
              // reserved EEPROM slots (32 chars SSID / 64 chars password), so an oversized value
              // can never spill into the neighbouring field.
              if (header.indexOf("GET /?StaSsid=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = urlDecode(header.substring(pos1 + 1, pos2)).substring(0, 32);
                if (valueString != STA_SSID)
                {
                  STA_SSID = valueString;
                  WiFiChanged = true;
                }
              }
              if (header.indexOf("GET /?StaPass=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = urlDecode(header.substring(pos1 + 1, pos2)).substring(0, 64);
                if (valueString != STA_PASSWORD)
                {
                  STA_PASSWORD = valueString;
                  WiFiChanged = true;
                }
              }

              if (header.indexOf("GET /mitte1/on") >= 0)
              {
                servo_pos[0] = servoCenterForChannel(0); // Center
              }
              if (header.indexOf("GET /mitte2/on") >= 0)
              {
                servo_pos[1] = servoCenterForChannel(1); // Center
              }
              if (header.indexOf("GET /mitte3/on") >= 0)
              {
                servo_pos[2] = servoCenterForChannel(2); // Center
              }
              if (header.indexOf("GET /mitte4/on") >= 0)
              {
                servo_pos[3] = servoCenterForChannel(3); // Center
              }
              if (header.indexOf("GET /mitte5/on") >= 0)
              {
                servo_pos[4] = servoCenterForChannel(4); // Center
              }
              if (header.indexOf("GET /back/on") >= 0)
              {
                Menu = Servotester_Auswahl;
              }
              if (header.indexOf("GET /10/on") >= 0)
              {
                Menu = Servotester_Menu;
              }
              if (header.indexOf("GET /20/on") >= 0)
              {
                Menu = Automatik_Modus_Menu;
              }
              if (header.indexOf("GET /30/on") >= 0)
              {
                Menu = Impuls_lesen_Menu;
              }
              if (header.indexOf("GET /40/on") >= 0)
              {
                Menu = Multiswitch_lesen_Menu;
              }
              if (header.indexOf("GET /50/on") >= 0)
              {
                Menu = SBUS_lesen_Menu;
              }
              if (header.indexOf("GET /60/on") >= 0)
              {
                Menu = IBUS_lesen_Menu;
              }
              if (header.indexOf("GET /120/on") >= 0)
              {
                Menu = Einstellung_Menu;
              }
              if (header.indexOf("GET /save/on") >= 0)
              {
                eepromWrite();
                if (WiFiChanged)
                {
                  WiFiChanged = false;
                  wifiSetup();
                }
              }
              if (header.indexOf("GET /factoryreset/on") >= 0)
              {
                eepromInit(); // Restore factory defaults immediately
              }
              if (header.indexOf("GET /pause/on") >= 0)
              {
                if (Auto_Pause)
                {
                  Auto_Pause = false;
                }
                else
                {
                  Auto_Pause = true;
                }
              }

              // Send the HTML page ------------------------------------------------------
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"icon\" href=\"data:,\">");
              // CSS for the buttons - feel free to change background color and font size to your liking
              client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
              client.println(".button { border: yes; color: white; padding: 10px 40px; width: 100%;");
              client.println("text-decoration: none; font-size: 20px; margin: 2px; cursor: pointer;}");
              client.println(".slider { -webkit-appearance: none; -moz-appearance: none; appearance: none; width: 100%; height: 25px; background: #d3d3d3; outline: none; opacity: 0.7; -webkit-transition: .2s; transition: opacity .2s; }");
              // Firefox needs its own thumb/track rules - "-webkit-appearance: none" alone strips Firefox's
              // native slider rendering too (track AND thumb) without replacing it with anything visible,
              // leaving a near-invisible, effectively undraggable line.
              client.println(".slider::-webkit-slider-thumb { -webkit-appearance: none; width: 25px; height: 25px; border-radius: 50%; background: #4CAF50; cursor: pointer; }");
              client.println(".slider::-moz-range-thumb { width: 25px; height: 25px; border-radius: 50%; background: #4CAF50; cursor: pointer; border: none; }");
              client.println(".slider::-moz-range-track { width: 100%; height: 25px; background: #d3d3d3; }");
              client.println(".button1 {background-color: #4CAF50;}");
              client.println(".button2 {background-color: #ff0000;}");
              client.println(".button3 {background-color: #777777;}");
              client.println(".buttonActive {background-color: #2196F3;}");
              client.println(".textbox {font-size: 25px; text-align: center;}");
              client.println("</style></head>");

              // Page heading
              client.println("</head><body><h1>Servo Tester</h1>");

              switch (Menu)
              {
              case Servotester_Menu:
                client.println("<h2>Servo Tester</h2>");

                for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
                {
                  int chMin = inStdMode ? SERVO_MIN_STD[ch] : SERVO_MIN_SANWA[ch];
                  int chMax = inStdMode ? SERVO_MAX_STD[ch] : SERVO_MAX_SANWA[ch];
                  valueString = String(servo_pos[ch], DEC);

                  client.println("<p><h3>Servo " + String(ch + 1) + " Microseconds: <span id=\"textServo" + String(ch) + "SliderValue\">" + valueString + "</span>");
                  client.println("<a href=\"/mitte" + String(ch + 1) + "/on\"><button class=\"button button1\">Center</button></a></p>");

                  client.println("<input type=\"range\" min=\"" + String(chMin, DEC) + "\" max=\"" + String(chMax, DEC) + "\" step=\"10\" class=\"slider\" id=\"Servo" + String(ch) + "Slider\" oninput=\"Servo" + String(ch) + "Speed(this.value)\" value=\"" + valueString + "\" /></p>");

                  client.println("<script> function Servo" + String(ch) + "Speed(pos) { ");
                  client.println("document.getElementById(\"textServo" + String(ch) + "SliderValue\").innerHTML = pos;");
                  client.println("var xhr = new XMLHttpRequest();");
                  client.println("xhr.open('GET', \"/?Pos" + String(ch) + "=\" + pos + \"&\", true);");
                  client.println("xhr.send(); } </script>");
                }

                client.println("<p><a href=\"/back/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;

              case Automatik_Modus_Menu:
                client.println("<h2>Automatic Mode</h2>");

                valueString = String(TimeAuto, DEC);

                client.println("<p><h3>Servo Speed: <span id=\"textServoSpeedValue\">" + valueString + "</span>");

                client.println("<input type=\"range\" min=\"0\" max=\"100\" step=\"5\" class=\"slider\" id=\"ServoSpeedAuto\" onchange=\"ServoSpeed(this.value)\" value=\"" + valueString + "\" /></p>");

                client.println("<script> function ServoSpeed(pos) { ");
                client.println("document.getElementById(\"textServoSpeedValue\").innerHTML = pos;");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?Speed=\" + pos + \"&\", true);");
                client.println("xhr.send(); } </script>");

                client.println("<p><a href=\"/pause/on\"><button class=\"button button1\">Pause</button></a></p>");
                client.println("<p><a href=\"/back/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;

              case Einstellung_Menu:
              {
                client.println("<h2>Settings</h2>");

                // Channel selector -----------------------------------------
                client.println("<p><h3>Servo Channel</h3>");
                for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
                {
                  String activeClass = (ch == selectedServo) ? "buttonActive" : "button3";
                  client.println("<a href=\"/?Ch=" + String(ch) + "&\"><button style=\"width:18%;display:inline-block;\" class=\"button " + activeClass + "\">CH" + String(ch + 1) + "</button></a>");
                }
                client.println("</p>");

                // Per-channel calibration -----------------------------------
                int chMax = inStdMode ? SERVO_MAX_STD[selectedServo] : SERVO_MAX_SANWA[selectedServo];
                int chMin = inStdMode ? SERVO_MIN_STD[selectedServo] : SERVO_MIN_SANWA[selectedServo];
                int chCenter = inStdMode ? SERVO_CENTER_STD[selectedServo] : SERVO_CENTER_SANWA[selectedServo];

                valueString = String(chMax, DEC);
                client.println("<p><h3>Servo Max (&micro;s): <span id=\"textMaxValue\">" + valueString + "</span>");
                client.println("<input type=\"text\" id=\"MaxInput\" class=\"textbox\" oninput=\"Maxchange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Maxchange(pos) { ");
                client.println("document.getElementById(\"textMaxValue\").innerHTML = pos;");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?Max=\" + pos + \"&\", true);");
                client.println("xhr.send(); } </script>");

                valueString = String(chMin, DEC);
                client.println("<p><h3>Servo Min (&micro;s): <span id=\"textMinValue\">" + valueString + "</span>");
                client.println("<input type=\"text\" id=\"MinInput\" class=\"textbox\" oninput=\"Minchange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Minchange(pos) { ");
                client.println("document.getElementById(\"textMinValue\").innerHTML = pos;");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?Min=\" + pos + \"&\", true);");
                client.println("xhr.send(); } </script>");

                valueString = String(chCenter, DEC);
                client.println("<p><h3>Servo Center (&micro;s): <span id=\"textCenterValue\">" + valueString + "</span>");
                client.println("<input type=\"text\" id=\"CenterInput\" class=\"textbox\" oninput=\"Centerchange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Centerchange(pos) { ");
                client.println("document.getElementById(\"textCenterValue\").innerHTML = pos;");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?Center=\" + pos + \"&\", true);");
                client.println("xhr.send(); } </script>");

                valueString = String(SERVO_DEGREES[selectedServo], DEC);
                client.println("<p><h3>Servo Angle (&deg;): <span id=\"textAngleValue\">" + valueString + "</span>");
                client.println("<input type=\"text\" id=\"AngleInput\" class=\"textbox\" oninput=\"Anglechange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Anglechange(pos) { ");
                client.println("document.getElementById(\"textAngleValue\").innerHTML = pos;");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?Angle=\" + pos + \"&\", true);");
                client.println("xhr.send(); } </script>");

                // Servo Hz / mode --------------------------------------------
                client.println("<p><h3>Servo Hz / Mode: " + String(SERVO_Hz) + " Hz (" + servoMode + ")</h3>");
                {
                  const char *modeNames[] = {"Std.", "NOR", "SHR", "SSR", "SUR", "SXR"};
                  for (int m = (int)STD; m <= (int)SXR; m++)
                  {
                    String activeClass = (m == SERVO_MODE) ? "buttonActive" : "button3";
                    client.println("<a href=\"/?Mode=" + String(m) + "&\"><button style=\"width:15%;display:inline-block;\" class=\"button " + activeClass + "\">" + String(modeNames[m]) + "</button></a>");
                  }
                }
                client.println("</p>");

                // Power scale --------------------------------------------
                valueString = String(POWER_SCALE, DEC);
                client.println("<p><h3>Power Scale: <span id=\"textPowerValue\">" + valueString + "</span> (Battery: " + String(batteryVoltage, 2) + "V)</h3>");
                client.println("<input type=\"text\" id=\"PowerInput\" class=\"textbox\" oninput=\"Powerchange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Powerchange(pos) { ");
                client.println("document.getElementById(\"textPowerValue\").innerHTML = pos;");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?Power=\" + pos + \"&\", true);");
                client.println("xhr.send(); } </script>");

                // SBUS inverted --------------------------------------------
                client.println("<p><h3>SBUS: " + String(SBUS_INVERTED == 1 ? "Standard" : "Inversed") + "</h3>");
                client.println("<a href=\"/?Sbus=1&\"><button style=\"width:48%;display:inline-block;\" class=\"button " + String(SBUS_INVERTED == 1 ? "buttonActive" : "button3") + "\">Standard</button></a>");
                client.println("<a href=\"/?Sbus=0&\"><button style=\"width:48%;display:inline-block;\" class=\"button " + String(SBUS_INVERTED == 0 ? "buttonActive" : "button3") + "\">Inversed</button></a></p>");

                // Encoder direction --------------------------------------------
                client.println("<p><h3>Encoder Direction: " + String(ENCODER_INVERTED == 0 ? "Standard" : "Inversed") + "</h3>");
                client.println("<a href=\"/?Enc=0&\"><button style=\"width:48%;display:inline-block;\" class=\"button " + String(ENCODER_INVERTED == 0 ? "buttonActive" : "button3") + "\">Standard</button></a>");
                client.println("<a href=\"/?Enc=1&\"><button style=\"width:48%;display:inline-block;\" class=\"button " + String(ENCODER_INVERTED == 1 ? "buttonActive" : "button3") + "\">Inversed</button></a></p>");

                // Speed curve --------------------------------------------
                valueString = String(SPEED_CURVE / 10.0, 1);
                client.println("<p><h3>Speed Curve: <span id=\"textSpeedCurveValue\">" + valueString + "</span>");
                client.println("<input type=\"range\" min=\"10\" max=\"40\" step=\"1\" class=\"slider\" id=\"SpeedCurveSlider\" oninput=\"SpeedCurveChange(this.value)\" value=\"" + String(SPEED_CURVE) + "\" /></p>");
                client.println("<script> function SpeedCurveChange(pos) { ");
                client.println("document.getElementById(\"textSpeedCurveValue\").innerHTML = (pos/10.0).toFixed(1);");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?SpeedCurve=\" + pos + \"&\", true);");
                client.println("xhr.send(); } </script>");

                // WiFi on/off --------------------------------------------
                client.println("<p><h3>WiFi: " + String(WIFI_ON == 1 ? "On" : "Off") + "</h3>");
                client.println("<a href=\"/?WifiOn=1&\"><button style=\"width:48%;display:inline-block;\" class=\"button " + String(WIFI_ON == 1 ? "buttonActive" : "button3") + "\">On</button></a>");
                client.println("<a href=\"/?WifiOn=0&\"><button style=\"width:48%;display:inline-block;\" class=\"button " + String(WIFI_ON == 0 ? "buttonActive" : "button3") + "\">Off</button></a></p>");

                // WiFi mode: own Access Point vs joining an existing (home) network -----
                client.println("<p><h3>WiFi Mode: " + String(WIFI_MODE == WIFI_STATION_MODE ? "Station (join network)" : "Access Point") + "</h3>");
                client.println("<a href=\"/?WifiMode=0&\"><button style=\"width:48%;display:inline-block;\" class=\"button " + String(WIFI_MODE == WIFI_AP_MODE ? "buttonActive" : "button3") + "\">Access Point</button></a>");
                client.println("<a href=\"/?WifiMode=1&\"><button style=\"width:48%;display:inline-block;\" class=\"button " + String(WIFI_MODE == WIFI_STATION_MODE ? "buttonActive" : "button3") + "\">Station</button></a></p>");

                if (wifiStaFallback)
                {
                  client.println("<p style=\"color:red;\">Could not join '" + STA_SSID + "', fell back to Access Point. Check the password below and Save again.</p>");
                }

                client.println("<p><h3>Home WiFi SSID (Station mode)</h3>");
                client.println("<input type=\"text\" id=\"StaSsidInput\" class=\"textbox\" oninput=\"StaSsidChange(this.value)\" value=\"" + STA_SSID + "\" /></p>");
                client.println("<script> function StaSsidChange(val) { ");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?StaSsid=\" + encodeURIComponent(val) + \"&\", true);");
                client.println("xhr.send(); } </script>");

                client.println("<p><h3>Home WiFi Password (Station mode)</h3>");
                client.println("<input type=\"password\" id=\"StaPassInput\" class=\"textbox\" oninput=\"StaPassChange(this.value)\" value=\"" + STA_PASSWORD + "\" /></p>");
                client.println("<script> function StaPassChange(val) { ");
                client.println("var xhr = new XMLHttpRequest();");
                client.println("xhr.open('GET', \"/?StaPass=\" + encodeURIComponent(val) + \"&\", true);");
                client.println("xhr.send(); } </script>");

                client.println("<p><a href=\"/save/on\"><button class=\"button button1\">Save</button></a></p>");
                client.println("<p><a href=\"/factoryreset/on\" onclick=\"return confirm('Reset all settings to factory defaults?');\"><button class=\"button button2\">Factory Reset</button></a></p>");
                client.println("<p><a href=\"/back/on\"><button class=\"button button3\">Menu</button></a></p>");
                break;
              }

              default:
                client.println("<h2>Menu</h2>");
                client.println("<p><a href=\"/10/on\"><button class=\"button button1\">Servo Tester</button></a></p>");
                client.println("<p><a href=\"/20/on\"><button class=\"button button1\">Automatic Mode</button></a></p>");
                client.println("<p><a href=\"/30/on\"><button class=\"button button1\">Read PWM Impulse</button></a></p>");
                client.println("<p><a href=\"/40/on\"><button class=\"button button1\">Read PPM Multiswitch</button></a></p>");
                client.println("<p><a href=\"/50/on\"><button class=\"button button1\">Read SBUS</button></a></p>");
                client.println("<p><a href=\"/60/on\"><button class=\"button button1\">Read IBUS</button></a></p>");
                client.println("<p><a href=\"/120/on\"><button class=\"button button1\">Settings</button></a></p>");
                break; // Not needed when statement(s) are present
              }

              client.println("</body></html>");

              // The HTTP response ends with another blank line
              client.println();
              // Break out of the while loop
              break;
            }
            else
            { // if you got a newline, then clear currentLine
              currentLine = "";
            }
          }
          else if (c != '\r')
          {                   // if you got anything else but a carriage return character,
            currentLine += c; // add it to the end of the currentLine
          }
        }
      }
      // Clear the header
      header = "";
      // Close the connection
      client.stop();
      Serial.println("Client disconnected.");
      Serial.println("");
    }
  }
}
