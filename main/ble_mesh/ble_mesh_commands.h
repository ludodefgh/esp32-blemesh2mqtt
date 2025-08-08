#pragma once
#include "ble_mesh_node.h"

void register_blemesh_action_commands();

void ble_mesh_gen_onoff_set(std::shared_ptr<bm2mqtt_node_info> node_info);

void ble_mesh_ctl_lightness_set(int lightness_value, const device_uuid128 &uuid);
void ble_mesh_hsl_range_get(std::shared_ptr<bm2mqtt_node_info> node_info);
void ble_mesh_lightness_range_get(std::shared_ptr<bm2mqtt_node_info> node_info);
void ble_mesh_lightness_set(std::shared_ptr<bm2mqtt_node_info> node_info);
void ble_mesh_ctl_set(std::shared_ptr<bm2mqtt_node_info> node_info);
void ble_mesh_ctl_temperature_get(std::shared_ptr<bm2mqtt_node_info> node_info);
void ble_mesh_ctl_temperature_set(std::shared_ptr<bm2mqtt_node_info> node_info);
void ble_mesh_ctl_temperature_range_get(std::shared_ptr<bm2mqtt_node_info> node_info);

void ble_mesh_light_hsl_set(std::shared_ptr<bm2mqtt_node_info> node_info);
