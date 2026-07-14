#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "RTClib.h"

#define RGB_PIN 48
#define NUM_PIXELS 1

// Serial1 communication with Mega
#define MEGA_RX_PIN 17
#define MEGA_TX_PIN 18

// Digital switch (MOSFET/relay) that powers the Mega
#define MEGA_POWER_PIN 4
#define MEGA_POWER_ON_LEVEL HIGH  // set to LOW if using an active-low relay module

#define BUZZER_PIN 13

Adafruit_NeoPixel pixel(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);
RTC_DS3231 rtc;

#define SERVICE_UUID       "12345678-1234-1234-1234-1234567890ab"
#define COMMAND_UUID       "12345678-1234-1234-1234-1234567890ac"
#define TIME_SYNC_UUID     "12345678-1234-1234-1234-1234567890ad"
#define SCHEDULE_UUID      "0000a3fe-0000-1000-8000-00805f9b34fb"

BLECharacteristic *commandCharacteristic;
BLECharacteristic *timeSyncCharacteristic;
BLECharacteristic *scheduleCharacteristic;

bool deviceConnected = false;

unsigned long lastBlink = 0;
bool blinkState = false;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void flashLED() {
  for (int i = 0; i < 6; i++) {
    setColor(255, 180, 0); // Yellow
    delay(100);
    setColor(0, 0, 0); // Off
    delay(100);
  }

  if (deviceConnected) {
    setColor(0, 255, 0); // Back to green
  } else {
    setColor(0, 0, 0);
  }
}

void printCurrentTime() {
  DateTime now = rtc.now();
  Serial.printf(
    "[RTC] %04d/%02d/%02d %02d:%02d:%02d\n",
    now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second()
  );
}

// --- Mega power management ---
// The Mega is normally unpowered. When a command arrives we switch it on,
// wait for it to boot (it should print "READY"), send the command, wait for
// "DONE", then cut power again after a short idle grace period.

enum MegaState { MEGA_OFF, MEGA_BOOTING, MEGA_IDLE, MEGA_BUSY };
MegaState megaState = MEGA_OFF;
unsigned long megaStateSince = 0;
String pendingCommand = "";

const unsigned long MEGA_BOOT_TIMEOUT_MS = 4000;      // send anyway if no READY
const unsigned long MEGA_DISPENSE_TIMEOUT_MS = 60000; // give up waiting for DONE
const unsigned long MEGA_IDLE_TIMEOUT_MS = 10000;     // power off after inactivity

void megaPowerOn() {
  Serial1.begin(115200, SERIAL_8N1, MEGA_TX_PIN, MEGA_RX_PIN);
  digitalWrite(MEGA_POWER_PIN, MEGA_POWER_ON_LEVEL);
  megaState = MEGA_BOOTING;
  megaStateSince = millis();
  Serial.println("[Power] Mega ON, waiting for boot");
}

void megaPowerOff() {
  digitalWrite(MEGA_POWER_PIN, !MEGA_POWER_ON_LEVEL);
  Serial1.end();
  // Hold our TX line low: a UART line idling high would back-power
  // the unpowered Mega through its input protection diodes.
  pinMode(MEGA_RX_PIN, OUTPUT);
  digitalWrite(MEGA_RX_PIN, LOW);
  megaState = MEGA_OFF;
  Serial.println("[Power] Mega OFF");
}

void queueMegaCommand(const String &cmd) {
  if (pendingCommand.length()) {
    Serial.println("[Mega] Warning: replacing pending command " + pendingCommand);
  }
  pendingCommand = cmd;
  if (megaState == MEGA_OFF) {
    megaPowerOn();
  }
}

void serviceMega() {
  String response = "";
  if (megaState != MEGA_OFF && Serial1.available()) {
    response = Serial1.readStringUntil('\n');
    response.trim();
    if (response.length()) {
      Serial.print("[Mega Response] ");
      Serial.println(response);
    }
  }

  unsigned long now = millis();

  switch (megaState) {
    case MEGA_OFF:
      break;

    case MEGA_BOOTING:
      if (response == "READY") {
        megaState = MEGA_IDLE;
        megaStateSince = now;
      } else if (now - megaStateSince >= MEGA_BOOT_TIMEOUT_MS) {
        Serial.println("[Mega] No READY, assuming booted");
        megaState = MEGA_IDLE;
        megaStateSince = now;
      }
      break;

    case MEGA_IDLE:
      if (pendingCommand.length()) {
        Serial.println("Relaying to Mega: " + pendingCommand);
        Serial1.println(pendingCommand);
        pendingCommand = "";
        megaState = MEGA_BUSY;
        megaStateSince = now;
      } else if (now - megaStateSince >= MEGA_IDLE_TIMEOUT_MS) {
        megaPowerOff();
      }
      break;

    case MEGA_BUSY:
      if (response.startsWith("DONE")) {
        megaState = MEGA_IDLE;
        megaStateSince = now;
      } else if (now - megaStateSince >= MEGA_DISPENSE_TIMEOUT_MS) {
        Serial.println("[Mega] Timeout waiting for DONE");
        megaState = MEGA_IDLE;
        megaStateSince = now;
      }
      break;
  }
}

