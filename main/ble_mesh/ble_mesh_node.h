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
    hs,
    color_temp,
};

typedef struct
{
    Uuid128 uuid;
    uint16_t unicast{0};
    uint16_t hsl_h{0};
    uint16_t hsl_s{0};
    uint16_t hsl_l{0};
    uint16_t min_temp{0};
    uint16_t max_temp{0};
    uint16_t curr_temp{0};
    int16_t level{0};
    uint16_t features{0};
    uint16_t features_to_bind{0};
    uint8_t elem_num{0};
    uint8_t onoff{0};
    uint8_t light_ctl_temp_offset{0}; // Element index for Light CTL Temperature
    color_mode_t color_mode = color_mode_t::color_temp;

} bm2mqtt_node_info;

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
                                               uint8_t elem_num, uint8_t onoff_state);

    void remove_node(const Uuid128& uuid);

    esp_err_t example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                              bm2mqtt_node_info *node,
                                              esp_ble_mesh_model_t *model, uint32_t opcode);

    void print_registered_nodes();

    void Initialize();
    void mark_node_info_dirty();
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
