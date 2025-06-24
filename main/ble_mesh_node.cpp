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
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_example_init.h"

#include "ble_mesh_provisioning.h"

#define TAG "APP_NODE"

extern struct example_info_store store;

bm2mqtt_node_info nodes[CONFIG_BLE_MESH_MAX_PROV_NODES] = {0};

void for_each_node(std::function<void( const bm2mqtt_node_info *)> func)
{
    for (auto i = 0; i < ARRAY_SIZE(nodes); i++)
    {
        if (nodes[i].unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
        {
           func(&nodes[i]);
        }
    }
}

bm2mqtt_node_info* GetNode(int nodeIndex)
{
    return &nodes[nodeIndex];
}

bm2mqtt_node_info* GetNodeFromMac(const std::string& mac)
{
    bm2mqtt_node_info* result = nullptr;
    for_each_provisioned_node([&mac, &result](const esp_ble_mesh_node_t * node)
    {
            const std::string inAddr {bt_hex(node->addr, BD_ADDR_LEN)};
            if (inAddr == mac)
            {
                for (auto i = 0; i < ARRAY_SIZE(nodes); i++)
                {
                    if (memcmp(nodes[i].uuid, node->dev_uuid, 16) == 0)
                    {
                        result =  &nodes[i];
                    }
                }
            }
    });

    return result;
}

esp_err_t example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
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


esp_err_t example_ble_mesh_store_node_info(const uint8_t uuid[16], uint16_t unicast,
                                                  uint8_t elem_num, uint8_t onoff_state)
{
    int i;

    if (!uuid || !ESP_BLE_MESH_ADDR_IS_UNICAST(unicast))
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* Judge if the device has been provisioned before */
    for (i = 0; i < ARRAY_SIZE(nodes); i++)
    {
        if (!memcmp(nodes[i].uuid, uuid, 16))
        {
            ESP_LOGW(TAG, "%s: reprovisioned device 0x%04x", __func__, unicast);
            nodes[i].unicast = unicast;
            nodes[i].elem_num = elem_num;
            nodes[i].onoff = onoff_state;
            nodes[i].hsl_h = 16000;
            nodes[i].hsl_s = 16000;
            nodes[i].hsl_l = 16000;
            return ESP_OK;
        }
    }

    for (i = 0; i < ARRAY_SIZE(nodes); i++)
    {
        if (nodes[i].unicast == ESP_BLE_MESH_ADDR_UNASSIGNED)
        {
            memcpy(nodes[i].uuid, uuid, 16);
            nodes[i].unicast = unicast;
            nodes[i].elem_num = elem_num;
            nodes[i].onoff = onoff_state;
            nodes[i].hsl_h = 16000;
            nodes[i].hsl_s = 16000;
            nodes[i].hsl_l = 16000;
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

bm2mqtt_node_info *example_ble_mesh_get_node_info(uint16_t unicast)
{
    int i;

    if (!ESP_BLE_MESH_ADDR_IS_UNICAST(unicast))
    {
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(nodes); i++)
    {
        if (nodes[i].unicast <= unicast &&
            nodes[i].unicast + nodes[i].elem_num > unicast)
        {
            return &nodes[i];
        }
    }

    return NULL;
}