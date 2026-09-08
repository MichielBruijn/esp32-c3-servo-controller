// Firmware update via GitHub Releases -----------------------------------------------------------
// Checks the repo's "latest" release for a newer version than the running one, and can download +
// flash that release's "firmware.bin" asset onto the OTA partition. Requires Station mode with a
// working internet connection - there's nothing to check against from the device's own Access Point.
//
// TLS note: uses WiFiClientSecure::setInsecure() (no certificate validation) rather than pinning
// GitHub's certificate, since a pinned cert breaks silently whenever GitHub rotates it and the
// ESP32 has no reliable way to fetch a fresh CA bundle on its own. Acceptable trade-off for a
// hobby project checking its own public repo; not the choice to make for anything security-sensitive.

const char *GITHUB_RELEASES_API_URL = "https://api.github.com/repos/MichielBruijn/esp32-servo-tester/releases/latest";
const char *FIRMWARE_ASSET_NAME = "firmware.bin";

// Extracts the string value of a "key":"value" pair from raw JSON text, starting the search at
// searchFrom. No JSON library needed for two flat string fields.
String jsonStringField(const String &json, const char *key, int searchFrom = 0)
{
  String needle = String("\"") + key + "\":\"";
  int keyIdx = json.indexOf(needle, searchFrom);
  if (keyIdx < 0)
    return "";
  int valueStart = keyIdx + needle.length();
  int valueEnd = json.indexOf('"', valueStart);
  if (valueEnd < 0)
    return "";
  return json.substring(valueStart, valueEnd);
}

void checkForFirmwareUpdate()
{
  if (WIFI_MODE != WIFI_STATION_MODE || wifiStaFallback || WiFi.status() != WL_CONNECTED)
    return; // No internet to check against (own Access Point, or Station not actually connected)

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setUserAgent("esp32-servo-tester"); // GitHub's API rejects requests with no User-Agent

  if (!https.begin(client, GITHUB_RELEASES_API_URL))
  {
    Serial.println("Update check: could not begin HTTPS connection");
    return;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("Update check: GitHub API returned HTTP %d\n", httpCode);
    https.end();
    return;
  }

  String payload = https.getString();
  https.end();

  String tag = jsonStringField(payload, "tag_name");
  if (tag.length() == 0)
  {
    Serial.println("Update check: no tag_name in release response");
    return;
  }
  if (tag.startsWith("v") || tag.startsWith("V"))
  {
    tag = tag.substring(1);
  }

  // Find the firmware.bin asset specifically (a release also always has auto-generated
  // source zip/tarball "assets" that aren't real assets, and might have other files attached)
  int nameIdx = payload.indexOf(String("\"name\":\"") + FIRMWARE_ASSET_NAME + "\"");
  String downloadUrl = (nameIdx >= 0) ? jsonStringField(payload, "browser_download_url", nameIdx) : "";

  if (downloadUrl.length() == 0)
  {
    Serial.println("Update check: release has no firmware.bin asset");
    return;
  }

  if (atof(tag.c_str()) > atof(codeVersion))
  {
    updateAvailable = true;
    latestFirmwareVersion = tag;
    latestFirmwareUrl = downloadUrl;
    Serial.println("Update available: v" + tag);
  }
  else
  {
    updateAvailable = false;
  }
}

// Downloads latestFirmwareUrl and writes it to the OTA partition. Blocks for the duration of the
// download (typically single-digit seconds on a home network). Restarts the device on success;
// on failure, leaves the running firmware untouched and sets updateErrorMessage.
bool installFirmwareUpdate()
{
  if (latestFirmwareUrl.length() == 0 || updateInProgress)
    return false;

  updateInProgress = true;

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 25, "Updating...");
  display.drawString(64, 45, "Do not power off");
  display.display();

  // GitHub release assets redirect (302) to a signed objects.githubusercontent.com URL - a
  // different host. HTTPClient's own setFollowRedirects() reuses the same underlying
  // connection across that redirect if the old one is still open, which sends the follow-up
  // request to the wrong host and hangs waiting for a response that never comes. Resolving the
  // redirect manually with client.stop()+reuse of one WiFiClientSecure instance turned out to
  // have the same problem one level down: the socket's internal state doesn't fully reset,
  // and the next begin() calls setsockopt() on a stale/invalid file descriptor (observed as
  // "setSocketOption(): fail on 0, errno: 9, Bad file number" on serial, followed by a hang).
  // So `client` and `https` are declared *inside* the loop body instead - a genuinely new
  // object per hop, destroyed and rebuilt by the language itself on each iteration, never
  // reused across a host change. The actual download+flash happens inline within the loop at
  // the point the real (non-redirect) response is found, so those fresh objects and the
  // stream they own stay alive for exactly as long as they're needed.
  String url = latestFirmwareUrl;
  const char *locationHeader[] = {"Location"};

  for (int hop = 0; hop < 5; hop++)
  {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);
    HTTPClient https;
    https.setUserAgent("esp32-servo-tester");
    https.setConnectTimeout(15000);
    https.setTimeout(15000);
    https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    https.collectHeaders(locationHeader, 1);

    if (!https.begin(client, url))
    {
      updateErrorMessage = "Could not connect";
      Serial.println("Firmware update: " + updateErrorMessage);
      updateInProgress = false;
      return false;
    }

    int httpCode = https.GET();

    if (httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND ||
        httpCode == HTTP_CODE_SEE_OTHER || httpCode == HTTP_CODE_TEMPORARY_REDIRECT ||
        httpCode == 308 /* Permanent Redirect */)
    {
      String location = https.header("Location");
      https.end();
      if (location.length() == 0)
      {
        updateErrorMessage = "Redirect with no Location header";
        Serial.println("Firmware update: " + updateErrorMessage);
        updateInProgress = false;
        return false;
      }
      url = location;
      continue; // client/https are destroyed here; next iteration builds brand new ones
    }

    if (httpCode != HTTP_CODE_OK)
    {
      updateErrorMessage = "Download failed (HTTP " + String(httpCode) + ")";
      Serial.println("Firmware update: " + updateErrorMessage);
      https.end();
      updateInProgress = false;
      return false;
    }

    int contentLength = https.getSize();
    if (contentLength <= 0)
    {
      updateErrorMessage = "Unknown download size";
      Serial.println("Firmware update: " + updateErrorMessage);
      https.end();
      updateInProgress = false;
      return false;
    }

    if (!Update.begin(contentLength))
    {
      updateErrorMessage = "Not enough OTA space";
      Serial.println("Firmware update: " + updateErrorMessage);
      https.end();
      updateInProgress = false;
      return false;
    }

    WiFiClient *stream = https.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    bool writtenOk = (written == (size_t)contentLength);
    bool endOk = Update.end();
    bool finishedOk = Update.isFinished();
    https.end();

    if (!writtenOk || !endOk || !finishedOk)
    {
      updateErrorMessage = "Write failed: " + String(written) + "/" + String(contentLength) +
                            " bytes, end=" + String(endOk) + ", " + Update.errorString();
      Serial.println("Firmware update: " + updateErrorMessage);
      Update.abort();
      updateInProgress = false;
      return false;
    }

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 25, "Update complete");
    display.drawString(64, 45, "Restarting...");
    display.display();
    delay(1500);
    ESP.restart();
    return true; // Unreachable, but keeps the compiler happy about all paths returning
  }

  updateErrorMessage = "Too many redirects";
  Serial.println("Firmware update: " + updateErrorMessage);
  updateInProgress = false;
  return false;
}
