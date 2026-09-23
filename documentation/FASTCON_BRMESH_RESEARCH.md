# FastCon / BRMesh Research Notes

Tracking document for the investigation into Melpo/BRMesh light compatibility with esp32-blemesh2mqtt.
Started: 2026-07-01 — triggered by discussion #38 (user bgiuriceo).

---

## Context

User reported Melpo BLFL-LFBA RGB flood lights that work with the iOS BRMesh and Magic Home apps but
are invisible to this project's provisioning scanner.

Root cause identified: these lights use **BroadLink FastCon**, a proprietary BLE protocol that is
fundamentally incompatible with standard SIG BLE Mesh.

---

## What FastCon Is

- Proprietary BroadLink protocol, marketed as "BRMesh" or "BRMesh Lighting"
- **Not** SIG Bluetooth Mesh — no provisioning, no unicast addresses, no models, no AppKeys
- Control is done via **raw BLE advertisement broadcasts** (non-connectable, `ADV_TYPE_NONCONN_IND`)
- No GATT connections involved in normal operation
- Commands are XOR-encrypted with a per-device 4-byte `mesh_key`
- Standard BLE tools (bluez) cannot send the low-level advertisement payloads needed

References:
- https://brmeshlighting.com/brmesh-flood-light-guide/
- https://stephencross.site/posts/brmesh-esphome-homeassistant/
- https://community.home-assistant.io/t/brmesh-app-bluetooth-lights/473486
- https://github.com/dennispg/esphome-fastcon
- https://github.com/ibroadlink/ha_magic_home

---

## Why esp32-blemesh2mqtt Cannot See These Lights

This project is a **SIG BLE Mesh provisioner**. It scans for unprovisioned device beacons (SIG spec).
FastCon lights do not broadcast such beacons — they speak a completely different protocol.
PB-ADV vs PB-GATT is irrelevant here; the incompatibility is at a deeper level.

---

## Existing Integration Options (as of 2026-07)

### 1. esphome-fastcon (community, local)
- https://github.com/dennispg/esphome-fastcon
- ESP32 running ESPHome, bridges FastCon → MQTT / Home Assistant
- Works; requires per-device 4-byte key
- Burst transmission (multiple ad packets per command) needed for reliability
- **Key extraction**:
  - iPhone: BRMesh app → "Add to Siri" → open Shortcut → read "Ctrl Key" field
  - Android: `adb logcat` during device pairing
- Does not run a BLE Mesh stack — has full raw GAP access

### 2. ha_magic_home (official BroadLink HACS)
- https://github.com/ibroadlink/ha_magic_home
- **Cloud-only** — sends HTTPS to BroadLink cloud → GW4C gateway → light
- Reportedly broke with HA 2026.3
- No local control; not suitable for offline setups

### 3. Matter via GW4C gateway
- BroadLink added Matter support to Magic Home app in recent firmware
- Still cloud-dependent via the gateway
- Dead end for local control

---

## Open Questions

### Q1: How does the BRMesh app obtain the device key?
Two hypotheses:
- **Key derived from MAC**: app computes key from device MAC address — no exchange needed.
  If true: scan for MAC, compute key, send commands immediately. Zero pairing required.
- **Key exchanged during setup**: light starts in factory mode with a known default key.
  App generates a random 4-byte key, sends "set key" command over the default channel.
  Light stores it. App keeps it (visible in adb logs / iPhone Shortcuts).

JADX decompilation of the BRMesh APK would answer this definitively.

### Q2: "Fake light" approach — bridge joins as a device
Idea: ESP32 advertises itself as an unprovisioned FastCon light. BRMesh app pairs it.
During pairing exchange, bridge captures the network key. Once "inside" the network,
it can send commands to all other lights.

Similar to how Zigbee coordinators join a network to obtain the network key.

Blockers:
- Requires implementing the "receiver" side of the FastCon pairing handshake
- Need to know exactly what a light responds during pairing (see JADX)
- If key is per-device (not a shared network key), approach may not generalize

### Q3: Is there a shared network key or is each light keyed individually?
Community evidence shows per-device keys (extracted per-light from adb / Shortcuts).
But unclear whether there's also a network-level key that allows broadcast to all devices.
JADX would clarify.

---

## Potential Path to Adding FastCon Support in This Project

### What would be needed
1. **Protocol layer**: port `fastcon_controller.cpp` + `protocol.cpp` from esphome-fastcon.
   Self-contained C++ with no ESPHome dependencies in the core logic. Straightforward.

2. **Key management**: web UI field for user to enter 4-byte hex key per device.
   Stored in NVS alongside existing SIG mesh config.
   Would become unnecessary if Q1 resolves to "key derived from MAC".

3. **BLE coexistence**: the existing BLE Mesh stack uses the advertising channel for beacons.
   FastCon burst transmission (rapid-fire ads) would conflict.
   Needs a spike test before committing to full integration.
   Worst case: dedicate a second ESP32 to FastCon only, bridge via MQTT.

4. **Device model**: FastCon devices have no unicast address, no SIG models.
   Need a parallel device list, MQTT topics, and web UI section.

### Constraints
- No BRMesh/FastCon hardware available for testing on the maintainer side.
- Any integration effort requires a test partner with the physical lights.
- Expect significant back-and-forth for debugging.

### Recommended next step
Decompile the BRMesh Android APK with JADX to answer Q1–Q3 before writing any code.
This will determine whether the "fake light" approach is viable and whether key management
can be made automatic (MAC derivation) vs manual (user entry).

---

## Related Files in This Repo
- `main/ble_mesh/ble_mesh_provisioning.cpp` — SIG BLE Mesh provisioning (not applicable to FastCon)
- `main/ble_mesh/ble_mesh_control.cpp` — bearer config (PB-ADV + PB-GATT enabled as of 2026-07-01)
- `BrMeshLinks.txt` — link collection
