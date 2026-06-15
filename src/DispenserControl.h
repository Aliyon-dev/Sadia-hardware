#ifndef DISPENSER_CONTROL_H
#define DISPENSER_CONTROL_H

#include <Arduino.h>
#include <ESP32Servo.h>

class DispenserControl {
  public:
    DispenserControl(int servoPin, int servoPin2);

    void begin();
    void dispenseTo(int tube, int openAngle);
    void setServoClosedAngle(int angle);
    void setGateDelay(int openMs, int closeMs);

  private:
    Servo gateServo;
    Servo gateServo2;

    int _servoPin;
    int _servoPin2;

    int _servoClosedAngle = 0;
    int _gateOpenDelay = 1000;
    int _gateCloseDelay = 400;
};

#endif
