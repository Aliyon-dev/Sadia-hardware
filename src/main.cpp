#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Adafruit_NeoPixel.h>
#include "RTClib.h"

#define RGB_PIN 48
#define NUM_PIXELS 1

// Serial1 communication with Mega
#define MEGA_RX_PIN 17
#define MEGA_TX_PIN 18

// Digital switch (MOSFET/relay) that powers the Mega
#define MEGA_POWER_PIN 4
#define MEGA_POWER_ON_LEVEL HIGH  // set to LOW if using an active-low relay module

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

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String value = pCharacteristic->getValue().c_str();
    value.trim();

    Serial.print("BLE Command received: ");
    Serial.println(value);

    if (value == "DISPENSE_1" || value == "DISPENSE_2" ||
        value == "DISPENSE_3" || value == "DISPENSE_4") {
      queueMegaCommand(value);
      flashLED();
    }
    else {
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

    Serial.print("Schedule/message received: ");
    Serial.println(value);
    Serial.print("Length: ");
    Serial.println(value.length());
    queueMegaCommand(value); // Relay to Mega (powers it on if needed)

    // For now just log raw incoming base64 text.
    // Later you can decode base64 + parse JSON here.

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

  // Mega starts powered off; Serial1 is opened on demand in megaPowerOn()
  pinMode(MEGA_POWER_PIN, OUTPUT);
  digitalWrite(MEGA_POWER_PIN, !MEGA_POWER_ON_LEVEL);
  pinMode(MEGA_RX_PIN, OUTPUT);
  digitalWrite(MEGA_RX_PIN, LOW); // keep TX line low so the off Mega isn't back-powered
  Serial.println("✓ Mega power control ready (Mega OFF)");

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