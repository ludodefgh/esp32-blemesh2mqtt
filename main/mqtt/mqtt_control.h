#pragma once
#include <string>
#include "mqtt_client.h"
#include "ble_mesh/ble_mesh_node.h"

esp_mqtt_client_handle_t mqtt_get_client();

bool mqtt5_app_start();
void mqtt5_app_stop();
void mqtt5_app_restart();
void RegisterMQTTDebugCommands();
void mqtt_parse_event_data(esp_mqtt_event_handle_t event);
void mqtt_subscribe_all_nodes(esp_mqtt_client_handle_t client);
void mqtt_subscribe_node(esp_mqtt_client_handle_t client, std::shared_ptr<bm2mqtt_node_info> node_info);

// Node topic generation functions
std::string mqtt_get_node_root_topic(std::shared_ptr<bm2mqtt_node_info> node_info);
std::string mqtt_get_node_set_topic(std::shared_ptr<bm2mqtt_node_info> node_info);
std::string mqtt_get_node_state_topic(std::shared_ptr<bm2mqtt_node_info> node_info);
std::string mqtt_get_node_discovery_id(std::shared_ptr<bm2mqtt_node_info> node_info);

// Node communication functions
void mqtt_node_send_status(std::shared_ptr<bm2mqtt_node_info> node_info);
void mqtt_send_discovery(std::shared_ptr<bm2mqtt_node_info> node_info);
void mqtt_remove_node(std::shared_ptr<bm2mqtt_node_info> node_info);
int mqtt_send_status(int argc, char **argv);