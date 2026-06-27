# Project Configurations
MCU = attiny414
PORT ?= /dev/ttyUSB1  # Serial port for avrdude UPDI

# Frequency selection: 5m, 10m, 16m, 20m
#   20m = 20 MHz  (default, no prescaler, OSCCFG=20MHz)
#   10m = 10 MHz  (/2 prescaler from 20 MHz osc)
#    5m = 5 MHz   (/4 prescaler from 20 MHz osc)
#   16m = 16 MHz  (no prescaler, OSCCFG=16MHz — requires fuse change)
FREQ ?= 20m
FREQS := 20m 10m 5m 16m

# Frequency-specific settings
ifeq ($(FREQ),20m)
  F_CPU = 20000000UL
  USART_BAUD = 694
  TCB_CCMP = 50000
  CLK_PRESCALER = 1
  OSC_FREQ = 20MHZ
else ifeq ($(FREQ),10m)
  F_CPU = 10000000UL
  USART_BAUD = 347
  TCB_CCMP = 25000
  CLK_PRESCALER = 2
  OSC_FREQ = 20MHZ
else ifeq ($(FREQ),5m)
  F_CPU = 5000000UL
  USART_BAUD = 174
  TCB_CCMP = 12500
  CLK_PRESCALER = 4
  OSC_FREQ = 20MHZ
else ifeq ($(FREQ),16m)
  F_CPU = 16000000UL
  USART_BAUD = 556
  TCB_CCMP = 40000
  CLK_PRESCALER = 1
  OSC_FREQ = 16MHZ
else
  $(error Unknown FREQ=$(FREQ). Use 5m, 10m, 16m, or 20m)
endif

PWM_MAX ?= 160

# Compiler Configurations
CXX = avr-g++
OBJCOPY = avr-objcopy
SIZE = avr-size
AVRDUDE = avrdude

CXXFLAGS = -mmcu=$(MCU) -Os -DF_CPU=$(F_CPU) -Wall -Wextra -std=c++17
CXXFLAGS += -ffunction-sections -fdata-sections
CXXFLAGS += -MMD -MP
CXXFLAGS += -DUSART_BAUD=$(USART_BAUD) -DTCB_CCMP=$(TCB_CCMP)
CXXFLAGS += -DCLK_PRESCALER=$(CLK_PRESCALER) -DPWM_MAX=$(PWM_MAX)
LDFLAGS = -Wl,--gc-sections

# prevent make from automatically cleaning up intermediary results - otherwise
# something like make flash would try to build due to missing .o files
.SECONDARY:

# All firmware targets
TARGETS := \
  firmware \
  firmware-springtide40 \
  firmware-test \
  firmware-test-uart \
  firmware-test-pb2 \
  firmware-test-water \
  firmware-test-pwm-port \
  firmware-test-pwm-starboard \
  firmware-test-gpio

# Kickstart variants (same source compiled with -DMOTOR_KICKSTART)
KS_BASE   := firmware firmware-test firmware-test-pwm-port firmware-test-pwm-starboard
KS_TARGETS := $(KS_BASE:%=%-ks)

# Water sensor variants define -DWATER_SENSOR
# TODO: We might also want to have some variants with both watersensor and kickstart
#       Unlike kickstart WS doesn't change common definitions, though, and is
#       purely an implementation detail in the specific firmware (so we can
#       have water sensor in the test firmware without this flag)
WS_BASE   := firmware-springtide40
WS_TARGETS := $(WS_BASE:%=%-ws)

ALL_TARGETS := $(TARGETS) $(KS_TARGETS) $(WS_TARGETS)

HEXS := $(ALL_TARGETS:=.hex)
DEPS := $(ALL_TARGETS:=.d)

# Default target builds current frequency
.PHONY: all clean flash all-freqs

all: $(HEXS)

# Build all frequencies, producing suffixed hex files
all-freqs:
	@for freq in $(FREQS); do \
		echo "=== Building for $$freq ==="; \
		$(MAKE) clean all FREQ=$$freq && \
		for t in $(ALL_TARGETS); do \
			if [ -f $$t.hex ]; then cp $$t.hex $$t-$$freq.hex; fi; \
		done; \
	done
	@echo ""
	@echo "=========================================="
	@echo " Build summary"
	@echo "=========================================="
	@for freq in $(FREQS); do \
		count=$$(ls -1 *-$$freq.hex 2>/dev/null | wc -l); \
		echo "  $$freq: $$count firmwares"; \
		done
	@total=$$(ls -1 *-5m.hex *-10m.hex *-16m.hex *-20m.hex 2>/dev/null | wc -l); \
	 echo "  ----------------------"; \
	 echo "  Total: $$total suffixed .hex files"
	@echo "=========================================="

# Pattern rules ----------------------------------------------------------

# Link .o -> .elf, then print size
%.elf: %.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	@$(SIZE) --format=avr --mcu=$(MCU) $@

# Convert .elf -> .hex
%.hex: %.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# Kickstart variants compile the same .cpp with MOTOR_KICKSTART defined
%-ks.o: %.cpp
	$(CXX) $(CXXFLAGS) -DMOTOR_KICKSTART -c $< -o $@

# Same for water sensors
%-ws.o: %.cpp
	$(CXX) $(CXXFLAGS) -DWATER_SENSOR -c $< -o $@

# Compile .cpp -> .o, generating dependency files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Auto-regenerate .o when included headers change
-include $(DEPS)

# Flashing ---------------------------------------------------------------
# Usage: make flash TARGET=firmware-test PORT=/dev/ttyUSB3

TARGET ?= firmware
flash: $(TARGET).hex
	$(AVRDUDE) -c serialupdi -P $(PORT) -p t414 -U flash:w:$(TARGET).hex:i

# Housekeeping -----------------------------------------------------------

clean:
	rm -f *.o *.elf *.d
	rm -f $(ALL_TARGETS:%=%.hex)

distclean: clean
	rm -f *-20m.hex *-10m.hex *-5m.hex *-16m.hex
