/**
 * @file firmware-test.cpp
 * @copyright Copyright (c) 2026 Bernd Wachter
 * @author Bernd Wachter <bernd-github@wachter.fi>
 * @date 2026
 *
 * Test firmware for manual motor control via UART.
 *
 * Test UART (for bench debugging) uses hardware USART0 on the
 * ELRS header. Connect GND and RX/TX to a UART adapter.
 *
 * Simple text protocol:
 *   L <value>   Set left motor (-255..255)
 *   R <value>   Set right motor (-255..255)
 *   S           Print status
 *   H / ?       Print help
 * Any invalid command stops both motors (fail-safe).
 */

#include <avr/version.h>

#include "common.hpp"

void printStatus();
void processLine(char *line);

#define LINE_BUF_SIZE 32

// Current motor speeds for status reporting
static volatile int16_t leftSpeed = 0;
static volatile int16_t rightSpeed = 0;

int main() {
  wdt_disable();

  bootstrap();
  initPWM();
  stopAll();

  char line[LINE_BUF_SIZE];
  uint8_t idx = 0;
  bool got_input = false;

  while (1) {
    wdt_reset();

    if (!got_input) {
      uartPrintLn("XX-39R motor test firmware ready. Send 'H' for help.");
      for (uint8_t i = 0; i < 5; i++) {
        _delay_ms(40);
        wdt_reset();
      }
    }

    if (uartAvailable()) {
      got_input = true;
      char c = uartRead();
      uartPutc(c);

      if (c == '\r' || c == '\n') {
        if (idx > 0) {
          line[idx] = '\0';
          processLine(line);
          idx = 0;
        }
      } else if (idx < LINE_BUF_SIZE - 1) {
        line[idx++] = c;
      }
    }
  }

  return 0;
}

void printStatus() {
  uartPrint("L=");
  if (leftSpeed > 0) uartPrint("FWD:");
  else if (leftSpeed < 0) uartPrint("REV:");
  else uartPrint("OFF:");
  printNumber(leftSpeed);

  uartPrint(" R=");
  if (rightSpeed > 0) uartPrint("FWD:");
  else if (rightSpeed < 0) uartPrint("REV:");
  else uartPrint("OFF:");
  printNumber(rightSpeed);

  uartPrint(" W=");
  uartPutc(isWaterDetected() ? '1' : '0');
  uartPrint(" raw=");
  printNumber(readADC(ADC_MUXPOS_AIN7_gc));

  uartPrintLn("");
}

void processLine(char *line) {
  while (*line == ' ' || *line == '\t') line++;

  char cmd = *line;
  if (cmd == '\0') {
    stopAll();
    uartPrintLn("STOP");
    return;
  }

  char *num = line + 1;
  while (*num == ' ' || *num == '\t') num++;

  int16_t val = 0;
  if (*num != '\0') {
    val = (int16_t)atoi(num);
  }

  switch (cmd) {
    case 'L':
    case 'l':
      setMotorPort(val);
      uartPrint("L=");
      uartPutc(val >= 0 ? '+' : '-');
      uartPrintLn("");
      break;

    case 'R':
    case 'r':
      setMotorStarboard(val);
      uartPrint("R=");
      uartPutc(val >= 0 ? '+' : '-');
      uartPrintLn("");
      break;

    case 'S':
    case 's':
      printStatus();
      break;

    case 'H':
    case 'h':
    case '?':
      uartPrintLn("Commands:");
      uartPrintLn("  L <val>  Left motor (-255..255)");
      uartPrintLn("  R <val>  Right motor (-255..255)");
      uartPrintLn("  S        Status (speeds + water sense)");
      uartPrintLn("  H / ?    Help");
      uartPrintLn("  <cr>     Stop all");
      break;

    default:
      stopAll();
      uartPrintLn("?");
      break;
  }
}
