# BleMesh2MQTT Bridge

| Supported Targets | ESP32 | ESP32-C3 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | --------- | -------- | -------- |

A powerful ESP32-based bridge that connects BLE Mesh devices to MQTT, enabling seamless integration with Home Assistant and other home automation platforms. This project transforms your ESP32 into a comprehensive IoT gateway with professional-grade features.

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

### 📊 **Comprehensive Monitoring**
- **Real-time Logging**: Live system logs via WebSocket connection
- **Performance Metrics**: Memory usage, network statistics, and system health
- **Debug Console**: Full ESP-IDF console access for advanced troubleshooting
- **Device Diagnostics**: Per-device status monitoring and error reporting

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
- **OTA Manager**: Advanced over-the-air update system with rollback protection

## 🚀 Quick Start

### Prerequisites
- ESP32 development board (ESP32, ESP32-C3, ESP32-C6, ESP32-C61, ESP32-H2, or ESP32-S3)
- Home Assistant instance with MQTT broker
- Development machine with Git and Python

### 1. Clone Repository
```bash
git clone --recursive https://github.com/ludodefgh/ESPIDFSengledB11N1EProto.git
cd ESPIDFSengledB11N1EProto
```

### 2. Setup Development Environment
```bash
# Run the automated setup script
./setup.sh

# Or manually setup ESP-IDF
cd esp-idf
./install.sh
source export.sh
cd ..
```

### 3. Configure and Build
```bash
# Set your target device (ESP32, ESP32-C3, etc.)
idf.py set-target esp32

# Configure project (optional - use defaults for quick start)
idf.py menuconfig

# Build the project
idf.py build
```

### 4. Flash and Monitor
```bash
# Flash to device (replace with your port)
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
- Execute ESP-IDF commands
- Advanced troubleshooting tools
- Performance monitoring

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
├── esp-idf/              # Custom ESP-IDF with BLE Mesh fixes
└── tutorial/             # Development guides and examples
```

### Custom ESP-IDF
This project includes a modified ESP-IDF with essential BLE Mesh fixes:
- **C99 Compatibility**: Resolved initialization issues in mesh core
- **Provisioning Stability**: Fixed OOB authentication problems
- **Enhanced Reliability**: Improved error handling and recovery

### Building Custom Features
The modular architecture allows easy extension:
- Add new device types in `ble_mesh/`
- Extend MQTT functionality in `mqtt/`
- Create new web interface features in `web_server/`
- Implement custom security features in `security/`

## 🐛 Troubleshooting

### Common Issues

**Device Not Found**
- Ensure ESP32 is powered and running
- Check serial logs for boot messages
- Verify correct port in flash command

**WiFi Connection Failed**
- Reset WiFi via web interface or button
- Check SSID/password in captive portal
- Verify network supports ESP32

**MQTT Not Connecting**
- Verify broker IP and port
- Check username/password
- Test MQTT broker accessibility
- Enable debug logs for detailed error info

**BLE Mesh Provisioning Failed**
- Ensure target device is in provisioning mode
- Check for interference from other BLE devices
- Reset mesh network if needed
- Verify device compatibility

### Debug Tools
- **Serial Monitor**: `idf.py monitor` for real-time logs
- **Web Console**: Access debug commands via web interface
- **MQTT Logs**: Monitor MQTT traffic for message debugging
- **Memory Monitoring**: Track heap usage for stability issues

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