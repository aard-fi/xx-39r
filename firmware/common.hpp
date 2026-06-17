/**
 * @file common.hpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 *
 * Shared inline helpers for XX-39R test firmwares.
 * Include once per .cpp; everything is inline/static to avoid ODR issues.
 *
 *
 *          [ BOW ]
 *            / \
 *           /   \
 *  [PORT]  |     |  [STARBOARD]
 *  (Left)  |     |  (Right)
 *          |_____|
 *          [STERN]
 *
 * Board wiring:
 *   PA4 -> INA1 (Port motor forward PWM  -> TCA0 HCMP1 / WO4)
 *   PA5 -> INB1 (Port motor backward PWM -> TCA0 HCMP2 / WO5)
 *   PB0 -> INA2 (Starboard motor forward PWM -> TCA0 LCMP0 / WO0)
 *   PB1 -> INB2 (Starboard motor backward PWM-> TCA0 LCMP1 / WO1)
 *   PA7 -> Water detection sense input (with pull-up)
 *   PB2 -> Receiver RX
 *   PB3 -> Receiver TX
 */

#ifndef COMMON_HPP
#define COMMON_HPP

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdlib.h>

#define WATER_SENSE_bm PIN7_bm

// UART ring buffers (interrupt-driven TX)
#define TX_BUFFER_SIZE 64

static volatile uint8_t tx_buffer[TX_BUFFER_SIZE];
static volatile uint8_t tx_head = 0;
static volatile uint8_t tx_tail = 0;

#define RX_BUFFER_SIZE 64
static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

static inline void uartStartTx() {
  USART0.CTRLA |= USART_DREIE_bm;
}

ISR(USART0_DRE_vect) {
  if (tx_head != tx_tail) {
    USART0.TXDATAL = tx_buffer[tx_tail];
    tx_tail = (tx_tail + 1) & (TX_BUFFER_SIZE - 1);
  }
  if (tx_head == tx_tail) {
    USART0.CTRLA &= ~USART_DREIE_bm;
  }
}

ISR(USART0_RXC_vect) {
  uint8_t data = USART0.RXDATAL;
  uint8_t next_head = (rx_head + 1) & (RX_BUFFER_SIZE - 1);
  if (next_head != rx_tail) {
    rx_buffer[rx_head] = data;
    rx_head = next_head;
  }
}

// UART helpers
static inline void uartPutc(char c) {
  uint8_t next_head = (tx_head + 1) & (TX_BUFFER_SIZE - 1);
  while (next_head == tx_tail);
  tx_buffer[tx_head] = c;
  tx_head = next_head;
  uartStartTx();
}

static inline void uartPrint(const char *str) {
  while (*str) uartPutc(*str++);
}

static inline void uartPrintLn(const char *str) {
  uartPrint(str);
  uartPrint("\r\n");
}

static inline void printNumber(int16_t n) {
  uint16_t v;
  if (n < 0) {
    uartPutc('-');
    v = (uint16_t)(-n);
  } else {
    v = (uint16_t)n;
  }
  if (v >= 1000) uartPutc('0' + (v / 1000));
  if (v >= 100)  uartPutc('0' + ((v / 100) % 10));
  else            uartPutc('0');
  if (v >= 10)   uartPutc('0' + ((v / 10) % 10));
  else            uartPutc('0');
  uartPutc('0' + (v % 10));
}

static inline uint8_t uartAvailable() {
  return (rx_head - rx_tail) & (RX_BUFFER_SIZE - 1);
}

static inline uint8_t uartRead() {
  while (rx_head == rx_tail) {
    wdt_reset();
  }
  uint8_t data = rx_buffer[rx_tail];
  rx_tail = (rx_tail + 1) & (RX_BUFFER_SIZE - 1);
  return data;
}

// system / clock init
static inline void initClock() {
  CPU_CCP = CCP_IOREG_gc;
  CLKCTRL.MCLKCTRLB = 0x00;
}

static inline void initPins() {
  // motor driver outputs (TCA0 will override when enabled)
  PORTA.DIRSET = PIN4_bm | PIN5_bm;   // PA4=INA1, PA5=INB1
  PORTB.DIRSET = PIN0_bm | PIN1_bm;   // PB0=INA2, PB1=INB2

  // UART debug on PB2/PB3 (default USART0 pins on ATtiny414)
  PORTB.DIRSET = PIN2_bm;             // PB2 = TXD
  PORTB.DIRCLR = PIN3_bm;             // PB3 = RXD

  // water detection sense input - no pullup, and we set up ADC later so
  // we can easily define thresholds what we count as "water detected"
  PORTA.DIRCLR = WATER_SENSE_bm;
}

// UART setup (PB2/PB3, 115200 @ 20 MHz)
static inline void initUART() {
  USART0.BAUD = 694;
  USART0.CTRLB = USART_RXEN_bm | USART_TXEN_bm;
  USART0.CTRLA = USART_RXCIE_bm | USART_DREIE_bm;
}

// ADC setup for water sensing
static inline void initADC() {
  // 10-bit ADC, VDD reference, prescaler /16 (1.25 MHz ADC clock @ 20 MHz CPU)
  ADC0.CTRLC = ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV16_gc;

  // give ADC 32 ADC clocks to stabilise after enable (~25 µs @ 1.25 MHz).
  ADC0.CTRLD = ADC_INITDLY_DLY32_gc;

  ADC0.CTRLA = ADC_ENABLE_bm;
}

