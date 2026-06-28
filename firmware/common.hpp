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

// TCA split-mode timer period (LPER/HPER).  This sets the PWM carrier
// frequency, NOT the maximum motor duty cycle.  The motor still sees 0..255
// as its full range; values are scaled to 0..PWM_MAX internally before
// writing the compare registers.
//
// The default is ~78 kHZ max at 20MHz clock. Low speed behaviour of the motors
// seems better at high PWM frequencies, so we try to bump that up for lower
// clock speed (at the expense of PWM resolution).
//
// PWM_MAX = 160 means 20 MHz / (160+1) = 124 Hz, or, at the 10MHz this is
// recommended to be used, about 62 Hz.
#ifndef PWM_MAX
#define PWM_MAX 160
#endif

// convert a user-level motor command (0..255 or -255..0) to the timer
// compare register range (0..PWM_MAX). This allows us to stick with the
// more common 0..255 PWM range in the rest of the code to avoid confusion
static inline uint8_t pwmScale(uint8_t speed) {
  return (uint8_t)(((uint16_t)speed * PWM_MAX + (255 / 2)) / 255);
}

// frequency-dependent defaults (overridden by Makefile)
// there should be no need to ever touch this - we just need this to be able to
// build firmware for multiple clocks
#ifndef USART_BAUD
#define USART_BAUD 694
#endif
#ifndef TCB_CCMP
#define TCB_CCMP 50000
#endif
#ifndef CLK_PRESCALER
#define CLK_PRESCALER 1
#endif

// parsed telemetry data
typedef struct {
    uint16_t steering; // Ch3 (Yaw/Rudder — left stick left/right)
    uint16_t throttle; // Ch1 (Pitch — right stick up/down, spring-centered)
    bool aux1;         // Ch5 (Toggle)
    uint8_t aux2;      // Ch6 (Tri-state: 0=Low, 1=Mid, 2=High)
    uint8_t aux3;      // Ch7 (Tri-state)
    bool aux4;         // Ch8 (Toggle)
    bool is_new_data;  // Flag to tell the model-specific loop to execute
} crsf_telemetry_t;

static volatile crsf_telemetry_t rc_data =
{992, 172, false, 1, 1, false, false};

typedef enum { STATE_ADDR, STATE_LEN, STATE_TYPE, STATE_PAYLOAD } crsf_state_t;
static crsf_state_t parser_state = STATE_ADDR;
static uint8_t payload_idx = 0;
static uint8_t packet_buffer[22];

// count bytes received outside a clean frame - and reset if we go through
// too many bytes without valid packet. Should help with desync issues.
static uint8_t parser_garbage_count = 0;
#define PARSER_GARBAGE_LIMIT 80

#define WATER_SENSE_bm PIN7_bm

// water safety compile-time mode:
//   0 = stop when dry, resume when wet (default for boats)
//   1 = trip when wet, require reset (latched fault)
#ifndef WATER_SAFETY_MODE
#define WATER_SAFETY_MODE 0
#endif

static volatile uint8_t water_detected = 0;
static volatile uint16_t battery_data = 0;

// attempt at atomic copy of battery_data
static inline uint16_t readBatteryData(void) {
  uint16_t val;
  cli();
  val = battery_data;
  sei();
  return val;
}

// motor soft-limiting: when stopAll() or failsafe fires, these flags tell
// setMotorPort/Starboard to reset their internal rate-limit state so the
// next commanded speed starts from a known idle instead of a stale value.
static volatile uint8_t motor_port_reset = 0;
static volatile uint8_t motor_starboard_reset = 0;

// low-voltage cutoff: stop everything if battery measurement drops below this.
// set to 0 to disable.  Default is 2800 mV (conservative for 3xAA/alkaline).
// When changing this take tolerances of the comparator into account, and set
// it at least 10% lower than what you actually want.
#ifndef LOW_VOLTAGE_CUTOFF_MV
#define LOW_VOLTAGE_CUTOFF_MV 2800
#endif

