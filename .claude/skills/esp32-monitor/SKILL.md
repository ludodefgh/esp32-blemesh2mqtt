---
name: esp32-monitor
description: Capture live serial log output from the bridge (/dev/ttyUSB0) or the C3 test node (/dev/ttyACM0) — including while the user performs an action on their phone/app — without needing an interactive TTY. Use whenever you need to see boot logs, debug a live issue, or confirm a fix on real hardware.
---

# esp32-monitor

`idf.py monitor` / `idf_monitor.py` **requires a real interactive TTY and will not work when run via the Bash tool** — don't try it. Use a small pyserial script instead; it's what this project has used successfully all along.

## The two ports behave differently

- **`/dev/ttyUSB0` (bridge, CH340 USB-UART bridge):** opening the port does **not** reset the board. You can attach and read its ongoing logs non-destructively — this is the port to use when you need to watch what happens live while the user does something (e.g. "retry the bind now, I'm capturing").
- **`/dev/ttyACM0` (C3, native USB-Serial-JTAG):** **opening the port always resets the board**, no matter what tool does it (pyserial, `cat`, VSCode's monitor, `idf.py monitor`) or what DTR/RTS state you request. There is no known reliable non-resetting read technique for this port. Don't fight it — if you need to watch a *live* action on the C3 without a reset, you can't with serial; fall back to asking the user what they observed, or accept the reset and design around it (e.g. capture from the C3's own fresh boot instead).

## Passive capture (no deliberate reset) — works reliably on ttyUSB0

```python
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
data = b""
start = time.time()
while time.time() - start < 90:   # generous window if waiting on a user action
    chunk = ser.read(4096)
    if chunk:
        data += chunk
ser.close()
with open('<scratchpad>/capture.log', 'wb') as f:
    f.write(data)
print(len(data), "bytes captured")
```

Run this **in the background** (trailing `&` in Bash) when you need to prompt the user to go do something while it captures:
```bash
python3 - <<'EOF' &
...script above...
EOF
echo "capture started, PID $!"
```
Then either tell the user "go do X now, capturing for N seconds" and wait for their reply, or poll for completion:
```bash
while ps -p <PID> > /dev/null 2>&1; do sleep 3; done
```
Always write to a **file**, not stdout — background job stdout is not visible to you. Read the file back with `strings <file>` (serial noise/partial frames can include non-UTF8 bytes) rather than `cat`.

## Deliberate reset + fresh boot capture — needed on ttyACM0, optional on ttyUSB0

```python
import serial, time
ser = serial.Serial('<port>', 115200, timeout=1)
ser.dtr = False
ser.rts = True
time.sleep(0.1)
ser.rts = False
time.sleep(0.3)
data = b""
start = time.time()
while time.time() - start < 15:
    chunk = ser.read(4096)
    if chunk:
        data += chunk
ser.close()
with open('<scratchpad>/boot.log', 'wb') as f:
    f.write(data)
```
Just opening `/dev/ttyACM0` already triggers this reset on its own — the explicit DTR/RTS toggle is mostly useful for `ttyUSB0` (which does *not* reset on open) when you specifically want a clean fresh-boot log there.

## `stty` fallback for ttyUSB0

If you ever fall back to plain `cat /dev/ttyUSB0` instead of pyserial and see garbage, the port's termios/baud state is stale from a previous tool. Fix:
```bash
stty -F /dev/ttyUSB0 115200 raw -echo
```
before `cat`. Prefer the pyserial approach above though — it's more reliable and lets you capture to a file with a real timeout.

## Reading the result

- Verbose BLE Mesh stack tracing (see `BLE_MESH_STACK_TRACE_LEVEL` in CLAUDE.md) can produce a lot of output — `grep`/`strings | grep` for the opcodes, addresses, or tags you care about rather than dumping the whole capture.
- Useful greps for this project: `recv,|send,|bind app key|Replay:|Config .* received|Guru|rst:|Backtrace` (BLE Mesh internals + our own `APP_CONTROL`/`APP_PROV`/`MESH_CFG` log lines + crash markers).
