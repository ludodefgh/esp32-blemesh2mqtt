#!/usr/bin/env python3
"""Hardware regression tests for nodes provisioned by the bridge (Standalone edition).

Covers issue #50: lost/ghost commands under a multi-light HA burst, "ON + brightness"
sending a single light Set, and (opt-in) a retained homeassistant/status not causing a
republish loop. Each test bulb (test/firmware/hsl_server) must be provisioned by the
bridge itself; its real state is read from its serial log ("STATE ..." lines).

Usage:
    python3 test/scripts/standalone_node_regression.py --bridge-topic blemesh2mqtt_<mac>
        --node <node mac>:/dev/ttyACM0 --node <node mac>:/dev/ttyACM1
        [--bridge-url http://<bridge ip>] [--retained-status]

<node mac> is the one in the node's topic (<bridge topic>/node_<mac>/set).
--retained-status publishes a retained homeassistant/status on the broker for ~3 min
(cleared afterwards) and restarts the bridge: every bridge on that broker sees it.
MQTT credentials come from the environment or .env (see external_node_regression.py).
Exit code is the number of failed tests.
"""

import argparse
import json
import os
import sys
import time
import urllib.request

import paho.mqtt.client as mqtt

from external_node_regression import Bulb, HomeAssistant, Rig, TestFailure, check, load_dotenv

HSL_HALF = 32767  # hs mode: the bridge sends HSL lightness = brightness * (max/2) / max
BURST_S = 20
BURST_PERIOD_S = 0.5
# A lost ack is retried only after the stack's ~4 s client timeout, so a command is done
# once HA got its status (published after the ack), not after a fixed quiet time.
COMMAND_TIMEOUT_S = 30.0
QUIET_S = 1.5


def wait_settled(rigs, settle_s=6.0, max_s=60.0):
    """Waits until no bulb nor HA state has changed for settle_s."""
    start = last_change = time.monotonic()
    seen = None
    while time.monotonic() - start < max_s:
        now_seen = tuple((rig.bulb.count(), rig.ha.count()) for rig in rigs.values())
        if now_seen != seen:
            seen, last_change = now_seen, time.monotonic()
        elif time.monotonic() - last_change >= settle_s:
            return
        time.sleep(0.25)


class AckedRig(Rig):
    def command(self, payload):
        """Sends a command and waits for HA's status for it, then for the bulb to go quiet."""
        bulb_mark, ha_mark = self.bulb.mark(), self.ha.mark()
        self.ha.send(payload)
        deadline = time.monotonic() + COMMAND_TIMEOUT_S
        while self.ha.count() == ha_mark and time.monotonic() < deadline:
            time.sleep(0.1)
        wait_settled({"rig": self}, QUIET_S, max(0.0, deadline - time.monotonic()))
        return self.bulb.latest_since(bulb_mark), self.ha.latest_since(ha_mark)


def expected_bulb_lightness(brightness):
    return brightness * HSL_HALF // 65535


def bridge_uptime(url):
    if not url:
        return None
    with urllib.request.urlopen(f"{url}/api/system_info", timeout=5) as resp:
        return json.load(resp)["uptime"]


def test_onoff_round_trip(rigs, args):
    for name, rig in rigs.items():
        rig.command({"state": "ON"})
        bulb, ha = rig.command({"state": "OFF"})
        check(bulb and bulb["onoff"] == 0, f"{name}: bulb not off after OFF: {bulb}")
        check(ha and ha.get("state") == "OFF", f"{name}: HA not told OFF: {ha}")
        bulb, ha = rig.command({"state": "ON"})
        check(bulb and bulb["onoff"] == 1 and bulb["lightness"] > 0, f"{name}: bulb not on after ON: {bulb}")
        check(ha and ha.get("state") == "ON", f"{name}: HA not told ON: {ha}")


def test_on_brightness_sends_only_light_set(rigs, args):
    for name, rig in rigs.items():
        rig.command({"state": "OFF"})
        mark = rig.bulb.mark()
        bulb, ha = rig.command({"state": "ON", "brightness": 30000})
        causes = [state["cause"] for state in rig.bulb.states_since(mark)]
        check(bulb and bulb["onoff"] == 1, f"{name}: ON+brightness left the bulb off: {bulb}")
        check(abs(bulb["lightness"] - expected_bulb_lightness(30000)) <= 2,
              f"{name}: wrong lightness after ON+brightness: {bulb}")
        check("onoff set" not in causes, f"{name}: an OnOff Set was still sent (bulb causes: {causes})")
        check(ha and ha.get("state") == "ON", f"{name}: HA not told ON: {ha}")


def test_quick_on_off_ends_off(rigs, args):
    # Ghost ON: an ON immediately followed by OFF must end OFF, on bulb and in HA.
    for rig in rigs.values():
        rig.command({"state": "OFF"})
    for _ in range(3):
        for rig in rigs.values():
            rig.ha.send({"state": "ON", "brightness": 50000})
        time.sleep(0.1)
        for rig in rigs.values():
            rig.ha.send({"state": "OFF"})
        time.sleep(0.3)
    wait_settled(rigs)
    for name, rig in rigs.items():
        bulb = rig.bulb.latest_since(0)
        ha = rig.ha.latest_since(0)
        check(bulb and bulb["onoff"] == 0, f"{name}: bulb ended ON after ON->OFF bursts: {bulb}")
        check(ha and ha.get("state") == "OFF", f"{name}: HA ended {ha} after ON->OFF bursts")


