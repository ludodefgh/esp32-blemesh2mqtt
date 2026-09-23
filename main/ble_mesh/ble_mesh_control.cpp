#include "ble_mesh_control.h"

// Standard C/C++ libraries
#include <inttypes.h>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <vector>

// ESP-IDF includes
#include "argtable3/argtable3.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// Project includes
#include "ble_mesh_commands.h"
#include "ble_mesh_node.h"
#include "ble_mesh_provisioning.h"
#include "external_node_queue.h"
#include "common/log_common.h"
#include "wifi/mesh_config.h"
#include "debug/console_cmd.h"
#include "debug/debug_commands_registry.h"
#include "debug_console_common.h"
#include "message_queue.h"
#include "mqtt/mqtt_bridge.h"
#include "mqtt/mqtt_control.h"
#include "mqtt/mqtt_external_control.h"
#include "sig_companies/company_map.h"
#include "sig_models/model_map.h"

#define TAG "APP_CONTROL"

#define CID_ESP 0x02E5

#define MSG_ROLE ROLE_PROVISIONER

#define APP_KEY_IDX 0x0000

#define APP_USE_ONOFF_CLIENT

static uint8_t dev_uuid[16];

extern struct mesh_network_info_store store;

// PROV_OWN_ADDR when standalone; the real provisioner-assigned address once joined
// as a node (see ble_mesh_init / ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT).
uint16_t local_element_addr = PROV_OWN_ADDR;

static std::mutex external_nodes_mutex;
static std::vector<external_mesh_node_t> external_nodes;

// Caller must hold external_nodes_mutex.
static external_mesh_node_t *find_external_node_locked(uint16_t addr)
{
    for (auto &n : external_nodes)
    {
        if (n.unicast == addr)
        {
            return &n;
        }
    }
    return nullptr;
}

// Caller must hold external_nodes_mutex.
static external_mesh_node_t &get_or_create_external_node_locked(uint16_t addr, bool *was_new = nullptr)
{
    if (external_mesh_node_t *n = find_external_node_locked(addr))
    {
        if (was_new) *was_new = false;
        return *n;
    }
    external_mesh_node_t node{};
    node.unicast = addr;
    node.color_mode = color_mode_t::brightness;
    external_nodes.push_back(node);
    if (was_new) *was_new = true;
    return external_nodes.back();
}

bool ble_mesh_find_external_node(uint16_t addr, external_mesh_node_t &out)
{
    std::lock_guard<std::mutex> lock(external_nodes_mutex);
    if (const external_mesh_node_t *n = find_external_node_locked(addr))
    {
        out = *n;
        return true;
    }
    return false;
}

static void upsert_external_node_onoff(uint16_t addr, uint8_t onoff)
{
    bool was_new = false;
    {
        std::lock_guard<std::mutex> lock(external_nodes_mutex);
        auto &node = get_or_create_external_node_locked(addr, &was_new);
        node.onoff = onoff;
        node.features |= FEATURE_GENERIC_ONOFF;
        node.last_seen_us = esp_timer_get_time();
    }
    mqtt_notify_external_node_changed(addr, was_new);
}

// Lightness/HSL/CTL Server models extend Generic Level Server (Mesh Model spec), so a
// dimmer would otherwise get a spurious standalone "Level" flag too.
static constexpr uint16_t GENERIC_LEVEL_EXTENDING_FEATURES =
    FEATURE_LIGHT_LIGHTNESS | FEATURE_LIGHT_HSL | FEATURE_LIGHT_CTL;

// Unicast Range Get, same as the provisioned-node path (ble_mesh_commands.cpp) — an
// AppKey-bound op, not DevKey-bound, so it works for a node we never provisioned too.
// Defined near the other external send functions further down; forward-declared here
// since the upsert_* functions below fire them on first sighting of each feature.
static void send_external_lightness_range_get(uint16_t addr);
static void send_external_hsl_range_get(uint16_t addr);
static void send_external_ctl_temperature_range_get(uint16_t addr);

static void upsert_external_node_level(uint16_t addr, int16_t level)
{
    bool was_new = false;
    bool excluded = false;
    {
        std::lock_guard<std::mutex> lock(external_nodes_mutex);
        auto &node = get_or_create_external_node_locked(addr, &was_new);
        if (node.features & GENERIC_LEVEL_EXTENDING_FEATURES)
        {
            excluded = true;
        }
        else
        {
            node.level = level;
            node.features |= FEATURE_GENERIC_LEVEL;
            node.last_seen_us = esp_timer_get_time();
        }
    }
    if (!excluded)
    {
        mqtt_notify_external_node_changed(addr, was_new);
    }
}

static void upsert_external_node_lightness(uint16_t addr, uint16_t lightness)
{
    bool was_new = false;
    bool feature_newly_set = false;
    {
        std::lock_guard<std::mutex> lock(external_nodes_mutex);
        auto &node = get_or_create_external_node_locked(addr, &was_new);
        feature_newly_set = !(node.features & FEATURE_LIGHT_LIGHTNESS);
        node.lightness = lightness;
        node.features |= FEATURE_LIGHT_LIGHTNESS;
        node.features &= ~FEATURE_GENERIC_LEVEL; // retroactive: probe order isn't guaranteed
        node.last_seen_us = esp_timer_get_time();
    }
    if (feature_newly_set)
    {
        send_external_lightness_range_get(addr);
    }
    mqtt_notify_external_node_changed(addr, was_new);
}

static void upsert_external_node_hsl(uint16_t addr, uint16_t hue, uint16_t saturation, uint16_t lightness)
{
    bool was_new = false;
    bool feature_newly_set = false;
    {
        std::lock_guard<std::mutex> lock(external_nodes_mutex);
        auto &node = get_or_create_external_node_locked(addr, &was_new);
        feature_newly_set = !(node.features & FEATURE_LIGHT_HSL);
        node.hue = hue;
        node.saturation = saturation;
        node.lightness = lightness;
        node.color_mode = color_mode_t::hs;
        node.features |= FEATURE_LIGHT_HSL;
        node.features &= ~FEATURE_GENERIC_LEVEL; // retroactive: probe order isn't guaranteed
        node.last_seen_us = esp_timer_get_time();
    }
    if (feature_newly_set)
    {
        send_external_hsl_range_get(addr);
    }
    mqtt_notify_external_node_changed(addr, was_new);
}

static void upsert_external_node_ctl(uint16_t addr, uint16_t temperature, uint16_t lightness)
{
    bool was_new = false;
    bool feature_newly_set = false;
    {
        std::lock_guard<std::mutex> lock(external_nodes_mutex);
        auto &node = get_or_create_external_node_locked(addr, &was_new);
        feature_newly_set = !(node.features & FEATURE_LIGHT_CTL);
        node.temperature = temperature;
        node.lightness = lightness;
        node.color_mode = color_mode_t::color_temp;
        node.features |= FEATURE_LIGHT_CTL;
        node.features &= ~FEATURE_GENERIC_LEVEL; // retroactive: probe order isn't guaranteed
        node.last_seen_us = esp_timer_get_time();
    }
    if (feature_newly_set)
    {
        send_external_ctl_temperature_range_get(addr);
    }
    mqtt_notify_external_node_changed(addr, was_new);
}

// Range replies only ever answer our own unicast Range Get to an already-known node;
// one for an unknown address (e.g. a late reply) is dropped rather than creating a
// featureless node.
static void upsert_external_node_lightness_range(uint16_t addr, uint16_t min_lightness, uint16_t max_lightness)
{
    {
        std::lock_guard<std::mutex> lock(external_nodes_mutex);
        external_mesh_node_t *node = find_external_node_locked(addr);
        if (!node)
        {
            return;
        }
        node->min_lightness = min_lightness;
        node->max_lightness = max_lightness;
    }
    // Re-announce discovery so HA picks up the real brightness_scale.
    mqtt_notify_external_node_changed(addr, true);
}

static void upsert_external_node_hsl_range(uint16_t addr, uint16_t min_hue, uint16_t max_hue,
                                            uint16_t min_saturation, uint16_t max_saturation)
{
    {
        std::lock_guard<std::mutex> lock(external_nodes_mutex);
        external_mesh_node_t *node = find_external_node_locked(addr);
        if (!node)
        {
            return;
        }
        node->min_hue = min_hue;
        node->max_hue = max_hue;
        node->min_saturation = min_saturation;
        node->max_saturation = max_saturation;
    }
    mqtt_notify_external_node_changed(addr, true);
}

