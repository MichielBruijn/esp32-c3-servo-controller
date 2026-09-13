// Firmware update via GitHub Releases -----------------------------------------------------------
// Checks the repo's "latest" release for a newer version than the running one, and can download +
// flash that release's "firmware.bin" asset onto the OTA partition. Requires Station mode with a
// working internet connection - there's nothing to check against from the device's own Access Point.
//
// TLS note: uses WiFiClientSecure::setInsecure() (no certificate validation) rather than pinning
// GitHub's certificate, since a pinned cert breaks silently whenever GitHub rotates it and the
// ESP32 has no reliable way to fetch a fresh CA bundle on its own. Acceptable trade-off for a
// hobby project checking its own public repo; not the choice to make for anything security-sensitive.

#include <esp_ota_ops.h> // for esp_ota_get_running_partition() - reading back the current firmware to offer as a download
#include <esp_partition.h>

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

// Shared between installFirmwareUpdate() (GitHub download) and uploadCurrentFirmware() (manual,
// no-internet upload) below - both block the main loop for their whole duration, so both need
// the same "silence the buzzer up front" and "show progress/completion" handling.

void beginFirmwareWrite()
{
  updateInProgress = true;

  // Silence any click-beep immediately - beep() (which normally turns it back off after
  // beepDuration ms) only runs from loop(), which this function blocks for its whole duration,
  // so without this the beep that fired on the button press triggering this would otherwise
  // ring continuously for as long as the update takes.
  ledcWrite(BUZZER_LEDC_CHANNEL, 0);
  beepDuration = 0;

  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 25, "Updating...");
  display.drawString(64, 45, "Do not power off");
  display.display();
}

void showFirmwareWriteProgress(size_t written, size_t total)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 15, "Updating...");
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 35, String((written * 100) / total) + "%");
  display.display();
}

void showFirmwareWriteCompleteAndRestart()
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 25, "Update complete");
  display.drawString(64, 45, "Restarting...");
  display.display();
  delay(1500);
  ESP.restart();
}

// Downloads latestFirmwareUrl and writes it to the OTA partition. Blocks for the duration of the
// download (typically single-digit seconds on a home network). Restarts the device on success;
// on failure, leaves the running firmware untouched and sets updateErrorMessage.
bool installFirmwareUpdate()
{
  if (latestFirmwareUrl.length() == 0 || updateInProgress)
    return false;

  beginFirmwareWrite();

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
    client.setTimeout(15); // WiFiClient::setTimeout() takes SECONDS, unlike HTTPClient's own (ms) setTimeout() below
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

    Update.onProgress(showFirmwareWriteProgress);

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

    showFirmwareWriteCompleteAndRestart();
    return true; // Unreachable, but keeps the compiler happy about all paths returning
  }

  updateErrorMessage = "Too many redirects";
  Serial.println("Firmware update: " + updateErrorMessage);
  updateInProgress = false;
  return false;
}

// Streams the currently-running firmware image as a downloadable .bin - so it can be copied onto
// another device (see uploadCurrentFirmware() below) without either device needing internet
// access. ESP.getSketchSize() gives the actual image size (not the whole, larger OTA partition),
// so the download is byte-exact with what `pio run` originally produced.
void sendRunningFirmwareAsDownload(WiFiClient &client)
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  size_t size = ESP.getSketchSize();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: application/octet-stream");
  client.println("Content-Disposition: attachment; filename=\"servotester-v" + String(codeVersion) + ".bin\"");
  client.println("Content-Length: " + String(size));
  client.println("Connection: close");
  client.println();

  uint8_t buf[1024];
  size_t offset = 0;
  unsigned long lastProgressMillis = millis();
  while (offset < size)
  {
    size_t toRead = min(sizeof(buf), size - offset);
    if (esp_partition_read(running, offset, buf, toRead) != ESP_OK)
      break;
    // client.write() returning 0 usually just means the TCP send buffer is momentarily full
    // (completely normal over WiFi) - treating that as "connection dropped" and bailing out
    // immediately is what made this download reliably cut off partway (~100+KB in) on a real
    // network. Only give up once the client has genuinely disconnected, or 0 bytes have gone
    // out for a full 15s straight - a real, sustained stall, not a momentary hiccup.
    size_t sent = 0;
    while (sent < toRead)
    {
      size_t n = client.write(buf + sent, toRead - sent);
      if (n > 0)
      {
        sent += n;
        lastProgressMillis = millis();
      }
      else
      {
        if (!client.connected() || millis() - lastProgressMillis > 15000)
          return;
        delay(2);
      }
    }
    offset += toRead;
  }
}

// Flashes a firmware.bin uploaded directly from a browser (e.g. one downloaded from another
// device above) - no internet or GitHub release involved. The upload page posts the raw file
// as the POST body (not a multipart form), so this just reads exactly contentLength raw bytes
// off the socket - the same idea as installFirmwareUpdate() above, but the bytes come from the
// browser instead of a GitHub download.
bool uploadCurrentFirmware(WiFiClient &client, size_t contentLength)
{
  if (contentLength == 0 || updateInProgress)
    return false;

  beginFirmwareWrite();

  if (!Update.begin(contentLength))
  {
    updateErrorMessage = "Not enough OTA space";
    Serial.println("Firmware update: " + updateErrorMessage);
    updateInProgress = false;
    return false;
  }

  Update.onProgress(showFirmwareWriteProgress);

  size_t received = 0;
  uint8_t buf[1024];
  unsigned long lastProgressMillis = millis();
  while (received < contentLength)
  {
    if (client.available())
    {
      int n = client.read(buf, min(sizeof(buf), contentLength - received));
      if (n > 0)
      {
        Update.write(buf, n);
        received += n;
        lastProgressMillis = millis();
      }
    }
    else if (millis() - lastProgressMillis > 15000)
    {
      updateErrorMessage = "Upload stalled at " + String(received) + "/" + String(contentLength) + " bytes";
      Serial.println("Firmware update: " + updateErrorMessage);
      Update.abort();
      updateInProgress = false;
      return false;
    }
  }

  bool endOk = Update.end();
  bool finishedOk = Update.isFinished();

  if (!endOk || !finishedOk)
  {
    updateErrorMessage = "Write failed: " + String(received) + "/" + String(contentLength) +
                          " bytes, end=" + String(endOk) + ", " + Update.errorString();
    Serial.println("Firmware update: " + updateErrorMessage);
    Update.abort();
    updateInProgress = false;
    return false;
  }

  showFirmwareWriteCompleteAndRestart();
  return true; // Unreachable, but keeps the compiler happy about all paths returning
}
