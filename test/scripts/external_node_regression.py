#!/usr/bin/env python3
"""Hardware regression tests for External Mesh Node control through the bridge.

Drives the bridge over MQTT exactly like Home Assistant does, and checks the result
against the real state of the test bulb (test/firmware/hsl_server), read from its
serial log ("STATE ..." lines). See test/README.md for the rig setup.

Usage:
    python3 test/scripts/external_node_regression.py --bridge-topic blemesh2mqtt_<mac>
    [--node 0006] [--bulb-port /dev/ttyACM1]

MQTT credentials come from the environment (MQTT_BROKER_HOST, MQTT_BROKER_PORT,
MQTT_USERNAME, MQTT_PASSWORD), or from .env at the repo root if present.
Exit code is the number of failed tests.
"""

import argparse
import json
import os
import re
import sys
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt
import serial

# A Set whose ack is still pending makes the next acked Set to the same node fail with
# "Busy" for up to MSG_TIMEOUT (4 s) — keep commands further apart than that.
COMMAND_SPACING_S = 5.0
STATE_RE = re.compile(r"STATE \((?P<cause>[^)]*)\) onoff=(?P<onoff>\d+) lightness=(?P<lightness>\d+) "
                      r"hsl\(h=(?P<h>\d+) s=(?P<s>\d+) l=(?P<l>\d+)\)")


def load_dotenv():
    env_file = Path(__file__).resolve().parents[2] / ".env"
    if not env_file.exists():
        return
    for line in env_file.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(), value.strip())


class Bulb:
    """Latest real state of the HSL test bulb, parsed from its serial log."""

    def __init__(self, port):
        self._serial = serial.Serial(port, 115200, timeout=0.2)
        self._lock = threading.Lock()
        self._states = []
        self._stop = False
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while not self._stop:
            line = self._serial.readline().decode(errors="replace")
            match = STATE_RE.search(line)
            if match:
                state = {k: (int(v) if v.isdigit() else v) for k, v in match.groupdict().items()}
                with self._lock:
                    self._states.append(state)

    def mark(self):
        with self._lock:
            return len(self._states)

    def latest_since(self, mark):
        with self._lock:
            return self._states[-1] if len(self._states) > mark else None

    def close(self):
        self._stop = True
        self._serial.close()


class HomeAssistant:
    """Publishes commands and records the state messages the bridge publishes."""

    def __init__(self, base_topic):
        self.set_topic = base_topic + "/set"
        self.state_topic = base_topic + "/state"
        self._lock = threading.Lock()
        self._states = []
        self._client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        self._client.username_pw_set(os.environ["MQTT_USERNAME"], os.environ["MQTT_PASSWORD"])
        self._client.on_connect = lambda c, u, f, rc, p=None: c.subscribe(self.state_topic)
        self._client.on_message = self._on_message
        self._client.connect(os.environ["MQTT_BROKER_HOST"], int(os.environ.get("MQTT_BROKER_PORT", 1883)), 30)
        self._client.loop_start()
        time.sleep(1)

    def _on_message(self, client, userdata, msg):
        with self._lock:
            self._states.append(json.loads(msg.payload.decode()))

    def mark(self):
        with self._lock:
            return len(self._states)

    def latest_since(self, mark):
        with self._lock:
            return self._states[-1] if len(self._states) > mark else None

    def send(self, payload):
        self._client.publish(self.set_topic, json.dumps(payload))

    def close(self):
        self._client.loop_stop()


class Rig:
    def __init__(self, bulb, ha):
        self.bulb = bulb
        self.ha = ha

    def command(self, payload):
        """Sends a command; returns (bulb state, HA state) observed afterwards (or None)."""
        bulb_mark, ha_mark = self.bulb.mark(), self.ha.mark()
        self.ha.send(payload)
        time.sleep(COMMAND_SPACING_S)
        return self.bulb.latest_since(bulb_mark), self.ha.latest_since(ha_mark)


class TestFailure(Exception):
    pass


def check(condition, message):
    if not condition:
        raise TestFailure(message)


def test_onoff_round_trip(rig):
    # Start from ON: the bulb may have just rebooted (opening its port can reset it) and
    # boots OFF, and a Set to the current value logs no state change.
    rig.command({"state": "ON"})
    bulb, ha = rig.command({"state": "OFF"})
    check(bulb and bulb["onoff"] == 0, f"bulb not off after OFF: {bulb}")
    check(ha and ha.get("state") == "OFF", f"HA not told OFF: {ha}")
    bulb, ha = rig.command({"state": "ON"})
    check(bulb and bulb["onoff"] == 1 and bulb["lightness"] > 0, f"bulb not on after ON: {bulb}")
    check(ha and ha.get("state") == "ON", f"HA not told ON (a lit light must not be reported OFF): {ha}")


def test_color_on_off_light_turns_it_on(rig):
    rig.command({"state": "OFF"})
    bulb, _ = rig.command({"state": "ON", "color": {"h": 120, "s": 100}})
    check(bulb and bulb["onoff"] == 1 and bulb["lightness"] > 0,
          f"picking a colour on an off light left it off: {bulb}")


def test_brightness_stable_across_color_changes(rig):
    bulb, ha = rig.command({"state": "ON", "brightness": 40000})
    check(bulb is not None, "bulb didn't react to a brightness command")
    reference = bulb["lightness"]
    for hue in (240, 0, 60):
        bulb, ha = rig.command({"color": {"h": hue, "s": 100}})
        check(bulb and abs(bulb["lightness"] - reference) <= 1,
              f"brightness drifted after colour h={hue}: bulb lightness {bulb and bulb['lightness']} vs {reference}")
        check(ha and abs(ha.get("brightness", -1) - 40000) <= 2,
              f"HA brightness drifted after colour h={hue}: {ha}")


def test_ha_reports_picked_color(rig):
    _, ha = rig.command({"state": "ON", "color": {"h": 240, "s": 100}})
    check(ha and ha.get("color_mode") == "hs", f"HA not in hs mode: {ha}")
    color = (ha or {}).get("color", {})
    check(abs(color.get("h", -99) - 240) <= 1 and abs(color.get("s", -99) - 100) <= 1,
          f"HA colour doesn't match what was set: {ha}")


TESTS = [
    test_onoff_round_trip,
    test_color_on_off_light_turns_it_on,
    test_brightness_stable_across_color_changes,
    test_ha_reports_picked_color,
]


def main():
    load_dotenv()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bridge-topic", default=os.environ.get("BRIDGE_MQTT_BASE"),
                        help="bridge MQTT base topic, e.g. blemesh2mqtt_8813bf8222a8 (or BRIDGE_MQTT_BASE)")
    parser.add_argument("--node", default="0006", help="external node unicast address, hex (default 0006)")
    parser.add_argument("--bulb-port", default="/dev/ttyACM1", help="serial port of the HSL test bulb")
    args = parser.parse_args()
    if not args.bridge_topic:
        parser.error("--bridge-topic (or BRIDGE_MQTT_BASE) is required")

    bulb = Bulb(args.bulb_port)
    ha = HomeAssistant(f"{args.bridge_topic}/ext_{args.node.upper()}")
    time.sleep(3)  # opening the C3's port may reset it — let it boot
    rig = Rig(bulb, ha)

    failures = 0
    for test in TESTS:
        try:
            test(rig)
            print(f"PASS  {test.__name__}")
        except TestFailure as err:
            failures += 1
            print(f"FAIL  {test.__name__}: {err}")

    ha.close()
    bulb.close()
    print(f"\n{len(TESTS) - failures}/{len(TESTS)} passed")
    sys.exit(failures)


if __name__ == "__main__":
    main()
