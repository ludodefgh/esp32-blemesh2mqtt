#pragma once
#include <cstdint>
#include <string>

#include "cJSON.h"
#include "mqtt_client.h"

// MQTT <-> Home Assistant bridging for External Mesh Nodes (ble_mesh/ble_mesh_control.h)
// — devices on a joined mesh this bridge never provisioned. Parallels mqtt_control.h's
// node_manager()-based flow, but keyed by unicast address instead of a DevKey/UUID,
// since external nodes have neither.

void mqtt_republish_all_external_nodes(void);
// Removes a forgotten node's HA entity and its command subscriptions.
struct external_mesh_node_t;
void mqtt_forget_external_node(const external_mesh_node_t &node);

// Called by ble_mesh_control.cpp whenever a discovery probe reply updates an external
// node's state. Publishes an updated status and, on first sighting, also subscribes the
// node and sends HA discovery.
void mqtt_notify_external_node_changed(uint16_t addr, bool is_new);

// Dispatches an incoming MQTT command whose topic contains "ext_". Returns false if the
// topic didn't match a known external node (nothing to do).
bool mqtt_handle_external_node_data(const std::string &topic, const char *data, int data_len);

// Applies a Home Assistant JSON light command ({"state","brightness","color":{h,s},
// "color_temp"}) to an external node — the MQTT path and the dashboard's HTTP API share
// it, so both get the same range mapping and off-light handling. False if unknown node.
bool external_node_apply_ha_command(uint16_t addr, const cJSON *payload);
