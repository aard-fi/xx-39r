# Project Configurations
MCU = attiny414
F_CPU = 20000000UL # run at 20MHz (matches factory fuse2)
PORT ?= /dev/ttyUSB1  # Serial port for avrdude UPDI

# Compiler Configurations
CXX = avr-g++
OBJCOPY = avr-objcopy
SIZE = avr-size
AVRDUDE = avrdude

CXXFLAGS = -mmcu=$(MCU) -Os -DF_CPU=$(F_CPU) -Wall -Wextra -std=c++17
CXXFLAGS += -ffunction-sections -fdata-sections
CXXFLAGS += -MMD -MP
LDFLAGS = -Wl,--gc-sections

# prevent make from automatically cleaning up intermediary results - otherwise
# something like make flash would try to build due to missing .o files
.SECONDARY:

# All firmware targets
TARGETS := \
  firmware \
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

HEXS := $(TARGETS:=.hex) $(KS_TARGETS:=.hex)
DEPS := $(TARGETS:=.d) $(KS_TARGETS:=.d)

# Default target builds everything
.PHONY: all clean flash

all: $(HEXS)

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
	rm -f *.o *.elf *.hex *.d