static void upsert_external_node_ctl_range(uint16_t addr, uint16_t min_temp, uint16_t max_temp)
{
    {
        std::lock_guard<std::mutex> lock(external_nodes_mutex);
        external_mesh_node_t *node = find_external_node_locked(addr);
        if (!node)
        {
            return;
        }
        node->min_temp = min_temp;
        node->max_temp = max_temp;
    }
    // Re-announce discovery so HA picks up the real min/max_kelvin.
    mqtt_notify_external_node_changed(addr, true);
}

static struct esp_ble_mesh_key
{
    uint8_t app_key[16];
} prov_key;

static esp_ble_mesh_cfg_srv_t config_server = {
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = 2,
};

esp_ble_mesh_client_t config_client;
esp_ble_mesh_client_t onoff_client;
esp_ble_mesh_client_t level_client;
esp_ble_mesh_client_t lightness_cli;
esp_ble_mesh_client_t hsl_cli;
esp_ble_mesh_client_t ctl_cli;

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_CLI(NULL, &lightness_cli),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_CLI(NULL, &hsl_cli),
    ESP_BLE_MESH_MODEL_GEN_LEVEL_CLI(NULL, &level_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(NULL, &onoff_client),
    ESP_BLE_MESH_MODEL_LIGHT_CTL_CLI(NULL, &ctl_cli),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};
uint8_t MyKey[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

static esp_ble_mesh_prov_t provision = {
#if CONFIG_BLE_MESH_NODE
    // Node-role field, available regardless of CONFIG_BLE_MESH_PROVISIONER.
    .uuid = dev_uuid,
#endif
#ifdef CONFIG_BLE_MESH_PROVISIONER
    // Provisioner-role fields — standalone SKU only.
    .prov_uuid = dev_uuid,
    .prov_unicast_addr = PROV_OWN_ADDR,
    .prov_start_address = 0x0005,
    .prov_attention = 0x00,
    .prov_algorithm = 0x00,
    .prov_pub_key_oob = 0x00,
    .prov_static_oob_val = NULL,
    .prov_static_oob_len = 0,
    .flags = 0x00,
    .iv_index = 0x00,
#endif
};

////////////////////////////////////////////////////////

typedef struct
{
    uint16_t unicast_addr{0};
    uint8_t element_count{0};
    uint8_t light_ctl_temp_offset{0}; // Element index for Light CTL Temperature
    uint16_t features{0};
} parsed_node_info_t;

bool get_composition_data_debug = false;
parsed_node_info_t parse_composition_data(const uint8_t *data, size_t length, uint16_t unicast_addr)
{
    parsed_node_info_t info = {
        .unicast_addr = unicast_addr,
        .element_count = 0,
        .features = 0};

    if (!data || length < 10)
        return info;

    size_t offset = 0;

    uint16_t cid = data[0] | (data[1] << 8);
    uint16_t pid = data[2] | (data[3] << 8);
    uint16_t vid = data[4] | (data[5] << 8);
    uint16_t crpl = data[6] | (data[7] << 8);
    uint16_t features = data[8] | (data[9] << 8);
    const char *company_name = lookup_company_name(cid);

    LOG_INFO(TAG, "Node composition - CID: 0x%04X (%s), PID: 0x%04X, VID: 0x%04X, CRPL: %d, Features: 0x%04X",
             cid, company_name, pid, vid, crpl, features);
    char feature_str[256] = {0};
    size_t pos = 0;
    if (features & BIT(0))
        pos += snprintf(feature_str + pos, sizeof(feature_str) - pos, "Relay ");
    if (features & BIT(1))
        pos += snprintf(feature_str + pos, sizeof(feature_str) - pos, "Proxy ");
    if (features & BIT(2))
        pos += snprintf(feature_str + pos, sizeof(feature_str) - pos, "Friend ");
    if (features & BIT(3))
        pos += snprintf(feature_str + pos, sizeof(feature_str) - pos, "LowPower ");
    if (strlen(feature_str) > 0)
    {
        LOG_INFO(TAG, "Supported features: %s", feature_str);
    }

    // Skip Composition Header:
    // CID (2), PID (2), VID (2), CRPL (2), Features (2)
    offset += 10;

    while (offset < length)
    {
        if ((offset + 4) > length)
            break;

        // Each element begins with:
        // Location (2), Num SIG Models (1), Num Vendor Models (1)
        // uint16_t loc = data[offset] | (data[offset + 1] << 8);
        uint8_t num_sig = data[offset + 2];
        uint8_t num_vendor = data[offset + 3];
        offset += 4;

        // SIG models: each 2 bytes
        for (int i = 0; i < num_sig && offset + 2 <= length; i++)
        {
            uint16_t model_id = data[offset] | (data[offset + 1] << 8);
            offset += 2;

            LOG_WARN(TAG, "Found model ID 0x%04X: %s at element index %i", model_id, lookup_model_name(model_id), info.element_count);
            if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV)
            {
                info.features |= FEATURE_GENERIC_ONOFF;
            }
            else if (model_id == ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV)
            {
                info.features |= FEATURE_GENERIC_LEVEL;
            }
            else if (model_id == ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV)
            {
                info.features |= FEATURE_LIGHT_LIGHTNESS;
            }
            else if (model_id == ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV)
            {
                info.features |= FEATURE_LIGHT_HSL;
            }
            else if (model_id == ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV)
            {
                info.features |= FEATURE_LIGHT_CTL;
            }

            if (model_id == ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_TEMP_SRV)
            {
                info.light_ctl_temp_offset = info.element_count; // Store the element index for Light CTL Temperature
                LOG_WARN(TAG, "Light CTL Temperature model found at element index %i", info.light_ctl_temp_offset);
            }
        }

        info.element_count++;

        // Vendor models: each 4 bytes
        for (int i = 0; i < num_vendor && offset + 4 <= length; i++)
        {
            offset += 4;
            // info.features |= FEATURE_VENDOR;
        }
    }

    return info;
}

