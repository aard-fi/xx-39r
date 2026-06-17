/**
 * @file firmware.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 * @date 2026
 *
 * ELRS receiver -> motor driver bridge for XX-39R replacement board.
 *
 * See common.hpp or README.org for wiring details.
 */

#include <avr/version.h>

#include "common.hpp"

void mixMotors(uint16_t steer, uint16_t thr);

// NOTE: the CRSF part is not properly tested yet - the rest should be fine
int main() {
  wdt_disable();

  bootstrap();
  initPWM();
  stopAll();

  while (1) {
    wdt_reset();

    // look for standard 26-byte CRSF frame
    if (uartAvailable() >= 26) {
      if (uartRead() == 0xC8) {     // CRSF address
        if (uartRead() == 24) {   // frame length
          if (uartRead() == 0x16) { // channels type packet

            uint8_t packet[22];
            for (uint8_t i = 0; i < 22; i++) {
              packet[i] = uartRead();
            }
            uartRead(); // consume incoming CRC byte

            // extract steering (Ch0) and throttle (Ch1)
            uint16_t steering = (packet[0] | (packet[1] << 8)) & 0x07FF;
            uint16_t throttle = ((packet[1] >> 3) | (packet[2] << 5)) & 0x07FF;

            mixMotors(steering, throttle);
          }
        }
      }
    }
  }
  return 0;
}

void mixMotors(uint16_t steer, uint16_t thr) {
  int16_t forwardSpeed = mapValue(thr, 172, 1811, -255, 255);
  int16_t steeringBias = mapValue(steer, 172, 1811, -255, 255);

  int16_t portMotor = forwardSpeed + steeringBias;
  int16_t starboardMotor = forwardSpeed - steeringBias;

  setMotorPort(portMotor);
  setMotorStarboard(starboardMotor);
}
