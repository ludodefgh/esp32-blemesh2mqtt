#pragma once
#include "esp_err.h"
#include "nodesManager.h"

long map(long x, long in_min, long in_max, long out_min, long out_max);

void ble_mesh_ctl_temperature_set(esp_ble_mesh_node_info_t *node_info);
void SendGenericOnOff(bool value);
void SendGenericOnOffToggle();
void SendHSL();

esp_err_t ble_mesh_init(void);
void RefreshNodes();
void RegisterBleMeshDebugCommands();