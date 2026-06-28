#!/usr/bin/env bash
#
# XX-39R Firmware Flasher for macOS / Linux
#
# Usage:
#   ./flash.sh              # interactive mode
#   ./flash.sh -p /dev/ttyUSB0  # specify port directly
#
# Requires: avrdude with serialupdi support
#

set -euo pipefail

# ANSI colours (disable if not a tty)
if [[ -t 1 ]]; then
  C_RED='\033[1;31m'
  C_GREEN='\033[1;32m'
  C_YELLOW='\033[1;33m'
  C_BLUE='\033[1;34m'
  C_CYAN='\033[1;36m'
  C_RESET='\033[0m'
else
  C_RED=''
  C_GREEN=''
  C_YELLOW=''
  C_BLUE=''
  C_CYAN=''
  C_RESET=''
fi

AVRDUDE="${AVRDUDE:-avrdude}"
MCU="t414"
PORT=""
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Default to ../firmware/build (scripts/ inside release zip sits next to firmware/)
HEX_DIR="$(cd "${SCRIPT_DIR}/../firmware/build" 2>/dev/null || cd "${SCRIPT_DIR}/../firmware" 2>/dev/null && pwd)"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

banner() {
  echo ""
  echo -e "${C_BLUE}========================================${C_RESET}"
  echo -e "${C_BLUE}  XX-39R Firmware Flasher${C_RESET}"
  echo -e "${C_BLUE}========================================${C_RESET}"
  echo ""
}

die() {
  echo -e "${C_RED}ERROR: $*${C_RESET}" >&2
  exit 1
}

warn() {
  echo -e "${C_YELLOW}WARNING: $*${C_RESET}" >&2
}

info() {
  echo -e "${C_CYAN}$*${C_RESET}"
}

ok() {
  echo -e "${C_GREEN}$*${C_RESET}"
}

prompt() {
  echo -n -e "${C_CYAN}$*${C_RESET}"
}

# ---------------------------------------------------------------------------
# Port detection
# ---------------------------------------------------------------------------

# Outputs one candidate port per line.  Does NOT do any interactive I/O —
# that must happen in the main shell so the user can see prompts.
find_ports() {
  # macOS
  for p in /dev/tty.usbserial* /dev/tty.usbmodem* /dev/cu.usbserial* /dev/cu.usbmodem*; do
    [[ -e "$p" ]] && echo "$p"
  done
  # Linux
  for p in /dev/ttyUSB* /dev/ttyACM*; do
    [[ -e "$p" ]] && echo "$p"
  done
}

# ---------------------------------------------------------------------------
# Fuse helpers
# ---------------------------------------------------------------------------

read_fuses() {
  local port="$1"
  local out
  out=$($AVRDUDE -c serialupdi -P "$port" -p "$MCU" \
    -U fuse0:r:-:h -U fuse1:r:-:h -U fuse2:r:-:h 2>&1) || {
    warn "avrdude failed to read fuses"
    echo "$out" >&2
    return 1
  }

  # Extract all 0xHH values.  avrdude outputs each fuse value on its own
  # line as "0xH" or "0xHH" after the "writing output file" message.
  # Allow 1-2 hex digits and strip trailing whitespace (BSD sed / macOS CR).
  local fuse0 fuse1 fuse2
  fuse0=$(echo "$out" | tr -d '\r' | sed -n 's/^0x\([0-9A-Fa-f]\{1,2\}\)\s*$/\1/p' | sed -n '1p')
  fuse1=$(echo "$out" | tr -d '\r' | sed -n 's/^0x\([0-9A-Fa-f]\{1,2\}\)\s*$/\1/p' | sed -n '2p')
  fuse2=$(echo "$out" | tr -d '\r' | sed -n 's/^0x\([0-9A-Fa-f]\{1,2\}\)\s*$/\1/p' | sed -n '3p')

  # If standalone extraction failed, fall back to scanning for 0xHH anywhere
  [[ -z "$fuse0" ]] && fuse0=$(echo "$out" | tr -d '\r' | sed -n 's/.*0x\([0-9A-Fa-f]\{1,2\}\).*/\1/p' | sed -n '1p')
  [[ -z "$fuse1" ]] && fuse1=$(echo "$out" | tr -d '\r' | sed -n 's/.*0x\([0-9A-Fa-f]\{1,2\}\).*/\1/p' | sed -n '2p')
  [[ -z "$fuse2" ]] && fuse2=$(echo "$out" | tr -d '\r' | sed -n 's/.*0x\([0-9A-Fa-f]\{1,2\}\).*/\1/p' | sed -n '3p')

  # Pad single-digit values to two digits
  [[ -n "$fuse0" ]] && fuse0=$(printf '%02s' "$fuse0" | tr ' ' '0')
  [[ -n "$fuse1" ]] && fuse1=$(printf '%02s' "$fuse1" | tr ' ' '0')
  [[ -n "$fuse2" ]] && fuse2=$(printf '%02s' "$fuse2" | tr ' ' '0')

  echo "$fuse0 $fuse1 $fuse2"
}

