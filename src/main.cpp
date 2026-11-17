#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// MR-ADV network prototype. Credentials/client key live in NVS, never in source.
static Preferences prefs;
static WebSocketsClient ssap;
static WebSocketsClient pointerWs;
static String tvHost, tvMac, ssid, wifiPass, clientKey, pointerPath;
static bool ssapConnected = false, registered = false, pointerReady = false;
static bool pointerEnabled = true;
static bool pointerLastSendOk = false;
static bool muted = false;
static bool helpPage = false;
static int volumeLevel = -1;
static uint32_t lastMotionMs = 0;
static uint32_t lastWifiLogMs = 0;
static float filteredGx = 0.0f, filteredGz = 0.0f;
static float motionRemainderX = 0.0f, motionRemainderY = 0.0f;
static constexpr float GYRO_DEADZONE_DPS = 3.0f;
static constexpr float FILTER_TIME_CONSTANT_S = 0.080f;
static constexpr float PIXELS_PER_DEGREE = 13.22f; // 3.3% above 12.8; 10.2% above baseline
static constexpr uint32_t MOTION_INTERVAL_MS = 20; // 50 Hz target

static void drawStatus(const char *line2 = "") {
  auto &d = M5Cardputer.Display;
  const uint16_t bg = 0x0841, panel = 0x10A2, accent = 0x2DDF, mutedColor = 0x94B2;
  d.fillScreen(bg); d.setTextSize(1);
  d.fillRoundRect(4, 3, 232, 21, 4, panel);
  d.setTextColor(TFT_WHITE, panel); d.setCursor(10, 10); d.print("MR-ADV");
  d.setTextColor(mutedColor, panel); d.setCursor(73, 10); d.print("LG C1");
  bool tvOnline = ssapConnected && registered;
  d.fillCircle(174, 13, 3, tvOnline ? TFT_GREEN : TFT_ORANGE);
  d.setTextColor(tvOnline ? TFT_GREEN : TFT_ORANGE, panel);
  d.setCursor(181, 10); d.print(tvOnline ? "TV ON" : "WAKE");

  if (helpPage) {
    d.setTextColor(accent, bg); d.setCursor(10, 31); d.print("REMOTE SHORTCUTS");
    d.setTextColor(TFT_WHITE, bg);
    d.setCursor(10, 46); d.print("Fn + ; , . /   navigate");
    d.setCursor(10, 59); d.print("Enter           OK / click");
    d.setCursor(10, 72); d.print("P               pointer on/off");
    d.setCursor(10, 85); d.print("H / B           Home / Back");
    d.setCursor(10, 98); d.print("0               power / wake");
    d.setCursor(10, 111); d.print("- / = / M       volume / mute");
    d.setTextColor(mutedColor, bg); d.setCursor(10, 124); d.print("Tab: return to remote");
    return;
  }

  d.setTextColor(mutedColor, bg); d.setCursor(9, 30);
  d.printf("WiFi %s  SSAP %s", WiFi.isConnected() ? "OK" : "OFF", registered ? "OK" : "--");
  d.setCursor(116, 30); d.printf("VOL %s", volumeLevel >= 0 ? String(volumeLevel).c_str() : "--");
  d.setCursor(181, 30); d.setTextColor(pointerEnabled ? accent : mutedColor, bg);
  d.printf("AIR %s", pointerEnabled ? "ON" : "OFF");

  // D-pad diagram mirrors the Fn + punctuation cluster on the keyboard.
  const uint16_t keyFill = 0x18E3;
  d.fillRoundRect(42, 42, 28, 19, 3, keyFill); d.setTextColor(TFT_WHITE, keyFill); d.setCursor(53, 48); d.print("^");
  d.fillRoundRect(12, 63, 28, 19, 3, keyFill); d.setCursor(22, 69); d.print("<");
  d.fillRoundRect(42, 63, 28, 19, 3, accent); d.setTextColor(bg, accent); d.setCursor(48, 69); d.print("OK");
  d.fillRoundRect(72, 63, 28, 19, 3, keyFill); d.setTextColor(TFT_WHITE, keyFill); d.setCursor(82, 69); d.print(">");
  d.fillRoundRect(42, 84, 28, 19, 3, keyFill); d.setCursor(53, 90); d.print("v");

  d.fillRoundRect(112, 42, 124, 21, 4, 0xB8A0);
  d.setTextColor(TFT_WHITE, 0xB8A0); d.setCursor(121, 49); d.print("0  POWER / WAKE");
  d.fillRoundRect(112, 67, 58, 20, 4, keyFill);
  d.fillRoundRect(178, 67, 58, 20, 4, keyFill);
  d.setTextColor(TFT_WHITE, keyFill); d.setCursor(126, 73); d.print("- VOL"); d.setCursor(192, 73); d.print("= VOL+");
  d.fillRoundRect(112, 91, 58, 20, 4, keyFill);
  d.fillRoundRect(178, 91, 58, 20, 4, pointerEnabled ? 0x16A5 : 0x39E7);
  d.setTextColor(TFT_WHITE, keyFill); d.setCursor(121, 97); d.printf("M %s", muted ? "MUTED" : "MUTE");
  d.setTextColor(TFT_WHITE, pointerEnabled ? 0x16A5 : 0x39E7); d.setCursor(185, 97); d.printf("P AIR %s", pointerEnabled ? "ON" : "OFF");

  d.drawFastHLine(8, 116, 224, 0x39E7);
  d.setTextColor(TFT_WHITE, bg); d.setCursor(9, 121); d.print("Fn arrows  Enter OK  H Home  Tab ?");
  if (line2 && line2[0]) { d.fillRect(8, 116, 224, 12, bg); d.setTextColor(accent, bg); d.setCursor(9, 119); d.print(line2); }
}

