#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstdarg>

namespace {

constexpr uint8_t TOUCH_PIN = T4;   // T4 is GPIO4 on the ESP32-S2.
constexpr uint8_t LED_PIN = 16;     // Keep GPIO19/20 free for native USB.
constexpr uint16_t LED_COUNT = 24;  // Three daisy-chained blocks of eight.

constexpr char AP_NAME[] = "LamparaCorazon";
constexpr char AP_PASSWORD[] = "corazon24";
constexpr char ADMIN_USER[] = "admin";
constexpr char ADMIN_PASSWORD[] = "corazon32";

constexpr uint8_t BRIGHTNESS_LEVELS[] = {0, 24, 64, 128, 192, 255};
constexpr size_t LEVEL_COUNT =
    sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);
constexpr uint32_t CALIBRATION_MS = 2000;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 45;
constexpr uint32_t RELEASE_DEBOUNCE_MS = 100;
constexpr uint32_t TOUCH_REPORT_MS = 500;
constexpr size_t LOG_LINES = 40;

Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
DNSServer dnsServer;
WebServer server(80);
Preferences preferences;

uint32_t baseline = 0;
size_t brightnessLevel = 0;
uint8_t lampRed = 255;
uint8_t lampGreen = 135;
uint8_t lampBlue = 35;
bool touchLatched = false;
uint32_t candidateSince = 0;
uint32_t lastTouchReport = 0;
String logLines[LOG_LINES];
size_t nextLogLine = 0;
size_t storedLogLines = 0;

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lampara Corazon</title>
<style>
body{font:16px system-ui;background:#17131a;color:#f8eefa;max-width:760px;margin:auto;padding:20px}
.card{background:#29222d;border-radius:14px;padding:18px;margin:14px 0}
button,input{font:inherit;padding:10px;margin:5px;border-radius:8px;border:1px solid #745f7c}
button{background:#df4e78;color:white;cursor:pointer}input[type=range]{width:70%}
pre{background:#0e0c10;padding:12px;overflow:auto;min-height:190px;white-space:pre-wrap}
.ok{color:#7ee09b}.muted{color:#beaebe}a{color:#ff9db9}
</style></head><body>
<h1>Lampara Corazon</h1>
<div class=card>
 <div id=status>Connecting...</div>
 <p>Brightness <b id=bv>0</b></p>
 <input id=b type=range min=0 max=255 value=0 oninput="bv.textContent=this.value">
 <button onclick="setBrightness()">Apply</button>
 <p>Color <input id=c type=color value="#ff8723">
 <button onclick="setColor()">Apply</button></p>
</div>
<div class=card>
 <h2>Capacitive sensor and logs</h2>
 <pre id=logs>Waiting for data...</pre>
</div>
<div class=card>
 <h2>Home Wi-Fi</h2>
 <p class=muted>The lamp access point always remains available at 192.168.4.1.</p>
 <form method=post action=/wifi>
  <input name=ssid placeholder="Wi-Fi name" required>
  <input name=password type=password placeholder="Wi-Fi password">
  <button>Save and connect</button>
 </form>
</div>
<div class=card>
 <h2>Firmware update</h2>
 <p>Upload PlatformIO's <code>.pio/build/lolin_s2_mini/firmware.bin</code>.</p>
 <form method=post action=/update enctype=multipart/form-data>
  <input type=file name=firmware accept=.bin required><button>Upload OTA</button>
 </form>
 <p class=muted>Wi-Fi and OTA ask for admin / corazon32.</p>
</div>
<script>
async function refresh(){
 try{
  let s=await (await fetch('/api/status')).json();
  status.innerHTML=`AP: <span class=ok>${s.ap_ip}</span> | Wi-Fi: ${s.sta}`;
  b.value=s.brightness;bv.textContent=s.brightness;c.value=s.color;
  logs.textContent=await (await fetch('/api/logs')).text();
 }catch(e){status.textContent='Connection lost';}
}
async function setBrightness(){await fetch('/api/brightness',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'value='+b.value});refresh()}
async function setColor(){await fetch('/api/color',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'value='+c.value.slice(1)});refresh()}
refresh();setInterval(refresh,1000);
</script></body></html>
)HTML";

void addLog(const char* format, ...) {
  char message[180];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  const String line = "[" + String(millis() / 1000.0F, 1) + "s] " + message;
  Serial.println(line);
  logLines[nextLogLine] = line;
  nextLogLine = (nextLogLine + 1) % LOG_LINES;
  if (storedLogLines < LOG_LINES) {
    ++storedLogLines;
  }
}

uint32_t readTouchAverage() {
  uint64_t sum = 0;
  constexpr uint8_t SAMPLE_COUNT = 8;
  for (uint8_t sample = 0; sample < SAMPLE_COUNT; ++sample) {
    sum += touchRead(TOUCH_PIN);
    delayMicroseconds(500);
  }
  return static_cast<uint32_t>(sum / SAMPLE_COUNT);
}

void showLamp() {
  pixels.setBrightness(BRIGHTNESS_LEVELS[brightnessLevel]);
  pixels.fill(pixels.Color(lampRed, lampGreen, lampBlue));
  pixels.show();
}

void setBrightness(uint8_t requested) {
  size_t closest = 0;
  uint16_t smallestDifference = 256;
  for (size_t level = 0; level < LEVEL_COUNT; ++level) {
    const uint16_t difference =
        abs(static_cast<int>(requested) - BRIGHTNESS_LEVELS[level]);
    if (difference < smallestDifference) {
      smallestDifference = difference;
      closest = level;
    }
  }
  brightnessLevel = closest;
  showLamp();
  addLog("Brightness set to %u/255", BRIGHTNESS_LEVELS[brightnessLevel]);
}

void calibrateTouch() {
  addLog("Touch calibration: do not touch the aluminum plate");
  uint64_t sum = 0;
  uint32_t samples = 0;
  const uint32_t start = millis();
  while (millis() - start < CALIBRATION_MS) {
    sum += touchRead(TOUCH_PIN);
    ++samples;
    delay(10);
  }
  baseline = static_cast<uint32_t>(sum / samples);
  addLog("Touch baseline=%lu", static_cast<unsigned long>(baseline));
}

bool requireAdmin() {
  if (server.authenticate(ADMIN_USER, ADMIN_PASSWORD)) {
    return true;
  }
  server.requestAuthentication();
  return false;
}

void sendDashboard() {
  server.send_P(200, "text/html", DASHBOARD_HTML);
}

void configureWebServer() {
  server.on("/", HTTP_GET, sendDashboard);

  server.on("/api/status", HTTP_GET, [] {
    const String station =
        WiFi.status() == WL_CONNECTED
            ? "\"" + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")\""
            : "\"not connected\"";
    char color[8];
    snprintf(color, sizeof(color), "#%02x%02x%02x", lampRed, lampGreen,
             lampBlue);
    const String json =
        "{\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",\"sta\":" +
        station + ",\"brightness\":" +
        String(BRIGHTNESS_LEVELS[brightnessLevel]) + ",\"color\":\"" + color +
        "\"}";
    server.send(200, "application/json", json);
  });

  server.on("/api/logs", HTTP_GET, [] {
    String output;
    output.reserve(4000);
    const size_t first =
        storedLogLines == LOG_LINES ? nextLogLine : 0;
    for (size_t index = 0; index < storedLogLines; ++index) {
      output += logLines[(first + index) % LOG_LINES] + '\n';
    }
    server.send(200, "text/plain; charset=utf-8", output);
  });

  server.on("/api/brightness", HTTP_POST, [] {
    if (!server.hasArg("value")) {
      server.send(400, "text/plain", "Missing brightness");
      return;
    }
    setBrightness(constrain(server.arg("value").toInt(), 0, 255));
    server.send(204);
  });

  server.on("/api/color", HTTP_POST, [] {
    const String value = server.arg("value");
    if (value.length() != 6) {
      server.send(400, "text/plain", "Expected RRGGBB");
      return;
    }
    const uint32_t color = strtoul(value.c_str(), nullptr, 16);
    lampRed = color >> 16;
    lampGreen = color >> 8;
    lampBlue = color;
    showLamp();
    addLog("Color set to #%s", value.c_str());
    server.send(204);
  });

  server.on("/wifi", HTTP_POST, [] {
    if (!requireAdmin()) {
      return;
    }
    const String ssid = server.arg("ssid");
    if (ssid.isEmpty()) {
      server.send(400, "text/plain", "Wi-Fi name is required");
      return;
    }
    preferences.putString("ssid", ssid);
    preferences.putString("pass", server.arg("password"));
    addLog("Saved Wi-Fi network '%s'; connecting", ssid.c_str());
    WiFi.begin(ssid.c_str(), server.arg("password").c_str());
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on(
      "/update", HTTP_POST,
      [] {
        if (!requireAdmin()) {
          return;
        }
        const bool success = !Update.hasError();
        server.send(200, "text/plain",
                    success ? "Update complete. Lamp is restarting..."
                            : "Update failed. Check the logs.");
        if (success) {
          delay(500);
          ESP.restart();
        }
      },
      [] {
        if (!server.authenticate(ADMIN_USER, ADMIN_PASSWORD)) {
          return;
        }
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
          addLog("OTA started: %s", upload.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) !=
              upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) {
            addLog("OTA complete: %u bytes", upload.totalSize);
          } else {
            Update.printError(Serial);
            addLog("OTA failed, error=%u", Update.getError());
          }
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          Update.abort();
          addLog("OTA upload aborted");
        }
      });

  // Common captive-portal probes. All roads lead to the dashboard.
  server.on("/generate_204", HTTP_ANY, sendDashboard);
  server.on("/hotspot-detect.html", HTTP_ANY, sendDashboard);
  server.on("/connecttest.txt", HTTP_ANY, sendDashboard);
  server.on("/ncsi.txt", HTTP_ANY, sendDashboard);
  server.onNotFound(sendDashboard);
  server.begin();
}

void startNetwork() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname("lampara-corazon");
  if (WiFi.softAP(AP_NAME, AP_PASSWORD)) {
    addLog("Access point '%s' ready at %s", AP_NAME,
           WiFi.softAPIP().toString().c_str());
  } else {
    addLog("ERROR: could not start access point");
  }
  dnsServer.start(53, "*", WiFi.softAPIP());

  const String ssid = preferences.getString("ssid", "");
  if (!ssid.isEmpty()) {
    addLog("Connecting to saved Wi-Fi '%s'", ssid.c_str());
    WiFi.begin(ssid.c_str(), preferences.getString("pass", "").c_str());
  } else {
    addLog("No home Wi-Fi saved; use the web dashboard");
  }
  configureWebServer();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);
  addLog("Booting Lampara Corazon firmware");

  pixels.begin();
  pixels.clear();
  pixels.show();
  preferences.begin("lamp", false);

  calibrateTouch();
  showLamp();
  startNetwork();
  addLog("Ready: tap the plate or open http://192.168.4.1");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  const uint32_t now = millis();
  const uint32_t reading = readTouchAverage();
  const uint32_t pressThreshold = baseline + max<uint32_t>(baseline / 5, 100);
  const uint32_t releaseThreshold = baseline + max<uint32_t>(baseline / 10, 50);
  const bool candidate = touchLatched ? reading > releaseThreshold
                                      : reading > pressThreshold;

  if (candidate != touchLatched) {
    if (candidateSince == 0) {
      candidateSince = now;
    }
    const uint32_t debounce =
        candidate ? TOUCH_DEBOUNCE_MS : RELEASE_DEBOUNCE_MS;
    if (now - candidateSince >= debounce) {
      touchLatched = candidate;
      candidateSince = 0;
      if (touchLatched) {
        brightnessLevel = (brightnessLevel + 1) % LEVEL_COUNT;
        showLamp();
        addLog("TOUCH reading=%lu baseline=%lu -> brightness=%u",
               static_cast<unsigned long>(reading),
               static_cast<unsigned long>(baseline),
               BRIGHTNESS_LEVELS[brightnessLevel]);
      } else {
        addLog("Touch released reading=%lu",
               static_cast<unsigned long>(reading));
      }
    }
  } else {
    candidateSince = 0;
  }

  if (!touchLatched) {
    baseline = (baseline * 255UL + reading) / 256UL;
  }

  if (now - lastTouchReport >= TOUCH_REPORT_MS) {
    lastTouchReport = now;
    addLog("CAP raw=%lu baseline=%lu press=%lu state=%s",
           static_cast<unsigned long>(reading),
           static_cast<unsigned long>(baseline),
           static_cast<unsigned long>(pressThreshold),
           touchLatched ? "TOUCHED" : "released");
  }
}
