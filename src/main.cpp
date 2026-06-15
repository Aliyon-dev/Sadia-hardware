#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Adafruit_NeoPixel.h>
#include "DispenserControl.h"

#define RGB_PIN 48
#define NUM_PIXELS 1

// Servo, IN1-IN4 pin definitions are in control.h for easier sharing with control.cpp
DispenserControl dispenser(9, 4,5,6,7);

#define SERVICE_UUID       "12345678-1234-1234-1234-1234567890ab"
#define COMMAND_UUID       "12345678-1234-1234-1234-1234567890ac"
#define TIME_SYNC_UUID     "12345678-1234-1234-1234-1234567890ad"
#define SCHEDULE_UUID      "0000a3fe-0000-1000-8000-00805f9b34fb"

Adafruit_NeoPixel pixel(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

BLECharacteristic *commandCharacteristic;
BLECharacteristic *timeSyncCharacteristic;
BLECharacteristic *scheduleCharacteristic;

const int TUBE_1 = 0;
const int TUBE_2 = 682;   // ~120 degrees if 2048 steps/rev
const int TUBE_3 = 1365;  // ~240 degrees if 2048 steps/rev

bool deviceConnected = false;

unsigned long lastBlink = 0;
bool blinkState = false;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
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

    Serial.print("Command received: ");
    Serial.println(value);

    if (value == "DISPENSE") {
      Serial.println("Simulating dispense");
      dispenser.dispenseTo(TUBE_2, 90);
      delay(2000); // Example: dispense from tube 2 with gate open at 90 degrees 

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
  dispenser.begin();

  //mechanical homing
  dispenser.setHome();

  //Servo closed 
  dispenser.setServoClosedAngle(0);
  dispenser.setGateDelay(1000, 400);

  pixel.begin();
  pixel.setBrightness(40);
  pixel.show();
  setColor(0, 0, 0);

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

  Serial.println("BLE dispenser ready");
}

void loop() {
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
}