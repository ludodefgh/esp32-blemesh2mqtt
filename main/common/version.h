#pragma once

#include "sdkconfig.h"

// Firmware version string - update this single location to change version everywhere
#define FIRMWARE_VERSION "0.1.8"

// User-facing edition name (documentation/EDITIONS.md): Standalone = Provisioner SKU, Companion = Node SKU
#ifdef CONFIG_BLE_MESH_PROVISIONER
#define FIRMWARE_EDITION "Standalone"
#else
#define FIRMWARE_EDITION "Companion"
#endif
