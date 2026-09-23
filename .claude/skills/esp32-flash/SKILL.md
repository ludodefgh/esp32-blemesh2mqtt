---
name: esp32-flash
description: Build and flash either the bridge (ESP32 WROOM, /dev/ttyUSB0, this repo) or the C3 test node (ESP32-C3, /dev/ttyACM0, /workspaces/onoff_server_test). Use whenever the user asks to flash, reflash, build, or erase either board.
---

# esp32-flash

Two boards live on this devcontainer:

| Target   | Chip       | Port           | Project dir                              |
|----------|------------|----------------|-------------------------------------------|
| `bridge` | ESP32 WROOM (CH340 USB bridge) | `/dev/ttyUSB0` | `/workspaces/ESPIDFSengledB11N1EProto` (this repo) |
| `c3`     | ESP32-C3 (native USB-Serial-JTAG) | `/dev/ttyACM0` | `/workspaces/onoff_server_test` (Espressif's stock `onoff_server` example — a disposable test fixture, not project code; safe to erase/reflash freely) |

## Steps

1. **Always export the ESP-IDF environment first**, once per shell session:
   ```bash
   source /opt/esp/idf/export.sh
   ```
   (Cheap to re-run if unsure whether it's already exported.)

2. **cd into the right project dir** for the target (see table above) before running any `idf.py` command — the two projects are entirely separate ESP-IDF projects with their own `sdkconfig`/`build/`.

3. **Build + flash**, always with an explicit `-p` (both ports can be attached at once, so an implicit/default port is not safe to assume):
   ```bash
   idf.py -p /dev/ttyUSB0 flash   # bridge
   idf.py -p /dev/ttyACM0 flash   # c3
   ```
   `flash` implies a build first; no need to call `build` separately unless you want to see compile errors before committing to a flash.

4. **Full erase** (clears ALL NVS state — WiFi/MQTT credentials, mesh keys, node identity, replay-protection list) when you need a truly clean slate, e.g. after changing mesh role, or when a device is stuck with a stale replay-protection (RPL) entry that's silently dropping legitimate messages:
   ```bash
   idf.py -p <port> erase-flash
   idf.py -p <port> flash
   ```
   A plain `flash` (no erase) preserves NVS across reflashes — use this by default; it's what lets you iterate on a debug log without losing the device's provisioned state.

## Gotchas (hard-won this project)

- **VSCode's Serial Monitor panel holds an exclusive lock on the port.** If flashing fails with `Could not exclusively lock port`, ask the user to close that panel first — you cannot close it yourself.
- **Kconfig *choice* options (e.g. `CONFIG_BLE_MESH_PROVISIONER`, `CONFIG_BLE_MESH_TRACE_LEVEL_*`) can silently revert** if you run `idf.py set-target` or let `reconfigure` re-merge `sdkconfig.defaults*` over a manual edit. After editing `sdkconfig` directly and running `idf.py reconfigure`, **always `grep` the resulting `sdkconfig` again** to confirm your edit actually stuck before building — don't assume it did.
- **Editing `sdkconfig` directly is fine** (this project does it routinely instead of interactive `menuconfig`, which doesn't work non-interactively anyway) — edit, then `idf.py reconfigure`, then verify, then `build`/`flash`.
- Devcontainer USB passthrough only binds devices present at container start — a replugged/new device needs a full devcontainer restart to appear, not just a rescan.
- After flashing, the bridge (`ttyUSB0`) auto-resets via RTS (`Hard resetting via RTS pin`). The C3 (`ttyACM0`) also resets on flash, but see `esp32-monitor` for why you'll usually want to trigger a *second*, deliberate reset before capturing its boot log.
