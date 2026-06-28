/**
 * @file firmware-springtide40.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 * @date 2026
 *
 * ELRS receiver -> motor driver bridge for XX-39R replacement board.
 *
 * See common.hpp or README.org for wiring details.
 *
 * This variants specifically targets the Revell Spring Tide 40,
 * trying to make it as nicely playable as possible.
 * https://revell.com/en-eu/products/241369090-speedboat-dolphin
 */

#include <avr/version.h>

// boat-specific motor tuning: these 130-size motors need ~140 on the 0-255
// scale (~55% duty with PWM_MAX=160) to reliably start from idle. Needs some
// proper testing and fine tuning in water still.
#define MOTOR_STALL_SPEED 140

#define WATER_SAFETY_MODE 0
#include "common.hpp"

void mixMotors(uint16_t steer, uint16_t thr);

// NOTE: the CRSF part is not properly tested yet - the rest should be fine
int main() {
  wdt_disable();

  bootstrap();
  initPWM();
  stopAll();

  // if running on battery and motors go full-throttle then freeze,
  // check supply voltage under load. 20 MHz requires VCC >= 4.5V per
  // datasheet; sag below that causes clock instability. When running from
  // batteries this only is supported with the 10MHz binary.

  while (1) {
    wdt_reset();

#ifdef WATER_SENSOR
    if (!water_detected) {
      stopAll();
      continue;
    }
#endif

    // low-voltage cutoff: if battery measurement is below threshold, stop
    // everything.  This protects the regulator and motors from brownout.
#if LOW_VOLTAGE_CUTOFF_MV > 0
    if (battery_data && battery_data < LOW_VOLTAGE_CUTOFF_MV) {
      stopAll();
      continue;
    }
#endif

    processCrsfBuffer();

    // failsafe: stop motors if no valid CRSF packet for ~1 second
    static uint16_t failsafe_cnt = 0;
    if (rc_data.is_new_data) {
      rc_data.is_new_data = false;
      failsafe_cnt = 0;
      mixMotors(rc_data.steering, rc_data.throttle);
    } else if (++failsafe_cnt >= 10000) {
      failsafe_cnt = 10000;
      stopAll();
    }

    /*
     * handle toggle switches
     if (rc_data.aux1){
     }
    */
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
