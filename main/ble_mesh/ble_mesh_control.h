#pragma once
#include <memory>

#include "esp_err.h"
#include "ble_mesh_node.h"

typedef enum
{
    FEATURE_GENERIC_ONOFF = 1 << 0,
    FEATURE_LIGHT_LIGHTNESS = 1 << 1,
    FEATURE_LIGHT_HSL = 1 << 2,
    FEATURE_LIGHT_CTL = 1 << 3,
} node_supported_features_t;

long map(long x, long in_min, long in_max, long out_min, long out_max);

esp_err_t ble_mesh_init(void);
void ble_mesh_on_composition_received(esp_ble_mesh_cfg_client_cb_param_t *param, std::shared_ptr<bm2mqtt_node_info> node);
void ble_mesh_refresh_all_nodes();
void ble_mesh_refresh_node(const std::shared_ptr<bm2mqtt_node_info>& node_info, const esp_ble_mesh_node_t *node);
void register_blemesh_commands();

// Provisioning control functions
void ble_mesh_set_provisioning_enabled(bool enabled_value);
bool ble_mesh_get_provisioning_enabled(void);

void ble_mesh_set_auto_provisioning_enabled(bool enabled_value);
bool ble_mesh_get_auto_provisioning_enabled(void);

// MQTT republish functions
void ble_mesh_republish_all_nodes_to_mqtt(void);

// Function declarations from main.cpp
esp_err_t bluetooth_init(void);
void ble_mesh_get_dev_uuid(uint8_t *dev_uuid);