void Bind_App_Key_queue(std::shared_ptr<bm2mqtt_node_info>& node)
{
    if (!node)
        return;

    LOG_WARN(TAG, "features_to_bind : [%d]", node->features_to_bind);

    if ((node->features_to_bind & FEATURE_GENERIC_ONOFF) != 0)
    {
        node->features_to_bind &= ~FEATURE_GENERIC_ONOFF; // Clear the feature to avoid rebinding

        message_queue().enqueue(node, message_payload{
                                          .send = [](std::shared_ptr<bm2mqtt_node_info> &node)
                                          {
                                              LOG_WARN(TAG, "Generic on/off model for node 0x%04X", node->unicast);
                                              esp_ble_mesh_client_common_param_t common = {0};
                                              esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                              node_manager().ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                                              set_state.model_app_bind.element_addr = node->unicast;
                                              set_state.model_app_bind.model_app_idx = store.app_idx;
                                              set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV;
                                              set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                                              esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                              if (err)
                                              {
                                                  LOG_ERROR(TAG, "call to esp_ble_mesh_config_client_set_state failed");
                                              }
                                          },
                                          .opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND,
                                          .retries_left = 3,
                                      });
    }

    if ((node->features_to_bind & FEATURE_GENERIC_LEVEL) != 0)
    {
        node->features_to_bind &= ~FEATURE_GENERIC_LEVEL;
        message_queue().enqueue(node, message_payload{
                                          .send = [](std::shared_ptr<bm2mqtt_node_info> &node)
                                          {
                                              LOG_INFO(TAG, "Generic Level model for node 0x%04X", node->unicast);
                                              esp_ble_mesh_client_common_param_t common = {0};
                                              esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                              node_manager().ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                                              set_state.model_app_bind.element_addr = node->unicast;
                                              set_state.model_app_bind.model_app_idx = store.app_idx;
                                              set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV;
                                              set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                                              esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                              if (err)
                                              {
                                                  LOG_ERROR(TAG, "call to esp_ble_mesh_config_client_set_state failed");
                                              }
                                          },
                                          .opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND,
                                          .retries_left = 3,
                                      });
    }

    if ((node->features_to_bind & FEATURE_LIGHT_HSL) != 0)
    {
        node->features_to_bind &= ~FEATURE_LIGHT_HSL; // Clear the feature to avoid rebinding
        message_queue().enqueue(node, message_payload{
                                          .send = [](std::shared_ptr<bm2mqtt_node_info> &node) // shared_ptr captured by value, keeps node alive
                                          {
                                              LOG_WARN(TAG, "Light hsl model for node 0x%04X", node->unicast);
                                              esp_ble_mesh_client_common_param_t common = {0};
                                              esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                              node_manager().ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                                              set_state.model_app_bind.element_addr = node->unicast;
                                              set_state.model_app_bind.model_app_idx = store.app_idx;
                                              set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV;
                                              set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                                              esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                              if (err)
                                              {
                                                  LOG_ERROR(TAG, "call to esp_ble_mesh_config_client_set_state failed");
                                              }
                                          },
                                          .opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND,
                                          .retries_left = 3,
                                      });
    }

    if ((node->features_to_bind & FEATURE_LIGHT_LIGHTNESS) != 0)
    {
        node->features_to_bind &= ~FEATURE_LIGHT_LIGHTNESS; // Clear the feature to avoid rebinding
        message_queue().enqueue(node, message_payload{
                                          .send = [](std::shared_ptr<bm2mqtt_node_info> &node) // shared_ptr captured by value, keeps node alive
                                          {
                                              LOG_WARN(TAG, "Light lightness model for node 0x%04X", node->unicast);
                                              esp_ble_mesh_client_common_param_t common = {0};
                                              esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                              node_manager().ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                                              set_state.model_app_bind.element_addr = node->unicast;
                                              set_state.model_app_bind.model_app_idx = store.app_idx;
                                              set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV;
                                              set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                                              esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                              if (err)
                                              {
                                                  LOG_ERROR(TAG, "call to esp_ble_mesh_config_client_set_state failed");
                                              }
                                          },
                                          .opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND,
                                          .retries_left = 3,
                                      });
    }

    if ((node->features_to_bind & FEATURE_LIGHT_CTL) != 0)
    {
        node->features_to_bind &= ~FEATURE_LIGHT_CTL; // Clear the feature to avoid rebinding
        message_queue().enqueue(node, message_payload{
                                          .send = [](std::shared_ptr<bm2mqtt_node_info> &node) // shared_ptr captured by value, keeps node alive
                                          {
                                              LOG_WARN(TAG, "Light CTL temperature model for node 0x%04X", node->unicast);
                                              esp_ble_mesh_client_common_param_t common = {0};
                                              esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                              node_manager().ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                                              set_state.model_app_bind.element_addr = node->unicast + node->light_ctl_temp_offset;
                                              set_state.model_app_bind.model_app_idx = store.app_idx;
                                              set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_TEMP_SRV;
                                              set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                                              esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                              if (err)
                                              {
                                                  LOG_ERROR(TAG, "call to esp_ble_mesh_config_client_set_state failed");
                                              }
                                          },
                                          .opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND,
                                          .retries_left = 3,
                                      });

        message_queue().enqueue(node, message_payload{
                                          .send = [](std::shared_ptr<bm2mqtt_node_info> &node) // shared_ptr captured by value, keeps node alive
                                          {
                                              LOG_WARN(TAG, "Light CTL model for node 0x%04X", node->unicast);
                                              esp_ble_mesh_client_common_param_t common = {0};
                                              esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                              node_manager().ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                                              set_state.model_app_bind.element_addr = node->unicast;
                                              set_state.model_app_bind.model_app_idx = store.app_idx;
                                              set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV;
                                              set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                                              esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                              if (err)
                                              {
                                                  LOG_ERROR(TAG, "call to esp_ble_mesh_config_client_set_state failed");
                                              }
                                          },
                                          .opcode = ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND,
                                          .retries_left = 3,
                                      });
    }

    if (node->features_to_bind == 0)
    {
        LOG_WARN(TAG, "All features bound, no more binding needed");
        ble_mesh_refresh_node(node, nullptr);

        message_queue().enqueue(node, message_payload{
                                          .send = [](std::shared_ptr<bm2mqtt_node_info> &node) // shared_ptr captured by value, keeps node alive
                                          {
                                              mqtt_subscribe_node(mqtt_get_client(), node);
                                              mqtt_send_discovery(node);
                                              mqtt_node_send_status(node);
                                          },
                                          .opcode = 0x0000, // No specific opcode, just a marker
                                          .retries_left = 0,
                                          .type = message_type_t::mqtt_message, // Indicate this is a MQTT message
                                      });
    }
}