#define BATTERY_MEASURE_INTERVAL 200  // 200 * 5ms = 1 second

static volatile uint16_t battery_timer_count = 0;

// TCB0: periodic tick (~5 ms).  Every tick we trigger a single-shot ADC
// conversion on AIN7 and check the result.  Every 200 ticks we also do
// an INTREF measurement for battery voltage.
//
// We do NOT use free-run mode. Every conversion is explicitly triggered
// so there is never a phantom conversion in flight when we change MUXPOS.
// We tried to be smart about that earlier with the water sensing fully
// utilising free-run, but that caused sync issues with the interrupt handling.
// We still measure fast enough that even with a few missed measurements safety
// should kick in fast enough.
ISR(TCB0_INT_vect) {
  TCB0.INTFLAGS = TCB_CAPT_bm;

  // water sensor (every tick)
  ADC0.MUXPOS = ADC_MUXPOS_AIN7_gc;
  ADC0.COMMAND = ADC_STCONV_bm;
  while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) {
    wdt_reset();
  }
  uint16_t water_sample = ADC0.RES;

// this is currently badly handled - mode 0 follows the water safety status,
// while mode 1 needs a manuel reset once triggered. That's OK as long as we're
// only doing the water checks in the firmware itself - but if we ever implement
// water shutoffs in common functions we'd need to have a third compile time
// option.
#if WATER_SAFETY_MODE == 0
  water_detected = (water_sample > 1000);
#else
  if (water_sample > 1000) water_detected = 1;
#endif

  // battery voltage (every ~1 s)
  battery_timer_count++;
  if (battery_timer_count >= BATTERY_MEASURE_INTERVAL) {
    battery_timer_count = 0;

    // switch to INTREF for battery measurement.
    // the S/H capacitor may still hold the AIN7 voltage, so the first
    // conversion after MUXPOS change seems to sometimes read garbage.  Trigger
    // a dummy conversion and discard it; the second one is accurate. No idea if
    // that was actually the issue - but at least this reliably solves it.
    ADC0.MUXPOS = ADC_MUXPOS_INTREF_gc;
    ADC0.COMMAND = ADC_STCONV_bm;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) {
      wdt_reset();
    }
    (void)ADC0.RES; // discard first reading

    ADC0.COMMAND = ADC_STCONV_bm;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) {
      wdt_reset();
    }
    uint16_t sample = ADC0.RES;
    if (sample > 0) {
      // VCC = (1.1V * 1024) / sample, in millivolts
      battery_data = (1126400UL / sample);
    }

    // switch back to AIN7.  Same S/H settling issue: do one dummy
    // conversion and discard it so the next water reading is accurate.
    ADC0.MUXPOS = ADC_MUXPOS_AIN7_gc;
    ADC0.COMMAND = ADC_STCONV_bm;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) {
      wdt_reset();
    }
    (void)ADC0.RES; // discard first reading
  }
}
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
  // print up to 5 digits (uint16_t max = 65535)
  uint16_t div = 10000;
  bool leading = true;
  while (div >= 1) {
    uint8_t digit = v / div;
    if (digit > 0 || !leading || div == 1) {
      uartPutc('0' + digit);
      leading = false;
    }
    v %= div;
    div /= 10;
  }
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
// note that depending on fuse settings there also could be 4 and 8MHz
// variants - but we don't support those.
static inline void initClock() {
#if CLK_PRESCALER == 1
  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0x00);  // 20 or 16 MHz, depending on fuse
#elif CLK_PRESCALER == 2
  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, CLKCTRL_PEN_bm);    // /2 -> 10 MHz
#elif CLK_PRESCALER == 4
  _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, CLKCTRL_PEN_bm | CLKCTRL_PDIV_4X_gc);  // /4 -> 5 MHz
#else
#error Unsupported CLK_PRESCALER value
#endif
}

