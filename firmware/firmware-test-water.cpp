/**
 * @file firmware-test-water.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 *
 * Continuously print water sensor state on UART.
 */

// This firmware does its own polling ADC reads — disable the window-mode
// interrupt setup that bootstrap() would otherwise enable.
#define NO_ADC_INTERRUPTS
#include "common.hpp"

int main() {
  bootstrap();

  while (1) {
    wdt_reset();

    uartPrint("W=");
    uartPutc(isWaterDetected() ? '1' : '0');
    uartPrint(" raw=");
    printNumber(readADC(ADC_MUXPOS_AIN7_gc));
    uartPrintLn("");

    for (uint8_t i = 0; i < 10; i++) {
      _delay_ms(10);
      wdt_reset();
    }
  }

  return 0;
}
