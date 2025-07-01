#include "ble_mesh_node.h"
#include <inttypes.h>
#include <string>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_example_init.h"

#include "ble_mesh_provisioning.h"

#define TAG "NODE_MANAGER"

constexpr uint32_t NODE_INFO_SCHEMA_VERSION = 1;

ble2mqtt_node_manager &node_manager()
{
    static ble2mqtt_node_manager instance;
    return instance;
}

extern struct example_info_store store;

void ble2mqtt_node_manager::for_each_node(std::function<void(const bm2mqtt_node_info *)> func)
{
    for (auto i = 0; i < tracked_nodes.size(); i++)
    {
        if (tracked_nodes[i].unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
        {
            func(&tracked_nodes[i]);
        }
    }
}

bm2mqtt_node_info *ble2mqtt_node_manager::get_node(const Uuid128& uuid)
{
    for (auto i = 0; i < tracked_nodes.size(); i++)
    {
        if (tracked_nodes[i].uuid == uuid)
        {
            return &tracked_nodes[i];
        }
    }
    return nullptr;
}

bm2mqtt_node_info *ble2mqtt_node_manager::get_or_create(const uint8_t uuid[16])
{
    const Uuid128 inuuid{uuid};
    for (auto i = 0; i < tracked_nodes.size(); i++)
    {
        if (tracked_nodes[i].uuid == inuuid)
        {
            return &tracked_nodes[i];
        }
    }

    tracked_nodes.emplace_back();
    tracked_nodes.back().uuid = inuuid;

    return &tracked_nodes.back();
}

bm2mqtt_node_info *ble2mqtt_node_manager::get_node(int nodeIndex)
{
    return &tracked_nodes[nodeIndex];
}

bm2mqtt_node_info *ble2mqtt_node_manager::get_node(const std::string &mac)
{
    bm2mqtt_node_info *result = nullptr;
    for_each_provisioned_node([&mac, &result, this](const esp_ble_mesh_node_t *node)
                              {
            const std::string inAddr {bt_hex(node->addr, BD_ADDR_LEN)};
            if (inAddr == mac)
            {
                for (auto i = 0; i < tracked_nodes.size(); i++)
                {
                    if (memcmp(tracked_nodes[i].uuid.raw(), node->dev_uuid, 16) == 0)
                    {
                        result =  &tracked_nodes[i];
                    }
                }
            } });

    return result;
}

void ble2mqtt_node_manager::remove_node(const Uuid128& uuid)
{
    for (std::vector<bm2mqtt_node_info>::iterator it = tracked_nodes.begin(); it != tracked_nodes.end();)
    {
        if((it->uuid) == uuid)
        {
            ESP_LOGW(TAG, "%s: Remove unprovisioned device 0x%04x", __func__, it->unicast);
            it = tracked_nodes.erase(it);
            break;
        }
    }

    mark_node_info_dirty();
}

esp_err_t ble2mqtt_node_manager::example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                                                 bm2mqtt_node_info *node,
                                                                 esp_ble_mesh_model_t *model, uint32_t opcode)
{
    if (!common || !node || !model)
    {
        return ESP_ERR_INVALID_ARG;
    }

    common->opcode = opcode;
    common->model = model;
    common->ctx.net_idx = store.net_idx;
    common->ctx.app_idx = store.app_idx;
    common->ctx.addr = node->unicast;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->msg_timeout = MSG_TIMEOUT;

    return ESP_OK;
}

esp_err_t ble2mqtt_node_manager::store_node_info(const Uuid128& uuid, uint16_t unicast,
                                                 uint8_t elem_num, uint8_t onoff_state)
{
    int i;

    if ( !ESP_BLE_MESH_ADDR_IS_UNICAST(unicast))
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* Judge if the device has been provisioned before */
    for (i = 0; i < tracked_nodes.size(); i++)
    {
        if(tracked_nodes[i].uuid != uuid)
        {
            ESP_LOGW(TAG, "%s: reprovisioned device 0x%04x", __func__, unicast);
            tracked_nodes[i].unicast = unicast;
            tracked_nodes[i].elem_num = elem_num;
            tracked_nodes[i].onoff = onoff_state;
            tracked_nodes[i].hsl_h = 16000;
            tracked_nodes[i].hsl_s = 16000;
            tracked_nodes[i].hsl_l = 16000;
            return ESP_OK;
        }
    }

    tracked_nodes.emplace_back();
    tracked_nodes.back().uuid = uuid;
    tracked_nodes.back().unicast = unicast;
    tracked_nodes.back().elem_num = elem_num;
    tracked_nodes.back().onoff = onoff_state;

    save_node_info_vector();

    return ESP_OK;
}

