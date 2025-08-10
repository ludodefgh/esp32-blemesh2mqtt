# ESP-IDF BleMesh2MQTT Bridge - Development Container

This devcontainer provides a complete, isolated development environment for the ESP-IDF BleMesh2MQTT Bridge project.

## Features

- **Pre-configured ESP-IDF Environment**: Latest ESP-IDF with all required tools
- **USB Device Access**: Direct access to ESP32 development boards
- **VS Code Extensions**: C/C++, ESP-IDF, Python, and web development tools
- **Port Forwarding**: Web interface (80), development server (8080), MQTT (1883/8883)
- **Non-root User**: Secure development with proper permissions

## Quick Start

1. **Open in VS Code**: Click "Reopen in Container" when prompted, or use Command Palette → "Dev Containers: Reopen in Container"

2. **Wait for Setup**: The container will automatically install ESP-IDF tools (takes 5-10 minutes on first run)

3. **Start Developing**: Once setup completes, you can immediately start building:
   ```bash
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

## Container Details

### User Configuration
- **User**: `esp` (non-root for security)
- **Working Directory**: `/workspace`
- **Home**: `/home/esp`

### Hardware Access
- USB serial devices (`/dev/ttyUSB0`, `/dev/ttyACM0`)
- Privileged mode for device access
- Full `/dev` mount for hardware debugging

### Development Tools
- ESP-IDF with all target support
- GCC cross-compilers for all ESP32 variants
- Python virtual environment
- CMake and Ninja build systems
- Git with host configuration sync

### VS Code Extensions
- **C/C++ IntelliSense**: Code completion and debugging
- **ESP-IDF Extension**: Official Espressif tooling
- **Serial Monitor**: Hardware debugging
- **Web Development**: HTML/CSS/JS support for web interface

## Environment Variables

The container sets up these ESP-IDF variables:
- `IDF_PATH=/workspace/esp-idf`
- `IDF_TOOLS_PATH=/opt/esp`
- `IDF_CCACHE_ENABLE=1` (faster builds)

## Port Forwarding

| Port | Service | Description |
|------|---------|-------------|
| 80 | Web Interface | Device management UI |
| 8080 | Development | Local development server |
| 1883 | MQTT | Message broker |
| 8883 | MQTT SSL | Encrypted message broker |

## Troubleshooting

### Device Not Found
```bash
# Check if device is detected
ls -la /dev/tty*

# Add udev rules if needed (run on host)
sudo usermod -a -G dialout $USER
```

### ESP-IDF Environment
```bash
# Manually source if needed
source esp-idf/export.sh

# Check installation
idf.py --version
```

### Container Rebuild
If you need to rebuild the container:
```bash
# Command Palette → "Dev Containers: Rebuild Container"
```

## Development Workflow

1. **Code**: Edit source files in `main/`
2. **Build**: `idf.py build`
3. **Flash**: `idf.py -p /dev/ttyUSB0 flash`
4. **Monitor**: `idf.py monitor` (Ctrl+] to exit)
5. **Debug**: Use VS Code debugging with ESP-IDF extension

## Custom Configuration

To customize the container, edit:
- `.devcontainer/Dockerfile` - Container image
- `.devcontainer/devcontainer.json` - VS Code integration
- `.devcontainer/post-create.sh` - Setup script
EOF < /dev/null
