# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

ESP32 firmware (ESP-IDF v5.5-dev, C++23) that bridges a BLE Mesh network to MQTT, with Home Assistant auto-discovery and a built-in web dashboard / captive-portal setup wizard. It can run as either of two roles from the **same codebase**, selected at build time (see Architecture below): a standalone BLE Mesh **Provisioner** that owns and provisions its own mesh, or a **Node** that joins an *existing* mesh (e.g. one already managed by nRF Mesh / real Sengled bulbs) without provisioning anything itself.

## Build, flash, monitor

Two ESP-IDF projects exist on this devcontainer, each with real attached hardware for live testing:

| Project | Chip | Port | Role |
|---|---|---|---|
| `/workspaces/ESPIDFSengledB11N1EProto` (this repo) | ESP32 WROOM | `/dev/ttyUSB0` | the bridge |
| `/workspaces/onoff_server_test` | ESP32-C3 | `/dev/ttyACM0` | disposable test node running Espressif's stock `onoff_server` example (not this project's code) |

Use the **`esp32-flash`** and **`esp32-monitor`** skills for build/flash/serial-log workflows — they encode hard-won gotchas (non-interactive `idf.py monitor` doesn't work, `ttyACM0` resets on every open, Kconfig *choice* options can silently revert on `reconfigure`, etc.) that are easy to rediscover the hard way otherwise.

Quick reference:
```bash
source /opt/esp/idf/export.sh
idf.py -p /dev/ttyUSB0 build          # compile only
idf.py -p /dev/ttyUSB0 flash          # build + flash (always pass -p; both ports can be attached at once)
idf.py -p /dev/ttyUSB0 erase-flash    # wipe all NVS state (WiFi/MQTT creds, mesh keys, node identity) — full reset
```
No automated test suite exists; validation is manual against real hardware (see `documentation/TEST_PLAN_JOIN_EXISTING_MESH.md`) plus a documented multi-target compile check before release (`documentation/RELEASE_PROCESS.md`): `for target in esp32 esp32c3 esp32c6 esp32h2; do idf.py set-target $target; idf.py build; done`.

## Architecture

### Two SKUs, one codebase: `CONFIG_BLE_MESH_PROVISIONER`

The device is either a mesh Provisioner (standalone mode, self-generates NetKey/AppKey/address) or a mesh Node (join-existing mode, provisioned by an external provisioner like nRF Mesh). **Both roles cannot be compiled in together** — `bt_mesh_init()` unconditionally initializes Provisioner-role internals whenever `CONFIG_BLE_MESH_PROVISIONER` is set, which crashes (`Guru Meditation`) if the runtime role is actually Node. Provisioner-only code is therefore guarded with `#ifdef CONFIG_BLE_MESH_PROVISIONER` throughout (`ble_mesh_control.cpp`, `ble_mesh_provisioning.cpp`, `ble_mesh_node.cpp`, `web_server.cpp`, `mqtt_control.cpp`) rather than deleted, so the standalone SKU keeps working when that config is re-enabled. Which mode is active at runtime is a separate, persisted setting (`mesh_config_t.mode`, `MESH_MODE_STANDALONE` vs `MESH_MODE_JOIN_EXISTING`) — the sdkconfig flag controls what's *possible to compile*, the persisted mode controls what actually *runs*.

### Two NVS namespaces, easy to conflate

- `mesh_cfg` (`MESH_CONFIG_NAMESPACE` in `wifi/mesh_config.*`): this app's own config — mode, net/app key (standalone mode only), group address, and (join-existing mode) the node identity learned from a real provisioning handshake (`node_addr`, `node_net_idx`, `node_app_idx`).
- `mesh_core`: the BLE Mesh **stack's own** persisted state (NetKey/AppKey material, sequence number, replay-protection list, role) — not exposed via any public `esp_ble_mesh_*` API to erase; `mesh_config_reset_stack_state()` does it by opening that namespace directly and calling `nvs_erase_all()`. Required before switching roles (the stack refuses to enable a role that mismatches whatever role it last persisted there).

### Provisioned nodes vs. "External Mesh Nodes" — separate systems that look similar in the UI

`node_manager()` (`ble_mesh_node.*`) tracks devices **this bridge itself provisioned** (standalone mode only) — it has their DevKey, so full Composition Data Get / Config Client operations work.

"External Mesh Nodes" (`ble_mesh_discover_external_nodes` and friends in `ble_mesh_control.cpp`) are devices on a joined mesh that this bridge never provisioned — no DevKey means Composition Data Get is unreachable by spec (Config Server only ever accepts the DevKey). Capability discovery instead probes each model type with a Get to the shared group address and records who answers as the closest available substitute. This is a distinct data structure (`external_mesh_node_t` / `external_nodes` vector) and code path from `node_manager()`, even though both render as similar-looking cards in the dashboard.

### BLE Mesh sends are serialized through one queue

Concurrent unacknowledged sends to the mesh stack are unreliable, so outbound BLE Mesh messages that need a response (Config Client ops, composition reads) go through `message_queue()` (`ble_mesh/message_queue.*`) — a single queue with per-message retry — rather than being fired directly from callbacks.

### Debug console commands self-register

Console commands register themselves via the `REGISTER_DEBUG_COMMAND(fn)` macro (`debug/debug_commands_registry.*`) at the bottom of whichever `.cpp` defines them — there is no central command list; grep for the macro to find them all.

### Web layer

`web_server/web_server.cpp` serves both the dashboard/API (once WiFi is configured) and, via `wifi/wifi_provisioning.cpp`, a captive-portal setup wizard for first-time WiFi configuration. Static assets (`main/littlefs/*`) are packed into a LittleFS image and flashed as a separate `storage` partition (see `littlefs_create_partition_image` in `main/CMakeLists.txt`) — editing `index.html`/`setup.html`/`js/main.js` requires reflashing to take effect, not just a app rebuild (though a normal `flash` does both).

`GET /api/mesh/debug` (`mesh_debug_status_handler`) dumps the mesh stack's internal state as JSON in one shot — SKU, mode, `local_element_addr`/`net_idx`/`app_idx`, group address, whether local keys actually resolve, and the external-nodes table — for diagnosing live without a serial capture.

## Live-hardware debugging notes

- **BLE Mesh stack's own debug logs are compile-time gated**, not just runtime-filtered: `BT_DBG`/`BT_INFO` calls inside the ESP-IDF BLE Mesh component only exist in the binary at all if `CONFIG_BLE_MESH_STACK_TRACE_LEVEL` (Kconfig, default `WARNING`=2) is raised to `DEBUG`=4 at build time — `esp_log_level_set()` at runtime cannot surface them if that Kconfig level wasn't already high enough at compile time. Raise it (in the relevant project's `sdkconfig`, then `idf.py reconfigure`) whenever you need to see transport/segment-level send/recv/bind activity, not just this app's own log lines.
- **A BLE Mesh device replying "successfully" on-device doesn't mean the requester (e.g. nRF Mesh) sees it.** The ack can be lost in transit (low TTL vs. actual hop count, an unstable/absent GATT Proxy connection to the requester) while the device's own log shows the operation completed correctly. Don't trust "Transaction Failed" in a phone app as proof nothing happened on the device — check the device's own log/state first.
- **A stale replay-protection (RPL) entry silently drops legitimate messages with no error to either side** — it shows up as `BLE_MESH: Replay: src 0x.... dst 0x.... seq 0x......` in the receiving device's log. Common after repeated test/reprovision cycles without a full erase; fixed by `erase-flash` + re-provision, not by re-sending.
- **BLE Mesh has no "is this device already provisioned?" query message** — provisioning and normal network traffic are separate bearers/protocols by spec. Don't design a feature assuming one exists.
