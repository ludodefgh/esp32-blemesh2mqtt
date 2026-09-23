#pragma once
#include <cstdint>
#include <string>

#include "mqtt_client.h"

// MQTT <-> Home Assistant bridging for External Mesh Nodes (ble_mesh/ble_mesh_control.h)
// — devices on a joined mesh this bridge never provisioned. Parallels mqtt_control.h's
// node_manager()-based flow, but keyed by unicast address instead of a DevKey/UUID,
// since external nodes have neither.

void mqtt_subscribe_all_external_nodes(esp_mqtt_client_handle_t client);
void mqtt_republish_all_external_nodes(void);

// Called by ble_mesh_control.cpp whenever a discovery probe reply updates an external
// node's state. Publishes an updated status and, on first sighting, also subscribes the
// node and sends HA discovery.
void mqtt_notify_external_node_changed(uint16_t addr, bool is_new);

// Dispatches an incoming MQTT command whose topic contains "ext_". Returns false if the
// topic didn't match a known external node (nothing to do).
bool mqtt_handle_external_node_data(const std::string &topic, const char *data, int data_len);
