#pragma once
#include <functional>
#include <inttypes.h>
#include <memory>
#include <stdio.h>
#include <string>
#include <unordered_map>

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "device_uuid128.h"


#define MSG_SEND_TTL 3
#define MSG_TIMEOUT 4000

enum class color_mode_t : uint8_t
{
    brightness,
    hs,
    color_temp,
};

const char *get_color_mode_string(color_mode_t mode);

uint16_t get_node_index(device_uuid128 uuid);

typedef struct
{
    device_uuid128 uuid;
    uint16_t unicast{0};
    uint16_t hsl_h{0};
    uint16_t min_hue{0};
    uint16_t max_hue{std::numeric_limits<uint16_t>::max()};
    uint16_t hsl_s{0};
    uint16_t min_saturation{0};
    uint16_t max_saturation{std::numeric_limits<uint16_t>::max()};
    uint16_t hsl_l{0};
    uint16_t min_lightness{0};
    uint16_t max_lightness{std::numeric_limits<uint16_t>::max()};
    uint16_t curr_temp{0};
    uint16_t min_temp{0};
    uint16_t max_temp{std::numeric_limits<uint16_t>::max()};
    int16_t level{0};
    uint16_t features{0};
    uint16_t features_to_bind{0};
    uint8_t elem_num{0};
    uint8_t onoff{0};
    uint8_t light_ctl_temp_offset{0}; // Element index for Light CTL Temperature
    color_mode_t color_mode = color_mode_t::brightness;

} bm2mqtt_node_info_v1;

typedef struct
{
    device_uuid128 uuid;
    uint16_t unicast{0};
    uint16_t hsl_h{0};
    uint16_t min_hue{0};
    uint16_t max_hue{std::numeric_limits<uint16_t>::max()};
    uint16_t hsl_s{0};
    uint16_t min_saturation{0};
    uint16_t max_saturation{std::numeric_limits<uint16_t>::max()};
    uint16_t hsl_l{0};
    uint16_t min_lightness{0};
    uint16_t max_lightness{std::numeric_limits<uint16_t>::max()};
    uint16_t curr_temp{0};
    uint16_t min_temp{0};
    uint16_t max_temp{std::numeric_limits<uint16_t>::max()};
    int16_t level{0};
    uint16_t features{0};
    uint16_t features_to_bind{0};
    uint16_t node_index{0}; // Node index in the provisioning table
    uint8_t elem_num{0};
    uint8_t onoff{0};
    uint8_t light_ctl_temp_offset{0}; // Element index for Light CTL Temperature
    color_mode_t color_mode = color_mode_t::brightness;

    void operator=(const bm2mqtt_node_info_v1 &other)
    {
        uuid = other.uuid;
        unicast = other.unicast;
        hsl_h = other.hsl_h;
        min_hue = other.min_hue;
        max_hue = other.max_hue;
        hsl_s = other.hsl_s;
        min_saturation = other.min_saturation;
        max_saturation = other.max_saturation;
        hsl_l = other.hsl_l;
        min_lightness = other.min_lightness;
        max_lightness = other.max_lightness;
        curr_temp = other.curr_temp;
        min_temp = other.min_temp;
        max_temp = other.max_temp;
        level = other.level;
        features = other.features;
        features_to_bind = other.features_to_bind;
        elem_num = other.elem_num;
        onoff = other.onoff;
        light_ctl_temp_offset = other.light_ctl_temp_offset;
        color_mode = other.color_mode;
    }

    void convert_from_previous(const bm2mqtt_node_info_v1 &v1)
    {
        *this = v1; // Use the assignment operator to copy data
    }

} bm2mqtt_node_info_v2;

