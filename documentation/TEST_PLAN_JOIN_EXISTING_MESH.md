# Test plan: Companion edition with nRF Mesh

Goal: validate the Companion edition end-to-end against a real phone provisioner — the
bridge joining an existing mesh as a node, finding its lights (External Mesh Nodes), and
controlling them from the dashboard and Home Assistant — without touching a production mesh.

For repeatable, phone-free testing, use the automated rig instead (`test/README.md` and the
`esp32-test-provisioner` skill): its provisioner stands in for nRF Mesh, and
`test/scripts/external_node_regression.py` checks light control against a test bulb's real
state. This plan covers what the rig can't: the real nRF Mesh flow a user goes through.

## What you need

- The bridge, flashed with the **Companion** edition (`erase-flash` first for a clean start).
- One spare ESP32-C3 as a test light, flashed with `test/firmware/hsl_server` (colour light)
  or `test/firmware/onoff_server` (on/off only).
- A phone with **nRF Mesh**, and an MQTT broker + Home Assistant.

## 1. Set up the bridge

1. Join the bridge's `BleMesh2MQTT-Setup-…` WiFi and enter your WiFi. In step 2 of the
   wizard, only "Join an existing mesh" can be selected. Then set the MQTT broker from the
   dashboard's Bridge page.
2. Open the dashboard: the header reads **BleMesh2MQTT Companion**, the Mesh page shows no
   Address yet and no provisioning sections.

## 2. Build a disposable network in nRF Mesh

1. Create a **new** network (never your real one).
2. Add the test light: provision it, bind the AppKey to its Server models (OnOff, Level,
   Lightness, HSL…), and subscribe those models to a new group, e.g. `0xC000`.
3. Add the bridge the same way (it advertises as an unprovisioned device), then bind the
   AppKey to its **Client** models: Generic OnOff, Generic Level, Light Lightness, Light
   HSL and Light CTL Client.

## 3. Connect the two

1. Dashboard → Mesh → **Group Address Subscriptions**: add `0xC000`.
2. **Mesh Network** card: Address shows what nRF Mesh assigned; NetKey/AppKey match
   nRF Mesh's keys.
3. Within ~15 s (or click **Discover**), the light appears under **External mesh nodes**
   with its features (onoff, lightness, hsl…).

## 4. Validate

- **Dashboard control**: Power, brightness and colour on the light's card change the test
  light, and nRF Mesh shows the same state (proves it's one live network).
- **Group ON / OFF**: switches the light through the group address.
- **Home Assistant**: the light appears as *External Node XXXX* under the bridge device,
  with the right controls; changes from HA reach the light and the state flows back.
- **Brightness vs. colour**: change the colour several times — brightness must not drift.
- **Reboot the bridge**: the light and its HA entity are back right away (restored from
  NVS), and its state refreshes within a minute.
- **Forget**: the card and the HA entity disappear; **Discover** brings them back.
- **Leave mesh network**: the bridge drops its address and all external nodes (HA entities
  removed) and can be added again from nRF Mesh without a reboot.
- **"Reset node" from nRF Mesh**: same result as Leave, triggered from the phone.
- **OTA guard**: uploading a Standalone firmware on the Firmware page is refused.

## Known pitfalls

- A device reset and re-provisioned at an address reused with a fresh sequence number gets
  its messages dropped by the other nodes' replay protection (`Replay:` in their logs).
  nRF Mesh assigns new addresses, so this mostly happens with the test rig; recover by
  erase-flashing the other nodes.
- "Transaction failed" in nRF Mesh doesn't prove nothing happened: check the device's own
  log/state (acks can be lost over the air).
- Throwaway network: delete it from nRF Mesh and erase-flash the boards afterwards.