static const char *wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "idle";
    case WL_NO_SSID_AVAIL: return "ssid-not-found";
    case WL_SCAN_COMPLETED: return "scan-complete";
    case WL_CONNECTED: return "connected";
    case WL_CONNECT_FAILED: return "auth-failed";
    case WL_CONNECTION_LOST: return "connection-lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "unknown";
  }
}

static void reportWifiStatus() {
  wl_status_t status = WiFi.status();
  Serial.printf("WiFi state=%s(%d) configured=%s", wifiStatusName(status), (int)status, ssid.c_str());
  if (status == WL_CONNECTED) {
    Serial.printf(" connected=%s ip=%s rssi=%d", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  Serial.println();
}

static void probeTvPorts() {
  Serial.printf("Probing TV %s\n", tvHost.c_str());
  for (uint16_t port : {3000, 3001}) {
    WiFiClient probe;
    probe.setTimeout(2500);
    bool reachable = probe.connect(tvHost.c_str(), port);
    Serial.printf("TV TCP %u: %s\n", port, reachable ? "reachable" : "unreachable");
    probe.stop();
  }
}

static bool sendSsap(const char *uri, const char *id, JsonDocument &payload) {
  if (!registered || !ssapConnected) return false;
  JsonDocument doc;
  doc["type"] = "request"; doc["id"] = id; doc["uri"] = uri;
  doc["payload"] = payload;
  String out; serializeJson(doc, out);
  return ssap.sendTXT(out);
}

static void requestVolumeStatus() {
  JsonDocument empty;
  sendSsap("ssap://audio/getVolume", "get-volume", empty);
}

static void sendVolumeStep(bool up) {
  JsonDocument empty;
  bool sent = sendSsap(up ? "ssap://audio/volumeUp" : "ssap://audio/volumeDown",
                       up ? "volume-up" : "volume-down", empty);
  if (sent) requestVolumeStatus();
  drawStatus(sent ? (up ? "Volume up" : "Volume down") : "TV not connected");
}

static void toggleMute() {
  JsonDocument payload;
  payload["mute"] = !muted;
  bool sent = sendSsap("ssap://audio/setMute", "set-mute", payload);
  if (sent) muted = !muted;
  drawStatus(sent ? (muted ? "Muted" : "Sound on") : "TV not connected");
}

static bool parseTvMac(uint8_t mac[6]) {
  unsigned int bytes[6];
  if (sscanf(tvMac.c_str(), "%x:%x:%x:%x:%x:%x", &bytes[0], &bytes[1], &bytes[2],
             &bytes[3], &bytes[4], &bytes[5]) != 6) return false;
  for (int i = 0; i < 6; ++i) mac[i] = (uint8_t)bytes[i];
  return true;
}

static void sendWakeOnLan() {
  if (!WiFi.isConnected()) { drawStatus("Wi-Fi unavailable; cannot wake"); return; }
  uint8_t mac[6];
  if (!parseTvMac(mac)) { drawStatus("Set valid TVMAC over serial"); return; }
  uint8_t packet[102];
  memset(packet, 0xFF, 6);
  for (int i = 0; i < 16; ++i) memcpy(packet + 6 + i * 6, mac, 6);
  WiFiUDP udp;
  if (!udp.begin(0)) { drawStatus("Wake packet setup failed"); return; }
  bool sent = false;
  for (int i = 0; i < 3; ++i) {
    if (udp.beginPacket(WiFi.broadcastIP(), 9)) {
      udp.write(packet, sizeof(packet));
      sent |= udp.endPacket() == 1;
    }
    delay(100);
  }
  udp.stop();
  drawStatus(sent ? "Wake sent; check TV LAN wake" : "Wake packet failed");
}

static void togglePower() {
  if (ssapConnected && registered) {
    JsonDocument empty;
    bool sent = sendSsap("ssap://system/turnOff", "power-off", empty);
    drawStatus(sent ? "Standby requested" : "Standby command failed");
  } else {
    sendWakeOnLan();
  }
}

static void askForPointerSocket() {
  JsonDocument empty;
  sendSsap("ssap://com.webos.service.networkinput/getPointerInputSocket", "get-pointer", empty);
}

static void sendHello() {
  JsonDocument doc; doc["type"] = "hello"; doc["id"] = "hello";
  JsonObject p = doc["payload"].to<JsonObject>();
  p["sdkVersion"] = "1.0"; p["deviceModel"] = "Cardputer-ADV";
  p["OSVersion"] = "ESP32"; p["resolution"] = "240x135";
  p["appId"] = "local.mr-adv"; p["appName"] = "MR-ADV"; p["appRegion"] = "US";
  String out; serializeJson(doc, out); ssap.sendTXT(out);
}

static void sendRegistration() {
  JsonDocument doc; doc["type"] = "register"; doc["id"] = "register";
  JsonObject p = doc["payload"].to<JsonObject>();
  p["pairingType"] = "PROMPT";
  if (clientKey.length()) p["client-key"] = clientKey;
  p["manifest"]["manifestVersion"] = 1;
  JsonArray permissions = p["manifest"]["permissions"].to<JsonArray>();
  permissions.add("CONTROL_MOUSE_AND_KEYBOARD");
  permissions.add("CONTROL_INPUT_TEXT");
  permissions.add("CONTROL_AUDIO");
  permissions.add("CONTROL_POWER");
  String out; serializeJson(doc, out); ssap.sendTXT(out);
}

static bool pointerMessage(const String &message) {
  pointerLastSendOk = false;
  if (pointerReady) { String frame = message; pointerLastSendOk = pointerWs.sendTXT(frame); }
  return pointerLastSendOk;
}

static bool pointerButton(const char *name) {
  return pointerMessage(String("type:button\nname:") + name + "\n\n");
}

static bool pointerClick() { return pointerMessage("type:click\n\n"); }

static void primaryAction() {
  // With air-pointer motion paused, use the TV's navigation OK key instead
  // of clicking at the last cursor position.
  if (pointerEnabled) pointerClick();
  else pointerButton("ENTER");
}

static void openPointerSocket(const String &path) {
  pointerPath = path;
  // The separate pointer socket may use ws:3000 while SSAP uses wss:3001.
  String host = tvHost, p = "/"; uint16_t port = 3000; bool secure = false;
  int scheme = path.indexOf("://");
  if (scheme >= 0) {
    secure = path.startsWith("wss://");
    int authorityStart = scheme + 3;
    int slash = path.indexOf('/', authorityStart);
    String authority = slash >= 0 ? path.substring(authorityStart, slash) : path.substring(authorityStart);
    p = slash >= 0 ? path.substring(slash) : "/";
    int colon = authority.lastIndexOf(':');
    if (colon >= 0) { host = authority.substring(0, colon); port = (uint16_t)authority.substring(colon + 1).toInt(); }
    else { host = authority; port = secure ? 3001 : 80; }
    Serial.printf("Pointer endpoint: %s %s:%u\n", secure ? "wss" : "ws", host.c_str(), port);
  } else if (path.length()) { p = path; Serial.println("Pointer socket path-only; using TV port 3000"); }
  pointerReady = false;
  if (secure) pointerWs.beginSSL(host.c_str(), port, p.c_str());
  else pointerWs.begin(host.c_str(), port, p.c_str());
  pointerWs.onEvent([](WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_CONNECTED) { pointerReady = true; Serial.println("Pointer WebSocket connected"); drawStatus("Pointer socket connected"); }
    else if (type == WStype_DISCONNECTED) { pointerReady = false; Serial.println("Pointer WebSocket disconnected"); }
    else if (type == WStype_ERROR) Serial.printf("Pointer WebSocket error (%u bytes)\n", (unsigned)length);
  });
  pointerWs.setReconnectInterval(5000);
}

static void ssapEvent(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) { ssapConnected = true; registered = false; Serial.println("SSAP WebSocket connected; sending hello"); drawStatus("Starting TV session"); sendHello(); return; }
  if (type == WStype_DISCONNECTED) { Serial.println("SSAP WebSocket disconnected"); ssapConnected = false; registered = false; pointerReady = false; drawStatus(); return; }
  if (type == WStype_ERROR) { Serial.printf("SSAP WebSocket error (%u bytes)\n", (unsigned)length); return; }
  if (type != WStype_TEXT) return;
  JsonDocument doc; if (deserializeJson(doc, payload, length)) return;
  const char *kind = doc["type"] | "";
  if (!strcmp(kind, "hello")) { Serial.println("TV hello received; registering"); sendRegistration(); return; }
  if (!strcmp(kind, "registered")) {
    clientKey = doc["payload"]["client-key"] | clientKey;
    if (clientKey.length()) prefs.putString("client_key", clientKey);
    registered = true; Serial.println("TV registration succeeded"); drawStatus("Remote connected"); askForPointerSocket(); requestVolumeStatus(); return;
  }
  if (!strcmp(kind, "response") && !strcmp(doc["id"] | "", "get-volume")) {
    JsonVariantConst muteValue = doc["payload"]["muted"];
    JsonVariantConst volumeValue = doc["payload"]["volume"];
    if (volumeValue.is<int>()) volumeLevel = volumeValue.as<int>();
    if (muteValue.is<bool>()) { muted = muteValue.as<bool>(); drawStatus(muted ? "TV is muted" : "TV sound is on"); }
    else drawStatus();
  } else if (!strcmp(kind, "response") && !strcmp(doc["id"] | "", "get-pointer")) {
    String path = doc["payload"]["socketPath"] | "";
    if (path.length()) { Serial.println("TV returned pointer socket"); openPointerSocket(path); }
    else { Serial.println("TV gave no pointer socket"); drawStatus("TV gave no pointer socket"); }
  } else if (!strcmp(kind, "error")) {
    Serial.printf("SSAP error: %s\n", doc["error"] | "unknown");
    drawStatus("SSAP error; check pairing / permissions");
  }
}