////////////////////////////////////////////////////////
static void ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                      esp_ble_mesh_cfg_client_cb_param_t *param)
{
    uint32_t opcode = param->params->opcode;
    uint16_t addr = param->params->ctx.addr;

    LOG_INFO(TAG, "error_code = 0x%02x, event = 0x%02x, addr: 0x%04x, opcode: 0x%04" PRIx32,
             param->error_code, event, param->params->ctx.addr, opcode);

    auto node = node_manager().get_node(addr);
    if (!node)
    {
        LOG_ERROR(TAG, "Get node info failed");
        return;
    }

    if (param->error_code)
    {
        LOG_ERROR(TAG, "Send config client message failed, opcode 0x%04" PRIx32, opcode);
        return;
    }

    switch (event)
    {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        message_queue().handle_ack(node, opcode);

        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET:
        {
            LOG_INFO(TAG, "Get : composition data %s", bt_hex(param->status_cb.comp_data_status.composition_data->data, param->status_cb.comp_data_status.composition_data->len));

            ble_mesh_on_composition_received(param, node);
            break;
        }

        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        message_queue().handle_ack(node, opcode);

        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        {
            LOG_INFO(TAG, "ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT -- > ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            node->features_to_bind = node->features;
            Bind_App_Key_queue(node);
        }
        break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        {
            LOG_INFO(TAG, "ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT -- > ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_NODE_RESET:
        {
            ble_mesh_reset_node(node);
        };
        break;
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT:
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_STATUS:
        {
            ESP_LOG_BUFFER_HEX("Publish evt : composition data %s", param->status_cb.comp_data_status.composition_data->data,
                               param->status_cb.comp_data_status.composition_data->len);

            ble_mesh_on_composition_received(param, node);
        }
        break;
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_STATUS:
            break;
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:

        message_queue().handle_timeout(node, opcode);

        break;
    default:
        LOG_ERROR(TAG, "Not a config client status message event");
        break;
    }
}

void ble_mesh_on_composition_received(esp_ble_mesh_cfg_client_cb_param_t *param, std::shared_ptr<bm2mqtt_node_info> node)
{
    uint16_t addr = param->params->ctx.addr;

    const struct net_buf_simple *buf = param->status_cb.comp_data_status.composition_data;
    const uint8_t *data = buf->data;
    size_t len = buf->len;
    parsed_node_info_t node_info = parse_composition_data(data, len, addr);

    LOG_INFO(TAG, "Parsed node 0x%04X: elements=%d features=0x%02X",
             node_info.unicast_addr, node_info.element_count, node_info.features);

    node->features = node_info.features;
    node->light_ctl_temp_offset = node_info.light_ctl_temp_offset;

    // Store the company ID from composition data (reuse existing buf and data variables)
    if (len >= 2)
    {
        uint16_t cid = data[0] | (data[1] << 8);
        node->company_id = cid;
        LOG_INFO(TAG, "Stored CID 0x%04X for node 0x%04X", cid, node->unicast);
    }

    if (node->features & FEATURE_LIGHT_LIGHTNESS)
    {
        LOG_WARN(TAG, "Light Lightness feature supported");
        node->color_mode = color_mode_t::brightness;
    }

    if (node->features & FEATURE_LIGHT_HSL)
    {
        LOG_WARN(TAG, "Light HSL feature supported");
        node->color_mode = color_mode_t::hs;
    }

    if (node->features & FEATURE_LIGHT_CTL)
    {
        LOG_WARN(TAG, "Light CTL feature supported");
        node->color_mode = color_mode_t::color_temp;
    }

    if (!get_composition_data_debug)
    {
        message_queue().enqueue(node, message_payload{
                                          .send = [](std::shared_ptr<bm2mqtt_node_info> &node) // shared_ptr captured by value, keeps node alive
                                          {
                                              LOG_WARN(TAG, "Requesting composition for node 0x%04X", node->unicast);
                                              esp_ble_mesh_client_common_param_t common = {0};
                                              esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                              node_manager().ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
                                              set_state.app_key_add.net_idx = store.net_idx;
                                              set_state.app_key_add.app_idx = store.app_idx;
                                              memcpy(set_state.app_key_add.app_key, prov_key.app_key, 16);
                                              esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                              if (err)
                                              {
                                                  LOG_ERROR(TAG, "Requesting composition failed");
                                              }
                                          },
                                          .opcode = ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD,
                                          .retries_left = 3,
                                      });
    }
    get_composition_data_debug = false;

    node_manager().mark_node_info_dirty();
}

static void ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                       esp_ble_mesh_generic_client_cb_param_t *param)
{
    uint32_t opcode = param->params->opcode;
    uint16_t addr = param->params->ctx.addr;

    LOG_INFO(TAG, "error_code = 0x%02x, event = 0x%02x, addr: 0x%04x, opcode: 0x%04" PRIx32,
             param->error_code, event, param->params->ctx.addr, opcode);

    if (param->error_code)
    {
        LOG_ERROR(TAG, "Send generic client message failed, opcode 0x%04" PRIx32, opcode);
        return;
    }

    auto node = node_manager().get_node(addr);
    if (!node)
    {
        // Not a node we provisioned — maybe a reply to a group-addressed discovery Get
        // (ble_mesh_discover_external_nodes). Group replies can't correlate to "the"
        // request, so they arrive as PUBLISH_EVT with opcode = the STATUS opcode, not
        // GET_STATE_EVT/SET_STATE_EVT with opcode = what we sent — handle both.
        if (event == ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT || event == ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT)
        {
            switch (opcode)
            {
            case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
                upsert_external_node_onoff(addr, param->status_cb.onoff_status.present_onoff);
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_GET:
            case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
                upsert_external_node_level(addr, param->status_cb.level_status.present_level);
                break;
            default:
                break;
            }
        }
        else if (event == ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT)
        {
            switch (opcode)
            {
            case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS:
                upsert_external_node_onoff(addr, param->status_cb.onoff_status.present_onoff);
                break;
            case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_STATUS:
                upsert_external_node_level(addr, param->status_cb.level_status.present_level);
                break;
            default:
                break;
            }
        }
        return;
    }

    switch (event)
    {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:

        message_queue().handle_ack(node, opcode);

        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET:
        {
            // esp_ble_mesh_generic_client_set_state_t set_state = {0};
            node->onoff = param->status_cb.onoff_status.present_onoff;
            LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET onoff: 0x%02x", node->onoff);
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_GET:
        {
            node->level = param->status_cb.level_status.present_level;
            LOG_INFO(TAG, "Level Status (Get): %d", node->level);
        }
        break;

        default:
            break;
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        message_queue().handle_ack(node, opcode);

        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        {
            node->onoff = param->status_cb.onoff_status.present_onoff;
            LOG_INFO(TAG, "[Ack] OnOff set: 0x%02x", node->onoff);
        }
        break;
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
        {
            node->level = param->status_cb.level_status.present_level;
            LOG_INFO(TAG, "[Ack] Level Set: %d", param->status_cb.level_status.present_level);
        }
        break;
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
        LOG_INFO(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT");
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        /* If failed to receive the responses, these messages will be resend */

        message_queue().handle_timeout(node, opcode);
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET:
        {
            LOG_WARN(TAG, "TIMEOUT: OnOFF Get");

            break;
        }
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        {
            node->onoff = param->status_cb.onoff_status.present_onoff;
            LOG_WARN(TAG, "TIMEOUT: OnOFF Set: 0x%02x", node->onoff);

            break;
        }
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
        {
            LOG_INFO(TAG, "TIMEOUT : Level Set");
        }
        break;
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_GET:
        {
            LOG_INFO(TAG, "TIMEOUT : Level Get");
        }
        break;
        default:
            break;
        }
        break;
    default:
        LOG_ERROR(TAG, "Not a generic client status message event");
        break;
    }
}

void ble_mesh_light_client_cb(esp_ble_mesh_light_client_cb_event_t event,
                              esp_ble_mesh_light_client_cb_param_t *param)
{
    uint32_t opcode = param->params->opcode;
    uint16_t addr = param->params->ctx.addr;

    LOG_INFO(TAG, "error_code = 0x%02x, event = 0x%02x, addr: 0x%04x, opcode: 0x%04" PRIx32,
             param->error_code, event, param->params->ctx.addr, opcode);

    if (param->error_code)
    {
        LOG_ERROR(TAG, "Send light client message failed, opcode 0x%04" PRIx32, opcode);
        return;
    }

    auto node = node_manager().get_node(addr);
    if (!node)
    {
        // Not a node we provisioned — maybe a discovery reply (see matching comment
        // in ble_mesh_generic_client_cb re: PUBLISH_EVT vs GET/SET_STATE_EVT).
        if ((event == ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_GET) ||
            (event == ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET) ||
            (event == ESP_BLE_MESH_LIGHT_CLIENT_PUBLISH_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_STATUS))
        {
            upsert_external_node_lightness(addr, param->status_cb.lightness_status.present_lightness);
        }
        else if ((event == ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET) ||
                 (event == ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET) ||
                 (event == ESP_BLE_MESH_LIGHT_CLIENT_PUBLISH_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS))
        {
            upsert_external_node_hsl(addr, param->status_cb.hsl_status.hsl_hue,
                                      param->status_cb.hsl_status.hsl_saturation,
                                      param->status_cb.hsl_status.hsl_lightness);
        }
        else if ((event == ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET) ||
                 (event == ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET) ||
                 (event == ESP_BLE_MESH_LIGHT_CLIENT_PUBLISH_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_STATUS))
        {
            upsert_external_node_ctl(addr, param->status_cb.ctl_status.present_ctl_temperature,
                                      param->status_cb.ctl_status.present_ctl_lightness);
        }
        // Range Get replies: always unicast (see send_external_*_range_get), so only
        // ever arrive as a GET_STATE_EVT ack, never PUBLISH_EVT.
        else if (event == ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET)
        {
            upsert_external_node_lightness_range(addr, param->status_cb.lightness_range_status.range_min,
                                                  param->status_cb.lightness_range_status.range_max);
        }
        else if (event == ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET)
        {
            upsert_external_node_hsl_range(addr, param->status_cb.hsl_range_status.hue_range_min,
                                            param->status_cb.hsl_range_status.hue_range_max,
                                            param->status_cb.hsl_range_status.saturation_range_min,
                                            param->status_cb.hsl_range_status.saturation_range_max);
        }
        else if (event == ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT && opcode == ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET)
        {
            upsert_external_node_ctl_range(addr, param->status_cb.ctl_temperature_range_status.range_min,
                                            param->status_cb.ctl_temperature_range_status.range_max);
        }
        return;
    }

    switch (event)
    {
    case ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT:
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET:
        {
            node->hsl_h = param->status_cb.hsl_status.hsl_hue;
            node->hsl_l = param->status_cb.hsl_status.hsl_lightness;
            node->hsl_s = param->status_cb.hsl_status.hsl_saturation;
            LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS h=%d s=%d l=%d", node->hsl_h, node->hsl_s, node->hsl_l);
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET:
        {
            // auto bla = param->status_cb.hsl_range_status.hue_range_min;
            LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS hue_min=%u hue_max=%u sat_min=%u sat_max=%u",
                     param->status_cb.hsl_range_status.hue_range_min, param->status_cb.hsl_range_status.hue_range_max,
                     param->status_cb.hsl_range_status.saturation_range_min, param->status_cb.hsl_range_status.saturation_range_max);

            node->min_hue = param->status_cb.hsl_range_status.hue_range_min;
            node->max_hue = param->status_cb.hsl_range_status.hue_range_max;
            node->min_saturation = param->status_cb.hsl_range_status.saturation_range_min;
            node->max_saturation = param->status_cb.hsl_range_status.saturation_range_max;
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_GET:
        {
            LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_GET present_lightness=%u target_lightness=%u remain_time=%u",
                     param->status_cb.lightness_status.present_lightness,
                     param->status_cb.lightness_status.target_lightness,
                     param->status_cb.lightness_status.remain_time);
            node->hsl_l = param->status_cb.lightness_status.present_lightness;
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET:
        {
            // auto bla = param->status_cb.hsl_range_status.hue_range_min;
            LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET lightness_min=%u lightness_max=%u",
                     param->status_cb.lightness_range_status.range_min, param->status_cb.lightness_range_status.range_max);

            node->min_lightness = param->status_cb.lightness_range_status.range_min;
            node->max_lightness = param->status_cb.lightness_range_status.range_max;
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET:
        {
            LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET temp=%u delta_uv=%u",
                     param->status_cb.ctl_temperature_status.present_ctl_temperature, param->status_cb.ctl_temperature_status.present_ctl_delta_uv);

            node->curr_temp = param->status_cb.ctl_temperature_status.present_ctl_temperature;
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET:
        {
            LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET range_min=%u range_max=%u",
                     param->status_cb.ctl_temperature_range_status.range_min, param->status_cb.ctl_temperature_range_status.range_max);

            node->min_temp = param->status_cb.ctl_temperature_range_status.range_min;
            node->max_temp = param->status_cb.ctl_temperature_range_status.range_max;
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET:
        {
            LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET present_ctl_lightness=%u present_ctl_temperature=%u remain_time=%u target_ctl_lightness=%u target_ctl_temperature=%u",
                     param->status_cb.ctl_status.present_ctl_lightness,
                     param->status_cb.ctl_status.present_ctl_temperature,
                     param->status_cb.ctl_status.remain_time,
                     param->status_cb.ctl_status.target_ctl_lightness,
                     param->status_cb.ctl_status.target_ctl_temperature);
        }
        break;

        default:
            break;
        }

        message_queue().handle_ack(node, opcode);
        break;

    case ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT:
        if (param->error_code != 0)
        {
            LOG_INFO("[ble_mesh_light_client_cb] [ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT]", "Set state response: Error Code : %i", param->error_code);
        }

        message_queue().handle_ack(node, opcode);

        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET:
        {
            // node->hsl_h = param->status_cb.hsl_status.hsl_hue;
            // node->hsl_l = param->status_cb.hsl_status.hsl_lightness;
            // node->hsl_s = param->status_cb.hsl_status.hsl_saturation;
            // LOG_INFO(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS h=%d s=%d l=%d", node->hsl_h, node->hsl_s ,node->hsl_l);
        }
        break;
        }
        break;

    case ESP_BLE_MESH_LIGHT_CLIENT_PUBLISH_EVT:
        LOG_INFO("LIGHT_CLI", "Publish received");
        break;

    case ESP_BLE_MESH_LIGHT_CLIENT_TIMEOUT_EVT:
        LOG_ERROR("LIGHT_CLI", "Timeout event received for opcode 0x%04" PRIx32, opcode);

        message_queue().handle_timeout(node, opcode);
        break;

    default:
        break;
    }
}
bool enable_provisioning = true;
bool enable_auto_provisioning = false;

static const uint16_t GROUP_SUBSCRIBABLE_MODELS[] = {
    ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI,
    ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_CLI,
    ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_CLI,
    ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_CLI,
    ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_CLI,
};

void ble_mesh_subscribe_group_addr(uint16_t group_addr)
{
    if (group_addr == 0) return;

    for (size_t i = 0; i < sizeof(GROUP_SUBSCRIBABLE_MODELS) / sizeof(GROUP_SUBSCRIBABLE_MODELS[0]); i++) {
        esp_err_t err = esp_ble_mesh_model_subscribe_group_addr(
            local_element_addr, ESP_BLE_MESH_CID_NVAL, GROUP_SUBSCRIBABLE_MODELS[i], group_addr);
        if (err != ESP_OK) {
            LOG_WARN(TAG, "Failed to subscribe model 0x%04X to group 0x%04X: %s",
                     GROUP_SUBSCRIBABLE_MODELS[i], group_addr, esp_err_to_name(err));
        } else {
            LOG_INFO(TAG, "Subscribed model 0x%04X to group 0x%04X", GROUP_SUBSCRIBABLE_MODELS[i], group_addr);
        }
    }
}

void ble_mesh_unsubscribe_group_addr(uint16_t group_addr)
{
    if (group_addr == 0) return;

    for (size_t i = 0; i < sizeof(GROUP_SUBSCRIBABLE_MODELS) / sizeof(GROUP_SUBSCRIBABLE_MODELS[0]); i++) {
        esp_err_t err = esp_ble_mesh_model_unsubscribe_group_addr(
            local_element_addr, ESP_BLE_MESH_CID_NVAL, GROUP_SUBSCRIBABLE_MODELS[i], group_addr);
        if (err != ESP_OK) {
            LOG_WARN(TAG, "Failed to unsubscribe model 0x%04X from group 0x%04X: %s",
                     GROUP_SUBSCRIBABLE_MODELS[i], group_addr, esp_err_to_name(err));
        } else {
            LOG_INFO(TAG, "Unsubscribed model 0x%04X from group 0x%04X", GROUP_SUBSCRIBABLE_MODELS[i], group_addr);
        }
    }
}

static void ble_mesh_subscribe_all_configured_groups(void)
{
    uint16_t group_addrs[MESH_MAX_GROUP_ADDRS] = {0};
    uint8_t count = 0;
    mesh_config_load_group_addrs(group_addrs, MESH_MAX_GROUP_ADDRS, &count);
    for (uint8_t i = 0; i < count; i++) {
        ble_mesh_subscribe_group_addr(group_addrs[i]);
    }
}

static void ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                       esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT)
    {
        return;
    }

    // Join-existing-as-node: AppKey Add tells us which app_idx to use. Model App Bind
    // is a separate step the provisioner sends per model — until it lands, sends fail
    // with "Model not bound to AppKey".
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD)
    {
        uint16_t app_idx = param->value.state_change.appkey_add.app_idx;
        LOG_INFO(TAG, "Config AppKey Add received: net_idx 0x%04x, app_idx 0x%04x",
                 param->value.state_change.appkey_add.net_idx, app_idx);
        store.app_idx = app_idx;
        mesh_config_save_node_app_idx(app_idx);
    }
    else if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND)
    {
        LOG_INFO(TAG, "Config Model App Bind received: element 0x%04x, app_idx 0x%04x, company 0x%04x, model 0x%04x",
                 param->value.state_change.mod_app_bind.element_addr,
                 param->value.state_change.mod_app_bind.app_idx,
                 param->value.state_change.mod_app_bind.company_id,
                 param->value.state_change.mod_app_bind.model_id);
    }
}

