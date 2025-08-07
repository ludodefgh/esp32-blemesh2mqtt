#include "ble_mesh_node.h"

// Standard C/C++ libraries
#include <inttypes.h>
#include <mutex>
#include <string>

// ESP-IDF includes
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "nvs_flash.h"

// Project includes
#include "ble_mesh_provisioning.h"
#include "common/log_common.h"

#define TAG "NODE_MANAGER"

static std::mutex tn_mutex;

const char *get_color_mode_string(color_mode_t mode)
{
    switch (mode)
    {
    case color_mode_t::brightness:
        return "brightness";
    case color_mode_t::hs:
        return "hs";
    case color_mode_t::color_temp:
        return "color_temp";
    default:
        return "unknown";
    }
}

uint16_t get_node_index(Uuid128 uuid)
{
    for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++)
    {
        const esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_table_entry()[i];
        if (node != nullptr && Uuid128(node->dev_uuid) == uuid)
        {
            return i;
        }
    }

    return std::numeric_limits<uint16_t>::max(); // Return max value if not found
}

ble2mqtt_node_manager &node_manager()
{
    static ble2mqtt_node_manager instance;
    return instance;
}

extern struct example_info_store store;

void ble2mqtt_node_manager::for_each_node(std::function<void(std::shared_ptr<bm2mqtt_node_info>)> func)
{
    std::lock_guard<std::mutex> lock(tn_mutex);
    for (auto& node : tracked_nodes)
    {
        if (node && node->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
        {
            func(node);
        }
    }
}

std::shared_ptr<bm2mqtt_node_info> ble2mqtt_node_manager::get_node(const Uuid128 &uuid)
{
    std::lock_guard<std::mutex> lock(tn_mutex);
    auto it = uuid_index.find(uuid);
    if (it != uuid_index.end())
    {
        return it->second;
    }
    return nullptr;
}
std::shared_ptr<bm2mqtt_node_info> ble2mqtt_node_manager::get_or_create(const Uuid128 &uuid)
{
    std::lock_guard<std::mutex> lock(tn_mutex);
    
    // Check if node already exists
    auto it = uuid_index.find(uuid);
    if (it != uuid_index.end())
    {
        return it->second;
    }

    // Create new node
    auto node = std::make_shared<bm2mqtt_node_info>();
    node->uuid = uuid;
    
    // Add to both containers
    tracked_nodes.push_back(node);
    uuid_index[uuid] = node;

    return node;
}

std::shared_ptr<bm2mqtt_node_info> ble2mqtt_node_manager::get_or_create(const uint8_t uuid[16])
{
    const Uuid128 inuuid{uuid};
    return get_or_create(inuuid);
}

std::shared_ptr<bm2mqtt_node_info> ble2mqtt_node_manager::get_node(int nodeIndex)
{
    std::lock_guard<std::mutex> lock(tn_mutex);
    if (nodeIndex >= 0 && nodeIndex < tracked_nodes.size())
    {
        return tracked_nodes[nodeIndex];
    }
    return nullptr;
}

std::shared_ptr<bm2mqtt_node_info> ble2mqtt_node_manager::get_node(const std::string &mac)
{
    std::shared_ptr<bm2mqtt_node_info> result = nullptr;
    for_each_provisioned_node([&mac, &result, this](const esp_ble_mesh_node_t *node, int node_index)
                              {
            const std::string inAddr {bt_hex(node->addr, BD_ADDR_LEN)};
            if (inAddr == mac)
            {
                const Uuid128 uuid{node->dev_uuid};
                result = get_node(uuid);
            } });

    return result;
}

void ble2mqtt_node_manager::remove_node(const Uuid128 &uuid)
{
    LOG_WARN(TAG, "Removing node with UUID %s", uuid.to_string().c_str());
    std::lock_guard<std::mutex> lock(tn_mutex);
    
    // Remove from UUID index first
    auto uuid_it = uuid_index.find(uuid);
    if (uuid_it != uuid_index.end())
    {
        LOG_WARN(TAG, "Remove unprovisioned device 0x%04x", uuid_it->second->unicast);
        
        // Remove from vector
        tracked_nodes.erase(
            std::remove_if(tracked_nodes.begin(), tracked_nodes.end(),
                [&uuid](const std::shared_ptr<bm2mqtt_node_info>& node) {
                    return node && node->uuid == uuid;
                }),
            tracked_nodes.end()
        );
        
        // Remove from index
        uuid_index.erase(uuid_it);
    }

    mark_node_info_dirty();
}

esp_err_t ble2mqtt_node_manager::example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                                                 std::shared_ptr<bm2mqtt_node_info> node,
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

esp_err_t ble2mqtt_node_manager::store_node_info(const Uuid128 &uuid, uint16_t unicast,
                                                 uint8_t elem_num, uint16_t node_index)
{
    if (!ESP_BLE_MESH_ADDR_IS_UNICAST(unicast))
    {
        return ESP_ERR_INVALID_ARG;
    }

    auto node = get_or_create(uuid);
    node->unicast = unicast;
    node->elem_num = elem_num;
    save_node_info_vector();

    return ESP_OK;
}

std::shared_ptr<bm2mqtt_node_info> ble2mqtt_node_manager::get_node(uint16_t unicast)
{
    if (!ESP_BLE_MESH_ADDR_IS_UNICAST(unicast))
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(tn_mutex);
    for (auto& node : tracked_nodes)
    {
        if (node && node->unicast <= unicast &&
            node->unicast + node->elem_num > unicast)
        {
            return node;
        }
    }

    return nullptr;
}

void ble2mqtt_node_manager::print_registered_nodes()
{
    std::lock_guard<std::mutex> lock(tn_mutex);
    LOG_INFO(TAG, "Provisioned nodes: %d", tracked_nodes.size());
    for (const auto &node : tracked_nodes)
    {
        if (!node) continue;
        
        // Get the node name from the provisioner
        uint16_t node_index = get_node_index(node->uuid); // Ensure node_index is set
        const char* node_name = esp_ble_mesh_provisioner_get_node_name(node_index);
        if (!node_name) {
            node_name = "Unknown";
        }
        
        LOG_INFO(TAG, "==device uuid: %s", bt_hex(node->uuid.raw(), 16));
        LOG_INFO(TAG, "  Node Name: %s", node_name);
        LOG_INFO(TAG, "  Primary Address: 0x%04X", node->unicast);
        LOG_INFO(TAG, "  Element Count: %d", node->elem_num);
        LOG_INFO(TAG, "  On/Off State: %d", node->onoff);
        LOG_INFO(TAG, "  Level: %d", node->level);
        LOG_INFO(TAG, "  HSL: H=%d S=%d L=%d", node->hsl_h, node->hsl_s, node->hsl_l);
        LOG_INFO(TAG, "  HSL Ranges - Hue: %d-%d, Saturation: %d-%d, Lightness: %d-%d", 
                 node->min_hue, node->max_hue, node->min_saturation, node->max_saturation, 
                 node->min_lightness, node->max_lightness);
        LOG_INFO(TAG, "  Temperature: Current=%d, Range=%d-%d", node->curr_temp, node->min_temp, node->max_temp);
        LOG_INFO(TAG, "  Color Mode: %s", get_color_mode_string(node->color_mode));
        LOG_INFO(TAG, "  Light CTL Temp Offset: %d", node->light_ctl_temp_offset);
        LOG_INFO(TAG, "  Features: 0x%04X", node->features);
        LOG_INFO(TAG, "  Features to Bind: 0x%04X", node->features_to_bind);
    }
}

esp_err_t ble2mqtt_node_manager::save_node_info_vector()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_mesh", NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    nvs_set_u32(handle, "version", NODE_INFO_SCHEMA_VERSION);

    {
        std::lock_guard<std::mutex> lock(tn_mutex);
        
        // Convert shared_ptr vector to raw data for NVS storage
        std::vector<bm2mqtt_node_info> raw_nodes;
        raw_nodes.reserve(tracked_nodes.size());
        
        for (const auto& node : tracked_nodes)
        {
            if (node)
            {
                raw_nodes.push_back(*node);
            }
        }
        
        size_t total_size = raw_nodes.size() * sizeof(bm2mqtt_node_info);
        err = nvs_set_blob(handle, "nodes", raw_nodes.data(), total_size);
        if (err == ESP_OK)
        {
            err = nvs_commit(handle);
        }
        else
        {
            LOG_ERROR(TAG, "Failed to save node info vector to NVS: %s", esp_err_to_name(err));
        }
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
        LOG_ERROR(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t version;
    if (nvs_get_u32(handle, "version", &version) != ESP_OK || version > NODE_INFO_SCHEMA_VERSION)
    {
        nvs_close(handle);
        LOG_ERROR(TAG, "NVS version mismatch: expected %u, got %u", NODE_INFO_SCHEMA_VERSION, version);
        // Optionally, you could handle the version mismatch by migrating data or resetting.
        // For now, we just return an error.
        return ESP_ERR_INVALID_VERSION;
    }

    size_t size = 0;
    err = nvs_get_blob(handle, "nodes", nullptr, &size);
    if (err != ESP_OK || size == 0)
    {
        nvs_close(handle);
        LOG_ERROR(TAG, "Failed to load node info vector to NVS: %s", esp_err_to_name(err));
        return err;
    }
    if (version == 1)
    {
        LOG_WARN(TAG, "Detected old node info schema version 1, converting to version 2");

        size_t count = size / sizeof(bm2mqtt_node_info_v1);
        
        std::vector<bm2mqtt_node_info_v1> tracked_nodes_v1(count);
        tracked_nodes_v1.resize(count);
        err = nvs_get_blob(handle, "nodes", tracked_nodes_v1.data(), &size);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Failed to read node info vector from NVS: %s", esp_err_to_name(err));
        }
        else
        {
            LOG_INFO(TAG, "Loaded %zu nodes from NVS", tracked_nodes_v1.size());
        }

        std::lock_guard<std::mutex> lock(tn_mutex);
        tracked_nodes.clear();
        uuid_index.clear();
        
        for (size_t i = 0; i < count; i++)
        {
            auto node = std::make_shared<bm2mqtt_node_info>();
            node->convert_from_v1(tracked_nodes_v1[i]);
            
            tracked_nodes.push_back(node);
            uuid_index[node->uuid] = node;
            
            LOG_INFO(TAG, "Converted node %zu: UUID=%s, Unicast=0x%04X, ElemNum=%d",
                     i, node->uuid.to_string().c_str(),
                     node->unicast, node->elem_num);
        }
        mark_node_info_dirty();
    } 
    else if (version == NODE_INFO_SCHEMA_VERSION)
    {
        size_t count = size / sizeof(bm2mqtt_node_info);
        
        // Load raw data first
        std::vector<bm2mqtt_node_info> raw_nodes(count);
        err = nvs_get_blob(handle, "nodes", raw_nodes.data(), &size);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Failed to read node info vector from NVS: %s", esp_err_to_name(err));
        }
        else
        {
            // Convert raw data to shared_ptr vector
            std::lock_guard<std::mutex> lock(tn_mutex);
            tracked_nodes.clear();
            uuid_index.clear();
            
            for (const auto& raw_node : raw_nodes)
            {
                auto node = std::make_shared<bm2mqtt_node_info>(raw_node);
                tracked_nodes.push_back(node);
                uuid_index[node->uuid] = node;
            }
            
            LOG_INFO(TAG, "Loaded %zu nodes from NVS", tracked_nodes.size());
        }
    }

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
        LOG_INFO(TAG, "Saving node info to NVS...");
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

void ble2mqtt_node_manager::initialize()
{
    init_node_save_timer();
    load_node_info_vector();
}

void ble2mqtt_node_manager::set_node_name(const Uuid128& uuid, const char* name)
{
    if (auto node = get_node(uuid))
    {
        uint16_t node_index = get_node_index(uuid); // Ensure node_index is set
        esp_ble_mesh_provisioner_set_node_name(node_index, name);
        mark_node_info_dirty();
    }
    else{
        LOG_WARN(TAG, "Node with UUID %s not found", uuid.to_string().c_str());
    }
}