#include "mesh_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "common/log_common.h"

#define TAG "MESH_CFG"

#define KEY_MODE     "mode"
#define KEY_NET_KEY  "net_key"
#define KEY_APP_KEY  "app_key"
#define KEY_GRP_ADDR "group_addr"

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

    nvs_close(handle);
    LOG_INFO(TAG, "Mesh config loaded: mode=%d, group_addr=0x%04X", cfg->mode, cfg->group_addr);
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
