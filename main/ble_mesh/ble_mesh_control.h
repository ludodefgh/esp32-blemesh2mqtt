#pragma once
#include "esp_err.h"
#include "ble_mesh_node.h"

long map(long x, long in_min, long in_max, long out_min, long out_max);


esp_err_t ble_mesh_init(void);
static void on_composition_received(esp_ble_mesh_cfg_client_cb_param_t *param, bm2mqtt_node_info *node, esp_ble_mesh_client_common_param_t &common);
void refresh_all_nodes();
void refresh_node(bm2mqtt_node_info *node_info, const esp_ble_mesh_node_t *node);
void RegisterBleMeshDebugCommands();

void ble_mesh_set_provisioning_enabled(bool enabled_value);