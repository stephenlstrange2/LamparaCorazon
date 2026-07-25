#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

namespace {

constexpr uint8_t TOUCH_PIN = T4;   // T4 is GPIO4 on the ESP32-S2.
constexpr uint8_t LED_PIN = 16;     // Keep GPIO19/20 free for native USB.
constexpr uint16_t LED_COUNT = 24;  // Three daisy-chained blocks of eight.

// Brightness starts off, increases on every touch, then wraps back to off.
constexpr uint8_t BRIGHTNESS_LEVELS[] = {0, 24, 64, 128, 192, 255};
constexpr size_t LEVEL_COUNT =
    sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);

constexpr uint32_t CALIBRATION_MS = 2000;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 45;
constexpr uint32_t RELEASE_DEBOUNCE_MS = 100;
constexpr uint32_t SERIAL_REPORT_MS = 250;

Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint32_t baseline = 0;
size_t brightnessLevel = 0;
bool touchLatched = false;
uint32_t candidateSince = 0;
uint32_t lastReport = 0;

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
  // A warm amber-white mix. Change these three values for another color.
  const uint32_t warmWhite = pixels.Color(255, 135, 35);
  pixels.setBrightness(BRIGHTNESS_LEVELS[brightnessLevel]);
  pixels.fill(warmWhite);
  pixels.show();
}

void calibrateTouch() {
  Serial.println("Touch calibration: do not touch the aluminum plate...");

  uint64_t sum = 0;
  uint32_t samples = 0;
  const uint32_t start = millis();
  while (millis() - start < CALIBRATION_MS) {
    sum += touchRead(TOUCH_PIN);
    ++samples;
    delay(10);
  }

  baseline = static_cast<uint32_t>(sum / samples);
  Serial.printf("Touch baseline: %lu\n", static_cast<unsigned long>(baseline));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);

  pixels.begin();
  pixels.clear();
  pixels.show();

  calibrateTouch();
  showLamp();
  Serial.println("Ready. Tap the plate to increase brightness.");
}

void loop() {
  const uint32_t now = millis();
  const uint32_t reading = readTouchAverage();

  // ESP32-S2 touch readings rise when touched. Percentage thresholds adapt to
  // different plate sizes; hysteresis prevents rapid toggling near the limit.
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
        Serial.printf("Touch! Brightness: %u/255\n",
                      BRIGHTNESS_LEVELS[brightnessLevel]);
      }
    }
  } else {
    candidateSince = 0;
  }

  // Slowly follow environmental drift only while the plate is untouched.
  if (!touchLatched) {
    baseline = (baseline * 255UL + reading) / 256UL;
  }

  if (now - lastReport >= SERIAL_REPORT_MS) {
    lastReport = now;
    Serial.printf("touch=%lu baseline=%lu threshold=%lu state=%s\n",
                  static_cast<unsigned long>(reading),
                  static_cast<unsigned long>(baseline),
                  static_cast<unsigned long>(pressThreshold),
                  touchLatched ? "TOUCHED" : "released");
  }
}
