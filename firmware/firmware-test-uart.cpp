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

#ifndef CLK_PRESCALER
#define CLK_PRESCALER 1
#endif

#ifndef USART_BAUD
#define USART_BAUD 694
#endif

int main() {
  wdt_disable();

#if CLK_PRESCALER == 1
  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0x00);
#elif CLK_PRESCALER == 2
  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, CLKCTRL_PEN_bm);
#elif CLK_PRESCALER == 4
  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, CLKCTRL_PEN_bm | 0x01);
#else
  #error Unsupported CLK_PRESCALER
#endif

  PORTB.DIRSET = PIN2_bm;   // PB2 = TX output
  PORTB.DIRCLR = PIN3_bm;   // PB3 = RX input

  USART0.BAUD = USART_BAUD;
  USART0.CTRLB = USART_TXEN_bm;

  while (1) {
    // Wait for USART TX data register empty
    while (!(USART0.STATUS & USART_DREIF_bm));

    // Send 'U' = 0x55 (alternating 0101 pattern)
    USART0.TXDATAL = 'U';

    _delay_us(500);
  }
}