static void connectTv() {
  if (!WiFi.isConnected() || !tvHost.length()) return;
  Serial.printf("Connecting SSAP to %s:3001\n", tvHost.c_str());
  ssap.beginSSL(tvHost.c_str(), 3001, "/");
  ssap.onEvent(ssapEvent); ssap.setReconnectInterval(5000);
}

static void navigate(const char *name) { pointerButton(name); }

static void setPointerEnabled(bool enabled) {
  pointerEnabled = enabled;
  filteredGx = filteredGz = 0.0f;
  motionRemainderX = motionRemainderY = 0.0f;
  Serial.printf("Air pointer %s\n", pointerEnabled ? "ON" : "OFF");
  drawStatus(pointerEnabled ? "Air pointer enabled" : "Air pointer paused");
}

static void sendText(const String &text) {
  JsonDocument p; p["text"] = text;
  sendSsap("ssap://com.webos.service.ime/insertText", "insert-text", p);
}

static void processKeys() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
  auto k = M5Cardputer.Keyboard.keysState();
  if (k.tab) { helpPage = !helpPage; drawStatus(); return; }
  if (k.enter) { primaryAction(); return; }
  if (k.del) { pointerButton("BACK"); return; }
  for (char c : k.word) {
    if (k.fn) {
      // Cardputer's printed arrow legends use Fn+; , . and /.
      switch (c) {
        case ';': navigate("UP"); continue;
        case ',': navigate("LEFT"); continue;
        case '.': navigate("DOWN"); continue;
        case '/': navigate("RIGHT"); continue;
      }
    }
    switch (tolower(c)) {
      case 'b': navigate("BACK"); break; case 'h': navigate("HOME"); break;
      case 'p': setPointerEnabled(!pointerEnabled); break;
      case 'g': primaryAction(); break;
      case '0': togglePower(); break;
      case '-': sendVolumeStep(false); break;
      case '=': case '+': sendVolumeStep(true); break;
      case 'm': toggleMute(); break;
      default: if (c >= 32 && c < 127) sendText(String(c)); break;
    }
  }
  for (uint8_t key : k.hid_keys) {
    // USB HID usage IDs for arrows and Enter.
    switch (key) { case 0x52: navigate("UP"); break; case 0x51: navigate("DOWN"); break;
      case 0x50: navigate("LEFT"); break; case 0x4f: navigate("RIGHT"); break;
      case 0x28: primaryAction(); break; }
  }
}

