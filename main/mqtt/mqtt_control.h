#pragma once
#include <string>
#include "mqtt_client.h"
#include "ble_mesh/ble_mesh_node.h"

esp_mqtt_client_handle_t get_mqtt_client();

void mqtt5_app_start();
void RegisterMQTTDebugCommands();
void parse_mqtt_event_data(esp_mqtt_event_handle_t event);

// Node topic generation functions
std::string get_node_root_topic(const bm2mqtt_node_info *node_info);
std::string get_node_set_topic(const bm2mqtt_node_info *node_info);
std::string get_node_state_topic(const bm2mqtt_node_info *node_info);
std::string get_node_discovery_id(const bm2mqtt_node_info *node_info);

// Node communication functions
void mqtt_node_send_status(const bm2mqtt_node_info *node_info);
void mqtt_send_discovery(const bm2mqtt_node_info *node_info);
int mqtt_send_status(int argc, char **argv);