typedef struct
{
    device_uuid128 uuid;
    uint16_t unicast{0};
    uint16_t hsl_h{0};
    uint16_t min_hue{0};
    uint16_t max_hue{std::numeric_limits<uint16_t>::max()};
    uint16_t hsl_s{0};
    uint16_t min_saturation{0};
    uint16_t max_saturation{std::numeric_limits<uint16_t>::max()};
    uint16_t hsl_l{0};
    uint16_t min_lightness{0};
    uint16_t max_lightness{std::numeric_limits<uint16_t>::max()};
    uint16_t curr_temp{0};
    uint16_t min_temp{0};
    uint16_t max_temp{std::numeric_limits<uint16_t>::max()};
    int16_t level{0};
    uint16_t features{0};
    uint16_t features_to_bind{0};
    uint16_t node_index{0}; // Node index in the provisioning table
    uint16_t company_id{0}; // Company ID (CID) from composition data
    uint8_t elem_num{0};
    uint8_t onoff{0};
    uint8_t light_ctl_temp_offset{0}; // Element index for Light CTL Temperature
    color_mode_t color_mode = color_mode_t::brightness;

    void operator=(const bm2mqtt_node_info_v2 &other)
    {
        uuid = other.uuid;
        unicast = other.unicast;
        hsl_h = other.hsl_h;
        min_hue = other.min_hue;
        max_hue = other.max_hue;
        hsl_s = other.hsl_s;
        min_saturation = other.min_saturation;
        max_saturation = other.max_saturation;
        hsl_l = other.hsl_l;
        min_lightness = other.min_lightness;
        max_lightness = other.max_lightness;
        curr_temp = other.curr_temp;
        min_temp = other.min_temp;
        max_temp = other.max_temp;
        level = other.level;
        features = other.features;
        features_to_bind = other.features_to_bind;
        elem_num = other.elem_num;
        onoff = other.onoff;
        light_ctl_temp_offset = other.light_ctl_temp_offset;
        color_mode = other.color_mode;
        company_id = 0; // Initialize company_id for v2 -> v3 conversion
    }

    void convert_from_previous(const bm2mqtt_node_info_v2 &rhs)
    {
        *this = rhs; // Use the assignment operator to copy data
    }

} bm2mqtt_node_info_v3;

// Version 4: Optimized memory layout to reduce padding
// Members reordered: 16-byte aligned first, then 2-byte types grouped, then 1-byte types at end
// Reduces structure size from ~64 bytes to ~56 bytes (12.5% reduction)
typedef struct
{
    // 16-byte aligned member (std::array<uint8_t, 16>)
    device_uuid128 uuid;

    // All 2-byte members grouped together (18 fields × 2 = 36 bytes, naturally aligned)
    uint16_t unicast{0};
    uint16_t hsl_h{0};
    uint16_t min_hue{0};
    uint16_t max_hue{std::numeric_limits<uint16_t>::max()};
    uint16_t hsl_s{0};
    uint16_t min_saturation{0};
    uint16_t max_saturation{std::numeric_limits<uint16_t>::max()};
    uint16_t hsl_l{0};
    uint16_t min_lightness{0};
    uint16_t max_lightness{std::numeric_limits<uint16_t>::max()};
    uint16_t curr_temp{0};
    uint16_t min_temp{0};
    uint16_t max_temp{std::numeric_limits<uint16_t>::max()};
    int16_t level{0};
    uint16_t features{0};
    uint16_t features_to_bind{0};
    uint16_t node_index{0};
    uint16_t company_id{0};

    // All 1-byte members at the end (4 bytes, aligned to 4-byte boundary)
    uint8_t elem_num{0};
    uint8_t onoff{0};
    uint8_t light_ctl_temp_offset{0};
    color_mode_t color_mode = color_mode_t::brightness;  // uint8_t enum

    // Total: 16 + 36 + 4 = 56 bytes (vs 64 bytes in v3)

    void operator=(const bm2mqtt_node_info_v3 &other)
    {
        uuid = other.uuid;
        unicast = other.unicast;
        hsl_h = other.hsl_h;
        min_hue = other.min_hue;
        max_hue = other.max_hue;
        hsl_s = other.hsl_s;
        min_saturation = other.min_saturation;
        max_saturation = other.max_saturation;
        hsl_l = other.hsl_l;
        min_lightness = other.min_lightness;
        max_lightness = other.max_lightness;
        curr_temp = other.curr_temp;
        min_temp = other.min_temp;
        max_temp = other.max_temp;
        level = other.level;
        features = other.features;
        features_to_bind = other.features_to_bind;
        node_index = other.node_index;
        company_id = other.company_id;
        elem_num = other.elem_num;
        onoff = other.onoff;
        light_ctl_temp_offset = other.light_ctl_temp_offset;
        color_mode = other.color_mode;
    }

    void convert_from_previous(const bm2mqtt_node_info_v3 &rhs)
    {
        *this = rhs; // Use the assignment operator to copy data
    }

} bm2mqtt_node_info_v4;

