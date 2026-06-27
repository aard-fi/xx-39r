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

# Build output directory (overrideable for per-frequency builds)
BUILD_DIR ?= build

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

# prevent make from automatically cleaning up intermediary results
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
WS_BASE   := firmware-springtide40
WS_TARGETS := $(WS_BASE:%=%-ws)

ALL_TARGETS := $(TARGETS) $(KS_TARGETS) $(WS_TARGETS)

# Intermediates (.o / .elf) live in build/ without suffix
OBJS := $(ALL_TARGETS:%=$(BUILD_DIR)/%.o)
ELFS := $(ALL_TARGETS:%=$(BUILD_DIR)/%.elf)
DEPS := $(ALL_TARGETS:%=$(BUILD_DIR)/%.d)

# Default target builds current frequency (unsuffixed hex in build/)
HEXS := $(ALL_TARGETS:%=$(BUILD_DIR)/%.hex)

.PHONY: all clean flash all-freqs

all: $(HEXS)

# Ensure build directory exists before compiling
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Per-frequency build directories
FREQ_BUILD_DIRS := $(foreach f,$(FREQS),build-$(f))
$(FREQ_BUILD_DIRS):
	@mkdir -p $@

# Per-frequency settings (uppercased for Make variable names)
FREQ_20M_F_CPU := 20000000UL
FREQ_20M_USART_BAUD := 694
FREQ_20M_TCB_CCMP := 50000
FREQ_20M_CLK_PRESCALER := 1
FREQ_20M_OSC_FREQ := 20MHZ

FREQ_10M_F_CPU := 10000000UL
FREQ_10M_USART_BAUD := 347
FREQ_10M_TCB_CCMP := 25000
FREQ_10M_CLK_PRESCALER := 2
FREQ_10M_OSC_FREQ := 20MHZ

FREQ_5M_F_CPU := 5000000UL
FREQ_5M_USART_BAUD := 174
FREQ_5M_TCB_CCMP := 12500
FREQ_5M_CLK_PRESCALER := 4
FREQ_5M_OSC_FREQ := 20MHZ

FREQ_16M_F_CPU := 16000000UL
FREQ_16M_USART_BAUD := 556
FREQ_16M_TCB_CCMP := 40000
FREQ_16M_CLK_PRESCALER := 1
FREQ_16M_OSC_FREQ := 16MHZ

# Helper: map a target name to its source file (strip -ks / -ws suffixes)
target_to_src = $(patsubst %.cpp,%.cpp,$(patsubst %-ws,%,$(patsubst %-ks,%,$(1)))).cpp

# Helper: look up a frequency variable, e.g. $(call fvar,20m,F_CPU)
fvar = $(FREQ_$(shell echo $(1) | tr a-z A-Z)_$(2))

# Generate per-frequency suffixed hex targets.
# Each variant is compiled directly (no recursive make) with its own
# frequency-specific flags.  Make caches objects in build-<freq>/ and
# only rebuilds when sources change.
define FREQ_RULE
# Compile object file with frequency-specific flags
build-$(2)/$(1).o: $(call target_to_src,$(1)) common.hpp Makefile | build-$(2)
	$$(CXX) -mmcu=$$(MCU) -Os -Wall -Wextra -std=c++17 \
		-ffunction-sections -fdata-sections -MMD -MP \
		-DF_CPU=$$(call fvar,$(2),F_CPU) \
		-DUSART_BAUD=$$(call fvar,$(2),USART_BAUD) \
		-DTCB_CCMP=$$(call fvar,$(2),TCB_CCMP) \
		-DCLK_PRESCALER=$$(call fvar,$(2),CLK_PRESCALER) \
		-DPWM_MAX=$$(PWM_MAX) \
		$$(if $$(filter %-ks,$(1)),-DMOTOR_KICKSTART) \
		$$(if $$(filter %-ws,$(1)),-DWATER_SENSOR) \
		-c $$(call target_to_src,$(1)) -o $$@

# Link ELF
build-$(2)/$(1).elf: build-$(2)/$(1).o
	$$(CXX) -mmcu=$$(MCU) -Os -Wl,--gc-sections -o $$@ $$^
	@$$(SIZE) --format=avr --mcu=$$(MCU) $$@

# Convert to HEX in per-frequency dir
build-$(2)/$(1).hex: build-$(2)/$(1).elf
	$$(OBJCOPY) -O ihex -R .eeprom $$< $$@

# Copy to build/ with frequency suffix
build/$(1)-$(2).hex: build-$(2)/$(1).hex | $$(BUILD_DIR)
	cp $$< $$@
endef

# Instantiate rules for every (target, frequency) pair
$(foreach t,$(ALL_TARGETS),$(foreach f,$(FREQS),$(eval $(call FREQ_RULE,$(t),$(f)))))