bm2mqtt_node_info *ble2mqtt_node_manager::get_node(uint16_t unicast)
{
    int i;

    if (!ESP_BLE_MESH_ADDR_IS_UNICAST(unicast))
    {
        return nullptr;
    }

    for (i = 0; i < tracked_nodes.size(); i++)
    {
        if (tracked_nodes[i].unicast <= unicast &&
            tracked_nodes[i].unicast + tracked_nodes[i].elem_num > unicast)
        {
            return &tracked_nodes[i];
        }
    }

    return nullptr;
}

void ble2mqtt_node_manager::print_registered_nodes()
{
    ESP_LOGI(TAG, "Provisioned nodes: %d", tracked_nodes.size());
    for (const auto &node : tracked_nodes)
    {
        ESP_LOGI(TAG, "==device uuid: %s", bt_hex(node.uuid.raw(), 16));
        ESP_LOGI(TAG, "  Primary Address: 0x%04X", node.unicast);
        ESP_LOGI(TAG, "  Element Count: %d", node.elem_num);
        ESP_LOGI(TAG, "  On/Off State: %d", node.onoff);
        ESP_LOGI(TAG, "  HSL: H=%d S=%d L=%d", node.hsl_h, node.hsl_s, node.hsl_l);
        ESP_LOGI(TAG, "  Min Temp: %d, Max Temp: %d, Current Temp: %d", node.min_temp, node.max_temp, node.curr_temp);
        ESP_LOGI(TAG, "  Color Mode: %s", node.color_mode == color_mode_t::hs ? "HS" : "Color Temp");
        ESP_LOGI(TAG, "  Features: 0x%04X", node.features);
        ESP_LOGI(TAG, "  Features to Bind: 0x%04X", node.features_to_bind);
    }
}

esp_err_t ble2mqtt_node_manager::save_node_info_vector()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_mesh", NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    nvs_set_u32(handle, "version", NODE_INFO_SCHEMA_VERSION);

    size_t total_size = tracked_nodes.size() * sizeof(bm2mqtt_node_info);
    err = nvs_set_blob(handle, "nodes", tracked_nodes.data(), total_size);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save node info vector to NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t ble2mqtt_node_manager::load_node_info_vector()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_mesh", NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t version;
    if (nvs_get_u32(handle, "version", &version) != ESP_OK || version != NODE_INFO_SCHEMA_VERSION)
    {
        nvs_close(handle);
        ESP_LOGE(TAG, "NVS version mismatch: expected %u, got %u", NODE_INFO_SCHEMA_VERSION, version);
        // Optionally, you could handle the version mismatch by migrating data or resetting.
        // For now, we just return an error.
        return ESP_ERR_INVALID_VERSION;
    }

    size_t size = 0;
    err = nvs_get_blob(handle, "nodes", nullptr, &size);
    if (err != ESP_OK || size % sizeof(bm2mqtt_node_info) != 0)
    {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to load node info vector to NVS: %s", esp_err_to_name(err));
        return err;
    }

    size_t count = size / sizeof(bm2mqtt_node_info);
    tracked_nodes.resize(count);

    err = nvs_get_blob(handle, "nodes", tracked_nodes.data(), &size);
    nvs_close(handle);
    return err;
}

void ble2mqtt_node_manager::save_timer_callback(void *arg)
{
    if (auto *manager = static_cast<ble2mqtt_node_manager *>(arg))
    {
        manager->on_timer_callback();
    }
}

void ble2mqtt_node_manager::on_timer_callback()
{
    if (node_info_dirty)
    {
        ESP_LOGI(TAG, "Saving node info to NVS...");
        save_node_info_vector();
        node_info_dirty = false;
    }
}

void ble2mqtt_node_manager::mark_node_info_dirty()
{
    node_info_dirty = true;
    esp_timer_start_once(save_timer, 10 * 1000000); // 10 seconds in microseconds
}

void ble2mqtt_node_manager::init_node_save_timer()
{
    const esp_timer_create_args_t timer_args = {
        .callback = &save_timer_callback,
        .arg = this,
        .name = "node_save_timer"};
    esp_timer_create(&timer_args, &save_timer);
}

void ble2mqtt_node_manager::Initialize()
{
    load_node_info_vector();
    init_node_save_timer();
}