parse_bodcfg() {
  local val="$1"
  local active_bits=$(( (0x$val >> 3) & 0x03 ))
  local lvl=$(( 0x$val & 0x07 ))
  local active_text=""
  local volt_text=""

  case $active_bits in
    0) active_text="DISABLED (unsafe for 3xAA!)" ;;
    1) active_text="enabled (continuous)" ;;
    2) active_text="enabled (sampled)" ;;
    3) active_text="enabled (sleep)" ;;
  esac

  case $lvl in
    0) volt_text="1.8V" ;;
    1) volt_text="2.15V" ;;
    2) volt_text="2.6V" ;;
    3) volt_text="2.9V" ;;
    4) volt_text="3.3V" ;;
    5) volt_text="3.7V" ;;
    6) volt_text="4.0V" ;;
    7) volt_text="4.3V" ;;
  esac

  echo "BOD $active_text, threshold $volt_text"
}

parse_osccfg() {
  local val="$1"
  case $val in
    01) echo "16 MHz internal oscillator" ;;
    02) echo "20 MHz internal oscillator" ;;
    *) echo "unknown (0x$val)" ;;
  esac
}

show_fuses() {
  local fuses
  fuses=$(read_fuses "$1")
  local fuse0="${fuses%% *}"
  local rest="${fuses#* }"
  local fuse1="${rest%% *}"
  local fuse2="${rest##* }"

  echo ""
  echo -e "${C_BLUE}Current fuse values:${C_RESET}"
  echo "  fuse0 (WDTCFG) = 0x${fuse0:-??}   -- WDT configuration fuse"
  echo "  fuse1 (BODCFG) = 0x${fuse1:-??}   -- $(parse_bodcfg "$fuse1")"
  echo "  fuse2 (OSCCFG) = 0x${fuse2:-??}   -- $(parse_osccfg "$fuse2")"
  echo ""

  if [[ "${fuse1:-00}" == "00" ]]; then
    warn "Brown-out detection is DISABLED. This is unsafe for 3xAA operation."
    warn "Consider running the fuse setup after flashing."
  fi
}

# ---------------------------------------------------------------------------
# Firmware selection
# ---------------------------------------------------------------------------

select_firmware() {
  local choice hex_file

  echo ""
  info "Select firmware category:"
  echo "  1) Main firmware (for driving the boat)"
  echo "  2) Test firmware (diagnostics / bench testing)"
  echo "  q) Quit"
  prompt "Choice: "
  read -r choice

  case $choice in
    1) select_main_firmware ;;
    2) select_test_firmware ;;
    q|Q) exit 0 ;;
    *) warn "Invalid choice"; select_firmware ;;
  esac
}

select_main_firmware() {
  local choice features
  echo ""
  info "Select main firmware:"
  echo "  1) Generic firmware (no stall compensation)"
  echo "  2) Spring Tide 40 (MOTOR_STALL_SPEED=140, kid-friendly)"
  echo "  b) Back"
  prompt "Choice: "
  read -r choice

  case $choice in
    1)
      select_features "firmware" "main"
      hex_file="${HEX_DIR}/firmware${FEATURES_SUFFIX}.hex"
      ;;
    2)
      select_features "firmware-springtide40" "springtide"
      hex_file="${HEX_DIR}/firmware-springtide40${FEATURES_SUFFIX}.hex"
      ;;
    b|B) select_firmware ;;
    *) warn "Invalid choice"; select_main_firmware ;;
  esac

  confirm_and_flash "$hex_file"
}

