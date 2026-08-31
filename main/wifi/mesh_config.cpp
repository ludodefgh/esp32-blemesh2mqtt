#include "mesh_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "common/log_common.h"

#define TAG "MESH_CFG"

#define KEY_MODE     "mode"
#define KEY_NET_KEY  "net_key"
#define KEY_APP_KEY  "app_key"
#define KEY_GRP_ADDR "group_addr"
#define KEY_NODE_ADDR    "node_addr"
#define KEY_NODE_NET_IDX "node_net_idx"
#define KEY_NODE_APP_IDX "node_app_idx"

// Default app key matches the former hardcoded value (0x12 repeated)
static const uint8_t DEFAULT_APP_KEY[16] = {
    0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
    0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12
};

esp_err_t mesh_config_load(mesh_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    cfg->mode = MESH_MODE_STANDALONE;
    memcpy(cfg->app_key, DEFAULT_APP_KEY, 16);
    memset(cfg->net_key, 0, 16);
    cfg->group_addr = 0;
    cfg->node_addr = 0;
    cfg->node_net_idx = 0;
    cfg->node_app_idx = 0xFFFF; // ESP_BLE_MESH_KEY_UNUSED — 0 is itself a valid index

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MESH_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        LOG_WARN(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    uint8_t mode = 0;
    if (nvs_get_u8(handle, KEY_MODE, &mode) == ESP_OK) {
        cfg->mode = (mesh_mode_t)mode;
    }

    size_t key_len = 16;
    nvs_get_blob(handle, KEY_NET_KEY, cfg->net_key, &key_len);
    key_len = 16;
    nvs_get_blob(handle, KEY_APP_KEY, cfg->app_key, &key_len);

    uint16_t group_addr = 0;
    if (nvs_get_u16(handle, KEY_GRP_ADDR, &group_addr) == ESP_OK) {
        cfg->group_addr = group_addr;
    }
    nvs_get_u16(handle, KEY_NODE_ADDR, &cfg->node_addr);
    nvs_get_u16(handle, KEY_NODE_NET_IDX, &cfg->node_net_idx);
    nvs_get_u16(handle, KEY_NODE_APP_IDX, &cfg->node_app_idx);

    nvs_close(handle);
    // DEBUG not INFO: called on nearly every request, would drown out the log otherwise.
    LOG_DEBUG(TAG, "Mesh config loaded: mode=%d, group_addr=0x%04X", cfg->mode, cfg->group_addr);
    return ESP_OK;
}

esp_err_t mesh_config_save(const mesh_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MESH_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, KEY_MODE, (uint8_t)cfg->mode);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_blob(handle, KEY_NET_KEY, cfg->net_key, 16);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_blob(handle, KEY_APP_KEY, cfg->app_key, 16);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_u16(handle, KEY_GRP_ADDR, cfg->group_addr);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_u16(handle, KEY_NODE_ADDR, cfg->node_addr);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_u16(handle, KEY_NODE_NET_IDX, cfg->node_net_idx);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_u16(handle, KEY_NODE_APP_IDX, cfg->node_app_idx);
    if (err != ESP_OK) goto cleanup;

    err = nvs_commit(handle);
    LOG_INFO(TAG, "Mesh config saved: mode=%d, group_addr=0x%04X", cfg->mode, cfg->group_addr);

cleanup:
    nvs_close(handle);
    return err;
}

esp_err_t mesh_config_load_group_addr(uint16_t *group_addr)
{
    if (!group_addr) return ESP_ERR_INVALID_ARG;
    *group_addr = 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MESH_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return ESP_OK;
    nvs_get_u16(handle, KEY_GRP_ADDR, group_addr);
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t mesh_config_save_group_addr(uint16_t group_addr)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MESH_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u16(handle, KEY_GRP_ADDR, group_addr);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t mesh_config_save_node_identity(uint16_t addr, uint16_t net_idx)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MESH_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Node identity NOT saved (nvs_open failed): %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u16(handle, KEY_NODE_ADDR, addr);
    if (err == ESP_OK) err = nvs_set_u16(handle, KEY_NODE_NET_IDX, net_idx);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        LOG_INFO(TAG, "Node identity saved: addr=0x%04X, net_idx=0x%04X", addr, net_idx);
    }
    else
    {
        LOG_ERROR(TAG, "Node identity NOT saved (addr=0x%04X, net_idx=0x%04X): %s",
                  addr, net_idx, esp_err_to_name(err));
    }
    return err;
}

esp_err_t mesh_config_save_node_app_idx(uint16_t app_idx)
{
    // Diagnostic: "nvs" is a shared 16KB partition, worth knowing if it's getting full.
    nvs_stats_t stats = {};
    if (nvs_get_stats(NULL, &stats) == ESP_OK)
    {
        LOG_INFO(TAG, "NVS usage: %u/%u entries used (%u free, %u namespaces)",
                 (unsigned)stats.used_entries, (unsigned)stats.total_entries,
                 (unsigned)stats.free_entries, (unsigned)stats.namespace_count);
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MESH_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Node AppKey index NOT saved (nvs_open failed): %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u16(handle, KEY_NODE_APP_IDX, app_idx);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        LOG_INFO(TAG, "Node AppKey index saved: app_idx=0x%04X", app_idx);
    }
    else
    {
        LOG_ERROR(TAG, "Node AppKey index NOT saved (app_idx=0x%04X): %s", app_idx, esp_err_to_name(err));
    }
    return err;
}

esp_err_t mesh_config_reset_stack_state(void)
{
    // "mesh_core" is the BLE Mesh stack's own NVS namespace (settings_nvs.c) — no
    // public API to erase it, so do it directly.
    nvs_handle_t handle;
    esp_err_t err = nvs_open("mesh_core", NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; // nothing persisted yet — nothing to clear
    }
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open mesh_core namespace: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        LOG_INFO(TAG, "Cleared persisted BLE Mesh stack state (mesh_core namespace)");
    } else {
        LOG_ERROR(TAG, "Failed to clear mesh_core namespace: %s", esp_err_to_name(err));
    }
    return err;
}
