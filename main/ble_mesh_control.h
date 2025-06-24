#pragma once
#include "esp_err.h"
#include "ble_mesh_node.h"

long map(long x, long in_min, long in_max, long out_min, long out_max);

void ble_mesh_ctl_set(bm2mqtt_node_info *node_info);
void ble_mesh_ctl_temperature_set(bm2mqtt_node_info *node_info);
void gen_onoff_set(bm2mqtt_node_info * node_info);
void SendGenericOnOffToggle();
void light_hsl_set(bm2mqtt_node_info * node_info);

esp_err_t ble_mesh_init(void);
void RefreshNodes();
void RegisterBleMeshDebugCommands();