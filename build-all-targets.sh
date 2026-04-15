#!/bin/bash
# Local build script for all supported targets
# Uses existing ESP-IDF setup (ninja + cmake)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get version from version.h
VERSION=$(grep -oP '#define FIRMWARE_VERSION "\K[^"]+' main/common/version.h)

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}BleMesh2MQTT Build Script${NC}"
echo -e "${BLUE}Version: ${VERSION}${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if ESP-IDF is sourced
if [ -z "$IDF_PATH" ]; then
    echo -e "${YELLOW}ESP-IDF environment not detected${NC}"

    # Try to source from local esp-idf
    if [ -f "esp-idf/export.sh" ]; then
        echo -e "${YELLOW}Sourcing local ESP-IDF from esp-idf/export.sh${NC}"
        source esp-idf/export.sh
    # Try global ESP-IDF installation (devcontainer)
    elif [ -f "/opt/esp/idf/export.sh" ]; then
        echo -e "${YELLOW}Sourcing global ESP-IDF from /opt/esp/idf/export.sh${NC}"
        source /opt/esp/idf/export.sh
    else
        echo -e "${RED}Error: ESP-IDF not found!${NC}"
        echo -e "${RED}Please run: source esp-idf/export.sh${NC}"
        echo -e "${RED}Or install ESP-IDF: ./setup.sh${NC}"
        exit 1
    fi
else
    # IDF_PATH is set, but we still need to source export.sh to get all tools
    echo -e "${YELLOW}IDF_PATH detected, ensuring ESP-IDF environment is complete...${NC}"
    if [ -f "$IDF_PATH/export.sh" ]; then
        source "$IDF_PATH/export.sh" > /dev/null 2>&1
    fi
fi

echo -e "${GREEN}✓ ESP-IDF detected: $IDF_PATH${NC}"
echo ""

# Targets to build
TARGETS="${1:-esp32 esp32s3 esp32c3 esp32c6}"

echo -e "${BLUE}Targets to build: ${TARGETS}${NC}"
echo ""

# Create releases directory
mkdir -p releases

# Build counter
BUILT=0
FAILED=0

