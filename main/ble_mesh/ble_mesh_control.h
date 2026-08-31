#pragma once
#include <functional>
#include <memory>

#include "esp_err.h"
#include "ble_mesh_node.h"

typedef enum
{
    FEATURE_GENERIC_ONOFF = 1 << 0,
    FEATURE_LIGHT_LIGHTNESS = 1 << 1,
    FEATURE_LIGHT_HSL = 1 << 2,
    FEATURE_LIGHT_CTL = 1 << 3,
    FEATURE_GENERIC_LEVEL = 1 << 4,
} node_supported_features_t;

long map(long x, long in_min, long in_max, long out_min, long out_max);

esp_err_t ble_mesh_init(void);
void ble_mesh_on_composition_received(esp_ble_mesh_cfg_client_cb_param_t *param, std::shared_ptr<bm2mqtt_node_info> node);
void ble_mesh_refresh_all_nodes();
void ble_mesh_refresh_node(const std::shared_ptr<bm2mqtt_node_info>& node_info, const esp_ble_mesh_node_t *node);
void register_blemesh_commands();

// Provisioning control functions
void ble_mesh_set_provisioning_enabled(bool enabled_value);
bool ble_mesh_get_provisioning_enabled(void);

void ble_mesh_set_auto_provisioning_enabled(bool enabled_value);
bool ble_mesh_get_auto_provisioning_enabled(void);

void ble_mesh_subscribe_group_addr(uint16_t group_addr);
// PROV_OWN_ADDR standalone, or the provisioner-assigned address once joined as a node.
extern uint16_t local_element_addr;
bool ble_mesh_get_local_keys_hex(char *net_key_hex, size_t net_key_hex_len,
                                  char *app_key_hex, size_t app_key_hex_len);
// Standalone SKU only — must match the #ifdef on its definition or a Node-only build
// compiles against this declaration and fails to link.
#ifdef CONFIG_BLE_MESH_PROVISIONER
esp_err_t ble_mesh_apply_local_app_key(const uint8_t app_key[16]);
#endif

// A device on a joined mesh we never provisioned (no DevKey, so no Composition Data
// Get) — discovered by probing each model type against the group address and noting
// who answers; `features` is the bitmask of what answered.
struct external_mesh_node_t
{
    uint16_t unicast;
    uint16_t features; // bitmask of node_supported_features_t actually observed
    uint8_t onoff;
    int16_t level;
    uint16_t lightness;
    int64_t last_seen_us;
};

esp_err_t ble_mesh_discover_external_nodes();
esp_err_t ble_mesh_send_external_command(uint16_t addr, bool onoff);
esp_err_t ble_mesh_send_external_level_command(uint16_t addr, int16_t level);
esp_err_t ble_mesh_send_external_lightness_command(uint16_t addr, uint16_t lightness);
void for_each_external_node(std::function<void(const external_mesh_node_t &)> func);

// MQTT republish functions
void ble_mesh_republish_all_nodes_to_mqtt(void);

// Function declarations from main.cpp
esp_err_t bluetooth_init(void);
void ble_mesh_get_dev_uuid(uint8_t *dev_uuid);