# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned
- [ ] Additional BLE Mesh device types support
- [ ] Enhanced web interface features
- [ ] Performance optimizations
- [ ] SSL/TLS support for MQTT

## [0.1.6] - 2026-04-15

### Fixed
- Bootloader flash offset corrected for RISC-V targets: ESP32-C3, C6, and H2
  use `0x0`; ESP32-C5 uses `0x2000`; Xtensa targets (ESP32, S2, S3) keep `0x1000`.
  Using the wrong offset caused a download-mode boot loop after flashing.
- ESP32-C6 now uses `--after watchdog-reset` when flashing via its native
  USB-Serial/JTAG port, preventing the chip from staying stuck in download mode
  (same fix already applied to ESP32-C3 in v0.1.5).
- Added per-target troubleshooting notes in generated `FLASH_INSTRUCTIONS.txt`
  for ESP32-C3 and ESP32-C6.

## [0.1.5] - 2026-03-08

### Added
- WiFi RSSI display in the Bridge page WiFi Status panel
- WiFi TX power control via web interface and `/api/wifi_power` API endpoint
- ESP32-C5 target support (requires `--preview` flag)

### Changed
- Brightness slider now syncs with the node's current state on page load
- HSL lightness range corrected; color mode handling improved in MQTT bridge

### Fixed
- Unprovision a node caused both "Provisioned Nodes" and "Unprovisioned Devices" lists to appear empty on page reload — invalid JSON was generated when the deleted node occupied slot 0 of the provisioner table (leading comma bug)

## [0.1.4] - 2026-02-19

### Added
- User guide documentation with screenshots (web interface and Home Assistant integration)

### Changed
- README: replaced manual ESP-IDF build instructions with Dev Container workflow
- README: updated project structure, custom ESP-IDF description, and troubleshooting section to reflect devcontainer setup
- README: added link to user guide

## [0.1.3] - 2026-02-16

### Added
- Initial public release preparation
- Comprehensive README with installation instructions
- GitHub Actions workflow for automated binary builds
- MIT License
- Pre-compiled binary support with flash instructions
- Release process documentation

### Changed
- Updated captive portal SSID format to "BleMesh2MQTT-Setup-XX:XX:XX"
- Improved troubleshooting documentation
- Updated supported targets list (ESP32, ESP32-S3, ESP32-C3, ESP32-C6)
- Added ESP32-S3 support (Xtensa dual-core with WiFi + BLE)
- Removed ESP32-H2 support (lacks WiFi connectivity required for MQTT)
- Enhanced README with Quick Start options (pre-compiled vs. from source)

### Fixed
- Documentation alignment with actual code implementation
- ESP-IDF version references (v5.5)

## [0.1.2] - 2025-12-11

### Added
- Captive portal WiFi provisioning
- RFC 8910 compliant portal detection
- iOS/Android automatic portal detection
- WiFi network scanning with dropdown selection
- Auto-recovery fallback to setup mode

### Changed
- Improved DHCP lease management
- Enhanced captive portal reliability

### Fixed
- DHCP lease persistence issues
- Duplicate WiFi scan events in APSTA mode
- 404 handling for backend endpoints during captive portal

## [0.1.1] - 2025-08-20

### Added
- Dark/light theme toggle in web interface
- Responsive design improvements
- Mobile hamburger menu

### Changed
- Updated web interface CSS (1,563 lines)
- Improved JavaScript functionality (1,660 lines)

### Fixed
- Mobile viewport optimization
- Theme persistence in localStorage

## [0.1.0] - 2025-08-08

### Added
- BLE Mesh provisioner implementation
- MQTT bridge with Home Assistant auto-discovery
- Modern responsive web interface
- Dual OTA updates (firmware + web UI)
- AES-256 credential encryption
- Real-time WebSocket logging
- System monitoring dashboard
- Device management interface
- Debug console

### Core Features
- Automatic BLE Mesh device discovery and provisioning
- Zero-configuration Home Assistant integration
- Encrypted WiFi and MQTT credential storage
- Over-the-air firmware updates with rollback protection
- LittleFS-based web interface storage
- Custom ESP-IDF with BLE Mesh fixes

### Supported Targets
- ESP32 (4MB+ flash)
- ESP32-S3 (4MB+ flash, WiFi, dual-core Xtensa)
- ESP32-C3 (4MB+ flash, WiFi, RISC-V)
- ESP32-C6 (4MB+ flash, WiFi, RISC-V)

---

## Version History Guidelines

### Version Format
- **Major.Minor.Patch** (e.g., 1.0.0)
- **Major**: Breaking changes or significant feature additions
- **Minor**: New features, backwards compatible
- **Patch**: Bug fixes, small improvements

### Categories
- **Added**: New features
- **Changed**: Changes in existing functionality
- **Deprecated**: Soon-to-be removed features
- **Removed**: Removed features
- **Fixed**: Bug fixes
- **Security**: Security improvements

### How to Update This File

When preparing a release:

1. Move items from **[Unreleased]** to a new version section
2. Add release date in YYYY-MM-DD format
3. Update version number in `main/common/version.h`
4. Create git tag: `git tag -a v1.0.0 -m "Release v1.0.0"`
5. Update README badges if needed

Example:
```markdown
## [1.0.0] - 2026-03-01

### Added
- New feature description

### Changed
- Modified feature description

### Fixed
- Bug fix description
```

---

[Unreleased]: https://github.com/ludodefgh/esp32-blemesh2mqtt/compare/v0.1.5...HEAD
[0.1.5]: https://github.com/ludodefgh/esp32-blemesh2mqtt/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/ludodefgh/esp32-blemesh2mqtt/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/ludodefgh/esp32-blemesh2mqtt/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/ludodefgh/esp32-blemesh2mqtt/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/ludodefgh/esp32-blemesh2mqtt/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/ludodefgh/esp32-blemesh2mqtt/releases/tag/v0.1.0
