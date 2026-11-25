#include <Arduino.h>

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

void startLedTimer(ChannelState& ch, unsigned long durationMs, unsigned long now) {
  ch.ledOn = true;
  ch.ledOffTime = now + durationMs;
  digitalWrite(ch.ledPin, HIGH);
  Serial.printf("[%s] ON for %lu seconds\n", ch.name, durationMs / 1000UL);
}

void updateChannel(ChannelState& ch, unsigned long now) {
  bool reading = (digitalRead(ch.switchPin) == LOW);

  // Reset debounce timer whenever raw input changes
  if (reading != ch.rawState) {
    ch.rawState = reading;
    ch.lastEdgeTime = now;
  }

  // Apply debounced state change
  if ((now - ch.lastEdgeTime) >= DEBOUNCE_MS && reading != ch.stablePressed) {
    ch.stablePressed = reading;

    if (ch.stablePressed) {
      // Switch just pushed toward this side
      ch.pressStartTime = now;
      ch.longPressTriggered = false;
    } else {
      // Switch released: if not already triggered as a long hold, trigger 10s timer
      if (!ch.longPressTriggered) {
        startLedTimer(ch, SHORT_ON_MS, now);
      }
    }
  }

  // While held down, trigger the 30s timer as soon as hold exceeds 2 seconds
  if (ch.stablePressed && !ch.longPressTriggered && (now - ch.pressStartTime) > HOLD_THRESHOLD_MS) {
    ch.longPressTriggered = true;
    startLedTimer(ch, LONG_ON_MS, now);
  }

  // Turn LED off when its active timer expires
  if (ch.ledOn && static_cast<long>(now - ch.ledOffTime) >= 0) {
    ch.ledOn = false;
    digitalWrite(ch.ledPin, LOW);
    Serial.printf("[%s] OFF\n", ch.name);
  }
}

void setup() {
  Serial.begin(115200);

  delay(200);  // Brief pause so USB/UART serial monitor is ready

  Serial.print("Booted file: ");
  Serial.println(__FILE__);

  pinMode(SWITCH_A_PIN, INPUT_PULLUP);
  pinMode(SWITCH_B_PIN, INPUT_PULLUP);

  pinMode(LED_A_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  digitalWrite(LED_A_PIN, LOW);
  digitalWrite(LED_B_PIN, LOW);

  Serial.println("ESP32 DPDT momentary LED controller ready.");
}

void loop() {
  unsigned long now = millis();
  updateChannel(chanA, now);
  updateChannel(chanB, now);
}
