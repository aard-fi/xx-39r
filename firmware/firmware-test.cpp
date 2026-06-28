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

// current motor speeds for status reporting (only accessed from main)
static int16_t leftSpeed = 0;
static int16_t rightSpeed = 0;

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
      uartPrint("XX-39R motor test firmware ready. Send 'H' for help.");
      uartPrint(" W=");
      uartPutc(water_detected ? '1' : '0');

      uartPrint(" V=");
      printNumber((int16_t)battery_data);
      uartPrint("mV");

      uartPrint(" F=");
      printNumber((int16_t)(readClockHz() / 1000));
      uartPrint("kHz");

      uartPrintLn("");
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
  uartPutc(water_detected ? '1' : '0');

  uartPrint(" V=");
  printNumber((int16_t)readBatteryData());
  uartPrint("mV");

  uartPrint(" F=");
  printNumber((int16_t)(readClockHz() / 1000));
  uartPrint("kHz");

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

  // auto-ramp helper: steps from current to target in 25-unit increments
  // with 40 ms pauses so the motor smoothly reaches the commanded speed.
  // we need this to bypass our rapid change protection in the main functions -
  // which are there so firmware can't bypass that. Problem is that a RC TX
  // will keep spamming the channel value until it changes, while that'd be
  // pretty annoying with the shitty serial interface we have. So we just
  // pretend we're spamming it here until teh value matches.

  auto ramp = [](int16_t &tracked, void (*setMotor)(int16_t), int16_t target) {
    int16_t step = (target > tracked) ? 25 : -25;
    while (tracked != target) {
      tracked += step;
      if ((step > 0 && tracked > target) || (step < 0 && tracked < target))
        tracked = target;
      setMotor(tracked);
      for (uint8_t i = 0; i < 4; i++) {
        _delay_ms(10);
        wdt_reset();
      }
    }
  };

  switch (cmd) {
    case 'L':
    case 'l':
      ramp(leftSpeed, setMotorPort, val);
      uartPrint("L=");
      printNumber(leftSpeed);
      uartPrintLn("");
      break;

    case 'R':
    case 'r':
      ramp(rightSpeed, setMotorStarboard, val);
      uartPrint("R=");
      printNumber(rightSpeed);
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
      uartPrintLn("  S        Status (speeds, water, battery)");
      uartPrintLn("  H / ?    Help");
      uartPrintLn("  <cr>     Stop all");
      break;

    default:
      stopAll();
      uartPrintLn("?");
      break;
  }
}
