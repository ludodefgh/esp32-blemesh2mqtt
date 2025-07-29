#pragma once
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
void on_composition_received(esp_ble_mesh_cfg_client_cb_param_t *param, bm2mqtt_node_info *node);
void refresh_all_nodes();
void refresh_node(bm2mqtt_node_info *node_info, const esp_ble_mesh_node_t *node);
void RegisterBleMeshDebugCommands();

void ble_mesh_set_provisioning_enabled(bool enabled_value);