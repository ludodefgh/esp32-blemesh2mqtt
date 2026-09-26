# Hardware test rig

Firmware and scripts for testing the bridge against real BLE Mesh devices, without a
phone or nRF Mesh in the loop. Each `firmware/*` directory is a standalone ESP-IDF
project for an ESP32-C3 (target set in its `sdkconfig.defaults`); they depend only on
ESP-IDF's `example_init` component, found through `IDF_PATH`.

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

Commands are spaced 5 s apart. Before the external node queue waited for acks and
retried, lost acks (likely WiFi/BLE coexistence on the WROOM) made these tests flaky —
consistently failing runs now point at a real regression.
