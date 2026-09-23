# Multi-Element BLE Mesh Node Support

**Status:** Waiting for data — need distributor composition logs
**GitHub issue:** https://github.com/ludodefgh/esp32-blemesh2mqtt/issues/33
**Opened by:** @rjaegers

---

## Context

The Häfele Connect Mesh ecosystem uses a 6-port distributor as the BLE Mesh node.
Each port drives one physical LED output. Depending on what is connected, ports are
consumed differently:

| Light type     | Ports used | Control |
|----------------|------------|---------|
| Monochrome     | 1 port     | On/Off + Dimming |
| Tunable white  | 2 ports    | On/Off + Dimming + Color temperature |
| RGB            | 3 ports    | On/Off + R/G/B levels |

For Tunable white and RGB, Häfele sells adapters (e.g. [RGB adapter P-01368829](https://www.hafele.com.de/en/product/adapter-haefele-loox5-24-v-4-pin-rgb-/P-01368829/))
that connect to multiple ports of the distributor. The semantic mapping (warm+cool = color
temp, R+G+B = color) is handled by the mobile app — **not** by the BLE Mesh composition data.

The distributor itself uses plain **Generic Level Server** models on each port.
This is a non-standard use of BLE Mesh (standard would be Light CTL Server / Light HSL Server),
but it is consistent with what the product description states:
> *"Adapters are required for Tunable white lights, for RGB lights or for electrically operated fittings."*

**Product reference (monochrome, 6-way):** https://www.hafele.com.de/en/product/6-way-distributor-haefele-connect-mesh-12-v-with-switching-function-2-pin-monochrome-/P-01320237/

---

## Current limitation

The bridge currently assumes **1 BLE Mesh node = 1 Home Assistant entity**.

A 6-port distributor with 6 monochrome lights provisioned would expose:
- 1 BLE Mesh node with 7 elements (1 root + 6 output elements)
- but only 1 HA light entity (only the primary unicast address is used)

---

## Waiting for: distributor composition logs

The logs in issue #33 were captured while provisioning a **remote control** (all models are
`*Client`), not the distributor itself. Client models send commands; Server models receive them.
The distributor would have Server models on its output elements.

### What to ask @rjaegers

Ask them to pair the actual distributor (not the remote) and share the provisioning logs.
Key things to look for:

1. `prov_complete` line — `element num` should be 7 for a 6-port monochrome unit (1 root + 6)
2. `parse_composition_data` lines — output elements should show:
   - `Generic OnOff Server (0x1000)` and/or `Generic Level Server (0x1002)` (Server, not Client)
3. Whether the provisioning is retained after a reboot (the remote in the current logs
   re-advertised as unprovisioned at the end, suggesting it lost provisioning state)

---

## Implementation plan (ready to execute once data confirmed)

### Step 1 — New `bm2mqtt_element_info` struct (`ble_mesh_node.h`)

```cpp
struct bm2mqtt_element_info {
    uint8_t      elem_index;   // unicast = primary_unicast + elem_index
    uint16_t     features;     // per-element feature bitmask
    uint8_t      onoff;
    int16_t      level;
    uint16_t     curr_temp;
    uint16_t     hsl_h, hsl_s, hsl_l;
    color_mode_t color_mode;
};
```

### Step 2 — `bm2mqtt_node_info_v5` (`ble_mesh_node.h`)

- Add `std::vector<bm2mqtt_element_info> elements`
- Add helper: `bool is_multi_element() const { return elements.size() > 1; }`
- NVS serialization: store element count + fixed-size array (cap at MAX_ELEMENTS = 32)

### Step 3 — Composition data parsing (`ble_mesh_control.cpp`)

- Track element index while parsing composition data
- Assign feature flags per element instead of aggregating to node-level only
- After parsing: populate `node->elements` with active output elements
  (active = has at least `FEATURE_GENERIC_ONOFF` or `FEATURE_LIGHT_LIGHTNESS`)
- Keep `node->features` as union of all element features (backward compat)

### Step 4 — MQTT topics (`mqtt_control.cpp`)

Backward-compatible:
- Single-element nodes → existing `node_<MAC>/state` + `node_<MAC>/set` (no change)
- Multi-element nodes → `node_<MAC>/elem_<N>/state` + `node_<MAC>/elem_<N>/set`

### Step 5 — HA Discovery (`mqtt_control.cpp`)

- Single-element → current behavior unchanged
- Multi-element → one HA `light` entity per element:
  - `unique_id`: `node_<MAC>_elem_<N>_light`
  - `name`: `<NodeName> Output <N>`
  - All share the same HA **device** block (keyed by `node_<MAC>`) → one device, N entities
  - Only `brightness` exposed (Generic Level) — no color_temp or hs auto-detected

### Step 6 — State publishing (`mqtt_control.cpp`)

- Multi-element: loop over elements, publish each to its element-level state topic

### Step 7 — Command routing (`mqtt_control.cpp`)

- Parse element index from topic (`/elem_<N>/set`)
- Route to unicast = `node->unicast + elem_index`

### Step 8 — NVS migration v4 → v5 (`ble_mesh_node.cpp`)

- `Converter<5>`: wrap existing flat fields into `elements[0]` with `elem_index = 0`

### Step 9 — Remove node cleanup

- `mqtt_remove_node()`: publish empty string to each element's discovery topic

### Step 10 — Web server (`web_server.cpp`)

- Add `elem_count` and active element list to nodes JSON response

---

## Out of scope (possible future work)

**RGB / Tunable white grouping wrapper**

If there is demand, a future feature could allow the bridge to expose:
- 2 consecutive Generic Level elements → 1 HA `light` with `color_temp` (warm + cool)
- 3 consecutive Generic Level elements → 1 HA `light` with `rgb_color` (R + G + B)

This would require either user-side configuration or Häfele-specific heuristics.
Not needed for initial monochrome support.
