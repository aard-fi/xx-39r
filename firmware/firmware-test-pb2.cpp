/**
 * @file firmware-test-pb2.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 *
 * Generate ~57kHz square wave on PB2 to verify UART port is correctly wired up.
 * With the scope correctly showing that the other UART test files should give
 * good results as well.
 */

#include <avr/io.h>
#include <util/delay.h>
#include <avr/wdt.h>

int main() {
  wdt_disable();

  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.MCLKCTRLB = 0x00;
  PORTB.DIRSET = PIN2_bm;
  while (1) {
    PORTB.OUTTGL = PIN2_bm;
    _delay_us(8.68); // one bit time @ 115200 baud
  }
}
