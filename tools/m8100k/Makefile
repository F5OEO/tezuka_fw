# Makefile for GPSDO GPS Configuration Tool
# Based on Arduino code by F1CJN/F1TE

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = 
TARGET = gpsdo_config
SRC = gpsdo_config.c

# Default GPS device
GPS_DEVICE ?= /dev/ttyACM0
FREQUENCY ?= 100000

# Installation directory
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all clean install uninstall run test help

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

# Clean build artifacts
clean:
	rm -f $(TARGET)
	@echo "Clean complete"

# Install to system
install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)
	@echo "Installed to $(BINDIR)/$(TARGET)"
	@echo "Run with: sudo $(TARGET)"

# Uninstall from system
uninstall:
	rm -f $(BINDIR)/$(TARGET)
	@echo "Uninstalled $(BINDIR)/$(TARGET)"

# Run with default settings
run: $(TARGET)
	@echo "Running with device=$(GPS_DEVICE) frequency=$(FREQUENCY)"
	sudo ./$(TARGET) $(FREQUENCY) $(GPS_DEVICE)

# Test with 10 KHz (good for testing - low jitter)
test: $(TARGET)
	@echo "Testing with 10 KHz on $(GPS_DEVICE)"
	sudo ./$(TARGET) 10000 $(GPS_DEVICE)

# Help message
help:
	@echo "GPSDO GPS Configuration Tool - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make              - Build the program"
	@echo "  make clean        - Remove built files"
	@echo "  make install      - Install to $(BINDIR)"
	@echo "  make uninstall    - Remove from $(BINDIR)"
	@echo "  make run          - Build and run with defaults"
	@echo "  make test         - Build and test with 10 KHz"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  GPS_DEVICE        - GPS device path (default: $(GPS_DEVICE))"
	@echo "  FREQUENCY         - TimePulse frequency in Hz (default: $(FREQUENCY))"
	@echo "  PREFIX            - Installation prefix (default: $(PREFIX))"
	@echo ""
	@echo "Examples:"
	@echo "  make run GPS_DEVICE=/dev/ttyUSB0"
	@echo "  make run FREQUENCY=10000"
	@echo "  make run GPS_DEVICE=/dev/ttyUSB0 FREQUENCY=8000000"
	@echo ""
	@echo "Good frequencies (low jitter - even divisors of 48 MHz):"
	@echo "  10 KHz, 100 KHz, 1 MHz, 8 MHz, 12 MHz, 24 MHz"
