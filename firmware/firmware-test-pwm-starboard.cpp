/**
 * @file firmware-test-pwm-starboard.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 *
 * Slowly cycle right motor through +/- PWM.
 * Scope PB0 (INA2) and PB1 (INB2):
 *   Positive speed -> PB0 = PWM, PB1 = flat low
 *   Negative speed -> PB0 = flat low, PB1 = PWM
 */

#include "common.hpp"

int main() {
  bootstrap();
  initPWM();
  stopAll();

  int16_t speed = 0;
  int8_t  dir   = 1;

  uartPrintLn("Right motor PWM sweep. Scope PB0 and PB1.");

  while (1) {
    wdt_reset();

    setMotorStarboard(speed);

    uartPrint("R=");
    printNumber(speed);
    uartPutc(' ');
    if (speed > 0) {
      uartPrint("FWD  PB0=");
      printNumber(speed);
      uartPrint(" PB1=0");
    } else if (speed < 0) {
      uartPrint("REV  PB0=0 PB1=");
      printNumber(-speed);
    } else {
      uartPrint("OFF  PB0=0 PB1=0");
    }
    uartPrintLn("");

    for (uint8_t i = 0; i < 4; i++) {
      _delay_ms(50);
      wdt_reset();
    }

    speed += 5 * dir;
    if (speed >= 255)  { speed = 255;  dir = -1; }
    if (speed <= -255) { speed = -255; dir =  1; }
  }

  return 0;
}
