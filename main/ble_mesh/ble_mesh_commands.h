#pragma once
#include "ble_mesh_node.h"

void RegisterBleMeshCommandsDebugCommands();


void gen_onoff_set(bm2mqtt_node_info *node_info);

int ble_mesh_ctl_lightness_set(int lightness_value, uint8_t uuid[16]);

void ble_mesh_hsl_range_get(bm2mqtt_node_info *node_info);


void ble_mesh_ctl_set(bm2mqtt_node_info *node_info);

void ble_mesh_ctl_temperature_get(bm2mqtt_node_info *node_info);
void ble_mesh_ctl_temperature_set(bm2mqtt_node_info *node_info);
int ble_mesh_ctl_temperature_set(int temperature_value, uint8_t uuid[16]);
void ble_mesh_ctl_temperature_range_get(bm2mqtt_node_info *node_info);

void light_hsl_set(bm2mqtt_node_info * node_info);

void SendGenericOnOffToggle();

