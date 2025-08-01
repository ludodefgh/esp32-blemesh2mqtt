#pragma once
#include <memory>
#include <string>
#include "cJSON.h"
#include "mqtt_client.h"

using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

// Bridge identifier and topic functions
std::string get_bridge_mac_identifier();
std::string get_bridge_base_topic();
const char* get_bridge_availability_topic();
const char* get_bridge_state_topic();
const char* get_bridge_provisioning_set_topic();
const char* get_bridge_provisioning_state_topic();
const char* get_bridge_restart_set_topic();

CJsonPtr create_provisioning_json();
CJsonPtr create_restart_json();
CJsonPtr create_uptime_json();
CJsonPtr create_bridge_info_json(const char *version);

void mqtt_publish_provisioning_enabled(bool enable_provisioning);
void send_bridge_discovery();
void publish_bridge_info(const char *version);
void start_periodic_publish_timer();
void mqtt_bridge_subscribe(esp_mqtt_client_handle_t client);