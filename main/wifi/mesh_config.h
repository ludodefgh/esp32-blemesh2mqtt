#pragma once
#include <stdint.h>
#include <string.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MESH_CONFIG_NAMESPACE "mesh_cfg"

typedef enum {
    MESH_MODE_STANDALONE = 0,
    MESH_MODE_JOIN_EXISTING = 1,
} mesh_mode_t;

typedef struct {
    mesh_mode_t mode;
    uint8_t net_key[16];
    uint8_t app_key[16];
    uint16_t group_addr;
} mesh_config_t;

esp_err_t mesh_config_load(mesh_config_t *cfg);
esp_err_t mesh_config_save(const mesh_config_t *cfg);
esp_err_t mesh_config_load_group_addr(uint16_t *group_addr);
esp_err_t mesh_config_save_group_addr(uint16_t group_addr);

#ifdef __cplusplus
}
#endif