select_test_firmware() {
  local choice features
  echo ""
  info "Select test firmware:"
  echo "  1) Motor test via UART (firmware-test)"
  echo "  2) GPIO toggle test (firmware-test-gpio)"
  echo "  3) Port motor PWM sweep (firmware-test-pwm-port)"
  echo "  4) Starboard motor PWM sweep (firmware-test-pwm-starboard)"
  echo "  5) UART loopback test (firmware-test-uart)"
  echo "  6) PB2 output test (firmware-test-pb2)"
  echo "  7) Water sensor test (firmware-test-water)"
  echo "  b) Back"
  prompt "Choice: "
  read -r choice

  case $choice in
    1)
      select_features "firmware-test" "main"
      hex_file="${HEX_DIR}/firmware-test${FEATURES_SUFFIX}.hex"
      ;;
    2)
      select_features "firmware-test-gpio" "gpio"
      hex_file="${HEX_DIR}/firmware-test-gpio${FEATURES_SUFFIX}.hex"
      ;;
    3)
      select_features "firmware-test-pwm-port" "pwm"
      hex_file="${HEX_DIR}/firmware-test-pwm-port${FEATURES_SUFFIX}.hex"
      ;;
    4)
      select_features "firmware-test-pwm-starboard" "pwm"
      hex_file="${HEX_DIR}/firmware-test-pwm-starboard${FEATURES_SUFFIX}.hex"
      ;;
    5)
      select_features "firmware-test-uart" "uart"
      hex_file="${HEX_DIR}/firmware-test-uart${FEATURES_SUFFIX}.hex"
      ;;
    6)
      select_features "firmware-test-pb2" "pb2"
      hex_file="${HEX_DIR}/firmware-test-pb2${FEATURES_SUFFIX}.hex"
      ;;
    7)
      select_features "firmware-test-water" "water"
      hex_file="${HEX_DIR}/firmware-test-water${FEATURES_SUFFIX}.hex"
      ;;
    b|B) select_firmware ;;
    *) warn "Invalid choice"; select_test_firmware ;;
  esac

  confirm_and_flash "$hex_file"
}

# Global set by select_features so callers don't need command substitution.
FEATURES_SUFFIX=""

