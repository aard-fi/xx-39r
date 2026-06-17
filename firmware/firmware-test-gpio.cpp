/**
 * @file firmware-test-gpio.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 *
 * GPIO toggle test — no TCA, no PWM, just bit-bang each motor pin.
 * Helps verify the physical pins driving the motors are wired up
 * correctly.
 *
 * Sequence: PA4 -> PA5 -> PB0 -> PB1, details logged via serial.
 * Motors should spin up for each direction for a moment.
 */

#include <avr/io.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <stdint.h>

#include "common.hpp"

static inline void pulsePin(uint8_t port, uint8_t pinbm, const char *name) {
  uartPrint("Pulsing ");
  uartPrintLn(name);

  // Slow burst: 1 s on, 500 ms off, 3 cycles
  for (uint8_t i = 0; i < 3; i++) {
    wdt_reset();
    if (port == 'A')
      PORTA.OUTSET = pinbm;
    else
      PORTB.OUTSET = pinbm;
    _delay_ms(1000);

    wdt_reset();
    if (port == 'A')
      PORTA.OUTCLR = pinbm;
    else
      PORTB.OUTCLR = pinbm;
    _delay_ms(500);
  }

  uartPrint("Done ");
  uartPrintLn(name);
}

int main() {
  wdt_disable();
  initClock();
  initPins();
  initUART();
  sei();

  // All motor pins as GPIO outputs, initially low
  PORTA.DIRSET = PIN4_bm | PIN5_bm;
  PORTB.DIRSET = PIN0_bm | PIN1_bm;
  PORTA.OUTCLR = PIN4_bm | PIN5_bm;
  PORTB.OUTCLR = PIN0_bm | PIN1_bm;

  uartPrintLn("GPIO toggle test starting.");

  wdt_enable(WDT_PERIOD_256CLK_gc);

  while (1) {
    wdt_reset();

    pulsePin('A', PIN4_bm, "PA4 left-fwd");
    pulsePin('A', PIN5_bm, "PA5 left-bwd");
    pulsePin('B', PIN0_bm, "PB0 right-fwd");
    pulsePin('B', PIN1_bm, "PB1 right-bwd");
  }

  return 0;
}
