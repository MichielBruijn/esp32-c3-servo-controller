
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
      // NOTE: no setNoDelay() here on purpose. It helped the old per-tick HTTP slider updates, but
      // now that those go over the WebSocket instead, disabling Nagle here only hurt full-page loads
      // (Menu/Settings/reload): a page is ~150 separate client.println() calls, and without Nagle's
      // batching each one tends to go out as its own WiFi packet - many more packets, each with real
      // per-frame WiFi overhead, made full pages noticeably slower to arrive.
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
          // Echoing every byte to Serial at 115200 baud added real, measurable latency to every
          // single request (a typical browser request is 300-600+ bytes) - exactly what made
          // dragging a slider (which fires a fresh request on every tick) feel jerky.
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
              client.println("Content-type:text/html; charset=utf-8");
              client.println("Connection: close");
              client.println();

              // Parse incoming requests -----------------------------------------------

              // These query keys are only ever fired from a slider/textbox's fire-and-forget
              // XHR (see oninput handlers below) - the JS never reads the response, so re-rendering
              // and sending the full settings/servo page HTML for every single one is pure wasted
              // time. That extra round-trip time is what made dragging a slider feel jerky: while
              // the ESP32's single-threaded server was still busy sending the previous full page,
              // the next XHR just queued up behind it. Skipping straight to an empty response for
              // these keeps each request short, so drag updates keep up in real time.
              bool xhrOnly = false;

              // GET /?Pos0=1500& HTTP/1.1
              if (header.indexOf("GET /?Pos0=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[0] = (valueString.toInt());
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Pos1=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[1] = (valueString.toInt());
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Pos2=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[2] = (valueString.toInt());
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Pos3=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[3] = (valueString.toInt());
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Pos4=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                servo_pos[4] = (valueString.toInt());
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Speed=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                TimeAuto = (valueString.toInt());
                xhrOnly = true;
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
                SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE] = valueString.toInt();
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Min=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE] = valueString.toInt();
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Center=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE] = valueString.toInt();
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Angle=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                SERVO_DEGREES[selectedServo] = valueString.toInt();
                xhrOnly = true;
              }
              if (header.indexOf("GET /?Mode=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                // Written straight into the timer group's array, same reason as Max/Min/Center above
                SERVO_MODE_PER_GROUP[servoTimerGroup(selectedServo)] = constrain(valueString.toInt(), (int)STD, (int)SXR);
              }
              if (header.indexOf("GET /?Power=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                POWER_SCALE = valueString.toInt();
                xhrOnly = true;
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
                xhrOnly = true;
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
              if (header.indexOf("GET /?JoyX=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                JOYSTICK_X_CHANNEL = constrain(valueString.toInt(), 0, NUM_SERVO_CHANNELS - 1);
              }
              if (header.indexOf("GET /?JoyY=") >= 0)
              {
                pos1 = header.indexOf('=');
                pos2 = header.indexOf('&');
                valueString = header.substring(pos1 + 1, pos2);
                JOYSTICK_Y_CHANNEL = constrain(valueString.toInt(), 0, NUM_SERVO_CHANNELS - 1);
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
                xhrOnly = true;
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
                xhrOnly = true;
              }

              // Center buttons now just call Servo{ch}Speed() with the known center value client-side
              // (see the Servotester_Menu render below), reusing the already-fast /?PosN= endpoint
              // instead of a separate full-page navigation that used to make Center feel slow.
              if (header.indexOf("GET /back/on") >= 0)
              {
                Menu = Servotester_Select;
                webJoystickMode = false;
              }
              if (header.indexOf("GET /10/on") >= 0)
              {
                Menu = Servotester_Menu;
                webJoystickMode = false;
              }
              if (header.indexOf("GET /joystick/on") >= 0)
              {
                // Reuses the Servotester_Menu screen so the mcpwm output loop there stays active -
                // only the web page rendering differs (see the Servotester_Menu case below).
                Menu = Servotester_Menu;
                webJoystickMode = true;
              }
              if (header.indexOf("GET /20/on") >= 0)
              {
                Menu = AutoMode_Menu;
              }
              if (header.indexOf("GET /expert/on") >= 0)
              {
                Menu = ExpertFunctions_Menu;
              }
              if (header.indexOf("GET /30/on") >= 0)
              {
                Menu = ReadPulse_Menu;
              }
              if (header.indexOf("GET /40/on") >= 0)
              {
                Menu = ReadMultiswitch_Menu;
              }
              if (header.indexOf("GET /50/on") >= 0)
              {
                Menu = ReadSbus_Menu;
              }
              if (header.indexOf("GET /60/on") >= 0)
              {
                Menu = ReadIbus_Menu;
              }
              if (header.indexOf("GET /80/on") >= 0)
              {
                Menu = Info_Menu;
              }
              if (header.indexOf("GET /90/on") >= 0)
              {
                Menu = Oscilloscope_Menu;
              }
              if (header.indexOf("GET /100/on") >= 0)
              {
                Menu = SignalGenerator_Menu;
              }
              if (header.indexOf("GET /120/on") >= 0)
              {
                Menu = Settings_Menu;
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
              if (header.indexOf("GET /checkupdate/on") >= 0)
              {
                checkForFirmwareUpdate();
                if (!updateAvailable)
                {
                  updateErrorMessage = "No update available (running v" + String(codeVersion) + ")";
                }
              }
              if (header.indexOf("GET /update/on") >= 0)
              {
                // Blocks for the download, then restarts on success - the browser will just see the
                // connection drop, which is expected. On failure it falls through to the normal page
                // render below, with updateErrorMessage set.
                installFirmwareUpdate();
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
              // Skipped for xhrOnly requests - see the comment where xhrOnly is declared above.
              if (!xhrOnly)
              {
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta charset=\"UTF-8\">");
              client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<title>Servo Tester</title>");
              // Emoji-as-favicon via an inline SVG data URI - a real icon without needing a separate served file/route.
              // The emoji is percent-encoded (not sent as raw UTF-8 bytes) so it can't be misread regardless of charset handling.
              client.println("<link rel=\"icon\" href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>%F0%9F%95%B9</text></svg>\">");
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
              client.println("</style>");

              // Throttled sender for slider/textbox updates: at most one request in flight per
              // control at a time. A fast drag fires oninput far faster than the single-threaded
              // ESP32 server can answer - without this, every tick queued up its own request and
              // the servo lagged the drag, then caught up in bursts. Here, extra ticks that arrive
              // while a request is still in flight just replace the pending value; once that
              // request completes, only the latest pending value (if any) is sent next.
              client.println("<script>");
              client.println("var pendingUrl = {}, inFlight = {};");
              client.println("function sendThrottled(key, url) {");
              client.println("  pendingUrl[key] = url;");
              client.println("  if (!inFlight[key]) { inFlight[key] = true; doSend(key); }");
              client.println("}");
              client.println("function doSend(key) {");
              client.println("  var url = pendingUrl[key]; pendingUrl[key] = null;");
              client.println("  var xhr = new XMLHttpRequest();");
              client.println("  xhr.onreadystatechange = function() {");
              client.println("    if (xhr.readyState === 4) {");
              client.println("      if (pendingUrl[key]) { doSend(key); } else { inFlight[key] = false; }");
              client.println("    }");
              client.println("  };");
              client.println("  xhr.open('GET', url, true);");
              client.println("  xhr.send();");
              client.println("}");
              // Low-latency channel for live servo position updates while dragging (port 81, see
              // WebSocketsServer in src.ino). A plain HTTP request per tick - even throttled - still
              // pays a fresh TCP handshake and full header parse each time, which is what made a
              // sustained drag feel laggy. Falls back to the throttled HTTP path (sendThrottled)
              // until the socket is open, or if it ever drops.
              client.println("var posSocket = null, posSocketReady = false;");
              client.println("function connectPosSocket() {");
              client.println("  posSocket = new WebSocket('ws://' + location.hostname + ':81/');");
              client.println("  posSocket.onopen = function() { posSocketReady = true; };");
              client.println("  posSocket.onclose = function() { posSocketReady = false; setTimeout(connectPosSocket, 1000); };");
              client.println("  posSocket.onerror = function() { posSocketReady = false; };");
              client.println("}");
              client.println("connectPosSocket();");
              client.println("function sendPos(ch, pos) {");
              client.println("  if (posSocketReady && posSocket.readyState === 1) { posSocket.send('Pos' + ch + '=' + pos); }");
              client.println("  else { sendThrottled('Pos' + ch, '/?Pos' + ch + '=' + pos + '&'); }");
              client.println("}");
              client.println("</script></head>");

              // Page heading (skipped for Joystick Mode - full-screen touch page, no room for it)
              client.println("</head><body>");
              if (!webJoystickMode)
              {
                client.println("<h1>Servo Tester</h1>");
                if (updateAvailable && Menu != Info_Menu)
                {
                  client.println("<p style=\"background:#2196F3;color:white;padding:8px;border-radius:6px;\">Update available: v" + latestFirmwareVersion + " - <a href=\"/80/on\" style=\"color:white;\">see Info</a></p>");
                }
              }

              switch (Menu)
              {
              case Servotester_Menu:
                if (webJoystickMode)
                {
                  // Steering (horizontal, left thumb) + throttle (vertical, right thumb) - the
                  // common dual-thumb driving layout. Reuses the same JOYSTICK_X/Y_CHANNEL mapping
                  // as the physical stick (Settings menu), so it's really just an alternative input
                  // for the same thing. Springs back to center on release, like a self-centering stick.
                  int throttleMode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_Y_CHANNEL)];
                  int throttleMin = SERVO_MIN_BY_MODE[JOYSTICK_Y_CHANNEL][throttleMode];
                  int throttleMax = SERVO_MAX_BY_MODE[JOYSTICK_Y_CHANNEL][throttleMode];
                  int throttleCenterVal = servoCenterForChannel(JOYSTICK_Y_CHANNEL);
                  int steerMode = SERVO_MODE_PER_GROUP[servoTimerGroup(JOYSTICK_X_CHANNEL)];
                  int steerMin = SERVO_MIN_BY_MODE[JOYSTICK_X_CHANNEL][steerMode];
                  int steerMax = SERVO_MAX_BY_MODE[JOYSTICK_X_CHANNEL][steerMode];
                  int steerCenterVal = servoCenterForChannel(JOYSTICK_X_CHANNEL);

                  // Both controls are custom track+thumb divs now - mixing a native <input
                  // type=range> (steer) with a custom Pointer Events div (throttle) turned out to
                  // be the real problem: whichever was touched *second* went completely
                  // unresponsive, order-dependently, no matter what CSS/capture combination was
                  // tried. That pattern only appeared once a native form control was in the mix, so
                  // this is presumably a GeckoView engine limitation on mixing the two for
                  // simultaneous multi-touch, not something fixable from here - using the same
                  // custom implementation for both avoids the mix entirely.
                  client.println("<style>");
                  client.println("body{margin:0;overflow:hidden;}");
                  client.println(".arcadeBar{position:fixed;top:0;left:0;right:0;display:flex;align-items:center;justify-content:space-between;gap:16px;padding:8px 16px;z-index:2;font-size:14px;color:#333;}");
                  // touch-action:none so 2 fingers on the tracks drive both controls instead of the
                  // browser's default pinch-to-zoom gesture. Earlier removed while chasing the
                  // native/custom mix bug (a different issue, now fixed by dropping the native
                  // input entirely) - safe to bring back with both controls being the same kind.
                  client.println(".arcadeTrack{position:fixed;background:#d3d3d3;border-radius:20px;user-select:none;touch-action:none;}");
                  client.println(".arcadeThumb{position:absolute;top:50%;left:50%;width:60px;height:60px;margin-top:-30px;margin-left:-30px;border-radius:50%;background:#4CAF50;box-shadow:0 2px 6px rgba(0,0,0,0.4);will-change:transform;}");
                  client.println("#steerTrack{left:5vw;width:39vw;height:70px;top:50%;margin-top:-35px;}");
                  client.println("#throttleTrack{left:82vw;top:50%;width:70px;height:50vh;margin-left:-35px;margin-top:-25vh;}");
                  client.println("</style>");

                  client.println("<div class=\"arcadeBar\"><a href=\"/back/on\"><button class=\"button button2\">Menu</button></a></div>");
                  client.println("<div class=\"arcadeTrack\" id=\"steerTrack\"><div class=\"arcadeThumb\" id=\"steerThumb\"></div></div>");
                  client.println("<div class=\"arcadeTrack\" id=\"throttleTrack\"><div class=\"arcadeThumb\" id=\"throttleThumb\"></div></div>");

                  client.println("<script>");
                  client.println("function setupArcadeTrack(trackId, thumbId, horizontal, ch, min, center, max) {");
                  client.println("  var track = document.getElementById(trackId), thumb = document.getElementById(thumbId);");
                  client.println("  var dragging = false, activePointerId = null, lastSent = 0, rect = track.getBoundingClientRect(), pendingVal = center, rafScheduled = false;");
                  client.println("  function valueFromPointer(e) {");
                  client.println("    var frac = horizontal ? (e.clientX - rect.left) / rect.width : 1 - (e.clientY - rect.top) / rect.height;");
                  client.println("    frac = Math.max(0, Math.min(1, frac));");
                  client.println("    return Math.round(frac >= 0.5 ? center + (frac - 0.5) * 2 * (max - center) : center - (0.5 - frac) * 2 * (center - min));");
                  client.println("  }");
                  client.println("  function setThumb(val) {");
                  client.println("    var frac = Math.max(0, Math.min(1, val >= center ? 0.5 + 0.5 * (val - center) / (max - center) : 0.5 - 0.5 * (center - val) / (center - min)));");
                  client.println("    var offset = (frac - 0.5) * (horizontal ? rect.width : rect.height);");
                  client.println("    thumb.style.transform = horizontal ? ('translateX(' + offset + 'px)') : ('translateY(' + (-offset) + 'px)');");
                  client.println("  }");
                  client.println("  function applyFrame() {");
                  client.println("    rafScheduled = false;");
                  client.println("    if (!dragging) return;");
                  client.println("    setThumb(pendingVal);");
                  client.println("    var now = Date.now();");
                  client.println("    if (now - lastSent >= 30) { lastSent = now; sendPos(ch, pendingVal); }");
                  client.println("  }");
                  // Pointer tracked by ID via window listeners, no setPointerCapture() - both were
                  // tried and ruled out as the cause of the order-dependent bug described above.
                  // No e.preventDefault() either: diagnostics showed the page stays at a steady
                  // 120fps but the second simultaneous touch's pointermove rate drops ~14x (112/s
                  // vs 8/s) - not a rendering problem, an event-*delivery* one. preventDefault()
                  // forces the browser to synchronously confirm with the main thread before it can
                  // fast-path touch dispatch on the compositor thread; touch-action:none (CSS)
                  // already suppresses scroll/zoom, so the JS-level preventDefault() is redundant
                  // and may be exactly what's throttling the second touch's event stream.
                  client.println("  function onMove(e) {");
                  client.println("    if (!dragging || e.pointerId !== activePointerId) return;");
                  client.println("    pendingVal = valueFromPointer(e);");
                  client.println("    if (!rafScheduled) { rafScheduled = true; requestAnimationFrame(applyFrame); }");
                  client.println("  }");
                  client.println("  function onStart(e) {");
                  client.println("    if (dragging) return;");
                  client.println("    dragging = true; activePointerId = e.pointerId; lastSent = 0; rect = track.getBoundingClientRect();");
                  client.println("    onMove(e);");
                  client.println("  }");
                  client.println("  function onEnd(e) {");
                  client.println("    if (!dragging || e.pointerId !== activePointerId) return;");
                  client.println("    dragging = false; activePointerId = null; pendingVal = center; setThumb(center); sendPos(ch, center);");
                  client.println("  }");
                  client.println("  track.addEventListener('pointerdown', onStart);");
                  client.println("  window.addEventListener('pointermove', onMove);");
                  client.println("  window.addEventListener('pointerup', onEnd);");
                  client.println("  window.addEventListener('pointercancel', onEnd);");
                  client.println("  setThumb(center);");
                  client.println("}");
                  client.println("setupArcadeTrack('steerTrack','steerThumb',true," + String(JOYSTICK_X_CHANNEL) + "," + String(steerMin) + "," + String(steerCenterVal) + "," + String(steerMax) + ");");
                  client.println("setupArcadeTrack('throttleTrack','throttleThumb',false," + String(JOYSTICK_Y_CHANNEL) + "," + String(throttleMin) + "," + String(throttleCenterVal) + "," + String(throttleMax) + ");");
                  client.println("</script>");
                  break;
                }

                client.println("<h2>Manual Mode</h2>");

                for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
                {
                  int chMode = SERVO_MODE_PER_GROUP[servoTimerGroup(ch)];
                  int chMin = SERVO_MIN_BY_MODE[ch][chMode];
                  int chMax = SERVO_MAX_BY_MODE[ch][chMode];
                  int chCenterVal = servoCenterForChannel(ch);
                  valueString = String(servo_pos[ch], DEC);

                  client.println("<p><h3>Servo " + String(ch + 1) + " Microseconds: <span id=\"textServo" + String(ch) + "SliderValue\">" + valueString + "</span>");
                  client.println("<button class=\"button button1\" onclick=\"centerServo" + String(ch) + "()\">Center</button></p>");

                  client.println("<input type=\"range\" min=\"" + String(chMin, DEC) + "\" max=\"" + String(chMax, DEC) + "\" step=\"10\" class=\"slider\" id=\"Servo" + String(ch) + "Slider\" oninput=\"Servo" + String(ch) + "Speed(this.value)\" value=\"" + valueString + "\" /></p>");

                  client.println("<script> function Servo" + String(ch) + "Speed(pos) { ");
                  client.println("document.getElementById(\"textServo" + String(ch) + "SliderValue\").innerHTML = pos;");
                  client.println("sendPos(" + String(ch) + ", pos);");
                  client.println("}");
                  // Center button: update the slider and label instantly client-side (no page
                  // reload), reusing the same throttled endpoint the slider itself already uses -
                  // this used to be a full page navigation, which is why it felt slow.
                  client.println("function centerServo" + String(ch) + "() {");
                  client.println("document.getElementById(\"Servo" + String(ch) + "Slider\").value = " + String(chCenterVal) + ";");
                  client.println("Servo" + String(ch) + "Speed(" + String(chCenterVal) + ");");
                  client.println("} </script>");
                }

                client.println("<p><a href=\"/joystick/on\"><button class=\"button button3\">Joystick Mode (mobile)</button></a></p>");
                client.println("<p><a href=\"/back/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;

              case AutoMode_Menu:
                client.println("<h2>Sweep Mode</h2>");

                valueString = String(TimeAuto, DEC);

                client.println("<p><h3>Servo Speed: <span id=\"textServoSpeedValue\">" + valueString + "</span>");

                client.println("<input type=\"range\" min=\"0\" max=\"100\" step=\"5\" class=\"slider\" id=\"ServoSpeedAuto\" onchange=\"ServoSpeed(this.value)\" value=\"" + valueString + "\" /></p>");

                client.println("<script> function ServoSpeed(pos) { ");
                client.println("document.getElementById(\"textServoSpeedValue\").innerHTML = pos;");
                client.println("sendThrottled('Speed', \"/?Speed=\" + pos + \"&\");");
                client.println("} </script>");

                client.println("<p><a href=\"/pause/on\"><button class=\"button button1\">Pause</button></a></p>");
                client.println("<p><a href=\"/back/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;

              case Info_Menu:
                client.println("<h2>Info</h2>");

                client.println("<h3>Wifi</h3>");
                if (WIFI_ON == 1)
                {
                  if (WIFI_MODE == WIFI_STATION_MODE && !wifiStaFallback)
                  {
                    client.println("<p>Station - SSID: " + STA_SSID + "<br>" + ipAddressString[LANGUAGE] + " " + wifiIpString + "<br>servotester.local</p>");
                  }
                  else
                  {
                    client.println("<p>" + String(wifiStaFallback ? "Access Point (fallback)" : "Access Point") + " - SSID: " + String(ssid) + "<br>" + passwordString[LANGUAGE] + " " + String(password) + "<br>" + ipAddressString[LANGUAGE] + " " + wifiIpString + "</p>");
                  }
                }
                else
                {
                  client.println("<p>Off</p>");
                }

                client.println("<h3>Controls (physical device)</h3>");
                client.println("<p>Turn: move through list / adjust value<br>Short press: select<br>Long press: back<br>Double-click: next channel<br>BOOT button: next channel</p>");

                client.println("<h3>Firmware</h3>");
                if (updateAvailable)
                {
                  client.println("<p>v" + String(codeVersion) + " - update available: v" + latestFirmwareVersion + "</p>");
                  client.println("<p><a href=\"/update/on\" onclick=\"return confirm('Download and install v" + latestFirmwareVersion + "? The device will restart.');\"><button class=\"button button1\">Install Update</button></a></p>");
                }
                else
                {
                  client.println("<p>v" + String(codeVersion) + " (up to date)</p>");
                }
                if (updateErrorMessage.length() > 0)
                {
                  client.println("<p style=\"color:red;\">" + updateErrorMessage + "</p>");
                  updateErrorMessage = "";
                }
                client.println("<p><a href=\"/checkupdate/on\"><button class=\"button button3\">Check for Update</button></a></p>");

                client.println("<p><a href=\"/back/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;

              case ExpertFunctions_Menu:
                client.println("<h2>Expert Functions</h2>");
                client.println("<p><a href=\"/30/on\"><button class=\"button button1\">Read PWM Impulse</button></a></p>");
                client.println("<p><a href=\"/40/on\"><button class=\"button button1\">Read PPM Multiswitch</button></a></p>");
                client.println("<p><a href=\"/50/on\"><button class=\"button button1\">Read SBUS</button></a></p>");
                client.println("<p><a href=\"/60/on\"><button class=\"button button1\">Read IBUS</button></a></p>");
                client.println("<p><a href=\"/90/on\"><button class=\"button button1\">Oscilloscope</button></a></p>");
                client.println("<p><a href=\"/100/on\"><button class=\"button button1\">Signal Generator</button></a></p>");
                client.println("<p><a href=\"/back/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;

              case Oscilloscope_Menu:
                client.println("<h2>Oscilloscope</h2>");
                client.println("<p>OLED-only feature - the live scope trace isn't available over the web, it's switched on now on the device itself. Probe input: GPIO39, 0-3.3V RC signals only.</p>");
                client.println("<p><a href=\"/expert/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;

              case SignalGenerator_Menu:
                client.println("<h2>Signal Generator</h2>");
                client.println("<p>OLED-only feature - waveform/frequency/ratio are set on the device itself, it's switched on now. Output: GPIO25, 0-3.3V.</p>");
                client.println("<p><a href=\"/expert/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;

              case Settings_Menu:
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
                int chMax = SERVO_MAX_BY_MODE[selectedServo][SERVO_MODE];
                int chMin = SERVO_MIN_BY_MODE[selectedServo][SERVO_MODE];
                int chCenter = SERVO_CENTER_BY_MODE[selectedServo][SERVO_MODE];

                valueString = String(chMax, DEC);
                client.println("<p><h3>Servo Max (&micro;s): <span id=\"textMaxValue\">" + valueString + "</span>");
                client.println("<input type=\"text\" id=\"MaxInput\" class=\"textbox\" oninput=\"Maxchange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Maxchange(pos) { ");
                client.println("document.getElementById(\"textMaxValue\").innerHTML = pos;");
                client.println("sendThrottled('Max', \"/?Max=\" + pos + \"&\");");
                client.println("} </script>");

                valueString = String(chMin, DEC);
                client.println("<p><h3>Servo Min (&micro;s): <span id=\"textMinValue\">" + valueString + "</span>");
                client.println("<input type=\"text\" id=\"MinInput\" class=\"textbox\" oninput=\"Minchange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Minchange(pos) { ");
                client.println("document.getElementById(\"textMinValue\").innerHTML = pos;");
                client.println("sendThrottled('Min', \"/?Min=\" + pos + \"&\");");
                client.println("} </script>");

                valueString = String(chCenter, DEC);
                client.println("<p><h3>Servo Center (&micro;s): <span id=\"textCenterValue\">" + valueString + "</span>");
                client.println("<input type=\"text\" id=\"CenterInput\" class=\"textbox\" oninput=\"Centerchange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Centerchange(pos) { ");
                client.println("document.getElementById(\"textCenterValue\").innerHTML = pos;");
                client.println("sendThrottled('Center', \"/?Center=\" + pos + \"&\");");
                client.println("} </script>");

                valueString = String(SERVO_DEGREES[selectedServo], DEC);
                client.println("<p><h3>Servo Angle (&deg;): <span id=\"textAngleValue\">" + valueString + "</span>");
                client.println("<input type=\"text\" id=\"AngleInput\" class=\"textbox\" oninput=\"Anglechange(this.value)\" value=\"" + valueString + "\" /></p>");
                client.println("<script> function Anglechange(pos) { ");
                client.println("document.getElementById(\"textAngleValue\").innerHTML = pos;");
                client.println("sendThrottled('Angle', \"/?Angle=\" + pos + \"&\");");
                client.println("} </script>");

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
                client.println("sendThrottled('Power', \"/?Power=\" + pos + \"&\");");
                client.println("} </script>");

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
                client.println("sendThrottled('SpeedCurve', \"/?SpeedCurve=\" + pos + \"&\");");
                client.println("} </script>");

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
                client.println("sendThrottled('StaSsid', \"/?StaSsid=\" + encodeURIComponent(val) + \"&\");");
                client.println("} </script>");

                client.println("<p><h3>Home WiFi Password (Station mode)</h3>");
                client.println("<input type=\"password\" id=\"StaPassInput\" class=\"textbox\" oninput=\"StaPassChange(this.value)\" value=\"" + STA_PASSWORD + "\" /></p>");
                client.println("<script> function StaPassChange(val) { ");
                client.println("sendThrottled('StaPass', \"/?StaPass=\" + encodeURIComponent(val) + \"&\");");
                client.println("} </script>");

                // Joystick Mode channel mapping (which channel each control drives) -----
                client.println("<p><h3>Joystick Steer -> Channel</h3>");
                for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
                {
                  String activeClass = (ch == JOYSTICK_X_CHANNEL) ? "buttonActive" : "button3";
                  client.println("<a href=\"/?JoyX=" + String(ch) + "&\"><button style=\"width:18%;display:inline-block;\" class=\"button " + activeClass + "\">CH" + String(ch + 1) + "</button></a>");
                }
                client.println("</p>");

                client.println("<p><h3>Joystick Throttle -> Channel</h3>");
                for (uint8_t ch = 0; ch < NUM_SERVO_CHANNELS; ch++)
                {
                  String activeClass = (ch == JOYSTICK_Y_CHANNEL) ? "buttonActive" : "button3";
                  client.println("<a href=\"/?JoyY=" + String(ch) + "&\"><button style=\"width:18%;display:inline-block;\" class=\"button " + activeClass + "\">CH" + String(ch + 1) + "</button></a>");
                }
                client.println("</p>");

                client.println("<p><a href=\"/save/on\"><button class=\"button button1\">Save</button></a></p>");
                client.println("<p><a href=\"/factoryreset/on\" onclick=\"return confirm('Reset all settings to factory defaults?');\"><button class=\"button button2\">Factory Reset</button></a></p>");
                client.println("<p><a href=\"/back/on\"><button class=\"button button2\">Menu</button></a></p>");
                break;
              }

              default:
                client.println("<h2>Menu</h2>");
                client.println("<p><a href=\"/10/on\"><button class=\"button button1\">Manual Mode</button></a></p>");
                client.println("<p><a href=\"/20/on\"><button class=\"button button1\">Sweep Mode</button></a></p>");
                client.println("<p><a href=\"/expert/on\"><button class=\"button button1\">Expert Functions</button></a></p>");
                client.println("<p><a href=\"/joystick/on\"><button class=\"button button1\">Joystick Mode (mobile)</button></a></p>");
                client.println("<p><a href=\"/80/on\"><button class=\"button button1\">Info</button></a></p>");
                client.println("<p><a href=\"/120/on\"><button class=\"button button1\">Settings</button></a></p>");
                break; // Not needed when statement(s) are present
              }

              // Shown on every page except Joystick Mode - source/copies for anyone who finds this
              // device, but a full-screen touch page has no room for it
              if (!webJoystickMode)
              {
                client.println("<p style=\"margin-top:20px;\"><a href=\"https://github.com/MichielBruijn/esp32-servo-tester\">github.com/MichielBruijn/esp32-servo-tester</a></p>");
              }

              client.println("</body></html>");
              } // if (!xhrOnly)

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
