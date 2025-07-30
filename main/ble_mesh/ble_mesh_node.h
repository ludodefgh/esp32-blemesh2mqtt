#pragma once
#include <stdio.h>
#include <string>
#include <inttypes.h>
#include <functional>

#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_example_init.h"

#include "esp_ble_mesh_defs.h"
#include <esp_timer.h>
#include "Uui128.h"

#define LED_OFF 0x0
#define LED_ON 0x1
#define MSG_SEND_TTL 3
#define MSG_TIMEOUT 4000

enum class color_mode_t : uint8_t
{
    brightness,
    hs,
    color_temp,
};

const char* get_color_mode_string(color_mode_t mode);

uint16_t get_node_index(Uuid128 uuid);

typedef struct
{
    Uuid128 uuid;
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
    Uuid128 uuid;
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

    void operator=(const bm2mqtt_node_info_v1& other)
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

    void convert_from_v1(const bm2mqtt_node_info_v1& v1)
    {
        *this = v1; // Use the assignment operator to copy data

        // node_index = get_node_index(uuid);
        // if (node_index == std::numeric_limits<uint16_t>::max())
        // {
        //     ESP_LOGW("bm2mqtt_node_info_v2", "Node with UUID %s not found in provisioning table",
        //              uuid.to_string().c_str());
        // }
    }

} bm2mqtt_node_info_v2;


constexpr uint32_t NODE_INFO_SCHEMA_VERSION = 2;

using bm2mqtt_node_info = bm2mqtt_node_info_v2;

class ble2mqtt_node_manager final
{
public:
    ble2mqtt_node_manager() = default;
    ~ble2mqtt_node_manager() = default;

    bm2mqtt_node_info *get_node(int nodeIndex);
    bm2mqtt_node_info *get_node(const Uuid128& uuid);
    bm2mqtt_node_info *get_node(const std::string &mac);
    bm2mqtt_node_info *get_node(uint16_t unicast);
    bm2mqtt_node_info* get_or_create(const uint8_t uuid[16]);
    bm2mqtt_node_info* get_or_create(const Uuid128& uuid);

    
    void for_each_node(std::function<void(const bm2mqtt_node_info *)> func);

    esp_err_t store_node_info(const Uuid128& uuid, uint16_t unicast,
                                               uint8_t elem_num, uint16_t node_index);

    void remove_node(const Uuid128& uuid);

    esp_err_t example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                              bm2mqtt_node_info *node,
                                              esp_ble_mesh_model_t *model, uint32_t opcode);

    void print_registered_nodes();

    void initialize();
    void mark_node_info_dirty();

    void set_node_name(const Uuid128& uuid, const char* name);
private:
    // Disable copy and move constructors and assignment operators
    ble2mqtt_node_manager(const ble2mqtt_node_manager &) = delete;
    ble2mqtt_node_manager &operator=(const ble2mqtt_node_manager &) = delete;
    ble2mqtt_node_manager(ble2mqtt_node_manager &&) = delete;
    ble2mqtt_node_manager &operator=(ble2mqtt_node_manager &&) = delete;

    static void save_timer_callback(void* arg);
    void on_timer_callback();
    
    void init_node_save_timer();

    esp_err_t save_node_info_vector();
    esp_err_t load_node_info_vector();

    std::vector<bm2mqtt_node_info> tracked_nodes{};

    esp_timer_handle_t save_timer;
    bool node_info_dirty = false;
};

ble2mqtt_node_manager& node_manager();