// trigger a clean software reset (WDT, RSTFR flags, etc.).
// Use only when the system is in an unrecoverable state.
static inline void triggerSoftwareReset(void) {
  _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRE_bm);
}

// read back the effective CPU clock frequency in Hz. Currently that's only used
// in the test firmware, as extra validation that the frequency switching looks
// good. We generally could also add frequency specific features to models,
// though. This uses MCLKCTRLB to decode the prescaler and FUSE.OSCCFG to get the
// OSCHF base frequency (16 MHz or 20 MHz).
static inline uint32_t readClockHz(void) {
  uint8_t ctrlb = CLKCTRL.MCLKCTRLB;

  // base frequency from OSCCFG fuse: bits 1:0 = FREQSEL
  //   0b01 -> 16 MHz, 0b10 -> 20 MHz
  uint8_t osccfg = FUSE_OSCCFG;
  uint32_t base = (osccfg & 0x02) ? 20000000UL : 16000000UL;

  if (!(ctrlb & CLKCTRL_PEN_bm))
    return base;  // prescaler disabled

  uint8_t pdiv = (ctrlb & CLKCTRL_PDIV_gm) >> CLKCTRL_PDIV_gp;
  switch (pdiv) {
    case 0x0: return base / 2;    // 2X
    case 0x1: return base / 4;    // 4X
    case 0x2: return base / 8;    // 8X
    case 0x3: return base / 16;   // 16X
    case 0x4: return base / 32;   // 32X
    case 0x5: return base / 64;   // 64X
    case 0x8: return base / 6;    // 6X
    case 0x9: return base / 10;   // 10X
    case 0xA: return base / 12;   // 12X
    case 0xB: return base / 24;   // 24X
    case 0xC: return base / 48;   // 48X
    default:  return base / 2;
  }
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

// UART setup (PB2/PB3, 115200 baud — BAUD value derived from F_CPU)
static inline void initUART() {
  USART0.BAUD = USART_BAUD;
  USART0.CTRLB = USART_RXEN_bm | USART_TXEN_bm;
  USART0.CTRLA = USART_RXCIE_bm | USART_DREIE_bm;
}

// ADC setup for water sensing (polling mode, used by test firmwares). Further
// ADC setup builds on top of that, so we always call that one in bootstrap.
static inline void initADC() {
  // 10-bit ADC, VDD reference, prescaler /16 (1.25 MHz ADC clock @ 20 MHz CPU)
  ADC0.CTRLC = ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV16_gc;

  // give ADC 32 ADC clocks to stabilise after enable (~25 µs @ 1.25 MHz).
  ADC0.CTRLD = ADC_INITDLY_DLY32_gc;

  ADC0.CTRLA = ADC_ENABLE_bm;
}

// ADC setup for main firmware: water sensing + INTREF battery measurement.
// Conversions are triggered explicitly by TCB0; no free-run mode is used.
// this naming is a leftover from our free run experiments - might want to
// clean that up eventually.
static inline void initADC_WindowMode() {
  // enable internal 1.1V reference output (needed for INTREF measurement)
  VREF.CTRLA = VREF_ADC0REFEN_bm | VREF_ADC0REFSEL_1V1_gc;

  // disable ADC while reconfiguring
  ADC0.CTRLA = 0;

  // select water channel; conversions are triggered explicitly by TCB0
  ADC0.MUXPOS = ADC_MUXPOS_AIN7_gc;
  ADC0.CTRLA = ADC_ENABLE_bm;
}

// TCB0 periodic timer: ~5ms tick, counted to ~1 second for battery reads
static inline void initBatteryTimer() {
  TCB0.CTRLB = TCB_CNTMODE_INT_gc;                     // periodic interrupt
  TCB0.CCMP = TCB_CCMP;                                // 5ms @ CLK_PER/2
  TCB0.INTCTRL = TCB_CAPT_bm;
  TCB0.CTRLA = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm;  // start
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
#ifndef NO_ADC_INTERRUPTS
  initADC_WindowMode();
  initBatteryTimer();
#endif
  initUART();
  sei();
  // WDT timeout: 1 second (WDTCFG fuse can override this, but setting it
  // here makes sure the period is reasonable even if fuses are at defaults).
  wdt_enable(WDT_PERIOD_1KCLK_gc);
}

// mapping function, similar to what Arduino is doing
static inline int16_t mapValue(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// minimum PWM value that actually moves the motor
// below this the motor stalls. Used by the main firmware to rescale
// joystick output so the usable range starts here. 0 = disabled.
#ifndef MOTOR_STALL_SPEED
#define MOTOR_STALL_SPEED 0
#endif

// rescale a clamped ±255 motor value so the usable range begins at
// MOTOR_STALL_SPEED (and -MOTOR_STALL_SPEED in reverse).  0 stays 0.
// Used only by the main firmware; test firmwares bypass this.
#if MOTOR_STALL_SPEED > 0
static inline int16_t applyStallMap(int16_t speed) {
  if (speed > 0)
    return mapValue(speed, 0, 255, MOTOR_STALL_SPEED, 255);
  if (speed < 0)
    return mapValue(speed, -255, 0, -255, -MOTOR_STALL_SPEED);
  return 0;
}
#else
static inline int16_t applyStallMap(int16_t speed) { return speed; }
#endif

// clamp raw motor value to ±255, then apply stall-deadband remapping.
// convenience wrapper used only by the main firmware.
static inline int16_t prepareMotorSpeed(int16_t raw) {
  if (raw > 255) raw = 255;
  if (raw < -255) raw = -255;
  return applyStallMap(raw);
}

// optional motor kickstart
//
// uncomment to enable kickstart pulses for low-speed motor startup, or pass
// as compiler flag:
// #define MOTOR_KICKSTART

#ifdef MOTOR_KICKSTART
#define MOTOR_KICK_THRESHOLD       150  // PWM speed below which kick may apply
#define MOTOR_KICK_STRENGTH        200  // PWM pulse that should be used for kicks
#define MOTOR_KICK_START_PULSES      4  // control-loop iterations to kick on startup
#define MOTOR_KICK_HOLD_INTERVAL   180  // re-kick interval while crawling. this
                                        // ensures motors don't stall at low speed

static inline int16_t motorKickApply(int16_t speed,
                                     int16_t *last,
                                     uint8_t *kick,
                                     uint8_t *hold) {
  int16_t applied = speed;
  if (speed != 0 && speed > -MOTOR_KICK_THRESHOLD && speed < MOTOR_KICK_THRESHOLD) {
    if (*last > -8 && *last < 8) {
      *kick = MOTOR_KICK_START_PULSES;
    } else if (++(*hold) > MOTOR_KICK_HOLD_INTERVAL) {
      *kick = 2;
      *hold = 0;
    }
  } else {
    *hold = 0;
  }
  if (*kick) {
    applied = (speed > 0) ? MOTOR_KICK_STRENGTH : -MOTOR_KICK_STRENGTH;
    (*kick)--;
  }
  *last = speed;
  return applied;
}
#endif

// motor soft-limiting: direction-change dead time and rate limiting.
// These protect the regulator from regenerative voltage spikes (especially
// when snapping from full forward to full reverse) and make the boat more
// controllable for kids.
#ifndef MOTOR_DEAD_TIME_TICKS
#define MOTOR_DEAD_TIME_TICKS 8   // coast for ~50-80ms when reversing direction
#endif
#ifndef MOTOR_ACCEL_LIMIT
#define MOTOR_ACCEL_LIMIT 25      // max PWM change per packet when speeding up
#endif
#ifndef MOTOR_DECEL_LIMIT
#define MOTOR_DECEL_LIMIT 50      // max PWM change per packet when slowing down
#endif

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
  // PORTMUX.CTRLC is CCP-protected on ATtiny414.
  uint8_t portmux_val = PORTMUX.CTRLC;
  portmux_val &= ~(PORTMUX_TCA00_bm | PORTMUX_TCA01_bm);
  _PROTECTED_WRITE(PORTMUX.CTRLC, portmux_val);

  // halt timer before enabling split mode (datasheet requirement, may have
  // fixed odd behaviour of the port motor)
  TCA0.SPLIT.CTRLA = 0;

  // TCA0 split mode:
  // L-counter: LCMP0->PB0 (INA2, stbd forward)
  //            LCMP1->PB1 (INB2, stbd backward)
  // H-counter: HCMP1->PA4 (INA1, port forward)
  //            HCMP2->PA5 (INB1, port backward)
  TCA0.SPLIT.CTRLD = TCA_SPLIT_SPLITM_bm;
  // with my test motors DIV1 starts motors at roughly 140 PWM signal, DIV4
  // at 150 and DIV16 at 160 - so seems it makes sense to stick with a
  // relatively high PWM frequency here
  TCA0.SPLIT.CTRLA = TCA_SPLIT_CLKSEL_DIV1_gc | TCA_SPLIT_ENABLE_bm;
  TCA0.SPLIT.CTRLB = TCA_SPLIT_LCMP0EN_bm      // PB0 = stbd forward
    | TCA_SPLIT_LCMP1EN_bm    // PB1 = stbd backward
    | TCA_SPLIT_HCMP1EN_bm    // PA4 = port forward
    | TCA_SPLIT_HCMP2EN_bm;   // PA5 = port backward
  TCA0.SPLIT.LPER = PWM_MAX;
  TCA0.SPLIT.HPER = PWM_MAX;
  TCA0.SPLIT.LCMP0 = 0;
  TCA0.SPLIT.LCMP1 = 0;
  TCA0.SPLIT.HCMP1 = 0;
  TCA0.SPLIT.HCMP2 = 0;
}

// we always clear the previously active channel before setting the new one.
// Setting both forward and reverse simultaneously shorts the MX1616's H-bridge
// outputs together — the datasheet calls this "brake" and warns against it.
static inline void setMotorPort(int16_t speed) {
  if (speed > 255) speed = 255;
  if (speed < -255) speed = -255;

#ifdef MOTOR_KICKSTART
  static int16_t last_speed = 0;
  static uint8_t kick_count = 0;
  static uint8_t hold_ticks = 0;
  int16_t target = motorKickApply(speed, &last_speed, &kick_count, &hold_ticks);
  bool kick_active = (target != speed);
#else
  int16_t target = speed;
  bool kick_active = false;
#endif

  // soft-limiting state
  static int16_t last_applied = 0;
  static uint8_t dead_time = 0;

  // if stopAll() or failsafe just fired, reset our tracked state so we
  // don't resume from a stale high value.
  if (motor_port_reset) {
    last_applied = 0;
    dead_time = 0;
    motor_port_reset = 0;
  }

  // direction-change dead time: coast when reversing to prevent
  // regenerative voltage spikes that can damage the regulator.
  if (dead_time) {
    dead_time--;
    // cancel early if user returned to the original direction.
    if ((last_applied > 0 && target >= 0) || (last_applied < 0 && target <= 0)) {
      dead_time = 0;
    } else {
      TCA0.SPLIT.HCMP1 = 0;  // coast
      TCA0.SPLIT.HCMP2 = 0;
      last_applied = 0;
      return;
    }
  }

  // detect a genuine direction reversal (non-zero crossing).
  //
  // EDGE CASE: if the transmitter happens to send a centre (0) packet
  // between +255 and -255, last_applied becomes 0, so the reversal
  // check below doesn't trigger.  The motor coasted briefly during
  // deceleration, which is usually enough to prevent a damaging spike.
  // If this proves insufficient in practice we'd need to add something
  // like a "recent_direction" variable that decays over a few packets after
  // reaching 0.
  if ((last_applied > 0 && target < 0) || (last_applied < 0 && target > 0)) {
    dead_time = MOTOR_DEAD_TIME_TICKS;
    TCA0.SPLIT.HCMP1 = 0;  // coast
    TCA0.SPLIT.HCMP2 = 0;
    last_applied = 0;
    return;
  }

  // rate limit: cap maximum change per call so kids can't snap from
  // idle to full throttle instantly.  Kickstart pulses bypass this.
  if (!kick_active) {
    int16_t diff = target - last_applied;
    int16_t limit = (abs(target) > abs(last_applied)) ? MOTOR_ACCEL_LIMIT
      : MOTOR_DECEL_LIMIT;
    if (diff > limit) diff = limit;
    if (diff < -limit) diff = -limit;
    target = last_applied + diff;
  }

  if (target >= 0) {
    TCA0.SPLIT.HCMP2 = 0;                            // clear reverse first
    TCA0.SPLIT.HCMP1 = pwmScale((uint8_t)target);      // then set forward
  } else {
    TCA0.SPLIT.HCMP1 = 0;                            // clear forward first
    TCA0.SPLIT.HCMP2 = pwmScale((uint8_t)(-target));   // then set reverse
  }

  last_applied = target;
}

static inline void setMotorStarboard(int16_t speed) {
  if (speed > 255) speed = 255;
  if (speed < -255) speed = -255;

#ifdef MOTOR_KICKSTART
  static int16_t last_speed = 0;
  static uint8_t kick_count = 0;
  static uint8_t hold_ticks = 0;
  int16_t target = motorKickApply(speed, &last_speed, &kick_count, &hold_ticks);
  bool kick_active = (target != speed);
#else
  int16_t target = speed;
  bool kick_active = false;
#endif

  static int16_t last_applied = 0;
  static uint8_t dead_time = 0;

  if (motor_starboard_reset) {
    last_applied = 0;
    dead_time = 0;
    motor_starboard_reset = 0;
  }

  // see setMotorPort() comment about the edge case where a centre (0)
  // packet slips between full forward and full reverse.

  if (dead_time) {
    dead_time--;
    if ((last_applied > 0 && target >= 0) || (last_applied < 0 && target <= 0)) {
      dead_time = 0;
    } else {
      TCA0.SPLIT.LCMP0 = 0;
      TCA0.SPLIT.LCMP1 = 0;
      last_applied = 0;
      return;
    }
  }

  if ((last_applied > 0 && target < 0) || (last_applied < 0 && target > 0)) {
    dead_time = MOTOR_DEAD_TIME_TICKS;
    TCA0.SPLIT.LCMP0 = 0;
    TCA0.SPLIT.LCMP1 = 0;
    last_applied = 0;
    return;
  }

  if (!kick_active) {
    int16_t diff = target - last_applied;
    int16_t limit = (abs(target) > abs(last_applied)) ? MOTOR_ACCEL_LIMIT
      : MOTOR_DECEL_LIMIT;
    if (diff > limit) diff = limit;
    if (diff < -limit) diff = -limit;
    target = last_applied + diff;
  }

  if (target >= 0) {
    TCA0.SPLIT.LCMP1 = 0;                            // clear reverse first
    TCA0.SPLIT.LCMP0 = pwmScale((uint8_t)target);     // then set forward
  } else {
    TCA0.SPLIT.LCMP0 = 0;                            // clear forward first
    TCA0.SPLIT.LCMP1 = pwmScale((uint8_t)(-target));   // then set reverse
  }

  last_applied = target;
}

static inline void stopAll() {
  // immediate register clear — bypasses any soft-limiting in setMotorXxx
  TCA0.SPLIT.HCMP1 = 0;
  TCA0.SPLIT.HCMP2 = 0;
  TCA0.SPLIT.LCMP0 = 0;
  TCA0.SPLIT.LCMP1 = 0;
  motor_port_reset = 1;
  motor_starboard_reset = 1;
}

// sensing parts

static inline uint16_t readADC(uint8_t muxpos) {
  ADC0.MUXPOS = muxpos;
  _delay_us(25); // delay is required for accurate readings
  ADC0.COMMAND = ADC_STCONV_bm;
  while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) {
    wdt_reset();
  }
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

// the crsf handling. All the existing libraries are too big for this tiny chip,
// so we need to handroll "good enough" for this thing.

// CRSF CRC-8 (DVB-S2, polynomial 0xD5).  Covers frame length + type + payload.
static inline uint8_t crsfCRC8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0xD5;
      else              crc <<= 1;
    }
  }
  return crc;
}

