#include "mesh_config.h"

#include <mutex>

#include "sdkconfig.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "common/log_common.h"

#define TAG "MESH_CFG"

#define KEY_MODE     "mode"
#define KEY_NET_KEY  "net_key"
#define KEY_APP_KEY  "app_key"
#define KEY_GRP_ADDR "group_addr"      // legacy single-address key, migrated on load
#define KEY_GRP_ADDRS "group_addrs"    // uint16_t[] blob, KEY_GRP_ADDR_COUNT entries
#define KEY_GRP_ADDR_COUNT "grp_cnt"
#define KEY_NODE_ADDR    "node_addr"
#define KEY_NODE_NET_IDX "node_net_idx"
#define KEY_NODE_APP_IDX "node_app_idx"

// Default app key matches the former hardcoded value (0x12 repeated)
static const uint8_t DEFAULT_APP_KEY[16] = {
    0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12,
    0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12
};

// Serializes every read-modify-write of mesh_cfg: HTTP/console handlers save the
// whole struct, while the BLE task writes node identity/app_idx directly.
static std::recursive_mutex s_cfg_mutex;

esp_err_t mesh_config_load(mesh_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::recursive_mutex> lock(s_cfg_mutex);

    // Unconfigured default is whichever role this SKU can actually run (see ble_mesh_init).
#ifdef CONFIG_BLE_MESH_PROVISIONER
    cfg->mode = MESH_MODE_STANDALONE;
#else
    cfg->mode = MESH_MODE_JOIN_EXISTING;
#endif
    memcpy(cfg->app_key, DEFAULT_APP_KEY, 16);
    memset(cfg->net_key, 0, 16);
    memset(cfg->group_addrs, 0, sizeof(cfg->group_addrs));
    cfg->group_addr_count = 0;
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

    uint8_t count = 0;
    if (nvs_get_u8(handle, KEY_GRP_ADDR_COUNT, &count) == ESP_OK) {
        size_t blob_len = sizeof(cfg->group_addrs);
        if (nvs_get_blob(handle, KEY_GRP_ADDRS, cfg->group_addrs, &blob_len) == ESP_OK) {
            // Never trust count past what the blob actually held.
            uint8_t stored = (uint8_t)(blob_len / sizeof(cfg->group_addrs[0]));
            cfg->group_addr_count = count > stored ? stored : count;
        }
    } else {
        // Migrate from the old single-address key — never written back here, just
        // presented as a 1-item list; the next add/remove call persists it properly.
        uint16_t legacy_addr = 0;
        if (nvs_get_u16(handle, KEY_GRP_ADDR, &legacy_addr) == ESP_OK && legacy_addr != 0) {
            cfg->group_addrs[0] = legacy_addr;
            cfg->group_addr_count = 1;
        }
    }

    nvs_get_u16(handle, KEY_NODE_ADDR, &cfg->node_addr);
    nvs_get_u16(handle, KEY_NODE_NET_IDX, &cfg->node_net_idx);
    nvs_get_u16(handle, KEY_NODE_APP_IDX, &cfg->node_app_idx);

    nvs_close(handle);
    // DEBUG not INFO: called on nearly every request, would drown out the log otherwise.
    LOG_DEBUG(TAG, "Mesh config loaded: mode=%d, group_addr_count=%d", cfg->mode, cfg->group_addr_count);
    return ESP_OK;
}

esp_err_t mesh_config_save(const mesh_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::recursive_mutex> lock(s_cfg_mutex);

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

    err = nvs_set_blob(handle, KEY_GRP_ADDRS, cfg->group_addrs, sizeof(cfg->group_addrs));
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_u8(handle, KEY_GRP_ADDR_COUNT, cfg->group_addr_count);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_u16(handle, KEY_NODE_ADDR, cfg->node_addr);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_u16(handle, KEY_NODE_NET_IDX, cfg->node_net_idx);
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_u16(handle, KEY_NODE_APP_IDX, cfg->node_app_idx);
    if (err != ESP_OK) goto cleanup;

    err = nvs_commit(handle);
    LOG_INFO(TAG, "Mesh config saved: mode=%d, group_addr_count=%d", cfg->mode, cfg->group_addr_count);

cleanup:
    nvs_close(handle);
    return err;
}

esp_err_t mesh_config_update(void (*mutate)(mesh_config_t *cfg, void *ctx), void *ctx)
{
    if (!mutate) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::recursive_mutex> lock(s_cfg_mutex);
    mesh_config_t cfg = {};
    mesh_config_load(&cfg);
    mutate(&cfg, ctx);
    return mesh_config_save(&cfg);
}

esp_err_t mesh_config_load_group_addrs(uint16_t *out_addrs, uint8_t max_count, uint8_t *out_count)
{
    if (!out_addrs || !out_count) return ESP_ERR_INVALID_ARG;
    *out_count = 0;

    mesh_config_t cfg = {};
    mesh_config_load(&cfg); // handles the legacy-key migration too

    uint8_t n = cfg.group_addr_count > max_count ? max_count : cfg.group_addr_count;
    for (uint8_t i = 0; i < n; i++) {
        out_addrs[i] = cfg.group_addrs[i];
    }
    *out_count = n;
    return ESP_OK;
}

esp_err_t mesh_config_add_group_addr(uint16_t group_addr)
{
    std::lock_guard<std::recursive_mutex> lock(s_cfg_mutex);
    mesh_config_t cfg = {};
    mesh_config_load(&cfg);

    for (uint8_t i = 0; i < cfg.group_addr_count; i++) {
        if (cfg.group_addrs[i] == group_addr) {
            return ESP_OK; // already present
        }
    }
    if (cfg.group_addr_count >= MESH_MAX_GROUP_ADDRS) {
        return ESP_ERR_NO_MEM;
    }
    cfg.group_addrs[cfg.group_addr_count++] = group_addr;
    return mesh_config_save(&cfg);
}

esp_err_t mesh_config_remove_group_addr(uint16_t group_addr)
{
    std::lock_guard<std::recursive_mutex> lock(s_cfg_mutex);
    mesh_config_t cfg = {};
    mesh_config_load(&cfg);

    uint8_t write = 0;
    for (uint8_t read = 0; read < cfg.group_addr_count; read++) {
        if (cfg.group_addrs[read] != group_addr) {
            cfg.group_addrs[write++] = cfg.group_addrs[read];
        }
    }
    if (write == cfg.group_addr_count) {
        return ESP_OK; // wasn't present
    }
    for (uint8_t i = write; i < cfg.group_addr_count; i++) {
        cfg.group_addrs[i] = 0;
    }
    cfg.group_addr_count = write;
    return mesh_config_save(&cfg);
}

esp_err_t mesh_config_save_node_identity(uint16_t addr, uint16_t net_idx)
{
    std::lock_guard<std::recursive_mutex> lock(s_cfg_mutex);
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

    std::lock_guard<std::recursive_mutex> lock(s_cfg_mutex);
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
