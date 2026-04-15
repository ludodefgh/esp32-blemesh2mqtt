# BleMesh2MQTT Bridge

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-blue.svg)](https://github.com/espressif/esp-idf)
[![Version](https://img.shields.io/badge/version-0.1.6-green.svg)](https://github.com/ludodefgh/esp32-blemesh2mqtt/releases)
[![GitHub issues](https://img.shields.io/github/issues/ludodefgh/esp32-blemesh2mqtt.svg)](https://github.com/ludodefgh/esp32-blemesh2mqtt/issues)

| Supported Targets | ESP32 | ESP32-S3 | ESP32-C3 | ESP32-C6 |
| ----------------- | ----- | -------- | -------- | -------- |

> **Note**: All supported targets require at least **4MB of flash memory** and **WiFi connectivity**.

A powerful ESP32-based bridge that connects BLE Mesh devices to MQTT, enabling seamless integration with Home Assistant and other home automation platforms. This project transforms your ESP32 into a comprehensive IoT gateway.

📖 **[User Guide — Web Interface & Home Assistant Integration](documentation/USER_GUIDE.md)**

## 🌟 Key Features

### 🔗 **Dual Connectivity Bridge**
- **BLE Mesh Provisioner**: Automatically discovers, provisions, and manages BLE Mesh devices
- **MQTT Publisher**: Bridges all mesh communications to MQTT with Home Assistant auto-discovery
- **Real-time Communication**: Bidirectional message forwarding between BLE Mesh and MQTT networks

### 🏠 **Home Assistant Integration**
- **Automatic Discovery**: Zero-configuration device setup with Home Assistant MQTT discovery
- **Device Management**: Complete device lifecycle management (provision, configure, remove)
- **Real-time Status**: Live device status updates and health monitoring
- **Native Entity Support**: Lights, switches, sensors appear as native Home Assistant entities

### 🌐 **Professional Web Interface**
- **Modern Responsive UI**: Clean, mobile-friendly interface with dark/light theme support
- **Real-time Dashboard**: Live system monitoring with memory usage, uptime, and connection status
- **Device Management**: Visual device provisioning, configuration, and control
- **System Administration**: WiFi configuration, MQTT setup, and system controls

### 🔄 **Advanced OTA System**
- **Dual OTA Support**: Separate firmware and web interface updates
- **Safe Updates**: Rollback protection and update validation
- **Live Updates**: Update web interface without device restart
- **Progress Monitoring**: Real-time upload progress and status feedback

### 🛡️ **Enterprise Security**
- **Credential Encryption**: Secure storage of WiFi and MQTT credentials
- **Secure Provisioning**: Safe device onboarding process

### 📱 **Smart WiFi Provisioning**
- **Captive Portal**: Automatic setup portal with iOS/Android detection
- **RFC 8910 Compliant**: Standards-based captive portal detection
- **WiFi Scanning**: Dropdown selection of available networks
- **Auto-Recovery**: Fallback to setup mode on connection failure

### 📊 **Comprehensive Monitoring**
- **Real-time Logging**: Live system logs via WebSocket connection

## 🏗️ Architecture Overview

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   BLE Mesh      │    │  BleMesh2MQTT    │    │  Home Assistant │
│   Devices       │◄──►│     Bridge       │◄──►│   via MQTT      │
│  (Lights, etc.) │    │    (ESP32)       │    │                 │
└─────────────────┘    └──────────────────┘    └─────────────────┘
                              │
                              ▼
                       ┌─────────────┐
                       │ Web Interface│
                       │ (Management) │
                       └─────────────┘
```

### Core Components

- **BLE Mesh Stack**: Custom provisioner implementation with enhanced stability
- **MQTT Bridge**: High-performance message translation and routing
- **Web Server**: Embedded HTTP server with WebSocket support for real-time updates
- **Storage System**: LittleFS-based file system for web assets and configuration
- **Security Layer**: Encrypted credential storage and secure communications
- **OTA Manager**: Over-the-air update system with rollback protection

## 🚀 Quick Start

### Option A: Pre-compiled Binaries (No ESP-IDF Required) ⚡

**Fastest way to get started - no development environment needed!**

1. **Download Pre-built Firmware**
   - Go to [Releases](https://github.com/ludodefgh/esp32-blemesh2mqtt/releases)
   - Download the `.zip` file for your board (e.g., `BleMesh2Mqtt-v1.0.0-esp32.zip`)
   - Extract the archive

2. **Flash to Device**
   - **Web Flash** (Easiest - Chrome/Edge): Visit [ESP Web Tools](https://web.esphome.io/)
   - **Command Line**: See `FLASH_INSTRUCTIONS.txt` in the downloaded archive
   - **Windows Tool**: Use [ESP Flash Download Tool](https://www.espressif.com/en/support/download/other-tools)

3. **Initial Setup**
   - Device creates WiFi AP: `BleMesh2MQTT-Setup-XX:XX:XX`
   - Connect and navigate to `192.168.4.1`
   - Configure WiFi and MQTT settings

### Option B: Build from Source (Dev Container) 🛠️

The recommended development environment uses a **Dev Container** that includes all tools pre-configured (custom ESP-IDF with BLE Mesh fixes, compilers, VS Code extensions).

### Prerequisites
- [Docker Desktop](https://www.docker.com/products/docker-desktop/)
- [VS Code](https://code.visualstudio.com/) with the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
- ESP32 development board with **4MB+ flash** and **WiFi** (ESP32, ESP32-S3, ESP32-C3, or ESP32-C6)
- Home Assistant instance with MQTT broker

### 1. Clone Repository
```bash
git clone https://github.com/ludodefgh/esp32-blemesh2mqtt.git
cd esp32-blemesh2mqtt
```

### 2. Open in Dev Container
- Open the folder in VS Code
- When prompted, click **"Reopen in Container"** — or use the Command Palette: `Dev Containers: Reopen in Container`
- Wait for the container to build (first time only, ~5-10 minutes — downloads the custom ESP-IDF image)

> **Note**: The container automatically includes the custom ESP-IDF fork with BLE Mesh fixes. No manual ESP-IDF installation is needed.

### 3. Configure and Build
```bash
# Set your target device (ESP32, ESP32-C3, etc.)
idf.py set-target esp32

# Configure project (optional - defaults work for most cases)
idf.py menuconfig

# Build the project
idf.py build
```

### 4. Flash and Monitor
```bash
# Flash to device (connected via USB to the host machine)
idf.py -p /dev/ttyUSB0 flash monitor
```

### 5. Initial Setup
1. **Connect to WiFi**: Device creates `BleMesh2MQTT-Setup` AP on first boot
2. **Access Web Interface**: Navigate to `192.168.4.1` and configure WiFi
3. **Configure MQTT**: Set your Home Assistant MQTT broker details
4. **Start Provisioning**: Enable device discovery to begin adding BLE Mesh devices

## 📱 Web Interface Guide

Access the web interface at your device's IP address after WiFi setup:

### **Bridge Dashboard** 🌉
- System status and health monitoring
- Memory usage and uptime tracking
- WiFi and MQTT connection status
- Bridge control functions

### **BLE Mesh Management** 📡
- View all provisioned devices
- Scan for new unprovisioned devices
- Provision devices with one click
- Monitor device health and status

### **Firmware Updates** 💾
- Upload new firmware binaries
- Update web interface files
- Monitor upload progress
- Automatic validation and rollback

### **Debug Console** 🐛
- Live system logs

## ⚙️ Configuration

### MQTT Settings
- **Broker Host**: Your MQTT broker IP or hostname
- **Port**: Usually 1883 (non-SSL) or 8883 (SSL)
- **Credentials**: Username and password for MQTT authentication
- **SSL/TLS**: Enable for encrypted communications

### BLE Mesh Configuration
- **Provisioning**: Automatic device discovery and provisioning
- **Network Key**: Automatically generated secure network keys
- **Address Assignment**: Dynamic address allocation for new devices

### System Configuration
- **WiFi**: Supports WPA2/WPA3 with automatic reconnection
- **Storage**: LittleFS partition for web assets and logs
- **Security**: Hardware-accelerated encryption for credentials

## 🏠 Home Assistant Integration

### Automatic Discovery
Devices appear automatically in Home Assistant with:
- **Device Information**: Model, manufacturer, firmware version
- **Entity Categories**: Lights, switches, sensors, diagnostics
- **Control Entities**: On/off, brightness, color control
- **Status Entities**: Battery level, signal strength, availability

### MQTT Topics Structure
```
blemesh2mqtt_<MAC>/
├── bridge/
│   ├── state          # Bridge status and info
│   ├── info           # System information
│   └── availability   # Online/offline status
└── device_<addr>/
    ├── light/set      # Light control commands
    ├── light/state    # Light status updates
    └── sensor/        # Sensor readings
```

## 🔧 Development

### Project Structure
```
├── main/
│   ├── ble_mesh/         # BLE Mesh provisioner implementation
│   ├── mqtt/             # MQTT bridge and Home Assistant integration
│   ├── web_server/       # HTTP server and REST API
│   ├── wifi/             # WiFi management and captive portal
│   ├── ota/              # Over-the-air update system
│   ├── security/         # Credential encryption and security
│   ├── littlefs/         # Web interface static files
│   └── common/           # Shared utilities and definitions
├── .devcontainer/        # Dev Container config (Docker + VS Code)
└── documentation/        # User guides and screenshots
```

### Custom ESP-IDF
This project uses a forked ESP-IDF (`ludodefgh/esp-idf`, branch `ble-mesh-fixes`) with essential BLE Mesh fixes:
- **C99 Compatibility**: Resolved initialization issues in mesh core
- **Provisioning Stability**: Fixed OOB authentication problems
- **Enhanced Reliability**: Improved error handling and recovery

The forked ESP-IDF is automatically included in the Dev Container image — no manual installation required.

### Building Custom Features
The modular architecture allows easy extension:
- Add new device types in `ble_mesh/`
- Extend MQTT functionality in `mqtt/`
- Create new web interface features in `web_server/`
- Implement custom security features in `security/`

## 🐛 Troubleshooting

### Common Issues

**Dev Container / ESP-IDF Issues**
```bash
# If the ESP-IDF environment is not sourced:
source /opt/esp/idf/export.sh

# If the container needs to be rebuilt (Command Palette in VS Code):
# Dev Containers: Rebuild Container

# Verify ESP-IDF is available:
idf.py --version
```

**Build Errors**
```bash
# Clean build and try again:
idf.py fullclean
idf.py build

# If you get partition errors:
idf.py erase-flash
idf.py flash
```

**Device Not Found**
- Ensure ESP32 is powered and running
- Check serial logs for boot messages
- Verify correct port in flash command (`ls /dev/tty*` on Linux/Mac, Device Manager on Windows)
- Try different USB cable or port
- Install USB-to-serial drivers (CP210x or CH340)

**WiFi Connection Failed**
- Reset WiFi via web interface or button
- Check SSID/password in captive portal
- Verify network supports ESP32 (2.4GHz only, not 5GHz)
- Check if network has client isolation enabled
- Try disabling 802.11w (Protected Management Frames)

**MQTT Not Connecting**
- Verify broker IP and port (use IP address, not hostname if DNS fails)
- Check username/password
- Test MQTT broker accessibility from your network
- Verify broker allows connections from ESP32's IP
- Enable debug logs for detailed error info
- Check firewall rules on broker

**BLE Mesh Provisioning Failed**
- Ensure target device is in provisioning mode (usually flashing/blinking)
- Check for interference from other BLE devices
- Move devices closer together (within 1-2 meters)
- Reset mesh network if needed
- Verify device compatibility (must support BLE Mesh, not just BLE)
- Check if device UUID matches expected format

**Captive Portal Not Appearing**
- Wait 30-60 seconds after connecting to AP
- Manually navigate to `192.168.4.1`
- Try forgetting and reconnecting to the WiFi network
- Disable mobile data on smartphone
- Some Android devices require "Use network as is" option

**Out of Memory Errors**
- Reduce `CONFIG_BLE_MESH_MAX_PROV_NODES` in menuconfig
- Disable debug logging in production builds
- Monitor heap usage via web interface
- Consider using ESP32 with more RAM (ESP32-S3 has more, but no BLE Mesh support)

### Debug Tools
- **Serial Monitor**: `idf.py monitor` for real-time logs
  - Press `Ctrl+]` to exit
  - Use `idf.py monitor -p /dev/ttyUSB0` to specify port
- **Web Console**: Access debug commands via web interface
- **MQTT Logs**: Monitor MQTT traffic for message debugging
  - Use `mosquitto_sub -h <broker> -t blemesh2mqtt_#` to monitor all topics
- **Memory Monitoring**: Track heap usage for stability issues
  - Available in web interface dashboard
  - Also check serial logs for heap warnings

## 🤝 Contributing

Contributions are welcome! This project is designed for the ESP32 and Home Assistant community.

### Development Setup
1. Fork the repository
2. Create a feature branch
3. Make changes following the existing code style
4. Test thoroughly with real hardware
5. Submit a pull request

### Areas for Contribution
- New BLE Mesh device support
- Enhanced web interface features
- Additional Home Assistant integrations
- Performance optimizations
- Documentation improvements

## 📄 License

This project is open source and available under the MIT License. See LICENSE file for details.

## 🙏 Acknowledgments

- **ESP-IDF Team**: For the excellent development framework
- **Home Assistant Community**: For inspiration and integration standards
- **BLE Mesh Community**: For protocol documentation and examples
- **Contributors**: Everyone who helps make this project better

## 📞 Support

- **Issues**: Report bugs via GitHub Issues
- **Discussions**: Join community discussions for questions
- **Documentation**: Check the tutorial folder for detailed guides
- **Updates**: Watch the repository for new releases and features

---

**Made with ❤️ for the Home Assistant and ESP32 communities**
