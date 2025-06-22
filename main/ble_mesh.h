#pragma once
#include "esp_err.h"

long map(long x, long in_min, long in_max, long out_min, long out_max);

void SendGenericOnOff(bool value);
void SendGenericOnOffToggle();
void SendHSL();

int unprovision(int argc, char **argv);
esp_err_t ble_mesh_init(void);