# Convenience target to build all frequency variants
ALL_FREQ_HEXS := $(foreach t,$(ALL_TARGETS),$(foreach f,$(FREQS),build/$(t)-$(f).hex))
.PHONY: all-freqs
all-freqs: $(ALL_FREQ_HEXS)
	@echo ""
	@echo "=========================================="
	@echo " Build summary"
	@echo "=========================================="
	@for freq in $(FREQS); do \
		count=$$(ls -1 build/*-$$freq.hex 2>/dev/null | wc -l); \
		echo "  $$freq: $$count firmwares"; \
	done
	@echo "  ----------------------"
	@total=$$(ls -1 build/*-5m.hex build/*-10m.hex build/*-16m.hex build/*-20m.hex 2>/dev/null | wc -l); \
	 echo "  Total: $$total suffixed .hex files"
	@echo "=========================================="
	@echo "All suffixed hex files are in build/"

# Pattern rules ----------------------------------------------------------

# Link .o -> .elf
$(BUILD_DIR)/%.elf: $(BUILD_DIR)/%.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	@$(SIZE) --format=avr --mcu=$(MCU) $@

# Convert .elf -> .hex
$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# Kickstart variants compile the same .cpp with MOTOR_KICKSTART defined
$(BUILD_DIR)/%-ks.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DMOTOR_KICKSTART -c $< -o $@

# Same for water sensors
$(BUILD_DIR)/%-ws.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DWATER_SENSOR -c $< -o $@

# Compile .cpp -> .o, generating dependency files
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Auto-regenerate .o when included headers change
-include $(DEPS)

# Flashing ---------------------------------------------------------------
# Usage: make flash TARGET=firmware-test-10m PORT=/dev/ttyUSB3
#
# TARGET can be either a base firmware name (e.g. firmware-test) or a
# suffixed variant (e.g. firmware-test-10m).  Base names are built on
# demand; suffixed files must already exist (run make all-freqs first).

TARGET ?= firmware
flash:
	@if echo "$(TARGET)" | grep -qE '\-(5m|10m|16m|20m)$$'; then \
		if [ ! -f "$(BUILD_DIR)/$(TARGET).hex" ]; then \
			echo "Error: $(BUILD_DIR)/$(TARGET).hex not found."; \
			echo "Run 'make all-freqs' first to create suffixed hex files."; \
			exit 1; \
		fi; \
		$(AVRDUDE) -c serialupdi -P $(PORT) -p t414 -U flash:w:$(BUILD_DIR)/$(TARGET).hex:i; \
	else \
		$(MAKE) $(BUILD_DIR)/$(TARGET).hex && \
		$(AVRDUDE) -c serialupdi -P $(PORT) -p t414 -U flash:w:$(BUILD_DIR)/$(TARGET).hex:i; \
	fi

# Read current fuse values (do this once and record them!)
# ATtiny414 fuse map:
read-fuses:
	$(AVRDUDE) -c serialupdi -P $(PORT) -p t414 \
	  -U fuse0:r:-:h -U fuse1:r:-:h -U fuse2:r:-:h \
	  -U fuse4:r:-:h -U fuse5:r:-:h -U fuse6:r:-:h \
	  -U fuse7:r:-:h -U fuse8:r:-:h

# Write fuses for safe 3xAA operation.
# NOTE: You should read fuses with 'make read-fuses' first and verify
# the values before writing.  Record the output — if something goes wrong
# you need to know the original values to restore them.
#
# What we'd expect to see after setting fuses is:
#
#   fuse0 (WDTCFG)  = 0x00   leave unchanged — we configure WDT at runtime
#   fuse1 (BODCFG)  = 0x0A   BOD enabled @ 2.6V (ACTIVE=01, LVL=010)
#   fuse2 (OSCCFG)  = 0x02   20 MHz osc, or 0x01 for 16 MHz
#
flash-fuses:
	@echo "Setting BODCFG = 0x0A (enabled, 2.6V threshold)"
	$(AVRDUDE) -c serialupdi -P $(PORT) -p t414 -U fuse1:w:0x0A:m
ifeq ($(OSC_FREQ),20MHZ)
	@echo "Setting OSCCFG = 0x02 (20 MHz oscillator)"
	$(AVRDUDE) -c serialupdi -P $(PORT) -p t414 -U fuse2:w:0x02:m
else ifeq ($(OSC_FREQ),16MHZ)
	@echo "Setting OSCCFG = 0x01 (16 MHz oscillator)"
	$(AVRDUDE) -c serialupdi -P $(PORT) -p t414 -U fuse2:w:0x01:m
endif

# Housekeeping -----------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR) $(FREQ_BUILD_DIRS)
