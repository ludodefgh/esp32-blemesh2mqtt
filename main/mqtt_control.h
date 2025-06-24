#pragma once
#include "mqtt_client.h"
#include "ble_mesh_node.h"

void mqtt5_app_start();
void RegisterMQTTDebugCommands();
void parse_mqtt_event_data(esp_mqtt_event_handle_t event);

void send_status(const bm2mqtt_node_info *node_info);
int send_status(int argc, char **argv);