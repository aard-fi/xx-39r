/**
 * @file firmware-test-pwm-port.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 *
 * Slowly cycle left motor through +/- PWM.
 * Scope PA4 (INA1) and PA5 (INB1):
 *   Positive speed -> PA4 = PWM, PA5 = flat low
 *   Negative speed -> PA4 = flat low, PA5 = PWM
 */

#include "common.hpp"

int main() {
  bootstrap();
  initPWM();
  stopAll();

  int16_t speed = 0;
  int8_t  dir   = 1;

  uartPrintLn("Left motor PWM sweep. Scope PA4 and PA5.");

  while (1) {
    wdt_reset();

    setMotorPort(speed);

    uartPrint("L=");
    printNumber(speed);
    uartPutc(' ');
    if (speed > 0) {
      uartPrint("FWD  PA4=");
      printNumber(speed);
      uartPrint(" PA5=0");
    } else if (speed < 0) {
      uartPrint("REV  PA4=0 PA5=");
      printNumber(-speed);
    } else {
      uartPrint("OFF  PA4=0 PA5=0");
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
