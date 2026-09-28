# Release Process for esp32-blemesh2mqtt

This document describes how to create a new release with pre-compiled binaries.

## Automatic Release (Recommended)

### Option 1: Create Release via GitHub UI

1. **Ensure code is ready**
   ```bash
   # Builds and packages both editions (Standalone + Companion) for every target
   ./build-all-targets.sh
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
   - GitHub Actions will automatically build binaries for both editions (Standalone and
     Companion, see [EDITIONS.md](EDITIONS.md)) on all supported targets
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
./build-all-targets.sh                        # both editions, default targets
./build-all-targets.sh "esp32 esp32c3" companion   # a subset
```

Each edition/target pair builds in its own `build_release/<edition>-<target>/` directory
with its own `sdkconfig`, from `sdkconfig.defaults` plus the edition overlay
(`sdkconfig.defaults.standalone` / `.companion`), so local `sdkconfig` edits (debug tools,
a switched role) never leak into release packages. Packages land in `releases/` as
`BleMesh2Mqtt-<Edition>-v<version>-<target>.zip`.

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

- ESP32, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-C5 (preview) — 4MB flash minimum
- ESP32-C3/C5/C6: firmware updates go over USB (single firmware slot, #44); the web interface still updates over WiFi

### 📦 Installation

**Two editions**: **Standalone** (the bridge creates its own mesh network) or
**Companion** (it joins a network you manage with nRF Mesh). Not sure? See
[Which edition do I need?](https://github.com/ludodefgh/esp32-blemesh2mqtt/blob/main/documentation/EDITIONS.md)

**Quick Flash (No ESP-IDF Required)**

1. Download the `.zip` for your edition and board (`BleMesh2Mqtt-<Edition>-<version>-<chip>.zip`)
2. Extract the archive
3. Follow `FLASH_INSTRUCTIONS.txt` inside

**From Source**: see the README's Dev Container instructions.

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

- [ ] Both editions build on all targets (`./build-all-targets.sh`)
- [ ] Test flash on at least one device per target family
- [ ] WiFi captive portal works
- [ ] MQTT connection works
- [ ] Home Assistant auto-discovery works
- [ ] BLE Mesh provisioning works (Standalone)
- [ ] Joining an existing mesh + External Mesh Nodes work (Companion)
- [ ] OTA refuses the other edition's firmware
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