#ifdef CONFIG_BLE_MESH_PROVISIONER
esp_err_t ble_mesh_apply_local_app_key(const uint8_t app_key[16])
{
    // add_local_app_key()/update_local_app_key() both go through the async BTC queue and
    // return ESP_OK as soon as the request is queued, regardless of whether the stack
    // actually accepts it — so a failed add (AppKeyIndex already in use, from any earlier
    // boot) can never be detected from its return value to fall back to update. Check
    // whether the index already holds a key first (a synchronous local lookup) instead.
    bool app_key_exists = esp_ble_mesh_provisioner_get_local_app_key(store.net_idx, store.app_idx) != NULL;
    return app_key_exists
               ? esp_ble_mesh_provisioner_update_local_app_key(app_key, store.net_idx, store.app_idx)
               : esp_ble_mesh_provisioner_add_local_app_key(app_key, store.net_idx, store.app_idx);
}
#endif // CONFIG_BLE_MESH_PROVISIONER

esp_err_t ble_mesh_init(void)
{
    ble_mesh_get_dev_uuid(dev_uuid);

    esp_err_t err = ESP_OK;

    mesh_config_t mesh_cfg = {};
    mesh_config_load(&mesh_cfg);

    store.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    store.app_idx = APP_KEY_IDX;
    memcpy(prov_key.app_key, mesh_cfg.app_key, sizeof(prov_key.app_key));

    esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(ble_mesh_config_server_cb);
    esp_ble_mesh_register_config_client_callback(ble_mesh_config_client_cb);
    esp_ble_mesh_register_generic_client_callback(ble_mesh_generic_client_cb);
    esp_ble_mesh_register_light_client_callback(ble_mesh_light_client_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to initialize mesh stack (err %d)", err);
        return err;
    }

    if (mesh_cfg.mode == MESH_MODE_JOIN_EXISTING)
    {
#ifdef CONFIG_BLE_MESH_NODE
        // Join as a real node (provisioned by nRF Mesh etc.) rather than self-assigning
        // an address — avoids the address collisions from issue #40. AppKey arrives
        // later via Config AppKey Add.
        local_element_addr = mesh_cfg.node_addr;
        store.net_idx = mesh_cfg.node_net_idx;
        store.app_idx = mesh_cfg.node_app_idx;

        err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Failed to enable node provisioning (err %d)", err);
            return err;
        }

        ble_mesh_subscribe_all_configured_groups();

        LOG_INFO(TAG, "BLE Mesh Node ready (mode=join_existing, addr=0x%04X)%s",
                 local_element_addr, local_element_addr == 0 ? " — not yet provisioned" : "");
        return ESP_OK;
#else
        // Mirror of the Node-only guard below: Node role isn't compiled into this SKU.
        LOG_ERROR(TAG, "Join-existing mode requested but this firmware is a Provisioner-only build (CONFIG_BLE_MESH_NODE disabled)");
        return ESP_ERR_NOT_SUPPORTED;
#endif // CONFIG_BLE_MESH_NODE
    }

