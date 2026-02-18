# Release Process for esp32-blemesh2mqtt

This document describes how to create a new release with pre-compiled binaries.

## Automatic Release (Recommended)

### Option 1: Create Release via GitHub UI

1. **Ensure code is ready**
   ```bash
   # Test build locally for all targets
   for target in esp32 esp32c3 esp32c6 esp32h2; do
     idf.py set-target $target
     idf.py build
   done
   ```

2. **Create and push a tag**
   ```bash
   git tag -a v1.0.0 -m "Release v1.0.0 - Initial public release"
   git push origin v1.0.0
   ```

3. **Create GitHub Release**
   - Go to https://github.com/ludodefgh/esp32-blemesh2mqtt/releases
   - Click "Draft a new release"
   - Select the tag you just created (v1.0.0)
   - Write release notes (see template below)
   - Click "Publish release"

4. **Automatic Build**
   - GitHub Actions will automatically build binaries for all supported targets
   - Binaries will be attached to the release (~5-10 minutes)

### Option 2: Manual Workflow Trigger

If you want to build binaries without creating a release:

1. Go to Actions tab: https://github.com/ludodefgh/esp32-blemesh2mqtt/actions
2. Select "Build Release Binaries" workflow
3. Click "Run workflow"
4. Enter version tag (e.g., v1.0.0)
5. Download artifacts from the workflow run

## Manual Release (If GitHub Actions Unavailable)

### Build Binaries Locally

```bash
#!/bin/bash
# build-release.sh - Build binaries for all targets

VERSION="v1.0.0"
TARGETS="esp32 esp32c3 esp32c6 esp32h2"

# Setup ESP-IDF environment
source esp-idf/export.sh

mkdir -p releases

for target in $TARGETS; do
  echo "Building for $target..."

  # Set target and build
  idf.py set-target $target
  idf.py build

  # Create release package
  PACKAGE_NAME="BleMesh2Mqtt-${VERSION}-${target}"
  mkdir -p releases/${PACKAGE_NAME}

  # Copy binaries
  cp build/BleMesh2Mqtt.bin releases/${PACKAGE_NAME}/
  cp build/bootloader/bootloader.bin releases/${PACKAGE_NAME}/
  cp build/partition_table/partition-table.bin releases/${PACKAGE_NAME}/
  cp build/ota_data_initial.bin releases/${PACKAGE_NAME}/
  cp build/storage.bin releases/${PACKAGE_NAME}/

  # Create flash instructions
  cat > releases/${PACKAGE_NAME}/FLASH_INSTRUCTIONS.txt << EOF
==========================================
BleMesh2MQTT Flash Instructions
==========================================

Target: ${target}
Version: ${VERSION}

QUICK START - Using esptool.py:
-------------------------------

esptool.py -p /dev/ttyUSB0 -b 460800 --chip ${target} write_flash \\
  0x1000 bootloader.bin \\
  0x8000 partition-table.bin \\
  0xd000 ota_data_initial.bin \\
  0x10000 BleMesh2Mqtt.bin \\
  0x3B0000 storage.bin

Replace /dev/ttyUSB0 with your port (COM3 on Windows, /dev/cu.* on Mac)

For detailed instructions and troubleshooting:
See README.md or https://github.com/ludodefgh/esp32-blemesh2mqtt
EOF

  # Create archive
  cd releases
  zip -r ${PACKAGE_NAME}.zip ${PACKAGE_NAME}/
  cd ..

  echo "✓ Created releases/${PACKAGE_NAME}.zip"
done

echo ""
echo "All binaries built successfully!"
echo "Upload files from releases/ folder to GitHub release"
```

Make the script executable and run:
```bash
chmod +x build-release.sh
./build-release.sh
```

### Upload to GitHub Release

1. Create release as described in Option 1
2. Manually upload the `.zip` files from `releases/` folder

## Release Notes Template

```markdown
## 🎉 BleMesh2MQTT v1.0.0

### ✨ Features

- BLE Mesh provisioner with automatic device discovery
- MQTT bridge with Home Assistant auto-discovery
- Modern web interface with dark/light theme
- Dual OTA updates (firmware + web UI)
- Captive portal for WiFi setup
- AES-256 credential encryption

### 🎯 Supported Targets

- ESP32 (4MB flash minimum)
- ESP32-C3 (4MB flash minimum)
- ESP32-C6 (4MB flash minimum)
- ESP32-H2 (4MB flash minimum)

### 📦 Installation

**Quick Flash (No ESP-IDF Required)**

1. Download the appropriate `.zip` file for your board
2. Extract the archive
3. Follow `FLASH_INSTRUCTIONS.txt` inside

**From Source**

```bash
git clone --recursive https://github.com/ludodefgh/esp32-blemesh2mqtt.git
cd esp32-blemesh2mqtt
./setup.sh
idf.py build flash monitor
```

### 📝 Full Changelog

- Initial public release
- Implements full BLE Mesh provisioner
- Home Assistant MQTT discovery integration
- Responsive web interface
- Captive portal WiFi setup
- OTA firmware updates

### 🐛 Known Issues

- None yet! Please report issues at: https://github.com/ludodefgh/esp32-blemesh2mqtt/issues

### 🙏 Acknowledgments

Special thanks to the ESP-IDF and Home Assistant communities!

---

**Full Documentation**: https://github.com/ludodefgh/esp32-blemesh2mqtt/blob/main/README.md
```

## Version Numbering

Follow Semantic Versioning (semver):
- **v1.0.0** - Major release (breaking changes)
- **v1.1.0** - Minor release (new features, backwards compatible)
- **v1.1.1** - Patch release (bug fixes)

## Pre-release Testing Checklist

Before creating a release:

- [ ] All targets build successfully (esp32, esp32c3, esp32c6, esp32h2)
- [ ] Test flash on at least one device per target family
- [ ] WiFi captive portal works
- [ ] MQTT connection works
- [ ] Home Assistant auto-discovery works
- [ ] BLE Mesh provisioning works
- [ ] OTA update works (both firmware and storage)
- [ ] Web interface loads and is functional
- [ ] Documentation is up to date
- [ ] CHANGELOG.md is updated
- [ ] LICENSE file is present

## Post-Release

1. **Verify release assets**
   - Check all ZIP files are attached
   - Download and test flash on one device

2. **Update documentation**
   - Update README.md with latest version info
   - Update any screenshots if UI changed

3. **Announce release**
   - Post in Discussions tab
   - Share in Home Assistant community forums (if appropriate)
   - Update any related documentation

## Troubleshooting Build Issues

**Submodule issues:**
```bash
git submodule update --init --recursive
```

**Clean build:**
```bash
idf.py fullclean
idf.py build
```

**Different ESP-IDF version:**
```bash
cd esp-idf
git fetch
git checkout v5.1
git submodule update --init --recursive
./install.sh
cd ..
```

For more help, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md) or open an issue.
