#pragma once
#include <stdint.h>
#include <string.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MESH_CONFIG_NAMESPACE "mesh_cfg"

// Matches CONFIG_BLE_MESH_MODEL_GROUP_COUNT's default (3) — each Client model's own
// subscription list is that deep, so more than this many wouldn't all fit anyway.
#define MESH_MAX_GROUP_ADDRS 3

typedef enum {
    MESH_MODE_STANDALONE = 0,
    MESH_MODE_JOIN_EXISTING = 1,
} mesh_mode_t;

typedef struct {
    mesh_mode_t mode;
    uint8_t net_key[16];
    uint8_t app_key[16];
    uint16_t group_addrs[MESH_MAX_GROUP_ADDRS];
    uint8_t group_addr_count;
    // Join-existing-as-node identity: unlike net_key/app_key above (manual entry,
    // superseded for this mode — see ble_mesh_init), these are never typed in.
    // They're learned from the real provisioning handshake (node_addr, node_net_idx)
    // and the Config AppKey Add the external provisioner sends afterward
    // (node_app_idx), then persisted here so we don't have to wait for those
    // events again on every subsequent boot.
    uint16_t node_addr;
    uint16_t node_net_idx;
    uint16_t node_app_idx;
} mesh_config_t;

esp_err_t mesh_config_load(mesh_config_t *cfg);
esp_err_t mesh_config_save(const mesh_config_t *cfg);
// Replaces the old single-address mesh_config_load_group_addr/save_group_addr.
esp_err_t mesh_config_load_group_addrs(uint16_t *out_addrs, uint8_t max_count, uint8_t *out_count);
// No-op (ESP_OK) if already present. ESP_ERR_NO_MEM if MESH_MAX_GROUP_ADDRS is already used.
esp_err_t mesh_config_add_group_addr(uint16_t group_addr);
// No-op (ESP_OK) if not present.
esp_err_t mesh_config_remove_group_addr(uint16_t group_addr);
esp_err_t mesh_config_save_node_identity(uint16_t addr, uint16_t net_idx);
esp_err_t mesh_config_save_node_app_idx(uint16_t app_idx);
// Erases the BLE Mesh stack's own persisted state (NVS namespace "mesh_core" — its
// NetKey/AppKey/seq/RPL/role, NOT WiFi or MQTT config). Required before switching
// between Provisioner and Node role: the stack refuses to enable a role that
// mismatches whatever role it last persisted, to avoid corrupting that state.
esp_err_t mesh_config_reset_stack_state(void);

#ifdef __cplusplus
}
#endif
