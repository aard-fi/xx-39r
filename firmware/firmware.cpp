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

#if WATER_SAFETY_MODE == 0
    if (!water_detected) {
      stopAll();
      continue;
    }
#else
    if (water_detected) {
      stopAll();
      continue;
    }
#endif

    // Low-voltage cutoff: if battery_data is available and below threshold,
    // stop everything and wait for voltage recovery.
#if LOW_VOLTAGE_CUTOFF_MV > 0
    if (battery_data && battery_data < LOW_VOLTAGE_CUTOFF_MV) {
      stopAll();
      continue;
    }
#endif

    // look for standard 26-byte CRSF frame
    if (uartAvailable() >= 26) {
      if (uartRead() == 0xC8) {     // CRSF address
        if (uartRead() == 24) {   // frame length
          if (uartRead() == 0x16) { // channels type packet

            uint8_t packet[22];
            for (uint8_t i = 0; i < 22; i++) {
              packet[i] = uartRead();
            }
            uint8_t crc = uartRead(); // CRC byte

            uint8_t crc_buf[24];
            crc_buf[0] = 24;
            crc_buf[1] = 0x16;
            for (uint8_t i = 0; i < 22; i++) crc_buf[i + 2] = packet[i];
            if (crc == crsfCRC8(crc_buf, 24)) {
              // extract steering (Ch3) and throttle (Ch1)
              uint16_t steering = ((packet[4] >> 1) | (packet[5] << 7)) & 0x07FF;
              uint16_t throttle = ((packet[1] >> 3) | (packet[2] << 5)) & 0x07FF;
              mixMotors(steering, throttle);
            }
            // CRC mismatch: silently drop the frame
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

  setMotorPort(prepareMotorSpeed(portMotor));
  setMotorStarboard(prepareMotorSpeed(starboardMotor));
}
