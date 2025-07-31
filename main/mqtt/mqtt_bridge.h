#pragma once
#include <memory>
#include <string>
#include "cJSON.h"
#include "mqtt_client.h"

using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

std::string get_bridge_mac_identifier();

CJsonPtr create_provisioning_json();
CJsonPtr create_uptime_json();
CJsonPtr create_bridge_info_json();

void mqtt_publish_provisioning_enabled(bool enable_provisioning);
void send_bridge_discovery();
void publish_bridge_info(int devices_provisioned, const char *version);
void start_periodic_publish_timer();
void mqtt_bridge_subscribe(esp_mqtt_client_handle_t client);