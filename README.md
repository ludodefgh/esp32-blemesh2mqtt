| Supported Targets | ESP32 | ESP32-C3 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | --------- | -------- | -------- |

# ESP-IDF Sengled B11N1E Prototype

ESP BLE Mesh to MQTT Bridge with Home Assistant integration.

## ⚠️ Important: Custom ESP-IDF Required

This project uses a **modified version of ESP-IDF** with BLE mesh fixes. The modifications are automatically included when you clone this repository.

## Quick Setup

### 1. Clone with Submodules
```bash
git clone --recursive https://github.com/ludodefgh/ESPIDFSengledB11N1EProto.git
cd ESPIDFSengledB11N1EProto
```

### 2. Setup ESP-IDF Environment
```bash
# Install ESP-IDF dependencies (if not already installed)
cd esp-idf
./install.sh

# Setup environment (run this in every new terminal)
source esp-idf/export.sh
```

### 3. Build and Flash
```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## ESP-IDF Modifications

This project includes a custom ESP-IDF fork with the following BLE mesh fixes:

### Fixed Files:
- **`components/bt/esp_ble_mesh/core/net.h`** - Fixed C99 initialization compatibility
- **`components/bt/esp_ble_mesh/core/prov_pvnr.c`** - Disabled OOB authentication, forced NO_OOB method

### Why These Changes?
1. **C99 Compatibility**: The original code used non-standard initialization that failed with strict C99 builds
2. **Simplified Provisioning**: OOB authentication was causing provisioning failures, so we force NO_OOB method

## Project Features

This demo shows how a BLE Mesh device can function as a provisioner with:

- **MQTT Bridge**: Forwards BLE mesh messages to MQTT for Home Assistant
- **Web Interface**: Control and monitor devices via web UI
- **WiFi Provisioning**: Captive portal for easy WiFi setup
- **Home Assistant Integration**: Automatic device discovery and control

## Manual ESP-IDF Setup (Alternative)

If you prefer to use your existing ESP-IDF installation:

1. **Apply the patches manually** by checking the differences in our fork:
   - https://github.com/ludodefgh/esp-idf/compare/master...ble-mesh-fixes

2. **Or use our ESP-IDF submodule**:
   ```bash
   export IDF_PATH=$(pwd)/esp-idf
   source esp-idf/export.sh
   ```

Please check the [tutorial](tutorial/BLE_Mesh_Provisioner_Example_Walkthrough.md) for more information about this example.