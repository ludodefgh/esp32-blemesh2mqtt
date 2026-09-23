# Test plan: join-existing-mesh / External Mesh Nodes

Goal: validate the "join an existing mesh" flow (NetKey/AppKey join, group
subscription, node discovery, individual + group commands) end-to-end,
without touching the real production mesh. Uses a disposable test network
created in nRF Mesh plus one spare ESP32.

Related: GitHub issue #40 (known problems on `feature/join-existing-mesh`,
commit 63d5d42) — not blocking this test, but keep in mind while probing
stability (see item 3, the message-queue concern).

## Hardware / software needed

- The bridge under test (this project, already flashed with the
  join-existing-mesh + External Mesh Nodes feature).
- One spare ESP32 to act as a fake "existing mesh" light.
- A phone with the **nRF Mesh** app installed.

## 1. Flash the fake test node

Build and flash ESP-IDF's `onoff_server` example onto the spare ESP32:

```
/opt/esp/idf/examples/bluetooth/esp_ble_mesh/onoff_models/onoff_server
```

This is a minimal BLE Mesh node with a Generic OnOff Server model — it
toggles the board's LED and is provisionable by any standard provisioner
app (nRF Mesh included).

## 2. Create a disposable test network in nRF Mesh

- Create a **new** network in nRF Mesh — do not reuse the real one.
- Note: nRF Mesh shows the NetKey and AppKey values directly (tap to
  reveal hex) under the network's Key settings — no NVS extraction needed
  this time.

## 3. Provision the fake node

- Scan for unprovisioned devices in nRF Mesh, provision the onoff_server
  board onto the new test network.
- Bind its Generic OnOff Server model to the network's AppKey.

## 4. Create a group and subscribe the fake node to it

- In nRF Mesh, create a Group (e.g. address `0xC000`).
- On the provisioned node's Generic OnOff Server model, use "Subscribe"
  to subscribe it to that group.

(This project's bridge currently has no UI to subscribe a *provisioned
node's* model to a group — only its own local client models — so this
step has to happen from nRF Mesh for now.)

## 5. Join the test mesh from the bridge

- Trigger the bridge's captive-portal setup (Reset WiFi button, or first
  boot) → Step 2 "Join an existing mesh".
- Paste the NetKey and AppKey copied from nRF Mesh.
- Set **Group Address Subscription** to the same group address used in
  step 4 (e.g. `0xC000`).
- Save & restart.

## 6. Validate

- **Mesh Keys panel** (Bridge section): confirm NetKey/AppKey shown match
  what nRF Mesh has for the test network.
- **Discover**: click Discover under External Mesh Nodes → the fake node
  should appear with its unicast address.
- **Individual command**: On/Off buttons for the discovered node → LED on
  the spare ESP32 should respond; nRF Mesh should reflect the same state
  (confirms it's genuinely the same live network, not two isolated ones).
- **Group command**: "Turn Group ON/OFF" → same check via the group
  address; the panel re-discovers automatically afterward since group
  Set is unacknowledged (no per-node ack to update the cache with).

## Notes / open risk while testing

- Per issue #40 item 3: `external_nodes` discovery/commands do **not**
  currently go through this project's serialized message queue. The BLE
  Mesh stack has previously been unstable under concurrent message
  sends — avoid firing Discover and several individual commands back to
  back until that's fixed, and watch the serial log for stack errors if
  testing more than one fake node.
- This is a throwaway network — safe to delete from nRF Mesh and re-flash
  the spare ESP32 afterward.