// bootstrap to be called at the beginning of each firmware
// we intentionally don't set up PWM here - that allows us to use this
// in our motor bitbang test firmware as well
//
// as we expect this to be the first thing to be run we take care of disabling
// and enabling the watchdog - just to avoid unpleasant surprises on refactoring
// it still is recommended to start any firmware main with wdt_disable()
static inline void bootstrap() {
  wdt_disable();
  initClock();
  initPins();
  initADC();
  initUART();
  sei();
  wdt_enable(WDT_PERIOD_256CLK_gc);
}

// mapping function, similar to what Arduino is doing
static inline int16_t mapValue(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


// PWM helpers
//
// initialise TCA0 in split mode and enable waveform output on all channels.
// PA4/PA5 and PB0/PB1 are the default pins for WO4/WO5 and WO0/WO1,
// so no PORTMUX is required. If you need a PWM channel not going via the motor
// controller your only option would be WO3 on PA3 (J9), also default pin. If
// you need more you'll either have to bitbang, or remap serial: WO2 is on PB2,
// WO3 can be remapped to PB3. The alt serial pins PA1/PA2 are available on
// J8 and J10.
//
// if a caller needs only one motor, just set the other to 0 via setMotorXxx(0).
static inline void initPWM() {
  // insure default TCA0 pin mappings - this is mostly relevant for testing:
  // without a proper reset after testing other PORTMUX settings this might
  // just behave in weird ways. On a clean board this is just a NOOP.
  PORTMUX.CTRLC &= ~(PORTMUX_TCA00_bm | PORTMUX_TCA01_bm);

  // halt timer before enabling split mode (datasheet requirement, may have
  // fixed odd behaviour of the port motor)
  TCA0.SPLIT.CTRLA = 0;

  // TCA0 split mode:
  // L-counter: LCMP0->PB0 (INA2, stbd forward)
  //            LCMP1->PB1 (INB2, stbd backward)
  // H-counter: HCMP1->PA4 (INA1, port forward)
  //            HCMP2->PA5 (INB1, port backward)
  TCA0.SPLIT.CTRLD = TCA_SPLIT_SPLITM_bm;
  TCA0.SPLIT.CTRLA = TCA_SPLIT_CLKSEL_DIV1_gc | TCA_SPLIT_ENABLE_bm;
  TCA0.SPLIT.CTRLB = TCA_SPLIT_LCMP0EN_bm      // PB0 = stbd forward
                     | TCA_SPLIT_LCMP1EN_bm    // PB1 = stbd backward
                     | TCA_SPLIT_HCMP1EN_bm    // PA4 = port forward
                     | TCA_SPLIT_HCMP2EN_bm;   // PA5 = port backward
  TCA0.SPLIT.LPER = 255;
  TCA0.SPLIT.HPER = 255;
  TCA0.SPLIT.LCMP0 = 0;
  TCA0.SPLIT.LCMP1 = 0;
  TCA0.SPLIT.HCMP1 = 0;
  TCA0.SPLIT.HCMP2 = 0;
}

// we always clear the active channel before setting the new one: doing it the
// other way round would short the pins on the MX1616, which is documented (but
// discouraged) brake behaviour
static inline void setMotorPort(int16_t speed) {
  if (speed > 255) speed = 255;
  if (speed < -255) speed = -255;

  if (speed >= 0) {
    TCA0.SPLIT.HCMP2 = 0;                     // clear reverse first
    TCA0.SPLIT.HCMP1 = (uint8_t)speed;        // then set forward
  } else {
    TCA0.SPLIT.HCMP1 = 0;                     // clear forward first
    TCA0.SPLIT.HCMP2 = (uint8_t)(-speed);     // then set reverse
  }
}

static inline void setMotorStarboard(int16_t speed) {
  if (speed > 255) speed = 255;
  if (speed < -255) speed = -255;

  if (speed >= 0) {
    TCA0.SPLIT.LCMP1 = 0;                     // clear reverse first
    TCA0.SPLIT.LCMP0 = (uint8_t)speed;        // then set forward
  } else {
    TCA0.SPLIT.LCMP0 = 0;                     // clear forward first
    TCA0.SPLIT.LCMP1 = (uint8_t)(-speed);     // then set reverse
  }
}

static inline void stopAll() {
  setMotorPort(0);
  setMotorStarboard(0);
}

// sensing parts

static inline uint16_t readADC(uint8_t muxpos) {
  ADC0.MUXPOS = muxpos;
  _delay_us(25); // delay is required for accurate readings
  ADC0.COMMAND = ADC_STCONV_bm;
  while (!(ADC0.INTFLAGS & ADC_RESRDY_bm));
  uint16_t val = ADC0.RES;
  ADC0.INTFLAGS = ADC_RESRDY_bm;  // clear flag
  return val;
}

static inline bool isWaterDetected() {
  uint16_t raw = readADC(ADC_MUXPOS_AIN7_gc);
  // this returns true if the readout is > 1020
  // the circuit seems to be sensitive enough that it always returns
  // 1023 when making any kind of contact
  return raw > 1020;
}

#endif // COMMON_HPP
