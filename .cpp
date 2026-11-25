#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// Wi-Fi & OTA Configuration
constexpr char WIFI_SSID[]     = "Fold6";
constexpr char WIFI_PASS[]     = "12345678";
constexpr char OTA_HOSTNAME[]  = "esp32-dpdt-led";

// Pins
constexpr uint8_t SWITCH_A_PIN = 25;  // DPDT momentary throw 1 (active LOW)
constexpr uint8_t SWITCH_B_PIN = 26;  // DPDT momentary throw 2 (active LOW)
constexpr uint8_t LED_A_PIN    = 27;  // LED / MOS output for Direction A
constexpr uint8_t LED_B_PIN    = 14;  // LED / MOS output for Direction B

// Timing constants (milliseconds)
constexpr unsigned long HOLD_THRESHOLD_MS = 2000UL;   // 2 seconds
constexpr unsigned long SHORT_ON_MS       = 10000UL;  // 10 seconds (held under 2s)
constexpr unsigned long LONG_ON_MS        = 30000UL;  // 30 seconds (held over 2s)
constexpr unsigned long DEBOUNCE_MS       = 30UL;
constexpr unsigned long WIFI_RETRY_MS     = 10000UL;

struct ChannelState {
  uint8_t switchPin;
  uint8_t ledPin;
  const char* name;

  bool rawState;
  bool stablePressed;
  unsigned long lastEdgeTime;
  unsigned long pressStartTime;
  bool longPressTriggered;

  bool ledOn;
  unsigned long ledOffTime;
};

ChannelState chanA = {SWITCH_A_PIN, LED_A_PIN, "LED A", HIGH, false, 0, 0, false, false, 0};
ChannelState chanB = {SWITCH_B_PIN, LED_B_PIN, "LED B", HIGH, false, 0, 0, false, false, 0};

unsigned long lastWifiCheck = 0;
bool otaInitialized = false;

static const char* baseFileName(const char* path) {
  const char* lastSlash = strrchr(path, '/');
  const char* lastBackslash = strrchr(path, '\\');
  const char* sep = lastSlash > lastBackslash ? lastSlash : lastBackslash;
  return sep ? (sep + 1) : path;
}

void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(WIFI_PASS);  // Uses 12345678 for OTA auth as well

  ArduinoOTA.onStart([]() {
    // Turn off both outputs safely before flashing
    digitalWrite(LED_A_PIN, LOW);
    digitalWrite(LED_B_PIN, LOW);
    chanA.ledOn = false;
    chanB.ledOn = false;
    Serial.println("\n[OTA] Update starting...");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update complete. Rebooting...");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\n[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  otaInitialized = true;
  Serial.printf("[OTA] Ready at %s.local (IP: %s)\n", OTA_HOSTNAME, WiFi.localIP().toString().c_str());
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);

  // Brief non-blocking-friendly initial wait (up to 5s) so switches still work if hotspot is off
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < 5000UL) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    setupOTA();
  } else {
    Serial.println("[WiFi] Hotspot not found yet; switches active and will keep retrying in background.");
  }
}

void startLedTimer(ChannelState& ch, unsigned long durationMs, unsigned long now) {
  ch.ledOn = true;
  ch.ledOffTime = now + durationMs;
  digitalWrite(ch.ledPin, HIGH);
  Serial.printf("[%s] ON for %lu seconds\n", ch.name, durationMs / 1000UL);
}

void updateChannel(ChannelState& ch, unsigned long now) {
  bool reading = (digitalRead(ch.switchPin) == LOW);

  if (reading != ch.rawState) {
    ch.rawState = reading;
    ch.lastEdgeTime = now;
  }

  if ((now - ch.lastEdgeTime) >= DEBOUNCE_MS && reading != ch.stablePressed) {
    ch.stablePressed = reading;

    if (ch.stablePressed) {
      ch.pressStartTime = now;
      ch.longPressTriggered = false;
    } else {
      if (!ch.longPressTriggered) {
        startLedTimer(ch, SHORT_ON_MS, now);
      }
    }
  }

  if (ch.stablePressed && !ch.longPressTriggered && (now - ch.pressStartTime) > HOLD_THRESHOLD_MS) {
    ch.longPressTriggered = true;
    startLedTimer(ch, LONG_ON_MS, now);
  }

  if (ch.ledOn && static_cast<long>(now - ch.ledOffTime) >= 0) {
    ch.ledOn = false;
    digitalWrite(ch.ledPin, LOW);
    Serial.printf("[%s] OFF\n", ch.name);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.printf("\nFile: %s | Built: %s %s\n", baseFileName(__FILE__), __DATE__, __TIME__);

  pinMode(SWITCH_A_PIN, INPUT_PULLUP);
  pinMode(SWITCH_B_PIN, INPUT_PULLUP);

  pinMode(LED_A_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  digitalWrite(LED_A_PIN, LOW);
  digitalWrite(LED_B_PIN, LOW);

  connectWiFi();
  Serial.println("ESP32 DPDT momentary LED controller ready.");
}

void loop() {
  unsigned long now = millis();

  // Keep switches responsive at all times
  updateChannel(chanA, now);
  updateChannel(chanB, now);

  // Handle OTA or reconnect Wi-Fi in background if hotspot turns on later
  if (WiFi.status() == WL_CONNECTED) {
    if (!otaInitialized) {
      setupOTA();
    }
    ArduinoOTA.handle();
  } else if ((now - lastWifiCheck) >= WIFI_RETRY_MS) {
    lastWifiCheck = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}
