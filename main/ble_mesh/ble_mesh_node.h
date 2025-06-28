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
    uint8_t uuid[16];
    uint16_t unicast;
    uint16_t hsl_h;
    uint16_t hsl_s;
    uint16_t hsl_l;
    uint16_t min_temp;
    uint16_t max_temp;
    uint16_t curr_temp;
    uint8_t elem_num;
    uint8_t onoff;
    int16_t level;
    color_mode_t color_mode = color_mode_t::hs;

} bm2mqtt_node_info;

bm2mqtt_node_info* GetNode(int nodeIndex);
bm2mqtt_node_info* GetNode(uint8_t uuid[16]);

bm2mqtt_node_info* GetNodeFromMac(const std::string& mac);
void for_each_node(std::function<void( const bm2mqtt_node_info *)> func);

esp_err_t example_ble_mesh_store_node_info(const uint8_t uuid[16], uint16_t unicast,
                                                  uint8_t elem_num, uint8_t onoff_state);


bm2mqtt_node_info *example_ble_mesh_get_node_info(uint16_t unicast);


esp_err_t example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                                 bm2mqtt_node_info *node,
                                                 esp_ble_mesh_model_t *model, uint32_t opcode);


void remove_provisioned_node(uint8_t uuid[16]);