#ifdef CONFIG_BLE_MESH_PROVISIONER
    local_element_addr = PROV_OWN_ADDR;
    err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to enable mesh provisioner (err %d)", err);
        return err;
    }

    err = ble_mesh_apply_local_app_key(prov_key.app_key);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to set AppKey (err %d)", err);
        return err;
    }

    ble_mesh_subscribe_all_configured_groups();

    LOG_INFO(TAG, "BLE Mesh Provisioner initialized (mode=standalone)");

    return err;
#else
    // Last-resort guard: this Node-only build can't do standalone mode at all.
    LOG_ERROR(TAG, "Standalone provisioner mode requested but this firmware is a Node-only build (CONFIG_BLE_MESH_PROVISIONER disabled)");
    return ESP_ERR_NOT_SUPPORTED;
#endif // CONFIG_BLE_MESH_PROVISIONER
}

bool ble_mesh_get_local_keys_hex(char *net_key_hex, size_t net_key_hex_len,
                                  char *app_key_hex, size_t app_key_hex_len)
{
    net_key_hex[0] = '\0';
    app_key_hex[0] = '\0';

    // Node role (join-existing) and Provisioner role (standalone) each keep their own
    // local key tables — pick the getter matching current mode.
    mesh_config_t mesh_cfg = {};
    mesh_config_load(&mesh_cfg);
    bool is_node = mesh_cfg.mode == MESH_MODE_JOIN_EXISTING;

    // Each getter only exists in a build with its role compiled in.
    const uint8_t *net_key = NULL;
    const uint8_t *app_key = NULL;
#ifdef CONFIG_BLE_MESH_NODE
    if (is_node)
    {
        net_key = esp_ble_mesh_node_get_local_net_key(store.net_idx);
        app_key = esp_ble_mesh_node_get_local_app_key(store.app_idx);
    }
#endif
#ifdef CONFIG_BLE_MESH_PROVISIONER
    if (!is_node)
    {
        net_key = esp_ble_mesh_provisioner_get_local_net_key(store.net_idx);
        app_key = esp_ble_mesh_provisioner_get_local_app_key(store.net_idx, store.app_idx);
    }
#endif
    if (net_key == NULL || app_key == NULL)
    {
        return false;
    }
    strlcpy(net_key_hex, bt_hex(net_key, 16), net_key_hex_len);
    strlcpy(app_key_hex, bt_hex(app_key, 16), app_key_hex_len);
    return true;
}

void for_each_external_node(std::function<void(const external_mesh_node_t &)> func)
{
    std::lock_guard<std::mutex> lock(external_nodes_mutex);
    for (const auto &n : external_nodes)
    {
        func(n);
    }
}

// Composition Data Get needs a DevKey we don't have for external nodes, so probe each
// model type instead and see who answers (see the client callbacks' unknown-node paths).
static esp_err_t send_group_get(esp_ble_mesh_model_t *model, uint32_t opcode, uint16_t group_addr, bool is_light)
{
    esp_ble_mesh_client_common_param_t common = {0};
    common.opcode = opcode;
    common.model = model;
    common.ctx.net_idx = store.net_idx;
    common.ctx.app_idx = store.app_idx;
    common.ctx.addr = group_addr;
    common.ctx.send_ttl = MSG_SEND_TTL;
    common.msg_timeout = MSG_TIMEOUT;

    if (is_light)
    {
        esp_ble_mesh_light_client_get_state_t get_state = {0};
        return esp_ble_mesh_light_client_get_state(&common, &get_state);
    }
    esp_ble_mesh_generic_client_get_state_t get_state = {0};
    return esp_ble_mesh_generic_client_get_state(&common, &get_state);
}

// Serialized via external_node_queue (see external_node_queue.h). Lightness/HSL/CTL
// are queued before Level so upsert_external_node_level already knows to exclude them.
static void queue_discovery_burst_for_group(uint16_t group_addr)
{
    external_node_queue().enqueue([group_addr]()
                                   {
        esp_err_t err = send_group_get(onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET, group_addr, false);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Failed to send OnOff discovery Get to group 0x%04X (err %d)", group_addr, err);
        } });
    external_node_queue().enqueue([group_addr]()
                                   {
        esp_err_t err = send_group_get(lightness_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_GET, group_addr, true);
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Failed to send Lightness discovery Get to group 0x%04X (err %d)", group_addr, err);
        } });
    external_node_queue().enqueue([group_addr]()
                                   {
        esp_err_t err = send_group_get(hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET, group_addr, true);
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Failed to send HSL discovery Get to group 0x%04X (err %d)", group_addr, err);
        } });
    external_node_queue().enqueue([group_addr]()
                                   {
        esp_err_t err = send_group_get(ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET, group_addr, true);
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Failed to send CTL discovery Get to group 0x%04X (err %d)", group_addr, err);
        } });
    external_node_queue().enqueue([group_addr]()
                                   {
        esp_err_t err = send_group_get(level_client.model, ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_GET, group_addr, false);
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Failed to send Level discovery Get to group 0x%04X (err %d)", group_addr, err);
        } });

    LOG_INFO(TAG, "Queued external node discovery Gets to group 0x%04X", group_addr);
}

esp_err_t ble_mesh_discover_external_nodes()
{
    uint16_t group_addrs[MESH_MAX_GROUP_ADDRS] = {0};
    uint8_t count = 0;
    mesh_config_load_group_addrs(group_addrs, MESH_MAX_GROUP_ADDRS, &count);
    if (count == 0)
    {
        LOG_WARN(TAG, "Cannot discover external nodes: no group address configured");
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < count; i++)
    {
        queue_discovery_burst_for_group(group_addrs[i]);
    }
    return ESP_OK;
}

esp_err_t ble_mesh_send_external_command(uint16_t addr, bool onoff)
{
    if (addr == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Queued (external_node_queue.h) — caller only gets "accepted", not a send result.
    external_node_queue().enqueue([addr, onoff]()
                                   {
        // Group Sets should be unacknowledged (Mesh spec) since multiple elements may reply.
        bool unicast = ESP_BLE_MESH_ADDR_IS_UNICAST(addr);

        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_generic_client_set_state_t set_state = {0};
        common.opcode = unicast ? ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET : ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
        common.model = onoff_client.model;
        common.ctx.net_idx = store.net_idx;
        common.ctx.app_idx = store.app_idx;
        common.ctx.addr = addr;
        common.ctx.send_ttl = MSG_SEND_TTL;
        common.msg_timeout = MSG_TIMEOUT;

        set_state.onoff_set.op_en = false;
        set_state.onoff_set.onoff = onoff ? 1 : 0;
        set_state.onoff_set.tid = store.tid++;

        esp_err_t err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Queued external OnOff Set to 0x%04X failed (err %d)", addr, err);
        } });

    return ESP_OK;
}