static inline void processCrsfBuffer(void) {
  while (rx_head != rx_tail) {
    uint8_t b = rx_buffer[rx_tail];
    rx_tail = (rx_tail + 1) & (RX_BUFFER_SIZE - 1);

    // reset if we can't find a clean packet
    if (parser_state != STATE_ADDR) {
      if (++parser_garbage_count >= PARSER_GARBAGE_LIMIT) {
        parser_garbage_count = 0;
        parser_state = STATE_ADDR;
        payload_idx = 0;
        stopAll();
        continue;
      }
    }

    switch (parser_state) {
      case STATE_ADDR:
        if (b == 0xC8) {
          parser_state = STATE_LEN;
        } else {
          // not a sync byte — count toward garbage limit
          // (already incremented above)
        }
        break;

      case STATE_LEN:
        if (b == 24) parser_state = STATE_TYPE;
        else parser_state = STATE_ADDR;
        break;

      case STATE_TYPE:
        if (b == 0x16) {
          payload_idx = 0;
          parser_state = STATE_PAYLOAD;
        } else {
          parser_state = STATE_ADDR;
        }
        break;

      case STATE_PAYLOAD:
        if (payload_idx < 22) {
          packet_buffer[payload_idx++] = b;
        } else {
          // b is the CRC byte.  Build the CRC input: [length, type, payload...]
          uint8_t crc_buf[24];
          crc_buf[0] = 24;        // frame length
          crc_buf[1] = 0x16;      // frame type (channels)
          for (uint8_t i = 0; i < 22; i++) crc_buf[i + 2] = packet_buffer[i];
          uint8_t expected_crc = crsfCRC8(crc_buf, 24);

          if (b == expected_crc) {
            // CRC OK — unpack channels
            rc_data.steering = ((packet_buffer[4] >> 1) | (packet_buffer[5] << 7)) & 0x07FF;
            rc_data.throttle = ((packet_buffer[1] >> 3) | (packet_buffer[2] << 5)) & 0x07FF;

            uint16_t ch5 = ((packet_buffer[6] >> 7) | (packet_buffer[7] << 1) | (packet_buffer[8] << 9)) & 0x07FF;
            rc_data.aux1 = (ch5 > 992);

            uint16_t ch6 = ((packet_buffer[8] >> 2) | (packet_buffer[9] << 6)) & 0x07FF;
            if (ch6 < 500)       rc_data.aux2 = 0;
            else if (ch6 > 1500) rc_data.aux2 = 2;
            else                 rc_data.aux2 = 1;

            uint16_t ch7 = ((packet_buffer[9] >> 5) | (packet_buffer[10] << 3)) & 0x07FF;
            if (ch7 < 500)       rc_data.aux3 = 0;
            else if (ch7 > 1500) rc_data.aux3 = 2;
            else                 rc_data.aux3 = 1;

            uint16_t ch8 = (packet_buffer[11] | (packet_buffer[12] << 8)) & 0x07FF;
            rc_data.aux4 = (ch8 > 992);

            rc_data.is_new_data = true;
            parser_garbage_count = 0;  // clean frame resets garbage counter
          }
          // else CRC mismatch — drop packet, don't set is_new_data
          parser_state = STATE_ADDR;
        }
        break;
    }
  }
}

#endif // COMMON_HPP
