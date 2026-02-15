# Claude Code Instructions for esp32-blemesh2mqtt

## Git Workflow

### Remote Configuration
**CRITICAL**: Always use SSH for git operations, never HTTPS.

```bash
# Remote should be configured as:
git remote set-url origin git@github.com:ludodefgh/esp32-blemesh2mqtt.git

# Verify with:
git remote -v
```

When pushing commits, always use:
```bash
git push origin main
```

### Commit Messages
Follow conventional commit style:
- Use present tense ("Add feature" not "Added feature")
- First line: concise summary (max 70 chars)
- Body: detailed explanation with bullet points for multiple changes
- Always include `Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>` footer

Example:
```
Fix captive portal DHCP lease persistence

- Destroy and recreate AP netif to clear DHCP state
- Improve captive portal detection for invalid credentials
- Add proper 404 handling for backend endpoints

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

## Project Architecture

### Captive Portal vs Bridge Mode
The web server operates in two distinct modes with separate handlers:

#### Captive Portal Mode
- **When**: WiFi not configured or connection failed
- **Handler**: `static_handler_captive_portal()` in `main/web_server/web_server.cpp`
- **Purpose**: Serves setup page for WiFi configuration
- **State**: `WIFI_PROV_STATE_AP_STARTED`, `WIFI_PROV_STATE_STA_FAILED`, etc.
- **Registered in**: `register_captive_portal_handlers()`

#### Bridge Mode
- **When**: WiFi connected successfully
- **Handler**: `static_handler()` in `main/web_server/web_server.cpp`
- **Purpose**: Serves BLE Mesh bridge UI from LittleFS
- **State**: `WIFI_PROV_STATE_STA_CONNECTED`
- **Registered in**: `register_bridge_handlers()`

### Important: Mode Separation
The two modes are **completely separated** with different URI handlers. This makes the captive portal easily extractable as a reusable component for other projects.

**DO NOT** mix captive portal logic into bridge mode handlers or vice versa.

## Key Components

### WiFi Provisioning (`main/wifi/`)
- Manages captive portal and WiFi credentials
- DHCP lease management (destroy/recreate netif to clear state)
- DNS server for captive portal detection
- Handles WiFi reconnection in bridge mode

### Web Server (`main/web_server/`)
- Two separate static file handlers (captive portal vs bridge)
- Dynamic handler registration based on WiFi state
- API endpoints for MQTT, nodes, OTA, etc.

### BLE Mesh (`main/ble_mesh/`)
- Provisioner implementation
- Node management and control
- MQTT bridge functionality

## Build & Flash

```bash
idf.py build
idf.py flash monitor
```

## Testing Captive Portal

1. **Fresh start**: Wipe flash → captive portal starts automatically
2. **Reset credentials**: From UI, click "Reset WiFi" → ESP reboots into captive portal
3. **Invalid credentials**: Enter wrong WiFi password → ESP detects failure and stays in captive portal mode

The captive portal should:
- ✅ Always assign IP addresses to connecting phones (DHCP lease issue fixed)
- ✅ Display setup page even with invalid credentials stored
- ✅ Return proper 404 for backend endpoints (`/api/*`, `/mqtt/*`, etc.)
- ✅ Handle duplicate WiFi scan events in APSTA mode

## Code Style

- Prefer specific file operations over wildcard git commands
- Use `git add <specific files>` instead of `git add .`
- Keep captive portal and bridge mode code separated
- Document complex state transitions with comments
- Use meaningful log levels (DEBUG, INFO, WARN, ERROR)

## Notes

- The ESP32 is a BLE Mesh provisioner that bridges BLE Mesh nodes to MQTT
- OTA updates use a cryptographically secure random key (not MAC-based)
- Captive portal uses esp_netif and lwIP DHCP server
- WiFi credentials are encrypted before storage in NVS