esp_err_t ble_mesh_send_external_level_command(uint16_t addr, int16_t level)
{
    if (addr == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    external_node_queue().enqueue([addr, level]()
                                   {
        bool unicast = ESP_BLE_MESH_ADDR_IS_UNICAST(addr);

        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_generic_client_set_state_t set_state = {0};
        common.opcode = unicast ? ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET : ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET_UNACK;
        common.model = level_client.model;
        common.ctx.net_idx = store.net_idx;
        common.ctx.app_idx = store.app_idx;
        common.ctx.addr = addr;
        common.ctx.send_ttl = MSG_SEND_TTL;
        common.msg_timeout = MSG_TIMEOUT;

        set_state.level_set.op_en = false;
        set_state.level_set.level = level;
        set_state.level_set.tid = store.tid++;

        esp_err_t err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Queued external Level Set to 0x%04X failed (err %d)", addr, err);
        } });

    return ESP_OK;
}

esp_err_t ble_mesh_send_external_lightness_command(uint16_t addr, uint16_t lightness)
{
    if (addr == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    external_node_queue().enqueue([addr, lightness]()
                                   {
        bool unicast = ESP_BLE_MESH_ADDR_IS_UNICAST(addr);

        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state = {0};
        common.opcode = unicast ? ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET : ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET_UNACK;
        common.model = lightness_cli.model;
        common.ctx.net_idx = store.net_idx;
        common.ctx.app_idx = store.app_idx;
        common.ctx.addr = addr;
        common.ctx.send_ttl = MSG_SEND_TTL;
        common.msg_timeout = MSG_TIMEOUT;

        set_state.lightness_set.op_en = false;
        set_state.lightness_set.lightness = lightness;
        set_state.lightness_set.tid = store.tid++;

        esp_err_t err = esp_ble_mesh_light_client_set_state(&common, &set_state);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Queued external Lightness Set to 0x%04X failed (err %d)", addr, err);
        } });

    return ESP_OK;
}

esp_err_t ble_mesh_send_external_hsl_command(uint16_t addr, uint16_t hue, uint16_t saturation, uint16_t lightness)
{
    if (addr == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    external_mesh_node_t node{};
    ble_mesh_find_external_node(addr, node); // max_lightness keeps its 65535 default if unknown

    external_node_queue().enqueue([addr, hue, saturation, lightness, max_lightness = node.max_lightness]()
                                   {
        bool unicast = ESP_BLE_MESH_ADDR_IS_UNICAST(addr);

        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state = {0};
        common.opcode = unicast ? ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET : ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET_UNACK;
        common.model = hsl_cli.model;
        common.ctx.net_idx = store.net_idx;
        common.ctx.app_idx = store.app_idx;
        common.ctx.addr = addr;
        common.ctx.send_ttl = MSG_SEND_TTL;
        common.msg_timeout = MSG_TIMEOUT;

        set_state.hsl_set.hsl_hue = hue;
        set_state.hsl_set.hsl_saturation = saturation;
        // Same scaling as ble_mesh_light_hsl_set (ble_mesh_commands.cpp): L=0.5 of max is
        // the pure saturated color in HSL space.
        set_state.hsl_set.hsl_lightness = max_lightness > 0
            ? (uint16_t)((uint32_t)lightness * (max_lightness / 2) / max_lightness)
            : lightness;
        set_state.hsl_set.op_en = false;
        set_state.hsl_set.delay = 0;
        set_state.hsl_set.tid = store.tid++;

        esp_err_t err = esp_ble_mesh_light_client_set_state(&common, &set_state);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Queued external HSL Set to 0x%04X failed (err %d)", addr, err);
        } });

    return ESP_OK;
}

esp_err_t ble_mesh_send_external_ctl_command(uint16_t addr, uint16_t temperature, uint16_t lightness)
{
    if (addr == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    external_node_queue().enqueue([addr, temperature, lightness]()
                                   {
        bool unicast = ESP_BLE_MESH_ADDR_IS_UNICAST(addr);

        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state = {0};
        common.opcode = unicast ? ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET : ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET_UNACK;
        common.model = ctl_cli.model;
        common.ctx.net_idx = store.net_idx;
        common.ctx.app_idx = store.app_idx;
        common.ctx.addr = addr;
        common.ctx.send_ttl = MSG_SEND_TTL;
        common.msg_timeout = MSG_TIMEOUT;

        set_state.ctl_set.ctl_temperature = temperature;
        set_state.ctl_set.ctl_lightness = lightness;
        set_state.ctl_set.ctl_delta_uv = 0;
        set_state.ctl_set.op_en = false;
        set_state.ctl_set.delay = 0;
        set_state.ctl_set.tid = store.tid++;

        esp_err_t err = esp_ble_mesh_light_client_set_state(&common, &set_state);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Queued external CTL Set to 0x%04X failed (err %d)", addr, err);
        } });

    return ESP_OK;
}

// Range Get carries no payload — just the opcode — same as the provisioned-node
// versions (ble_mesh_commands.cpp's ble_mesh_lightness_range_get and friends).
static void send_external_lightness_range_get(uint16_t addr)
{
    external_node_queue().enqueue([addr]()
                                   {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state = {0};
        common.opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET;
        common.model = lightness_cli.model;
        common.ctx.net_idx = store.net_idx;
        common.ctx.app_idx = store.app_idx;
        common.ctx.addr = addr;
        common.ctx.send_ttl = MSG_SEND_TTL;
        common.msg_timeout = MSG_TIMEOUT;

        esp_err_t err = esp_ble_mesh_light_client_get_state(&common, &get_state);
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Queued external Lightness Range Get to 0x%04X failed (err %d)", addr, err);
        } });
}

static void send_external_hsl_range_get(uint16_t addr)
{
    external_node_queue().enqueue([addr]()
                                   {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state = {0};
        common.opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET;
        common.model = hsl_cli.model;
        common.ctx.net_idx = store.net_idx;
        common.ctx.app_idx = store.app_idx;
        common.ctx.addr = addr;
        common.ctx.send_ttl = MSG_SEND_TTL;
        common.msg_timeout = MSG_TIMEOUT;

        esp_err_t err = esp_ble_mesh_light_client_get_state(&common, &get_state);
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Queued external HSL Range Get to 0x%04X failed (err %d)", addr, err);
        } });
}

static void send_external_ctl_temperature_range_get(uint16_t addr)
{
    external_node_queue().enqueue([addr]()
                                   {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state = {0};
        common.opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET;
        common.model = ctl_cli.model;
        common.ctx.net_idx = store.net_idx;
        common.ctx.app_idx = store.app_idx;
        common.ctx.addr = addr;
        common.ctx.send_ttl = MSG_SEND_TTL;
        common.msg_timeout = MSG_TIMEOUT;

        esp_err_t err = esp_ble_mesh_light_client_get_state(&common, &get_state);
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Queued external CTL Temperature Range Get to 0x%04X failed (err %d)", addr, err);
        } });
}

void ble_mesh_refresh_all_nodes()
{
    LOG_INFO(TAG, "Refreshing all nodes");
    for_each_provisioned_node([](const esp_ble_mesh_node_t *node, int node_index)
                              {
        if (auto node_info = node_manager().get_or_create(node->dev_uuid))
        {
            ble_mesh_refresh_node(node_info, node);
        } });
}

