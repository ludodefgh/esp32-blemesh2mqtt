# Hardware test rig

Firmware and scripts for testing the bridge against real BLE Mesh devices, without a
phone or nRF Mesh in the loop. Each `firmware/*` directory is a standalone ESP-IDF
project for an ESP32-C3 (target set in its `sdkconfig.defaults`); they depend only on
ESP-IDF's `example_init` component, found through `IDF_PATH`. `hsl_server` also builds
for an ESP32-S3 (`sdkconfig.defaults.esp32s3`), in its own build dir:
`idf.py -B build_s3 -D IDF_TARGET=esp32s3 -D SDKCONFIG=build_s3/sdkconfig -p <port> flash`.

| Firmware | Role |
|---|---|
| `firmware/provisioner` | Stands in for nRF Mesh: auto-provisions any unprovisioned device, adds AppKey `0x12…12`, binds every Client/Server model either device type can have, and subscribes the light Server models (OnOff, Level, Lightness, HSL, CTL) to group `0xC000`. Based on Espressif's `provisioner` example. |
| `firmware/hsl_server` | HSL test bulb: Generic OnOff + Level, Light Lightness (+Setup), Light HSL (+Setup) Servers on one element, with the state bindings of a real bulb (OnOff ⇔ Lightness ⇔ HSL lightness). Logs a `STATE (...) onoff=… lightness=… hsl(h=… s=… l=…)` line on every change, which the scripts read as ground truth. |
| `firmware/onoff_server` | Plain on/off node (Espressif's `onoff_server` example) — the simplest External Mesh Node. |

Build and flash like any ESP-IDF project, e.g.
`cd test/firmware/hsl_server && idf.py -p /dev/ttyACM1 erase-flash flash`.
See the `esp32-test-provisioner` skill for the full bring-up sequence (bridge in
join-existing mode, Node SKU with `CONFIG_BM2MQTT_DEBUG_TOOLS=y`, group `0xC000`) and
its gotchas — notably, don't open the provisioner's serial port between provisioning
two devices.

## Regression tests

`scripts/external_node_regression.py` drives the bridge over MQTT exactly like Home
Assistant and checks the HSL test bulb's real state on its serial port:

```bash
python3 test/scripts/external_node_regression.py --bridge-topic blemesh2mqtt_<bridge-mac> \
    [--node 0006] [--bulb-port /dev/ttyACM1]
```

MQTT credentials come from the environment or the repo's `.env`
(`MQTT_BROKER_HOST/PORT/USERNAME/PASSWORD`). Exit code = number of failed tests.

Each command waits until neither the bulb nor HA has changed for 3 s (15 s max, longer than one ack retry), since
a lost ack is retried by the bridge and can legitimately take a few seconds.

`scripts/standalone_node_regression.py` does the same for nodes the bridge provisioned
itself (Standalone edition, no test provisioner): flash `hsl_server` on one or more
boards, provision them from the dashboard, then pass each node's MAC (from its
`<bridge topic>/node_<mac>` topic) and bulb port:

```bash
python3 test/scripts/standalone_node_regression.py --bridge-topic blemesh2mqtt_<bridge-mac> \
    --node <mac>:/dev/ttyACM0 --node <mac>:/dev/ttyACM1 [--bridge-url http://<bridge-ip>] \
    [--retained-status]
```

It covers on/off, "ON + brightness" sending only the light Set, a quick ON→OFF ending
OFF, and a burst on all nodes at once (a command every 0.5 s each) ending on the last
command without a bridge reboot. A command is done once HA got its status, which the
bridge publishes after the node's ack. `--retained-status` also checks that a retained
`homeassistant/status` doesn't make the bridge republish in a loop: it leaves one on the
broker for ~3 min and restarts the bridge, so every bridge on that broker sees it.