# $1 = base name, $2 = category hint
select_features() {
  local base="$1"
  local hint="$2"
  local freq=""
  local ks=""
  local ws=""

  # Frequency — only offer variants that actually exist
  local avail_freqs=()
  [[ $HAS_20M -eq 1 ]] && avail_freqs+=("20 MHz (default)")
  [[ $HAS_10M -eq 1 ]] && avail_freqs+=("10 MHz (/2 prescaler)")
  [[ $HAS_5M  -eq 1 ]] && avail_freqs+=("5 MHz (/4 prescaler)")
  [[ $HAS_16M -eq 1 ]] && avail_freqs+=("16 MHz (requires 16 MHz fuse)")

  if [[ ${#avail_freqs[@]} -gt 1 ]]; then
    echo ""
    info "Select CPU frequency:"
    local idx=1
    for desc in "${avail_freqs[@]}"; do
      echo "  $idx) $desc"
      ((idx++))
    done
    prompt "Choice: "
    read -r freq_choice
    # Build a lookup array: index -> suffix
    local -a freq_map=("" "")  # index 0 unused
    [[ $HAS_20M -eq 1 ]] && freq_map+=("")
    [[ $HAS_10M -eq 1 ]] && freq_map+=("-10m")
    [[ $HAS_5M  -eq 1 ]] && freq_map+=("-5m")
    [[ $HAS_16M -eq 1 ]] && freq_map+=("-16m")
    if [[ "$freq_choice" =~ ^[0-9]+$ ]] && (( freq_choice >= 1 && freq_choice < ${#freq_map[@]} )); then
      freq="${freq_map[$freq_choice]}"
    else
      freq=""
    fi
  fi

  # Kickstart — only offer if ks variants exist
  if [[ $HAS_KS -eq 1 ]] && [[ "$hint" == "main" || "$hint" == "pwm" ]]; then
    echo ""
    info "Enable motor kickstart? (helps low-speed startup)"
    echo "  1) No (default)"
    echo "  2) Yes"
    prompt "Choice: "
    read -r ks_choice
    case $ks_choice in
      2) ks="-ks" ;;
      *) ks="" ;;
    esac
  fi

  # Water sensor — only offer if ws variants exist
  if [[ $HAS_WS -eq 1 ]] && [[ "$hint" == "springtide" ]]; then
    echo ""
    info "Enable water safety sensor? (stops motors when out of water)"
    echo "  1) No (default)"
    echo "  2) Yes"
    prompt "Choice: "
    read -r ws_choice
    case $ws_choice in
      2) ws="-ws" ;;
      *) ws="" ;;
    esac
  fi

  # Suffix order: -ks -ws -freq
  FEATURES_SUFFIX="${ks}${ws}${freq}"
}

confirm_and_flash() {
  local hex_file="$1"

  # If the exact file doesn't exist, try the suffixed variant (e.g.
  # firmware-test.hex might actually be firmware-test-20m.hex when built
  # with all-freqs).
  if [[ ! -f "$hex_file" ]]; then
    local base="${hex_file%.hex}"
    local alt="${base}-20m.hex"
    if [[ -f "$alt" ]]; then
      hex_file="$alt"
    else
      die "Hex file not found: $(basename "$hex_file") (also tried $(basename "$alt"))"
    fi
  fi

  echo ""
  info "Firmware: $(basename "$hex_file")"
  info "Size: $(wc -c < "$hex_file") bytes"
  echo ""
  prompt "Flash this firmware to ATtiny414? (y/N): "
  read -r confirm
  if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
    info "Aborted."
    return
  fi

  echo ""
  info "Flashing..."
  $AVRDUDE -c serialupdi -P "$PORT" -p "$MCU" \
    -U flash:w:"$hex_file":i || die "Flash failed"

  ok "Flash successful!"
}

# ---------------------------------------------------------------------------
# Fuse writing
# ---------------------------------------------------------------------------

offer_fuses() {
  echo ""
  info "Would you like to set safe fuses for 3xAA operation?"
  echo "  Recommended: BOD enabled @ 2.6V, 20 MHz oscillator"
  echo ""
  prompt "Write safe fuses? (y/N): "
  read -r confirm
  if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
    return
  fi

  # Show current first
  show_fuses "$PORT"

  echo ""
  info "Proposed new values:"
  echo "  fuse1 (BODCFG) = 0x0A   -- BOD enabled @ 2.6V"
  echo "  fuse2 (OSCCFG) = 0x02   -- 20 MHz oscillator"
  echo ""
  prompt "Confirm fuse write? (y/N): "
  read -r confirm2
  if [[ ! "$confirm2" =~ ^[Yy]$ ]]; then
    info "Fuse write cancelled."
    return
  fi

  $AVRDUDE -c serialupdi -P "$PORT" -p "$MCU" \
    -U fuse1:w:0x0A:m \
    -U fuse2:w:0x02:m || die "Fuse write failed"

  ok "Fuses written successfully."
  echo ""
  show_fuses "$PORT"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

main() {
  banner

  # Parse args
  while getopts ":p:" opt; do
    case $opt in
      p) PORT="$OPTARG" ;;
      *) die "Usage: $0 [-p /dev/ttyUSB0]" ;;
    esac
  done

  # Check avrdude
  if ! command -v "$AVRDUDE" &>/dev/null; then
    die "avrdude not found in PATH. Please install it."
  fi
  ok "avrdude found: $(which "$AVRDUDE")"

  # Detect or ask for port (all interactive I/O done in main shell)
  if [[ -z "$PORT" ]]; then
    info "Detecting programmer port..."
    local -a candidates=()
    while IFS= read -r line; do
      [[ -n "$line" ]] && candidates+=("$line")
    done < <(find_ports)

    if [[ ${#candidates[@]} -eq 0 ]]; then
      echo ""
      warn "Could not auto-detect programmer port."
      prompt "Please enter port manually (e.g. /dev/ttyUSB0): "
      read -r PORT
      [[ -z "$PORT" ]] && die "No port specified."
    elif [[ ${#candidates[@]} -eq 1 ]]; then
      PORT="${candidates[0]}"
    else
      echo ""
      info "Multiple serial ports found:"
      local i=1
      for p in "${candidates[@]}"; do
        echo "  $i) $p"
        ((i++))
      done
      prompt "Select port (1-$((i-1))): "
      read -r choice
      if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice < i )); then
        PORT="${candidates[$((choice-1))]}"
      else
        die "Invalid selection."
      fi
    fi
  fi
  ok "Using port: $PORT"

  # -------------------------------------------------------------------
  # Scan available hex files to build dynamic menus
  # -------------------------------------------------------------------
  if [[ ! -d "$HEX_DIR" ]] || ! ls "$HEX_DIR"/*.hex &>/dev/null; then
    die "No .hex files found in $HEX_DIR"
  fi

  # Discover which features are actually available
  HAS_KS=0
  HAS_WS=0
  HAS_5M=0
  HAS_10M=0
  HAS_16M=0
  HAS_20M=0

  for f in "$HEX_DIR"/*.hex; do
    local base=$(basename "$f")
    [[ "$base" == *"-ks"* ]] && HAS_KS=1
    [[ "$base" == *"-ws"* ]] && HAS_WS=1
    [[ "$base" == *"-5m"* ]] && HAS_5M=1
    [[ "$base" == *"-10m"* ]] && HAS_10M=1
    [[ "$base" == *"-16m"* ]] && HAS_16M=1
  done
  # Default 20 MHz files have no suffix, but check for any .hex that lacks frequency suffix
  for f in "$HEX_DIR"/*.hex; do
    local base=$(basename "$f")
    [[ ! "$base" =~ -(5m|10m|16m)\.hex$ ]] && HAS_20M=1
  done

  # Main loop
  while true; do
    echo ""
    echo "Main menu:"
    echo "  1) Read current fuses"
    echo "  2) Flash firmware"
    echo "  3) Write safe fuses (BOD @ 2.6V, 20 MHz)"
    echo "  q) Quit"
    prompt "Choice: "
    read -r menu_choice

    case $menu_choice in
      1) show_fuses "$PORT" ;;
      2) select_firmware ;;
      3) offer_fuses ;;
      q|Q) ok "Goodbye!"; exit 0 ;;
      *) warn "Invalid choice" ;;
    esac
  done
}

main "$@"