for target in $TARGETS; do
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}Building for target: ${target}${NC}"
    echo -e "${BLUE}========================================${NC}"

    # Set target
    echo -e "${YELLOW}Setting target to ${target}...${NC}"
    if ! idf.py set-target "$target"; then
        echo -e "${RED}✗ Failed to set target ${target}${NC}"
        FAILED=$((FAILED + 1))
        continue
    fi

    # Build
    echo -e "${YELLOW}Building firmware...${NC}"
    if ! idf.py build; then
        echo -e "${RED}✗ Build failed for ${target}${NC}"
        FAILED=$((FAILED + 1))
        continue
    fi

    echo -e "${GREEN}✓ Build successful for ${target}${NC}"

    # Create release package
    PACKAGE_NAME="BleMesh2Mqtt-v${VERSION}-${target}"
    PACKAGE_DIR="releases/${PACKAGE_NAME}"

    echo -e "${YELLOW}Creating release package...${NC}"
    mkdir -p "$PACKAGE_DIR"

    # Copy binaries
    cp build/BleMesh2Mqtt.bin "$PACKAGE_DIR/" 2>/dev/null || \
       cp build/*.bin "$PACKAGE_DIR/" 2>/dev/null || true
    cp build/bootloader/bootloader.bin "$PACKAGE_DIR/"
    cp build/partition_table/partition-table.bin "$PACKAGE_DIR/"
    cp build/ota_data_initial.bin "$PACKAGE_DIR/"
    cp build/storage.bin "$PACKAGE_DIR/"

    # Get partition addresses
    # Bootloader offset by chip family:
    #   Xtensa (esp32, esp32s2, esp32s3)  → 0x1000
    #   RISC-V (esp32c3, esp32c6, esp32h2) → 0x0
    #   ESP32-C5 (RISC-V, special case)    → 0x2000
    if [ "$target" = "esp32c5" ]; then
        BOOTLOADER_OFFSET="0x2000"
    elif [ "$target" = "esp32c3" ] || [ "$target" = "esp32c6" ] || [ "$target" = "esp32h2" ]; then
        BOOTLOADER_OFFSET="0x0"
    else
        BOOTLOADER_OFFSET="0x1000"
    fi
    PARTITION_OFFSET="0x8000"
    OTA_DATA_OFFSET="0xd000"
    APP_OFFSET="0x10000"
    STORAGE_OFFSET="0x3B0000"

    # When flashing via the native USB-Serial/JTAG port (not an external UART
    # adapter), ESP32-C3 and ESP32-C6 need --after watchdog-reset instead of
    # hard_reset. A USB-triggered reset is only a core reset and does not
    # re-sample the strapping pins, so the chip stays in download mode.
    # A watchdog reset forces a full system reset that re-samples the BOOT pin.
    # Note: ESP32-H2 has the same USB-JTAG peripheral and would need the same
    # treatment if added as a supported target.
    if [ "$target" = "esp32c3" ] || [ "$target" = "esp32c6" ]; then
        AFTER_RESET="watchdog-reset"
    else
        AFTER_RESET="hard_reset"
    fi

    # Create flash instructions
    cat > "$PACKAGE_DIR/FLASH_INSTRUCTIONS.txt" << EOF
==========================================
BleMesh2MQTT Flash Instructions
==========================================

Target: ${target}
Version: v${VERSION}
Build Date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")

PREREQUISITES:
--------------
- Install esptool.py: pip install esptool
- Or use ESP Flash Download Tool (Windows): https://www.espressif.com/en/support/download/other-tools

METHOD 1: Using esptool.py (Linux/Mac/Windows)
-----------------------------------------------

esptool.py -p /dev/ttyUSB0 -b 460800 --before default_reset --after ${AFTER_RESET} \\
  --chip ${target} write_flash --flash_mode dio --flash_size detect --flash_freq 40m \\
  ${BOOTLOADER_OFFSET} bootloader.bin \\
  ${PARTITION_OFFSET} partition-table.bin \\
  ${OTA_DATA_OFFSET} ota_data_initial.bin \\
  ${APP_OFFSET} BleMesh2Mqtt.bin \\
  ${STORAGE_OFFSET} storage.bin

Note: Replace /dev/ttyUSB0 with your serial port:
- Linux: /dev/ttyUSB0 or /dev/ttyACM0
- Mac: /dev/cu.usbserial-* or /dev/cu.SLAB_USBtoUART
- Windows: COM3, COM4, etc.

METHOD 2: Web-based Flash (Chrome/Edge only)
---------------------------------------------

1. Visit: https://web.esphome.io/
2. Click "Connect"
3. Select your device
4. Click "Install" and choose "Manual Installation"
5. Upload the files with these addresses:
   - ${BOOTLOADER_OFFSET}: bootloader.bin
   - ${PARTITION_OFFSET}: partition-table.bin
   - ${OTA_DATA_OFFSET}: ota_data_initial.bin
   - ${APP_OFFSET}: BleMesh2Mqtt.bin
   - ${STORAGE_OFFSET}: storage.bin

METHOD 3: Using idf.py (if ESP-IDF installed)
----------------------------------------------

cd /path/to/esp32-blemesh2mqtt
idf.py set-target ${target}
idf.py -p /dev/ttyUSB0 flash

AFTER FLASHING:
---------------

1. Device will create WiFi AP: "BleMesh2MQTT-Setup-XX:XX:XX"
2. Connect to this AP (no password)
3. Navigate to: http://192.168.4.1
4. Configure WiFi and MQTT settings
5. Device will reboot and connect to your network

TROUBLESHOOTING:
----------------

Flash fails:
- Try lower baud rate: -b 115200
- Hold BOOT button while connecting
- Verify correct COM port
- Erase flash first: esptool.py --chip ${target} erase_flash
$([ "$target" = "esp32c3" ] && cat << 'ESP32C3'

ESP32-C3 stays in download mode after flashing:
- This is expected behaviour with the built-in USB-Serial/JTAG peripheral.
  A core reset does NOT re-sample the strapping pins (GPIO9/BOOT), so the
  chip remains in download mode even after the flash command completes.
- The flash command above already uses --after watchdog-reset which triggers
  a full system reset that re-evaluates GPIO9 and boots normally.
- If the device still does not boot, exit download mode manually:
    1. Hold the BOOT button (GPIO9)
    2. Press and release the RESET (EN) button
    3. Release the BOOT button — the chip will now sample GPIO9 as HIGH
       and enter normal SPI boot mode.
- Ensure no external circuitry or capacitor is holding GPIO9 LOW.
ESP32C3
)
$([ "$target" = "esp32c6" ] && cat << 'ESP32C6'

ESP32-C6 stays in download mode after flashing:
- The ESP32-C6 bootloader is located at address 0x0 (not 0x1000 as on the
  classic ESP32). Using the wrong offset will cause the chip to appear to
  flash successfully but fail to boot (download mode loop).
- The flash command above already uses --after watchdog-reset and the correct
  bootloader offset (0x0) for this chip.
- If the device still does not boot, exit download mode manually:
    1. Hold the BOOT button (GPIO9)
    2. Press and release the RESET button
    3. Release the BOOT button — the chip will sample GPIO9 as HIGH
       and enter normal SPI boot mode.
- Ensure no external circuitry or capacitor is holding GPIO9 LOW.
ESP32C6
)

Captive portal doesn't appear:
- Wait 30-60 seconds
- Manually navigate to 192.168.4.1
- Disable mobile data on phone
- Forget and reconnect to WiFi

For more help: https://github.com/ludodefgh/esp32-blemesh2mqtt/issues

Binary checksums:
-----------------
$(cd "$PACKAGE_DIR" && sha256sum *.bin)
EOF

    # Create archive
    echo -e "${YELLOW}Creating ZIP archive...${NC}"
    cd releases
    zip -q -r "${PACKAGE_NAME}.zip" "${PACKAGE_NAME}/"
    cd ..

    echo -e "${GREEN}✓ Package created: releases/${PACKAGE_NAME}.zip${NC}"
    BUILT=$((BUILT + 1))
    echo ""
done

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Build Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Successfully built: ${BUILT} target(s)${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Failed builds: ${FAILED} target(s)${NC}"
fi
echo ""

if [ $BUILT -gt 0 ]; then
    echo -e "${GREEN}Release packages created in: releases/${NC}"
    ls -lh releases/*.zip 2>/dev/null || true
    echo ""
    echo -e "${YELLOW}To flash a device:${NC}"
    echo -e "  1. Extract the ZIP for your target"
    echo -e "  2. Follow FLASH_INSTRUCTIONS.txt inside"
fi

exit $FAILED