// --- Schedule storage (NVS flash) ---
// Schedules are kept on the ESP32 (it has the RTC); the Mega is only powered
// on to execute a dispense. Stored as a JSON array under "schedules".

Preferences prefs;
const char *PREFS_NAMESPACE = "sadia";
const char *SCHEDULES_KEY = "schedules";

bool scheduleCacheDirty = true; // reparse stored JSON on next check

String loadSchedules() {
  prefs.begin(PREFS_NAMESPACE, true);
  String stored = prefs.getString(SCHEDULES_KEY, "[]");
  prefs.end();
  return stored;
}

void printStoredSchedules() {
  Serial.println("[Schedules] Stored: " + loadSchedules());
}

void clearSchedules() {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.remove(SCHEDULES_KEY);
  prefs.end();
  scheduleCacheDirty = true;
  Serial.println("[Schedules] Cleared");
}

// Payload may be a single schedule object (merged into the list, replacing an
// existing entry with the same "id") or a full array (replaces the list).
bool storeScheduleJson(const String &jsonText) {
  JsonDocument incoming;
  if (deserializeJson(incoming, jsonText)) {
    return false;
  }

  String toStore;

  if (incoming.is<JsonArray>()) {
    toStore = jsonText;
  } else if (incoming.is<JsonObject>()) {
    JsonDocument db;
    if (deserializeJson(db, loadSchedules()) || !db.is<JsonArray>()) {
      db.to<JsonArray>();
    }
    JsonArray arr = db.as<JsonArray>();

    bool replaced = false;
    if (!incoming["id"].isNull()) {
      for (JsonVariant entry : arr) {
        if (entry["id"] == incoming["id"]) {
          entry.set(incoming.as<JsonObject>());
          replaced = true;
          break;
        }
      }
    }
    if (!replaced) {
      arr.add(incoming.as<JsonObject>());
    }
    serializeJson(arr, toStore);
  } else {
    return false;
  }

  prefs.begin(PREFS_NAMESPACE, false);
  size_t written = prefs.putString(SCHEDULES_KEY, toStore);
  prefs.end();

  if (written == 0) {
    Serial.println("[Schedules] ERROR: NVS write failed (payload too large?)");
    return false;
  }
  scheduleCacheDirty = true;
  Serial.printf("[Schedules] Saved %u bytes to flash\n", written);
  return true;
}

// --- Buzzer alarm ---
// Beeps 300ms on / 300ms off for BUZZER_DURATION_MS when a schedule fires.

unsigned long buzzerStart = 0;
bool buzzerActive = false;
const unsigned long BUZZER_DURATION_MS = 15000;

void startBuzzer() {
  buzzerActive = true;
  buzzerStart = millis();
  Serial.println("[Buzzer] Ringing");
}

void stopBuzzer() {
  buzzerActive = false;
  noTone(BUZZER_PIN);
}

void serviceBuzzer() {
  if (!buzzerActive) return;

  unsigned long elapsed = millis() - buzzerStart;
  if (elapsed >= BUZZER_DURATION_MS) {
    stopBuzzer();
    Serial.println("[Buzzer] Done");
    return;
  }

  if ((elapsed / 300) % 2 == 0) {
    tone(BUZZER_PIN, 2000);
  } else {
    noTone(BUZZER_PIN);
  }
}

// --- Schedule alarm check ---
// Stored JSON is parsed into a small cache of times; the RTC is polled once
// per second and the buzzer fires when a schedule's hour:minute comes up.
// App payload: [{"id":"...","hour":2,"period":"AM","pills":2}]
// hour is 12-hour clock with AM/PM; no minute field means HH:00.
// {"minute": 30} and {"time": "08:30"} are also accepted.

const int MAX_SCHEDULES = 16;
struct ScheduleTime { int hour; int minute; int pills; };
ScheduleTime scheduleCache[MAX_SCHEDULES];
int scheduleCount = 0;

