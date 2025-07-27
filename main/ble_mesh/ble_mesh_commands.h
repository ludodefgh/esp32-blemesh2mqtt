#pragma once
#include "ble_mesh_node.h"

void RegisterBleMeshCommandsDebugCommands();


void gen_onoff_set(bm2mqtt_node_info *node_info);

void ble_mesh_ctl_lightness_set(int lightness_value, const Uuid128& uuid);
void ble_mesh_hsl_range_get(bm2mqtt_node_info *node_info);
void ble_mesh_lightness_range_get(bm2mqtt_node_info *node_info);
void ble_mesh_lightness_set(bm2mqtt_node_info *node_info);
void ble_mesh_ctl_set(bm2mqtt_node_info *node_info);
void ble_mesh_ctl_temperature_get(bm2mqtt_node_info *node_info);
void ble_mesh_ctl_temperature_set(bm2mqtt_node_info *node_info);
void ble_mesh_ctl_temperature_range_get(bm2mqtt_node_info *node_info);

void light_hsl_set(bm2mqtt_node_info * node_info);

void SendGenericOnOffToggle();

