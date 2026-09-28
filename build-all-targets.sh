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

# Targets and editions to build, e.g. ./build-all-targets.sh "esp32 esp32c3" "companion"
TARGETS="${1:-esp32 esp32s3 esp32c3 esp32c6}"
EDITIONS="${2:-standalone companion}"

echo -e "${BLUE}Targets to build: ${TARGETS}${NC}"
echo -e "${BLUE}Editions to build: ${EDITIONS}${NC}"
echo ""

# Create releases directory
mkdir -p releases

# Build counter
BUILT=0
FAILED=0

for edition in $EDITIONS; do
if [ ! -f "sdkconfig.defaults.${edition}" ]; then
    echo -e "${RED}✗ Unknown edition '${edition}' (no sdkconfig.defaults.${edition})${NC}"
    FAILED=$((FAILED + 1))
    continue
fi
# Display name: standalone -> Standalone
EDITION_NAME="$(tr '[:lower:]' '[:upper:]' <<< "${edition:0:1}")${edition:1}"

for target in $TARGETS; do
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}Building ${EDITION_NAME} edition for target: ${target}${NC}"
    echo -e "${BLUE}========================================${NC}"

    # Own build dir + sdkconfig per edition/target, so the local sdkconfig(.defaults) edits never leak in
    BUILD_DIR="build_release/${edition}-${target}"
    rm -rf "$BUILD_DIR"
    echo -e "${YELLOW}Building firmware...${NC}"
    if ! idf.py -B "$BUILD_DIR" -D IDF_TARGET="$target" -D SDKCONFIG="$BUILD_DIR/sdkconfig" \
            -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.${edition}" build; then
        echo -e "${RED}✗ Build failed for ${EDITION_NAME} / ${target}${NC}"
        FAILED=$((FAILED + 1))
        continue
    fi

    echo -e "${GREEN}✓ Build successful for ${EDITION_NAME} / ${target}${NC}"

    # Create release package
    PACKAGE_NAME="BleMesh2Mqtt-${EDITION_NAME}-v${VERSION}-${target}"
    PACKAGE_DIR="releases/${PACKAGE_NAME}"

    echo -e "${YELLOW}Creating release package...${NC}"
    rm -rf "$PACKAGE_DIR"
    mkdir -p "$PACKAGE_DIR"

    # Copy binaries
    cp "$BUILD_DIR/BleMesh2Mqtt.bin" "$PACKAGE_DIR/"
    cp "$BUILD_DIR/bootloader/bootloader.bin" "$PACKAGE_DIR/"
    cp "$BUILD_DIR/partition_table/partition-table.bin" "$PACKAGE_DIR/"
    cp "$BUILD_DIR/ota_data_initial.bin" "$PACKAGE_DIR/"
    cp "$BUILD_DIR/storage.bin" "$PACKAGE_DIR/"

    # Combined firmware + web-interface OTA bundle (dashboard "Firmware + Web" option)
    python3 tools/make_update_bundle.py "$BUILD_DIR/BleMesh2Mqtt.bin" "$BUILD_DIR/storage.bin" "$PACKAGE_DIR/update_bundle.bin" || true

    # Flash offsets come from the build itself: they differ per chip (bootloader) and per
    # partition table (storage: 0x3B0000 dual-ota on esp32/esp32s3, 0x2C0000 single-ota on C3/C5/C6)
    flash_offset() {
        python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(next(o for o, f in d["flash_files"].items() if f == sys.argv[2]))' \
            "$BUILD_DIR/flasher_args.json" "$1"
    }
    BOOTLOADER_OFFSET=$(flash_offset bootloader/bootloader.bin)
    PARTITION_OFFSET=$(flash_offset partition_table/partition-table.bin)
    OTA_DATA_OFFSET=$(flash_offset ota_data_initial.bin)
    APP_OFFSET=$(flash_offset BleMesh2Mqtt.bin)
    STORAGE_OFFSET=$(flash_offset storage.bin)

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

Edition: ${EDITION_NAME} (see documentation/EDITIONS.md)
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
idf.py -B build_${edition} -D IDF_TARGET=${target} -D SDKCONFIG=build_${edition}/sdkconfig \\
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.${edition}" -p /dev/ttyUSB0 flash

AFTER FLASHING:
---------------

1. Device will create WiFi AP: "BleMesh2MQTT-Setup-XX:XX:XX"
2. Connect to this AP (no password)
3. Navigate to: http://192.168.4.1
4. Enter your WiFi settings
5. Device will reboot and connect to your network
   Then open its dashboard (its IP address) and set your MQTT broker on the Bridge page
$([ "${edition}" = "companion" ] && echo "6. Companion edition: add the bridge to your mesh with nRF Mesh, then bind the
   AppKey to its client models (see documentation/EDITIONS.md)")

UPDATING AN EXISTING BRIDGE:
----------------------------
$(if [ "$target" = "esp32c3" ] || [ "$target" = "esp32c5" ] || [ "$target" = "esp32c6" ]; then
echo "This chip has a single firmware slot, so the firmware can't be updated from the
dashboard. Run the METHOD 1 command again, WITHOUT erase_flash: WiFi/MQTT settings and
your mesh (lights, keys) are kept."
else
echo "From the dashboard: Firmware page, 'Firmware + Web', upload update_bundle.bin.
Only the same edition is accepted (${EDITION_NAME})."
fi)

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
done

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Build Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Successfully built: ${BUILT} package(s)${NC}"
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}Failed builds: ${FAILED}${NC}"
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