void rebuildScheduleCache() {
  scheduleCacheDirty = false;
  scheduleCount = 0;

  JsonDocument db;
  if (deserializeJson(db, loadSchedules()) || !db.is<JsonArray>()) return;

  for (JsonObject entry : db.as<JsonArray>()) {
    if (scheduleCount >= MAX_SCHEDULES) {
      Serial.println("[Schedules] Warning: more entries than cache slots, extras ignored");
      break;
    }

    int hour = entry["hour"] | -1;
    int minute = entry["minute"] | 0; // app sends no minute -> fire at HH:00
    if (hour < 0 && entry["time"].is<const char *>()) {
      if (sscanf(entry["time"].as<const char *>(), "%d:%d", &hour, &minute) != 2) {
        hour = -1;
      }
    }

    // Convert 12-hour clock ("period": "AM"/"PM") to 24-hour
    const char *period = entry["period"] | (const char *)nullptr;
    if (period && hour >= 1 && hour <= 12) {
      bool pm = (period[0] == 'P' || period[0] == 'p');
      if (pm && hour != 12) hour += 12;
      if (!pm && hour == 12) hour = 0;
    }

    if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
      scheduleCache[scheduleCount].hour = hour;
      scheduleCache[scheduleCount].minute = minute;
      scheduleCache[scheduleCount].pills = entry["pills"] | 0;
      Serial.printf("[Schedules] #%d -> %02d:%02d, %d pill(s)\n",
                    scheduleCount + 1, hour, minute, scheduleCache[scheduleCount].pills);
      scheduleCount++;
    } else {
      Serial.println("[Schedules] Entry without a valid time, skipped");
    }
  }
  Serial.printf("[Schedules] %d timed entr%s active\n",
                scheduleCount, scheduleCount == 1 ? "y" : "ies");
}

unsigned long lastScheduleCheck = 0;
int prevMinuteOfDay = -1;
bool triggeredThisMinute = false;

void checkSchedules() {
  unsigned long nowMs = millis();
  if (nowMs - lastScheduleCheck < 1000) return;
  lastScheduleCheck = nowMs;

  if (scheduleCacheDirty) rebuildScheduleCache();
  if (scheduleCount == 0) return;

  DateTime now = rtc.now();
  int minuteOfDay = now.hour() * 60 + now.minute();
  if (minuteOfDay != prevMinuteOfDay) {
    prevMinuteOfDay = minuteOfDay;
    triggeredThisMinute = false;
  }
  if (triggeredThisMinute) return;

  for (int i = 0; i < scheduleCount; i++) {
    if (scheduleCache[i].hour == now.hour() && scheduleCache[i].minute == now.minute()) {
      triggeredThisMinute = true;
      Serial.printf("[Alarm] Schedule matched %02d:%02d, %d pill(s) due\n",
                    now.hour(), now.minute(), scheduleCache[i].pills);
      startBuzzer();
      break;
    }
  }
}

// Returns decoded text, or "" if the input isn't valid base64.
String tryBase64Decode(const String &input) {
  size_t outLen = 0;
  mbedtls_base64_decode(nullptr, 0, &outLen,
                        (const unsigned char *)input.c_str(), input.length());
  if (outLen == 0) return "";

  unsigned char *buf = (unsigned char *)malloc(outLen + 1);
  if (!buf) return "";

  String result = "";
  if (mbedtls_base64_decode(buf, outLen, &outLen,
                            (const unsigned char *)input.c_str(), input.length()) == 0) {
    buf[outLen] = '\0';
    result = String((char *)buf);
  }
  free(buf);
  return result;
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    deviceConnected = true;
    Serial.println("Device connected");
    setColor(0, 255, 0); // Green
  }

  void onDisconnect(BLEServer *pServer) override {
    deviceConnected = false;
    Serial.println("Device disconnected");
    BLEDevice::startAdvertising();
  }
};

// Plain-text commands, accepted on both the command and schedule
// characteristics. Returns false if the value isn't a known command.
bool handleTextCommand(const String &value) {
  if (value == "DISPENSE_1" || value == "DISPENSE_2" ||
      value == "DISPENSE_3" || value == "DISPENSE_4") {
    queueMegaCommand(value);
    flashLED();
  }
  else if (value == "LIST_SCHEDULES") {
    printStoredSchedules();
  }
  else if (value == "CLEAR_SCHEDULES") {
    clearSchedules();
  }
  else if (value == "TEST_BUZZER") {
    startBuzzer();
  }
  else if (value == "STOP_BUZZER") {
    stopBuzzer();
  }
  else {
    return false;
  }
  return true;
}

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String value = pCharacteristic->getValue().c_str();
    value.trim();

    Serial.print("BLE Command received: ");
    Serial.println(value);

    if (!handleTextCommand(value)) {
      Serial.println("Unknown command");
    }
  }
};



class TimeSyncCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    std::string value = pCharacteristic->getValue();

    Serial.print("Time sync received, length: ");
    Serial.println(value.length());

    if (value.length() == 7) {
      uint8_t year = (uint8_t)value[0];
      uint8_t month = (uint8_t)value[1];
      uint8_t day = (uint8_t)value[2];
      uint8_t hour = (uint8_t)value[3];
      uint8_t minute = (uint8_t)value[4];
      uint8_t second = (uint8_t)value[5];
      uint8_t dayOfWeek = (uint8_t)value[6];

      Serial.printf(
        "Parsed time -> %04d-%02d-%02d %02d:%02d:%02d DOW=%d\n",
        2000 + year, month + 1, day, hour, minute, second, dayOfWeek
      );

      rtc.adjust(DateTime(2000 + year, month + 1, day, hour, minute, second));
      Serial.println("RTC updated from BLE time sync");
      printCurrentTime();

      // quick blink to show activity
      for (int i = 0; i < 2; i++) {
        setColor(255, 180, 0);
        delay(80);
        setColor(0, 0, 0);
        delay(80);
      }

      if (deviceConnected) setColor(0, 255, 0);
    } else {
      Serial.println("Invalid time sync payload. Expected 7 bytes.");
    }
  }
};

class ScheduleCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String value = pCharacteristic->getValue().c_str();

    value.trim();

    Serial.print("Schedule received (");
    Serial.print(value.length());
    Serial.println(" bytes): " + value);

    // The app sends text commands on this characteristic too
    if (handleTextCommand(value)) {
      return;
    }

    // Accept plain JSON, or base64-wrapped JSON from the app.
    String jsonText = value;
    JsonDocument probe;
    if (deserializeJson(probe, jsonText)) {
      String decoded = tryBase64Decode(value);
      if (decoded.length() && !deserializeJson(probe, decoded)) {
        Serial.println("[Schedules] Decoded base64: " + decoded);
        jsonText = decoded;
      } else {
        Serial.println("[Schedules] Payload is neither JSON nor base64 JSON, ignoring");
        return;
      }
    }

    if (storeScheduleJson(jsonText)) {
      printStoredSchedules();
    }

    for (int i = 0; i < 2; i++) {
      setColor(255, 180, 0);
      delay(80);
      setColor(0, 0, 0);
      delay(80);
    }

    if (deviceConnected) setColor(0, 255, 0);
  }
};

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\n=== SMART PILL DISPENSER (BLE Relay) ===");

  Wire.begin(8, 9);
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1) {
      delay(1000);
    }
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting compile time");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  printCurrentTime();

  // Show what survived the reboot — this is the persistence test
  printStoredSchedules();

  // Mega starts powered off; Serial1 is opened on demand in megaPowerOn()
  pinMode(MEGA_POWER_PIN, OUTPUT);
  digitalWrite(MEGA_POWER_PIN, !MEGA_POWER_ON_LEVEL);
  pinMode(MEGA_RX_PIN, OUTPUT);
  digitalWrite(MEGA_RX_PIN, LOW); // keep TX line low so the off Mega isn't back-powered
  Serial.println("✓ Mega power control ready (Mega OFF)");

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // Initialize LED
  pixel.begin();
  pixel.setBrightness(40);
  pixel.show();
  setColor(0, 0, 0);

  // Initialize BLE
  BLEDevice::init("ZAM_MED_01");

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  commandCharacteristic = pService->createCharacteristic(
    COMMAND_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  commandCharacteristic->setCallbacks(new CommandCallbacks());

  timeSyncCharacteristic = pService->createCharacteristic(
    TIME_SYNC_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  timeSyncCharacteristic->setCallbacks(new TimeSyncCallbacks());

  scheduleCharacteristic = pService->createCharacteristic(
    SCHEDULE_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  scheduleCharacteristic->setCallbacks(new ScheduleCallbacks());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();

  Serial.println("✓ BLE relay ready");
}

void loop() {
  // Drive Mega power state machine and monitor its responses
  serviceMega();

  // Fire the buzzer when a stored schedule's time comes up
  checkSchedules();
  serviceBuzzer();

  if (!deviceConnected) {
    unsigned long now = millis();

    if (now - lastBlink >= 700) {
      blinkState = !blinkState;

      if (blinkState) {
        setColor(0, 0, 255); // Blue
      } else {
        setColor(0, 0, 0); // Off
      }

      lastBlink = now;
    }
  }

  delay(10);
}