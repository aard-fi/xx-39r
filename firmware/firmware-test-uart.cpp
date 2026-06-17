/**
 * @file firmware-test-water.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 *
 * Continuously print U to the UART.
 */

#include <avr/io.h>
#include <util/delay.h>
#include <avr/wdt.h>

int main() {
  wdt_disable();

  // 20 MHz, no prescaler
  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.MCLKCTRLB = 0x00;

  PORTB.DIRSET = PIN2_bm;   // PB2 = TX output
  PORTB.DIRCLR = PIN3_bm;   // PB3 = RX input

  // 115200 @ 20 MHz
  USART0.BAUD = 694;
  USART0.CTRLB = USART_TXEN_bm;

  while (1) {
    // Wait for USART TX data register empty
    while (!(USART0.STATUS & USART_DREIF_bm));

    // Send 'U' = 0x55 (alternating 0101 pattern)
    USART0.TXDATAL = 'U';

    _delay_us(500);
  }
}
