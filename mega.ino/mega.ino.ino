// Arduino Mega 2560 - 4 Servo Pill Dispenser
// Servos 1 & 4: 180-degree positional
// Servos 2 & 3: 360-degree continuous rotation
#include <Servo.h>

#define SERVO_1_PIN 2
#define SERVO_2_PIN 3
#define SERVO_3_PIN 4
#define SERVO_4_PIN 5

Servo servo1, servo2, servo3, servo4;

// --- Positional servo settings (servos 1 & 4) ---
#define DISPENSE_ANGLE 90
#define HOME_ANGLE     0
#define DISPENSE_TIME  2000   // ms held open

// --- Continuous servo settings (servos 2 & 3) ---
#define CR_STOP        90     // neutral: no rotation
#define CR_FORWARD     130    // moderate speed one direction (tune this)
#define CR_SPIN_TIME   800    // ms of rotation per dispense (tune this)

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) delay(10);
  Serial.println("\n\n=== Arduino Mega - 4 Servo Pill Dispenser ===");

  servo1.attach(SERVO_1_PIN);
  servo2.attach(SERVO_2_PIN);
  servo3.attach(SERVO_3_PIN);
  servo4.attach(SERVO_4_PIN);

  // Home = angle 0 for positional, STOP for continuous
  servo1.write(HOME_ANGLE);
  servo2.write(CR_STOP);
  servo3.write(CR_STOP);
  servo4.write(HOME_ANGLE);
  delay(500);
  Serial.println("✓ All 4 servos attached and homed/stopped");

  Serial1.begin(115200);
  delay(100);
  Serial.println("✓ Serial1 initialized - Waiting for commands from ESP32 RESPONSE READY");
  Serial1.println("READY");
}

void loop() {
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');
    command.trim();
    Serial.print("Received command: ");
    Serial.println(command);

    if      (command == "DISPENSE_1") { dispensePositional(servo1, 1); Serial1.println("ACK: Tube 1 dispensed"); }
    else if (command == "DISPENSE_2") { dispenseContinuous(servo2, 2); Serial1.println("ACK: Tube 2 dispensed"); }
    else if (command == "DISPENSE_3") { dispenseContinuous(servo3, 3); Serial1.println("ACK: Tube 3 dispensed"); }
    else if (command == "DISPENSE_4") { dispensePositional(servo4, 4); Serial1.println("ACK: Tube 4 dispensed"); }
    else if (command == "HOME")       { homeAllServos();               Serial1.println("ACK: All servos homed"); }
    else                              { Serial1.println("ERROR: Unknown command"); }
  }
  delay(10);
}

// 180° servo: open gate, hold, close
void dispensePositional(Servo &servo, int tube) {
  Serial.print("Dispensing (positional) Tube ");
  Serial.println(tube);
  servo.write(DISPENSE_ANGLE);
  delay(DISPENSE_TIME);
  servo.write(HOME_ANGLE);
  delay(500);
  Serial.print("Tube ");
  Serial.print(tube);
  Serial.println(" dispensed");
}

// 360° servo: spin for a fixed time, then stop
void dispenseContinuous(Servo &servo, int tube) {
  Serial.print("Dispensing (continuous) Tube ");
  Serial.println(tube);
  servo.write(CR_FORWARD);
  delay(CR_SPIN_TIME);
  servo.write(CR_STOP);
  delay(500);
  Serial.print("Tube ");
  Serial.print(tube);
  Serial.println(" dispensed");
}

void homeAllServos() {
  Serial.println("Homing/stopping all servos...");
  servo1.write(HOME_ANGLE);
  servo2.write(CR_STOP);
  servo3.write(CR_STOP);
  servo4.write(HOME_ANGLE);
  delay(500);
  Serial.println("Done");
}
