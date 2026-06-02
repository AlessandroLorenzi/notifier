# Arduino CLI Makefile for WaveshareEink
# Usage examples:
#   make compile
#   make upload
#   make monitor
#   make all
# Override defaults if needed:
#   make compile FQBN=esp32:esp32:esp32s3
#   make upload PORT=/dev/ttyACM0

SKETCH := notifier.ino
FQBN ?= esp32:esp32:esp32s3
PORT ?= /dev/ttyACM0

# Serial monitor baud rate.
BAUD ?= 115200

.PHONY: all compile upload monitor clean check-port board-list install-libs

all: compile upload

compile:
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)

check-port:
	@if [ -z "$(PORT)" ]; then \
		echo "No serial port detected."; \
		echo "Set it manually, for example: make upload PORT=/dev/ttyACM0"; \
		exit 1; \
	fi

upload: check-port
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)

monitor: check-port
	python3 -m serial.tools.miniterm --raw $(PORT) $(BAUD)

clean:
	rm -rf build

board-list:
	arduino-cli board list

install-libs:
	arduino-cli lib install "GxEPD2"
	arduino-cli lib install "Adafruit SHTC3 Library"
	arduino-cli lib install "PubSubClient"
