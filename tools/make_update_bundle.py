#!/usr/bin/env python3
"""Pack the app image and the LittleFS storage image into a single OTA bundle.

The bundle is uploaded once through the dashboard's "Firmware + Web Interface"
option; the bridge writes the firmware first (switching the boot partition) then
the storage image, and restarts once.

Layout (must match ota_bundle_header_t in main/web_server/web_server.cpp):

    magic     4 bytes  b"B2MU"
    version   1 byte   = 1
    reserved  3 bytes  = 0
    app_size  uint32 little-endian
    fs_size   uint32 little-endian
    <app image>
    <storage image>

Usage:
    # from the project root, after `idf.py build`
    python tools/make_update_bundle.py
    # or explicitly
    python tools/make_update_bundle.py build/BleMesh2Mqtt.bin build/storage.bin build/update_bundle.bin
"""

import json
import struct
import sys
from pathlib import Path

MAGIC = b"B2MU"
VERSION = 1


def _default_paths():
    root = Path(__file__).resolve().parent.parent
    build = root / "build"
    app = None
    desc = build / "project_description.json"
    if desc.is_file():
        try:
            app = build / json.loads(desc.read_text())["app_bin"]
        except (KeyError, ValueError):
            app = None
    if app is None or not app.is_file():
        bins = [p for p in build.glob("*.bin")
                if p.name not in ("storage.bin", "update_bundle.bin", "ota_data_initial.bin")]
        app = bins[0] if len(bins) == 1 else None
    return app, build / "storage.bin", build / "update_bundle.bin"


def main(argv):
    if len(argv) == 1:
        app_path, fs_path, out_path = _default_paths()
    elif len(argv) == 4:
        app_path, fs_path, out_path = (Path(argv[1]), Path(argv[2]), Path(argv[3]))
    else:
        print(__doc__)
        return 2

    if not app_path or not app_path.is_file():
        print(f"error: firmware image not found: {app_path}", file=sys.stderr)
        return 1
    if not fs_path.is_file():
        print(f"error: storage image not found: {fs_path}", file=sys.stderr)
        return 1

    app = app_path.read_bytes()
    fs = fs_path.read_bytes()

    header = MAGIC + struct.pack("<B3xII", VERSION, len(app), len(fs))
    assert len(header) == 16

    out_path.write_bytes(header + app + fs)
    print(f"wrote {out_path}  ({len(header) + len(app) + len(fs):,} bytes)")
    print(f"  firmware {app_path.name}: {len(app):,} bytes")
    print(f"  storage  {fs_path.name}: {len(fs):,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
