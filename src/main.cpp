#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Adafruit_NeoPixel.h>

#define RGB_PIN 48
#define NUM_PIXELS 1

// Serial1 communication with Mega
#define MEGA_RX_PIN 17
#define MEGA_TX_PIN 18

Adafruit_NeoPixel pixel(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

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

void checkMegaResponse() {
  if (Serial1.available()) {
    String response = Serial1.readStringUntil('\n');
    response.trim();
    Serial.print("[Mega Response] ");
    Serial.println(response);
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

    if (value == "DISPENSE_1") {
      Serial.println("Relaying to Mega: DISPENSE_1");
      Serial1.println("DISPENSE_1");
      flashLED();
    }
    else if (value == "DISPENSE_2") {
      Serial.println("Relaying to Mega: DISPENSE_2");
      Serial1.println("DISPENSE_2");
      flashLED();
    }
    else if (value == "DISPENSE_3") {
      Serial.println("Relaying to Mega: DISPENSE_3");
      Serial1.println("DISPENSE_3");
      flashLED();
    }
    else if (value == "DISPENSE_4") {
      Serial.println("Relaying to Mega: DISPENSE_4");
      Serial1.println("DISPENSE_4");
      flashLED();
    }
    else {
      Serial.println("Unknown command");
    }
  }
};

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

  // Initialize Serial1 for Mega communication
  Serial1.begin(115200, SERIAL_8N1, MEGA_RX_PIN, MEGA_TX_PIN);
  delay(500);
  Serial.println("✓ Connected to Arduino Mega via Serial1");

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
  // Monitor Mega responses
  checkMegaResponse();

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