static void updateAirPointer() {
  uint32_t now = millis();
  if (!pointerReady || !pointerEnabled) { lastMotionMs = now; return; }
  if (now - lastMotionMs < MOTION_INTERVAL_MS) return;
  uint32_t elapsedMs = now - lastMotionMs;
  lastMotionMs = now;
  // Avoid a large cursor jump after a temporary loop stall.
  float dt = min(elapsedMs, (uint32_t)80) / 1000.0f;
  float gx=0, gy=0, gz=0;
  if (!M5.Imu.getGyro(&gx, &gy, &gz)) return;
  float filterAlpha = dt / (FILTER_TIME_CONSTANT_S + dt);
  filteredGx += filterAlpha * (gx - filteredGx);
  filteredGz += filterAlpha * (gz - filteredGz);
  // Integrate angular velocity into a pointer delta. Both signs are inverted
  // to match the user's observed Cardputer-to-screen direction.
  float rateX = fabsf(filteredGz) > GYRO_DEADZONE_DPS
      ? copysignf(fabsf(filteredGz) - GYRO_DEADZONE_DPS, filteredGz) : 0.0f;
  float rateY = fabsf(filteredGx) > GYRO_DEADZONE_DPS
      ? copysignf(fabsf(filteredGx) - GYRO_DEADZONE_DPS, filteredGx) : 0.0f;
  motionRemainderX += -rateX * dt * PIXELS_PER_DEGREE;
  motionRemainderY += -rateY * dt * PIXELS_PER_DEGREE;
  int dx = (int)motionRemainderX;
  int dy = (int)motionRemainderY;
  motionRemainderX -= dx;
  motionRemainderY -= dy;
  if (dx || dy) pointerMessage(String("type:move\ndx:") + dx + "\ndy:" + dy + "\ndown:0\n\n");
}

