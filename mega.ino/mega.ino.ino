// Arduino Mega 2560 - 4 Servo Pill Dispenser
// Receives dispense commands from ESP32 via Serial1
// Controls 4 servos independently (one per tube)

#include <Servo.h>

// Define servo pins (PWM pins on Mega)
#define SERVO_1_PIN 9
#define SERVO_2_PIN 10
#define SERVO_3_PIN 11
#define SERVO_4_PIN 12

// Create servo objects
Servo servo1, servo2, servo3, servo4;

// Dispense angles (adjust to your servo calibration)
#define DISPENSE_ANGLE 90
#define HOME_ANGLE 0
#define DISPENSE_TIME 2000  // ms to hold open

void setup() {
  // Initialize Serial (USB) for debugging
  Serial.begin(115200);
  while (!Serial && millis() < 5000) {
    delay(10);
  }

  Serial.println("\n\n=== Arduino Mega - 4 Servo Pill Dispenser ===");

  // Attach servos
  servo1.attach(SERVO_1_PIN);
  servo2.attach(SERVO_2_PIN);
  servo3.attach(SERVO_3_PIN);
  servo4.attach(SERVO_4_PIN);

  // Initialize all servos to home position
  servo1.write(HOME_ANGLE);
  servo2.write(HOME_ANGLE);
  servo3.write(HOME_ANGLE);
  servo4.write(HOME_ANGLE);

  delay(500);
  Serial.println("✓ All 4 servos attached and homed");

  // Initialize Serial1 for ESP32 communication (pins 19 RX, 18 TX)
  Serial1.begin(115200);
  delay(100);
  Serial.println("✓ Serial1 initialized - Waiting for commands from ESP32");
}

void loop() {
  // Check if data is available from ESP32
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');
    command.trim();

    Serial.print("Received command: ");
    Serial.println(command);

    if (command == "DISPENSE_1") {
      dispensePill(1);
      Serial1.println("ACK: Tube 1 dispensed");
    }
    else if (command == "DISPENSE_2") {
      dispensePill(2);
      Serial1.println("ACK: Tube 2 dispensed");
    }
    else if (command == "DISPENSE_3") {
      dispensePill(3);
      Serial1.println("ACK: Tube 3 dispensed");
    }
    else if (command == "DISPENSE_4") {
      dispensePill(4);
      Serial1.println("ACK: Tube 4 dispensed");
    }
    else if (command == "HOME") {
      homeAllServos();
      Serial1.println("ACK: All servos homed");
    }
    else {
      Serial1.println("ERROR: Unknown command");
    }
  }

  delay(10);
}

void dispensePill(int tube) {
  Servo *servo;

  // Select servo based on tube number
  switch (tube) {
    case 1:
      servo = &servo1;
      Serial.println("Dispensing from Tube 1...");
      break;
    case 2:
      servo = &servo2;
      Serial.println("Dispensing from Tube 2...");
      break;
    case 3:
      servo = &servo3;
      Serial.println("Dispensing from Tube 3...");
      break;
    case 4:
      servo = &servo4;
      Serial.println("Dispensing from Tube 4...");
      break;
    default:
      Serial.println("ERROR: Invalid tube number");
      return;
  }

  // Dispense sequence
  Serial.print("Opening servo ");
  Serial.println(tube);
  servo->write(DISPENSE_ANGLE);
  delay(DISPENSE_TIME);

  Serial.print("Closing servo ");
  Serial.println(tube);
  servo->write(HOME_ANGLE);
  delay(500);

  Serial.print("Tube ");
  Serial.print(tube);
  Serial.println(" dispensed successfully");
}

void homeAllServos() {
  Serial.println("Homing all servos...");
  servo1.write(HOME_ANGLE);
  servo2.write(HOME_ANGLE);
  servo3.write(HOME_ANGLE);
  servo4.write(HOME_ANGLE);
  delay(500);
  Serial.println("All servos homed");
}