void ble_mesh_refresh_node(const std::shared_ptr<bm2mqtt_node_info>& node_info, const esp_ble_mesh_node_t *node)
{
    LOG_INFO(TAG, "Refreshing node 0x%04X", node_info->unicast);
    if (node != nullptr)
    {
        node_info->uuid = device_uuid128{node->dev_uuid};
        node_info->unicast = node->unicast_addr;
        node_info->elem_num = node->element_num;
    }

    if (node_info->features & FEATURE_GENERIC_ONOFF)
    {
        LOG_INFO(TAG, "Refreshing ON/OFF for node 0x%04X", node_info->unicast);

        message_queue().enqueue(node_info, message_payload{
                                               .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info) // shared_ptr captured by value, keeps node alive
                                               {
                                                   LOG_WARN(TAG, "Generic on/off model for node 0x%04X", node_info->unicast);
                                                   esp_ble_mesh_client_common_param_t common = {0};
                                                   esp_ble_mesh_generic_client_get_state_t get_state = {0};
                                                   node_manager().ble_mesh_set_msg_common(&common, node_info, onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET);
                                                   int err = esp_ble_mesh_generic_client_get_state(&common, &get_state);
                                                   if (err)
                                                   {
                                                       LOG_ERROR(TAG, "Generic OnOff Get failed");
                                                       return;
                                                   }
                                               },
                                               .opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET,
                                               .retries_left = 3,
                                           });
    }

    if (node_info->features & FEATURE_LIGHT_HSL)
    {
        {
            message_queue().enqueue(node_info, message_payload{
                                                   .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info)
                                                   {
                                                       LOG_WARN(TAG, "Light HSL model for node 0x%04X", node_info->unicast);
                                                       esp_ble_mesh_client_common_param_t common = {0};
                                                       esp_ble_mesh_light_client_get_state_t get_state_light = {0};
                                                       node_manager().ble_mesh_set_msg_common(&common, node_info, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET);
                                                       int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
                                                       if (err)
                                                       {
                                                           LOG_ERROR(TAG, "Refreshing HSL failed");
                                                           return;
                                                       }
                                                   },
                                                   .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET,
                                                   .retries_left = 3,
                                               });
        }
        {
            message_queue().enqueue(node_info, message_payload{
                                                   .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info)
                                                   {
                                                       LOG_WARN(TAG, "HSL range for node 0x%04X", node_info->unicast);
                                                       ble_mesh_hsl_range_get(node_info);
                                                   },
                                                   .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET,
                                                   .retries_left = 3,
                                               });
        }
    }

    if (node_info->features & FEATURE_LIGHT_LIGHTNESS)
    {
        message_queue().enqueue(node_info, message_payload{
                                               .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info)
                                               {
                                                   LOG_WARN(TAG, "Light lightness model for node 0x%04X", node_info->unicast);
                                                   esp_ble_mesh_client_common_param_t common = {0};
                                                   esp_ble_mesh_light_client_get_state_t get_state_light = {0};
                                                   node_manager().ble_mesh_set_msg_common(&common, node_info, lightness_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_GET);
                                                   int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
                                                   if (err)
                                                   {
                                                       LOG_ERROR(TAG, "Refreshing Lightness failed");
                                                       return;
                                                   }
                                               },
                                               .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_GET,
                                               .retries_left = 3,
                                           });
        message_queue().enqueue(node_info, message_payload{
                                               .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info)
                                               {
                                                   LOG_WARN(TAG, "Light lightness range for node 0x%04X", node_info->unicast);
                                                   ble_mesh_lightness_range_get(node_info);
                                               },
                                               .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET,
                                               .retries_left = 3,
                                           });
    }

    if (node_info->features & FEATURE_LIGHT_CTL)
    {
        message_queue().enqueue(node_info, message_payload{
                                               .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info) // shared_ptr captured by value, keeps node alive
                                               {
                                                   LOG_WARN(TAG, "Refreshing Temperature Range for node 0x%04X", node_info->unicast);
                                                   ble_mesh_ctl_temperature_range_get(node_info);
                                               },
                                               .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET,
                                               .retries_left = 3,
                                           });

        message_queue().enqueue(node_info, message_payload{
                                               .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info) // shared_ptr captured by value, keeps node alive
                                               {
                                                   LOG_INFO(TAG, "Refreshing CTL Temperature for node 0x%04X", node_info->unicast);

                                                   ble_mesh_ctl_temperature_get(node_info);
                                               },
                                               .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET,
                                               .retries_left = 3,
                                           });
    }

    message_queue().enqueue(node_info, message_payload{
                                           .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info)
                                           {
                                               mqtt_node_send_status(node_info);
                                           },
                                           .opcode = 0x0000, // No specific opcode, just a marker
                                           .retries_left = 0,
                                           .type = message_type_t::mqtt_message, // Indicate this is a MQTT message
                                       });
}

typedef struct
{
    struct arg_int *truefalse;
    struct arg_end *end;
} ble_mesh_ctl_bool_set_args_t;
ble_mesh_ctl_bool_set_args_t ctl_bool_set_args;

void ble_mesh_set_provisioning_enabled(bool enabled_value)
{
#ifdef CONFIG_BLE_MESH_PROVISIONER
    LOG_INFO(TAG, "Current Value : %s Requested Value : %s", enable_provisioning ? "ON" : "OFF", enabled_value ? "ON" : "OFF");
    if (enabled_value != enable_provisioning)
    {
        enable_provisioning = enabled_value;

        if (enable_provisioning)
        {
            int err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
            if (err != ESP_OK)
            {
                LOG_ERROR(TAG, "Failed to enable PB-ADV | PB-GATT provisioning (err %d)", err);
            }
            else
            {
                LOG_INFO(TAG, "PB-ADV | PB-GATT provisioning enabled");
            }
            mqtt_publish_provisioning_enabled(enable_provisioning);
        }
        else if (!enable_provisioning)
        {
            int err = esp_ble_mesh_provisioner_prov_disable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
            if (err != ESP_OK)
            {
                LOG_ERROR(TAG, "Failed to disable PB-ADV | PB-GATT provisioning (err %d)", err);
            }
            else
            {
                LOG_INFO(TAG, "PB-ADV | PB-GATT provisioning disabled");
            }
            mqtt_publish_provisioning_enabled(enable_provisioning);
        }
    }
#else
    // No local provisioner role to toggle in a Node-only build.
    (void)enabled_value;
    LOG_WARN(TAG, "ble_mesh_set_provisioning_enabled() ignored — this is a Node-only build");
#endif // CONFIG_BLE_MESH_PROVISIONER
}

bool ble_mesh_get_provisioning_enabled(void)
{
    return enable_provisioning;
}

void ble_mesh_set_auto_provisioning_enabled(bool enabled_value)
{
    LOG_INFO(TAG, "Auto-provisioning: Current Value : %s Requested Value : %s", enable_auto_provisioning ? "ON" : "OFF", enabled_value ? "ON" : "OFF");
    if (enabled_value != enable_auto_provisioning)
    {
        enable_auto_provisioning = enabled_value;
        LOG_INFO(TAG, "Auto-provisioning %s", enable_auto_provisioning ? "enabled" : "disabled");
        
        // Publish the change to MQTT
        mqtt_publish_auto_provisioning_enabled(enable_auto_provisioning);
    }
}

bool ble_mesh_get_auto_provisioning_enabled(void)
{
    return enable_auto_provisioning;
}

void ble_mesh_republish_all_nodes_to_mqtt(void)
{
    LOG_INFO(TAG, "Republishing all nodes to MQTT");
    node_manager().for_each_node([](std::shared_ptr<bm2mqtt_node_info>& node)
    {
        message_queue().enqueue(node, message_payload{
            .send = [](std::shared_ptr<bm2mqtt_node_info> &node)
            {
                mqtt_subscribe_node(mqtt_get_client(), node);
                mqtt_send_discovery(node);
                mqtt_node_send_status(node);
            },
            .opcode = 0x0000,
            .retries_left = 0,
            .type = message_type_t::mqtt_message,
        });
    });
}

int ble_mesh_set_provisioning_enabled(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ctl_bool_set_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, ctl_bool_set_args.end, argv[0]);
        return 1;
    }

    bool enabled_value = ctl_bool_set_args.truefalse->ival[0] != 0;
    ble_mesh_set_provisioning_enabled(enabled_value);
    return 0;
}

int print_registered_nodes(int argc, char **argv)
{
    node_manager().print_registered_nodes();
    return 0;
}

void register_blemesh_commands()
{
    /* Register commands */

    ctl_bool_set_args.truefalse = arg_int1("v", "value", "<true_false>", "0 for false, anything else for true");
    ctl_bool_set_args.end = arg_end(2);

    const esp_console_cmd_t ble_mesh_toggle_provisioning_cmd = {
        .command = "ble_mesh_set_provisioning_enabled",
        .help = "Toggle the provisioning functionality",
        .hint = NULL,
        .func = &ble_mesh_set_provisioning_enabled,
        .argtable = &ctl_bool_set_args,
    };
    ESP_ERROR_CHECK(register_console_command(&ble_mesh_toggle_provisioning_cmd));

    const esp_console_cmd_t print_nodes_cmd = {
        .command = "list_registered_devices",
        .help = "List all registered devices in the node manager",
        .hint = NULL,
        .func = &print_registered_nodes,
        .argtable = &ctl_bool_set_args,
    };
    ESP_ERROR_CHECK(register_console_command(&print_nodes_cmd));
}

REGISTER_DEBUG_COMMAND(register_blemesh_commands);