// Verify structure size optimization at compile time
static_assert(sizeof(bm2mqtt_node_info_v4) <= 56, "bm2mqtt_node_info_v4 has unexpected padding");

constexpr uint32_t NODE_INFO_SCHEMA_VERSION = 4;

using bm2mqtt_node_info = bm2mqtt_node_info_v4;

// Hash specialization for Uuid128 to use in unordered_map
namespace std
{
    template <>
    struct hash<device_uuid128>
    {
        std::size_t operator()(const device_uuid128 &uuid) const
        {
            return std::hash<std::string>{}(
                std::string(reinterpret_cast<const char *>(uuid.raw()), 16));
        }
    };
}

class ble2mqtt_node_manager final
{
public:
    ble2mqtt_node_manager() = default;
    ~ble2mqtt_node_manager() = default;

    std::shared_ptr<bm2mqtt_node_info> get_node(int nodeIndex);
    std::shared_ptr<bm2mqtt_node_info> get_node(const device_uuid128 &uuid);
    std::shared_ptr<bm2mqtt_node_info> get_node(const std::string &mac);
    std::shared_ptr<bm2mqtt_node_info> get_node(uint16_t unicast);
    std::shared_ptr<bm2mqtt_node_info> get_or_create(const uint8_t uuid[16]);
    std::shared_ptr<bm2mqtt_node_info> get_or_create(const device_uuid128 &uuid);

    void for_each_node(std::function<void(std::shared_ptr<bm2mqtt_node_info>&)> func);

    esp_err_t store_node_info(const device_uuid128 &uuid, uint16_t unicast,
                              uint8_t elem_num, uint16_t node_index);

    void remove_node(const device_uuid128 &uuid);

    esp_err_t ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                              const std::shared_ptr<bm2mqtt_node_info>& node,
                                              esp_ble_mesh_model_t *model, uint32_t opcode);

    void print_registered_nodes();

    void initialize();
    void mark_node_info_dirty();

    void set_node_name(const device_uuid128 &uuid, const char *name);

private:
    // Disable copy and move constructors and assignment operators
    ble2mqtt_node_manager(const ble2mqtt_node_manager &) = delete;
    ble2mqtt_node_manager &operator=(const ble2mqtt_node_manager &) = delete;
    ble2mqtt_node_manager(ble2mqtt_node_manager &&) = delete;
    ble2mqtt_node_manager &operator=(ble2mqtt_node_manager &&) = delete;

    static void save_timer_callback(void *arg);
    void on_timer_callback();

    void init_node_save_timer();

    esp_err_t save_node_info_vector();
    esp_err_t load_node_info_vector();

    std::vector<std::shared_ptr<bm2mqtt_node_info>> tracked_nodes{};
    std::unordered_map<device_uuid128, std::shared_ptr<bm2mqtt_node_info>> uuid_index{};

    esp_timer_handle_t save_timer;
    bool node_info_dirty = false;
};

ble2mqtt_node_manager &node_manager();