def test_multi_light_burst(rigs, args):
    # The #50 reporter's load (two lights in an HA group, a command every 0.5 s each), on every --node.
    uptime_before = bridge_uptime(args.bridge_url)
    end = time.monotonic() + BURST_S
    step = 0
    while time.monotonic() < end:
        brightness = 5000 + (step * 7919) % 60000
        for rig in rigs.values():
            rig.ha.send({"state": "ON", "brightness": brightness})
        step += 1
        time.sleep(BURST_PERIOD_S)
    final = {name: 20000 + 10000 * index for index, name in enumerate(rigs)}
    for name, rig in rigs.items():
        rig.ha.send({"state": "ON", "brightness": final[name]})
    wait_settled(rigs)
    for name, rig in rigs.items():
        bulb = rig.bulb.latest_since(0)
        ha = rig.ha.latest_since(0)
        check(bulb and bulb["onoff"] == 1 and abs(bulb["lightness"] - expected_bulb_lightness(final[name])) <= 2,
              f"{name}: bulb didn't end at the last command ({final[name]}): {bulb}")
        check(ha and ha.get("state") == "ON" and abs(ha.get("brightness", -1) - final[name]) <= 2,
              f"{name}: HA didn't end at the last command ({final[name]}): {ha}")
    if uptime_before is not None:
        uptime_after = bridge_uptime(args.bridge_url)
        check(uptime_after > uptime_before, f"bridge rebooted during the burst (uptime {uptime_before} -> {uptime_after})")


def test_retained_ha_status_no_loop(rigs, args):
    if not args.retained_status:
        return "needs --retained-status"
    bridge_topic = args.bridge_topic
    discovery = []
    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(os.environ["MQTT_USERNAME"], os.environ["MQTT_PASSWORD"])

    def on_message(_client, _userdata, msg):
        if msg.topic.startswith("homeassistant/light/") and bridge_topic.encode() in msg.payload:
            discovery.append(time.monotonic())

    client.on_message = on_message
    client.on_connect = lambda c, u, f, rc, p=None: c.subscribe("homeassistant/light/#")
    client.connect(os.environ["MQTT_BROKER_HOST"], int(os.environ.get("MQTT_BROKER_PORT", 1883)), 30)
    client.loop_start()
    try:
        client.publish("homeassistant/status", "online", retain=True).wait_for_publish()
        time.sleep(1)
        discovery.clear()
        client.publish(f"{bridge_topic}/bridge/restart/set", "RESTART").wait_for_publish()
        time.sleep(150)  # the old loop republished every 60 s
    finally:
        client.publish("homeassistant/status", b"", retain=True).wait_for_publish()
        client.loop_stop()
    check(discovery, "no node discovery after the bridge reconnected")
    late = [round(t - discovery[0]) for t in discovery if t - discovery[0] > 30]
    check(not late, f"node discovery republished again {late} s after connect: retained status still loops")


TESTS = [
    test_onoff_round_trip,
    test_on_brightness_sends_only_light_set,
    test_quick_on_off_ends_off,
    test_multi_light_burst,
    test_retained_ha_status_no_loop,
]


def main():
    load_dotenv()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bridge-topic", default=os.environ.get("BRIDGE_MQTT_BASE"),
                        help="bridge MQTT base topic, e.g. blemesh2mqtt_8813bf8222a8 (or BRIDGE_MQTT_BASE)")
    parser.add_argument("--node", action="append", required=True, metavar="MAC:PORT",
                        help="node MAC from its topic and its bulb's serial port; repeat per node")
    parser.add_argument("--bridge-url", help="bridge web UI, e.g. http://192.168.2.97 (checks for reboots)")
    parser.add_argument("--retained-status", action="store_true",
                        help="also run the retained homeassistant/status test (touches the shared broker)")
    args = parser.parse_args()
    if not args.bridge_topic:
        parser.error("--bridge-topic (or BRIDGE_MQTT_BASE) is required")

    rigs = {}
    for spec in args.node:
        mac, port = spec.split(":", 1)
        rigs[mac.lower()] = AckedRig(Bulb(port), HomeAssistant(f"{args.bridge_topic}/node_{mac.lower()}"))
    time.sleep(3)  # opening a C3's port may reset it — let it boot

    failures = 0
    for test in TESTS:
        try:
            skipped = test(rigs, args)
            print(f"SKIP  {test.__name__}: {skipped}" if skipped else f"PASS  {test.__name__}")
        except TestFailure as err:
            failures += 1
            print(f"FAIL  {test.__name__}: {err}")

    for rig in rigs.values():
        rig.ha.close()
        rig.bulb.close()
    print(f"\n{len(TESTS) - failures}/{len(TESTS)} passed")
    sys.exit(failures)


if __name__ == "__main__":
    main()
