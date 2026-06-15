#include "DispenserControl.h"

DispenserControl::DispenserControl(int servoPin, int servoPin2) {
  _servoPin = servoPin;
  _servoPin2 = servoPin2;
}

void DispenserControl::begin() {
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  gateServo.setPeriodHertz(50);
  gateServo.attach(_servoPin, 500, 2400);
  gateServo.write(_servoClosedAngle);

  gateServo2.setPeriodHertz(50);
  gateServo2.attach(_servoPin2, 500, 2400);
  gateServo2.write(_servoClosedAngle);
}

void DispenserControl::setServoClosedAngle(int angle) {
  _servoClosedAngle = angle;
  gateServo.write(_servoClosedAngle);
  gateServo2.write(_servoClosedAngle);
}

void DispenserControl::setGateDelay(int openMs, int closeMs) {
  _gateOpenDelay = openMs;
  _gateCloseDelay = closeMs;
}

void DispenserControl::dispenseTo(int tube, int openAngle) {
  Servo& s = (tube == 1) ? gateServo : gateServo2;

  for (int pos = _servoClosedAngle; pos <= openAngle; pos++) {
    s.write(pos);
    delay(15);
  }

  delay(_gateOpenDelay);

  for (int pos = openAngle; pos >= _servoClosedAngle; pos--) {
    s.write(pos);
    delay(15);
  }

  delay(_gateCloseDelay);
}
