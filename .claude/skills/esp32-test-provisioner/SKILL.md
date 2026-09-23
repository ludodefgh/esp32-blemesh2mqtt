---
name: esp32-test-provisioner
description: Set up and use the fully-automated 3-device test rig for the bridge's join-existing-mesh mode — a custom ESP32-C3 test-harness Provisioner that auto-provisions and configures the bridge (and an External Mesh Node target) without touching nRF Mesh or any phone. Use whenever testing join-existing-mesh, External Mesh Nodes discovery, or anything that would otherwise require manually provisioning devices from a phone app.
---

# esp32-test-provisioner

Manually provisioning via nRF Mesh works but is slow to repeat and requires a phone in the loop for every test cycle. This rig replaces that with a second ESP32 board running a small custom Provisioner firmware that auto-provisions and configures anything unprovisioned it sees — fully scriptable from the devcontainer.

## The three devices

| Project | Port (varies) | Role |
|---|---|---|
| `/workspaces/ESPIDFSengledB11N1EProto` (this repo) | `/dev/ttyUSB0` | the bridge under test (join-existing-mesh / Node-only SKU) |
| `/workspaces/provisioner_test` | `/dev/ttyACM0` | the test-harness Provisioner (this skill) |
| `/workspaces/onoff_server_test` | `/dev/ttyACM1` | optional: an "External Mesh Node" target (stock `onoff_server` example) |

`provisioner_test` is a copy of Espressif's stock `esp_ble_mesh/provisioner` example (`/opt/esp/idf/examples/bluetooth/esp_ble_mesh/provisioner`) with two changes from stock, both in `main/main.c`:

1. **No `esp_ble_mesh_provisioner_set_dev_uuid_match()` call.** The stock example filters to devices whose UUID starts with `{0xdd, 0xdd}` — silently ignoring everything else, including our bridge. Removed entirely; this rig provisions whatever it sees.
2. **Binds every model either device type could have**, not just one hardcoded target. `CLIENT_MODELS_TO_BIND[]` lists the bridge's 5 Client models (OnOff/Level/Lightness/HSL/CTL) *and* `ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV` (for a generic Server test node like `onoff_server`). A bind attempt against a model ID absent on that element just gets a graceful `STATUS_INVALID_MODEL` — logged, then the loop moves on to the next model regardless of that status (the code doesn't check it). After every model in the list has been attempted, it also sends **Config Model Subscription Add** for `ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV` to `TEST_GROUP_ADDR` (`0xC000`, matching this project's own group-address convention) — needed for a Server-model test node to actually respond to a group Get, since AppKey binding alone doesn't subscribe it to anything.

3. **`CONFIG_BLE_MESH_SETTINGS=y`** (in both `sdkconfig` and `sdkconfig.defaults`, unlike the stock example). Without it, the provisioner forgets its own NetKey and node/address table on every reboot — and since `/dev/ttyACM0`-style ports reset on every serial open (see `esp32-monitor`), *checking its log* would otherwise silently reset it. Even with persistence on, still avoid it — see Gotchas.

Rebuild after editing: `cd /workspaces/provisioner_test && source /opt/esp/idf/export.sh && idf.py build`. It targets `esp32c3` (`idf.py set-target esp32c3` if a fresh checkout defaults elsewhere).

## Running a full test cycle from scratch

1. **Erase and reflash the bridge** (see `esp32-flash`): `idf.py -p /dev/ttyUSB0 erase-flash` then `flash`. This wipes WiFi credentials too, so the bridge boots into captive-portal mode — but there's no network path from this devcontainer to that temporary AP, so the wizard can't be driven over HTTP.
2. **Configure WiFi + mesh mode over serial instead**, via the `wifi_set` debug console command (`main/wifi/wifi_commands.cpp`) added for exactly this: connects over UART and sends one line, no phone/browser needed:
   ```python
   import serial, time
   ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
   time.sleep(4)  # let boot finish — the console isn't ready immediately after reset
   ser.write(b'wifi_set "<ssid>" "<password>"\n')
   ```
   This saves WiFi credentials, sets mesh mode to `MESH_MODE_JOIN_EXISTING`, and restarts. Get credentials from `.env` at the repo root if present (gitignored) rather than asking the user to retype them each time.
3. **Flash `provisioner_test` once, then leave its port alone for the rest of the cycle.** It auto-provisions anything unprovisioned within seconds. Verify success from the *other* devices' own state (bridge's `/api/mesh/debug`, or the target's serial log at the very end) — not by opening the provisioner's own port mid-session (see Gotchas: doing so is exactly what causes the address-collision failure mode below).
4. **Set the bridge's group address** (the erase wiped it too): `curl -X POST http://<bridge-ip>/api/mesh/settings -d '{"group_addr":"0xC000"}'` — this both persists it and calls `ble_mesh_subscribe_group_addr()` immediately, no reboot needed. Get the bridge's IP from its own boot log (`wifi_provisioning: Got IP address: ...`) or `wifi_status` over serial.
5. **Confirm** via `GET http://<bridge-ip>/api/mesh/debug`: `local_keys_available: true`, `net_idx`/`app_idx` both `0x0000`, `local_element_addr` non-zero.
6. **Optional — add an External Mesh Node target**: flash `onoff_server_test` (see `esp32-flash`) on a third board and power it on. The same running `provisioner_test` will auto-provision and configure it too (its OnOff Server model gets bound and subscribed to `0xC000` by the same logic above). Trigger discovery from the bridge: `POST /api/mesh/external/discover`, then check `GET /api/mesh/external/nodes` or `/api/mesh/debug`'s `external_nodes` array.

## Gotchas specific to this rig

- **Don't open the provisioner's serial port (`esp32-monitor` or otherwise) between provisioning two different devices in the same session — even with `CONFIG_BLE_MESH_SETTINGS=y`.** `/dev/ttyACM0`-style ports reset on every open; if that reset lands before the just-generated NetKey/node-table update has actually flushed to flash, the provisioner comes back with a *different* NetKey and no memory of the address it already handed out — so the next device it provisions collides on the same address (confirmed: bridge and target both ended up at `0x0005` with different NetKeys this way). If you must check its log mid-session, treat the whole cycle as compromised and start over: erase+reflash the provisioner *and* every device it already provisioned, then redo the cycle without touching its port again until the end.
- If a device was provisioned once already (bridge or `onoff_server_test`) and stops responding to a *new* provisioning attempt, it's not unprovisioned anymore — a provisioned node never re-advertises the unprovisioned beacon. Use `erase-flash` on that device to reset it, not just a reflash.
- New USB devices (a freshly plugged second/third board) don't appear in the devcontainer until it's fully restarted — this is a container/passthrough limitation, not something fixable from inside the session.