void setup() {
  auto cfg = M5.config(); cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  M5Cardputer.begin(cfg);
  // Rotation 0 restores the panel's native landscape axes; the previous
  // rotation 1 was portrait and clipped the 240 x 135 remote dashboard.
  M5Cardputer.Display.setRotation(0);
  Serial.begin(115200);
  Serial.printf("Display orientation: %d x %d (landscape expected)\n",
                M5Cardputer.Display.width(), M5Cardputer.Display.height());
  prefs.begin("mr-adv", false);
  ssid = prefs.getString("ssid", ""); wifiPass = prefs.getString("wifi_pass", "");
  tvHost = prefs.getString("tv_host", "[REDACTED-TV-IP]"); clientKey = prefs.getString("client_key", "");
  tvMac = prefs.getString("tv_mac", "[REDACTED-TV-MAC]");
  drawStatus("Set Wi-Fi once over Serial: WIFI ssid|password");
  Serial.println("MR-ADV setup: WIFI <ssid>|<password> or TV <ip>; credentials stay in NVS.");
  Serial.printf("Saved Wi-Fi SSID: %s\n", ssid.c_str());
  if (ssid.length()) { WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(), wifiPass.c_str()); }
}

void loop() {
  M5Cardputer.update(); ssap.loop(); pointerWs.loop(); processKeys(); updateAirPointer();
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n'); line.trim();
    if (line.startsWith("WIFI ")) {
      int sep = line.indexOf('|', 5);
      if (sep > 5) {
        ssid=line.substring(5,sep); wifiPass=line.substring(sep+1);
        prefs.putString("ssid",ssid); prefs.putString("wifi_pass",wifiPass);
        Serial.printf("Saved Wi-Fi SSID: %s; reconnecting\n", ssid.c_str());
        WiFi.disconnect(false, false); WiFi.mode(WIFI_STA); WiFi.begin(ssid.c_str(),wifiPass.c_str());
      } else Serial.println("WIFI command format error; use WIFI ssid|password");
    } else if (line.startsWith("TVMAC ")) {
      String candidate = line.substring(6); candidate.toUpperCase();
      String previous = tvMac; tvMac = candidate;
      uint8_t mac[6];
      if (parseTvMac(mac)) { prefs.putString("tv_mac", tvMac); Serial.printf("Saved TV MAC: %s\n", tvMac.c_str()); }
      else { tvMac = previous; Serial.println("TVMAC format error; use TVMAC [REDACTED-TV-MAC]"); }
    }
    else if (line.startsWith("TV ")) { tvHost=line.substring(3); prefs.putString("tv_host",tvHost); }
    else if (line == "STATUS") {
      reportWifiStatus();
      Serial.printf("TV state: ssap=%s registered=%s pointer=%s air-pointer=%s\n", ssapConnected ? "connected" : "disconnected", registered ? "yes" : "no", pointerReady ? "ready" : "not-ready", pointerEnabled ? "on" : "off");
    }
    else if (line == "PROBE") probeTvPorts();
    else if (line == "POINTER ON") setPointerEnabled(true);
    else if (line == "POINTER OFF") setPointerEnabled(false);
    else if (line == "POWER") togglePower();
    else if (line == "VOLUP") sendVolumeStep(true);
    else if (line == "VOLDOWN") sendVolumeStep(false);
    else if (line == "MUTE") toggleMute();
    else if (line == "CLICK") { bool ok = pointerClick(); Serial.printf("Click sendTXT=%s\n", ok ? "ok" : "failed"); }
    else if (line.startsWith("BUTTON ")) {
      String name = line.substring(7); name.toUpperCase();
      if (name == "UP" || name == "DOWN" || name == "LEFT" || name == "RIGHT" || name == "BACK" || name == "HOME" || name == "ENTER") {
        bool ok = pointerButton(name.c_str()); Serial.printf("Button %s sendTXT=%s\n", name.c_str(), ok ? "ok" : "failed");
      } else Serial.println("Use BUTTON UP|DOWN|LEFT|RIGHT|BACK|HOME|ENTER");
    }
    else if (line.startsWith("MOVE ")) {
      int split = line.indexOf(' ', 5);
      if (split > 5) {
        int dx = line.substring(5, split).toInt(); int dy = line.substring(split + 1).toInt();
        bool ok = pointerMessage(String("type:move\ndx:") + dx + "\ndy:" + dy + "\ndown:0\n\n");
        Serial.printf("Pointer move sendTXT=%s\n", ok ? "ok" : "failed");
      }
    }
    else if (line == "RESETKEY") {
      prefs.remove("client_key"); clientKey = ""; registered = false;
      pointerWs.disconnect(); ssap.disconnect();
      Serial.println("Cleared TV key; reconnecting for fresh permission approval");
    }
  }
  if (millis() - lastWifiLogMs >= 2000) { lastWifiLogMs = millis(); reportWifiStatus(); }
  static bool started=false;
  if (WiFi.isConnected() && !started) { started=true; drawStatus("Connecting to TV..."); connectTv(); }
  if (!WiFi.isConnected()) started=false;
  delay(2);
}
