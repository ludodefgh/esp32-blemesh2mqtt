#include "ble_mesh_control.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <memory>

#include "esp_log.h"
#include "nvs_flash.h"
#include "argtable3/argtable3.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_console.h"
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_example_init.h"

#include "ble_mesh_node.h"
#include "ble_mesh_provisioning.h"
#include "debug_console_common.h"
#include "mqtt/mqtt_bridge.h"
#include "sig_models/model_map.h"
#include "sig_companies/company_map.h"

#define TAG "APP_CONTROL"

#define CID_ESP 0x02E5

#define MSG_ROLE ROLE_PROVISIONER

#define APP_KEY_IDX 0x0000
#define APP_KEY_OCTET 0x12

#define APP_USE_ONOFF_CLIENT

static uint8_t dev_uuid[16];

static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "onoff_client";
bool init_done = true;

int ShitShowAppKeyBind = 0;
extern struct example_info_store store;

static void mesh_example_info_store(void)
{
    ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &store, sizeof(store));
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

    .uuid = dev_uuid,

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
};

////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
typedef enum
{
    FEATURE_ONOFF = 1 << 0,
    FEATURE_LIGHTNESS = 1 << 1,
    FEATURE_HSL = 1 << 2,
    FEATURE_CTL = 1 << 3,
    FEATURE_VENDOR = 1 << 4,
} node_features_t;

typedef struct
{
    uint16_t unicast_addr;
    uint8_t element_count;
    uint16_t features;
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

    ESP_LOGI(TAG, "CID: 0x%04X (%s)", cid, company_name);
    ESP_LOGI(TAG, "PID: 0x%04X, VID: 0x%04X", pid, vid);
    ESP_LOGI(TAG, "CRPL: %d, Features: 0x%04X", crpl, features);
    if (features & BIT(0)) ESP_LOGI(TAG, "  - Relay feature supported");
    if (features & BIT(1)) ESP_LOGI(TAG, "  - Proxy feature supported");
    if (features & BIT(2)) ESP_LOGI(TAG, "  - Friend feature supported");
    if (features & BIT(3)) ESP_LOGI(TAG, "  - Low Power feature supported");

    // Skip Composition Header:
    // CID (2), PID (2), VID (2), CRPL (2), Features (2)
    offset += 10;

    while (offset < length)
    {
        if ((offset + 4) > length)
            break;

        // Each element begins with:
        // Location (2), Num SIG Models (1), Num Vendor Models (1)
        uint16_t loc = data[offset] | (data[offset + 1] << 8);
        uint8_t num_sig = data[offset + 2];
        uint8_t num_vendor = data[offset + 3];
        offset += 4;

        info.element_count++;

        // SIG models: each 2 bytes
        for (int i = 0; i < num_sig && offset + 2 <= length; i++)
        {
            uint16_t model_id = data[offset] | (data[offset + 1] << 8);
            offset += 2;

            ESP_LOGW(TAG, "Found model ID 0x%04X: %s", model_id, lookup_model_name(model_id));
        }

        // Vendor models: each 4 bytes
        for (int i = 0; i < num_vendor && offset + 4 <= length; i++)
        {
            offset += 4;
            info.features |= FEATURE_VENDOR;
        }
    }

    return info;
}
////////////////////////////////////////////////////////
void Bind_App_Key(bm2mqtt_node_info *node)
{
    ESP_LOGW(TAG, "-----Bind_App_Key-----");
    vTaskDelay(pdMS_TO_TICKS(500)); // Delay 500msw

    esp_ble_mesh_client_common_param_t common = {0};
    int err = 0;

    if (ShitShowAppKeyBind == 0)
    {
        // Brightness
        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
        set_state.model_app_bind.element_addr = node->unicast;
        set_state.model_app_bind.model_app_idx = store.app_idx;
        set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV;
        set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: Config Model App (2) Bind failed", __func__);
            return;
        }
        ESP_LOGW(TAG, "[BRIGHTNESS] Bound Gen Level model");
        ++ShitShowAppKeyBind;
    }
    else if (ShitShowAppKeyBind == 1)
    {
        ++ShitShowAppKeyBind;
// Now request to bind the second shit
#if defined(APP_USE_ONOFF_CLIENT)
        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
        set_state.model_app_bind.element_addr = node->unicast;
        set_state.model_app_bind.model_app_idx = store.app_idx;
        set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV;
        set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: Config Model App (1) Bind failed", __func__);
            return;
        }
        ESP_LOGW(TAG, "Bound Gen ONOFF model");
#endif
    }
    else if (ShitShowAppKeyBind == 2)
    {
        //[TEMPERATURE]
        ++ShitShowAppKeyBind;
        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
        common.ctx.addr = node->unicast;
        set_state.model_app_bind.element_addr = node->unicast + 1;
        set_state.model_app_bind.model_app_idx = store.app_idx;
        set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV;
        set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: Config Model App (2) Bind failed", __func__);
            return;
        }
        ESP_LOGW(TAG, "[Generic level] Bound Gen Level model");
    }
    else if (ShitShowAppKeyBind == 3)
    {
        // [HSL]
        ++ShitShowAppKeyBind;
        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
        common.ctx.addr = node->unicast;
        set_state.model_app_bind.element_addr = node->unicast;
        set_state.model_app_bind.model_app_idx = store.app_idx;
        set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV;
        set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: Config Model App (3) Bind failed", __func__);
            return;
        }
        ESP_LOGW(TAG, "[HSL] Bound Light HSL model");
    }
    else if (ShitShowAppKeyBind == 4)
    {
        // [Lightness]
        ++ShitShowAppKeyBind;
        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
        common.ctx.addr = node->unicast;
        set_state.model_app_bind.element_addr = node->unicast;
        set_state.model_app_bind.model_app_idx = store.app_idx;
        set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV;
        set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: Config Model App (4) Bind failed", __func__);
            return;
        }
        ESP_LOGW(TAG, "[Lightness] Bound model");
    }
    else if (ShitShowAppKeyBind == 5)
    {
        // [CTl_temp]
        ++ShitShowAppKeyBind;
        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
        common.ctx.addr = node->unicast;
        set_state.model_app_bind.element_addr = node->unicast + 1;
        set_state.model_app_bind.model_app_idx = store.app_idx;
        set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_TEMP_SRV;
        set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: Config Model App (4) Bind failed", __func__);
            return;
        }
        ESP_LOGW(TAG, "[Light Ctl Temp Server] Bound model");
    }
    else if (ShitShowAppKeyBind == 6)
    {
        // [CTl_temp]
        ++ShitShowAppKeyBind;
        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
        example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
        common.ctx.addr = node->unicast;
        set_state.model_app_bind.element_addr = node->unicast;
        set_state.model_app_bind.model_app_idx = store.app_idx;
        set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV;
        set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
        err = esp_ble_mesh_config_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: Config Model App (4) Bind failed", __func__);
            return;
        }
        ESP_LOGW(TAG, "[Light Ctl Server] Bound model");
    }
    else
    {
        init_done = true;
    }
}

////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
static void ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                      esp_ble_mesh_cfg_client_cb_param_t *param)
{
    esp_ble_mesh_client_common_param_t common = {0};
    bm2mqtt_node_info *node = NULL;
    uint32_t opcode;
    uint16_t addr;
    int err;

    opcode = param->params->opcode;
    addr = param->params->ctx.addr;

    ESP_LOGI(TAG, "%s, error_code = 0x%02x, event = 0x%02x, addr: 0x%04x, opcode: 0x%04" PRIx32,
             __func__, param->error_code, event, param->params->ctx.addr, opcode);

    node = example_ble_mesh_get_node_info(addr);
    if (!node)
    {
        ESP_LOGE(TAG, "%s: Get node info failed", __func__);
        return;
    }

    if (param->error_code)
    {
        Bind_App_Key(node);
        ESP_LOGE(TAG, "Send config client message failed, opcode 0x%04" PRIx32, opcode);
        return;
    }

    switch (event)
    {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET:
        {
            if (!get_composition_data_debug)
            {
                init_done = false;
            }

            ESP_LOGI(TAG, "composition data %s", bt_hex(param->status_cb.comp_data_status.composition_data->data, param->status_cb.comp_data_status.composition_data->len));

            uint16_t addr = param->params->ctx.addr;

            const struct net_buf_simple *buf = param->status_cb.comp_data_status.composition_data;
            const uint8_t *data = buf->data;
            size_t len = buf->len;

            parsed_node_info_t node_info = parse_composition_data(data, len, addr);

            ESP_LOGI(TAG, "Parsed node 0x%04X: elements=%d features=0x%02X",
                     node_info.unicast_addr, node_info.element_count, node_info.features);

            if (node_info.features & FEATURE_HSL)
            {
                // esp_ble_mesh_provisioner_set_node_name_by_addr(addr, "RGB Light");
                ESP_LOGI(TAG, "HSL feature detected, setting node name to 'RGB Light'");
            }
            if (node_info.features & FEATURE_LIGHTNESS)
            {
                // esp_ble_mesh_provisioner_set_node_name_by_addr(addr, "Dimmable Light");
                ESP_LOGI(TAG, "Lightness feature detected, setting node name to 'Dimmable Light'");
            }
            if (node_info.features & FEATURE_ONOFF)
            {
                // esp_ble_mesh_provisioner_set_node_name_by_addr(addr, "Switch");
                ESP_LOGI(TAG, "OnOff feature detected, setting node name to 'Switch'");
            }
            else
            {
                // esp_ble_mesh_provisioner_set_node_name_by_addr(addr, "Unknown Device");
            }

            // process_composition_data(param);

            if (!get_composition_data_debug)
            {
                esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
                set_state.app_key_add.net_idx = store.net_idx;
                set_state.app_key_add.app_idx = store.app_idx;
                memcpy(set_state.app_key_add.app_key, prov_key.app_key, 16);
                err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                if (err)
                {
                    ESP_LOGE(TAG, "%s: Config AppKey Add failed", __func__);
                    return;
                }
            }
            get_composition_data_debug = false;
            break;
        }
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        {
            Bind_App_Key(node);
            if (ShitShowAppKeyBind == 0)
            {
                // Brightness
                esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
                set_state.model_app_bind.element_addr = node->unicast;
                set_state.model_app_bind.model_app_idx = store.app_idx;
                set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV;
                set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
                err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                if (err)
                {
                    ESP_LOGE(TAG, "%s: Config Model App (2) Bind failed", __func__);
                    return;
                }
                ESP_LOGW(TAG, "[BRIGHTNESS] Bound Gen Level model");
                ++ShitShowAppKeyBind;
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        {
            Bind_App_Key(node);
        }
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT:
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_STATUS:
        {
            ESP_LOG_BUFFER_HEX("composition data %s", param->status_cb.comp_data_status.composition_data->data,
                               param->status_cb.comp_data_status.composition_data->len);
            // esp_ble_mesh_composition_head head = {0};
            // esp_ble_mesh_composition_decode data = {0};

        }
        break;
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_STATUS:
            break;
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET:
        {
            esp_ble_mesh_cfg_client_get_state_t get_state = {0};
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
            get_state.comp_data_get.page = COMP_DATA_PAGE_0;
            err = esp_ble_mesh_config_client_get_state(&common, &get_state);
            if (err)
            {
                ESP_LOGE(TAG, "%s: Config Composition Data Get failed", __func__);
                return;
            }
            break;
        }
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        {
            esp_ble_mesh_cfg_client_set_state_t set_state = {0};
            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set_state.app_key_add.net_idx = store.net_idx;
            set_state.app_key_add.app_idx = store.app_idx;
            memcpy(set_state.app_key_add.app_key, prov_key.app_key, 16);
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err)
            {
                ESP_LOGE(TAG, "%s: Config AppKey (1) Add failed", __func__);
                return;
            }

            break;
        }
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        {
            esp_ble_mesh_cfg_client_set_state_t set_state = {0};

            example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            set_state.model_app_bind.element_addr = node->unicast;
            set_state.model_app_bind.model_app_idx = store.app_idx;
            set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV;
            set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
            err = esp_ble_mesh_config_client_set_state(&common, &set_state);
            if (err)
            {
                ESP_LOGE(TAG, "%s: Config Model App (2) Bind failed", __func__);
                return;
            }

            // mesh_example_info_store();
            break;
        }
        default:
            break;
        }
        break;
    default:
        ESP_LOGE(TAG, "Not a config client status message event");
        break;
    }
}

static void ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                       esp_ble_mesh_generic_client_cb_param_t *param)
{
    bm2mqtt_node_info *node = NULL;
    uint32_t opcode;
    uint16_t addr;

    opcode = param->params->opcode;
    addr = param->params->ctx.addr;

    ESP_LOGI(TAG, "%s, error_code = 0x%02x, event = 0x%02x, addr: 0x%04x, opcode: 0x%04" PRIx32,
             __func__, param->error_code, event, param->params->ctx.addr, opcode);

    if (param->error_code)
    {
        ESP_LOGE(TAG, "Send generic client message failed, opcode 0x%04" PRIx32, opcode);
        return;
    }

    node = example_ble_mesh_get_node_info(addr);
    if (!node)
    {
        ESP_LOGE(TAG, "%s: Get node info failed", __func__);
        return;
    }

    switch (event)
    {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET:
        {
            // esp_ble_mesh_generic_client_set_state_t set_state = {0};
            node->onoff = param->status_cb.onoff_status.present_onoff;
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET onoff: 0x%02x", node->onoff);
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_GET:
        {
            ESP_LOGI(TAG, "Level Status (Get): %d", param->status_cb.level_status.present_level);
        }
        break;

        default:
            break;
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        {
            node->onoff = param->status_cb.onoff_status.present_onoff;
            ESP_LOGI(TAG, "[Ack] OnOff set: 0x%02x", node->onoff);
        }
        break;
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
        {
            node->level = param->status_cb.level_status.present_level;
            ESP_LOGI(TAG, "[Ack] Level Set: %d", param->status_cb.level_status.present_level);
        }
        break;
        default:
            break;
        }
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT");
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        /* If failed to receive the responses, these messages will be resend */
        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET:
        {
            ESP_LOGW(TAG, "TIMEOUT: OnOFF Get");

            break;
        }
        case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
        {
            // esp_ble_mesh_generic_client_set_state_t set_state = {0};
            node->onoff = param->status_cb.onoff_status.present_onoff;
            ESP_LOGW(TAG, "TIMEOUT: OnOFF Set: 0x%02x", node->onoff);

            break;
        }
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
        {
            // esp_ble_mesh_generic_client_set_state_t set_state = {0};
            // node->level = param->status_cb.level_status.present_level;
            ESP_LOGI(TAG, "TIMEOUT : Level Set");
        }
        break;
        case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_GET:
        {
            ESP_LOGI(TAG, "TIMEOUT : Level Get");
        }
        break;
        default:
            break;
        }
        break;
    default:
        ESP_LOGE(TAG, "Not a generic client status message event");
        break;
    }
}

void ble_mesh_light_client_cb(esp_ble_mesh_light_client_cb_event_t event,
                              esp_ble_mesh_light_client_cb_param_t *param)
{
    bm2mqtt_node_info *node = NULL;
    uint32_t opcode;
    uint16_t addr;

    opcode = param->params->opcode;
    addr = param->params->ctx.addr;

    ESP_LOGI(TAG, "%s, error_code = 0x%02x, event = 0x%02x, addr: 0x%04x, opcode: 0x%04" PRIx32,
             __func__, param->error_code, event, param->params->ctx.addr, opcode);

    if (param->error_code)
    {
        ESP_LOGE(TAG, "Send light client message failed, opcode 0x%04" PRIx32, opcode);
        return;
    }

    node = example_ble_mesh_get_node_info(addr);
    if (!node)
    {
        ESP_LOGE(TAG, "%s: Get node info failed", __func__);
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
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS h=%d s=%d l=%d", node->hsl_h, node->hsl_s, node->hsl_l);
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET:
        {
            // auto bla = param->status_cb.hsl_range_status.hue_range_min;
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS hue_min=%u hue_max=%u sat_min=%u sat_max=%u",
                     param->status_cb.hsl_range_status.hue_range_min, param->status_cb.hsl_range_status.hue_range_max,
                     param->status_cb.hsl_range_status.saturation_range_min, param->status_cb.hsl_range_status.saturation_range_max);
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET:
        {
            // auto bla = param->status_cb.hsl_range_status.hue_range_min;
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET lightness_min=%u lightness_max=%u",
                     param->status_cb.lightness_range_status.range_min, param->status_cb.lightness_range_status.range_max);
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET:
        {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET temp=%u delta_uv=%u",
                     param->status_cb.ctl_temperature_status.present_ctl_temperature, param->status_cb.ctl_temperature_status.present_ctl_delta_uv);
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET:
        {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET range_min=%u range_max=%u",
                     param->status_cb.ctl_temperature_range_status.range_min, param->status_cb.ctl_temperature_range_status.range_max);

            node->min_temp = param->status_cb.ctl_temperature_range_status.range_min;
            node->max_temp = param->status_cb.ctl_temperature_range_status.range_max;
        }
        break;

        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET:
        {
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET present_ctl_lightness=%u present_ctl_temperature=%u remain_time=%u target_ctl_lightness=%u target_ctl_temperature=%u",
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

        break;

    case ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT:
        ESP_LOGI("LIGHT_CLI", "Set state response: Error Code : %i", param->error_code);

        switch (opcode)
        {
        case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET:
        {
            // node->hsl_h = param->status_cb.hsl_status.hsl_hue;
            // node->hsl_l = param->status_cb.hsl_status.hsl_lightness;
            // node->hsl_s = param->status_cb.hsl_status.hsl_saturation;
            // ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS h=%d s=%d l=%d", node->hsl_h, node->hsl_s ,node->hsl_l);
            // ESP_LOGE(TAG, "YOUPEEEEEE");
        }
        break;
        }
        break;

    case ESP_BLE_MESH_LIGHT_CLIENT_PUBLISH_EVT:
        ESP_LOGI("LIGHT_CLI", "Publish received");
        break;

    case ESP_BLE_MESH_LIGHT_CLIENT_TIMEOUT_EVT:
        ESP_LOGE("LIGHT_CLI", "Message timeout ");
        break;

    default:
        break;
    }
}
bool enable_provisioning = false;

esp_err_t ble_mesh_init(void)
{
    ble_mesh_get_dev_uuid(dev_uuid);

    // uint8_t match[2] = {0xdd, 0xdd};
    esp_err_t err = ESP_OK;

    store.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    store.app_idx = APP_KEY_IDX;
    memset(prov_key.app_key, APP_KEY_OCTET, sizeof(prov_key.app_key));

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_client_callback(ble_mesh_config_client_cb);
    esp_ble_mesh_register_generic_client_callback(ble_mesh_generic_client_cb);
    esp_ble_mesh_register_light_client_callback(ble_mesh_light_client_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
        return err;
    }

    // err = esp_ble_mesh_provisioner_set_dev_uuid_match(match, sizeof(match), 0x0, false);
    // if (err != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to set matching device uuid (err %d)", err);
    //     return err;
    // }

    err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable mesh provisioner (err %d)", err);
        return err;
    }

    err = esp_ble_mesh_provisioner_add_local_app_key(prov_key.app_key, store.net_idx, store.app_idx);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add local AppKey (err %d)", err);
        return err;
    }

    ESP_LOGI(TAG, "BLE Mesh Provisioner initialized");

    return err;
}

void RefreshNodes()
{
    uint16_t node_count = 0;
    for_each_provisioned_node([&node_count](const esp_ble_mesh_node_t *node)
                              {
        bm2mqtt_node_info* node_info = GetNode(node_count);
        
        memcpy(node_info->uuid, node->dev_uuid, 16);
        node_info->unicast = node->unicast_addr;
        node_info->elem_num = node->element_num;

        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_generic_client_get_state_t get_state = {0};
        example_ble_mesh_set_msg_common(&common, node_info, onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET);
        int err = esp_ble_mesh_generic_client_get_state(&common, &get_state);
        if (err) {
            ESP_LOGE(TAG, "%s: Generic OnOff Get failed", __func__);
            return;
        }


        esp_ble_mesh_light_client_get_state_t get_state_light = {0};
        example_ble_mesh_set_msg_common(&common, node_info, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_GET);
        esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err) {
            ESP_LOGE(TAG, "%s: Generic light Get failed", __func__);
            return;
        }

        example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET);
        common.ctx.addr = node_info->unicast;
        err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET Get failed", __func__);
        }


        ++node_count; });
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LightControl
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region LightControl

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void ble_mesh_ctl_set(bm2mqtt_node_info *node_info)
{
    node_info->color_mode = color_mode_t::color_temp;

    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_light_client_set_state_t set_state_light = {0};

    example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET);

    set_state_light.ctl_set.ctl_temperature = node_info->curr_temp;
    set_state_light.ctl_set.ctl_lightness = node_info->hsl_l;
    set_state_light.ctl_set.ctl_delta_uv = 0;
    set_state_light.ctl_set.op_en = false;
    set_state_light.ctl_set.delay = 0;
    set_state_light.ctl_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
    int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
    if (err)
    {
        ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET Set failed", __func__);
    }
}

void ble_mesh_ctl_temperature_set(bm2mqtt_node_info *node_info)
{
    node_info->color_mode = color_mode_t::color_temp;

    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_light_client_set_state_t set_state_light = {0};

    example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET_UNACK);
    common.ctx.addr = node_info->unicast + 1;

    set_state_light.ctl_temperature_set.ctl_temperature = node_info->curr_temp;
    set_state_light.ctl_temperature_set.ctl_delta_uv = 0;
    set_state_light.ctl_temperature_set.op_en = false;
    set_state_light.ctl_temperature_set.delay = 0;
    set_state_light.ctl_temperature_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
    int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
    if (err)
    {
        ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET Set failed", __func__);
    }
}

void light_hsl_set(bm2mqtt_node_info *node_info)
{
    if (node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state = {0};

        example_ble_mesh_set_msg_common(&common, node_info, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET);

        node_info->color_mode = color_mode_t::hs;
        set_state.hsl_set.hsl_hue = node_info->hsl_h;
        set_state.hsl_set.hsl_saturation = node_info->hsl_s;
        set_state.hsl_set.hsl_lightness = node_info->hsl_l;
        set_state.hsl_set.op_en = false;
        set_state.hsl_set.delay = 0;
        set_state.hsl_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
        esp_err_t err = esp_ble_mesh_light_client_set_state(&common, &set_state);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "[HSL] Failed to send hsl set message (err: 0x%X)", err);
            return;
        }
    }
}

void gen_onoff_set(bm2mqtt_node_info *node_info)
{
    if (node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_generic_client_set_state_t set_state = {0};

        example_ble_mesh_set_msg_common(&common, node_info, onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET);
        set_state.onoff_set.op_en = false;
        set_state.onoff_set.onoff = node_info->onoff;
        set_state.onoff_set.tid = store.tid++;
        int err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: debounce_timer_callback : Generic OnOff Set failed", __func__);
            return;
        }
    }
}

#pragma endregion LightControl

typedef struct
{
    struct arg_int *temperature;
    struct arg_end *end;
} ble_mesh_ctl_temperature_set_args_t;
ble_mesh_ctl_temperature_set_args_t ctl_temperature_set_args;

typedef struct
{
    struct arg_int *lightness;
    struct arg_end *end;
} ble_mesh_ctl_lightness_set_args_t;
ble_mesh_ctl_lightness_set_args_t ctl_lightness_set_args;

typedef struct
{
    struct arg_int *truefalse;
    struct arg_end *end;
} ble_mesh_ctl_bool_set_args_t;
ble_mesh_ctl_bool_set_args_t ctl_bool_set_args;

int ble_mesh_hsl_range_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = GetNode(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state_light = {0};

        example_ble_mesh_set_msg_common(&common, node_info, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET);
        int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET Get failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_lightness_range_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = GetNode(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state_light = {0};

        example_ble_mesh_set_msg_common(&common, node_info, lightness_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET);
        int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET Get failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_ctl_temperature_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = GetNode(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state_light = {0};

        example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET);
        common.ctx.addr = node_info->unicast + 1;
        int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET Get failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_ctl_temperature_range_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = GetNode(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state_light = {0};

        example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET);
        common.ctx.addr = node_info->unicast; // + 1;

        int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET Get failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_ctl_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = GetNode(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state_light = {0};

        example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET);
        int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET Get failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_ctl_lightness_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ctl_lightness_set_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, ctl_lightness_set_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = GetNode(0); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state_light = {0};

        example_ble_mesh_set_msg_common(&common, node_info, lightness_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET);
        common.ctx.addr = node_info->unicast;

        set_state_light.lightness_set.lightness = ctl_lightness_set_args.lightness->ival[0];
        set_state_light.lightness_set.op_en = false;
        set_state_light.lightness_set.delay = 0;
        set_state_light.lightness_set.tid = store.tid++;
        int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET Set failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_ctl_lightness_set(int lightness_value, uint8_t uuid[16])
{
    if (bm2mqtt_node_info *node_info = GetNode(uuid); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state_light = {0};

        example_ble_mesh_set_msg_common(&common, node_info, lightness_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET);
        common.ctx.addr = node_info->unicast;

        set_state_light.lightness_set.lightness = lightness_value;
        set_state_light.lightness_set.op_en = false;
        set_state_light.lightness_set.delay = 0;
        set_state_light.lightness_set.tid = store.tid++;
        int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET Set failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_ctl_temperature_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ctl_temperature_set_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, ctl_temperature_set_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = GetNode(0); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state_light = {0};

        example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET_UNACK);
        common.ctx.addr = node_info->unicast + 1;

        set_state_light.ctl_temperature_set.ctl_temperature = ctl_temperature_set_args.temperature->ival[0];
        set_state_light.ctl_temperature_set.ctl_delta_uv = 0;
        set_state_light.ctl_temperature_set.op_en = false;
        set_state_light.ctl_temperature_set.delay = 0;
        set_state_light.ctl_temperature_set.tid = store.tid++;
        int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET Get failed", __func__);
        }
    }
    return 0;
}

void ble_mesh_set_provisioning_enabled(bool enabled_value)
{
    ESP_LOGI(TAG, "[%s] Current Value : %s Requested Value : %s", __func__, enable_provisioning ? "ON" : "OFF", enabled_value ? "ON" : "OFF");
    if (enabled_value != enable_provisioning)
    {
        enable_provisioning = enabled_value;

        if (enable_provisioning)
        {
            int err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV));
            if (err != ESP_OK)
            {
                ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_ADV enabled");
            }
            mqtt_publish_provisioning_enabled(enable_provisioning);
        }
        else if (!enable_provisioning)
        {
            int err = esp_ble_mesh_provisioner_prov_disable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV));
            if (err != ESP_OK)
            {
                ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_ADV disabled");
            }
            mqtt_publish_provisioning_enabled(enable_provisioning);
        }
    }
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

void RegisterBleMeshDebugCommands()
{
    /* Register commands */

    node_index_args.node_index = arg_int1("n", "node", "<node_index>", "Node index as reported by prov_list_nodes command");
    node_index_args.end = arg_end(2);

    const esp_console_cmd_t ble_mesh_hsl_range_get_cmd = {
        .command = "ble_mesh_hsl_range_get",
        .help = "Light HSL Range Get",
        .hint = NULL,
        .func = &ble_mesh_hsl_range_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_hsl_range_get_cmd));

    const esp_console_cmd_t ble_mesh_lightness_range_get_cmd = {
        .command = "ble_mesh_lightness_range_get",
        .help = "Lightness Range Get",
        .hint = NULL,
        .func = &ble_mesh_lightness_range_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_lightness_range_get_cmd));

    ctl_lightness_set_args.lightness = arg_int1("l", "lightness", "<lightness>", "lightness");
    ctl_lightness_set_args.end = arg_end(2);

    const esp_console_cmd_t ble_mesh_ctl_lightness_set_cmd = {
        .command = "ble_mesh_ctl_lightness_set",
        .help = "Ctl temperature Set",
        .hint = NULL,
        .func = &ble_mesh_ctl_lightness_set,
        .argtable = &ctl_lightness_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_lightness_set_cmd));

    const esp_console_cmd_t ble_mesh_ctl_temperature_get_cmd = {
        .command = "ble_mesh_ctl_temperature_get",
        .help = "Clt Temperature Get",
        .hint = NULL,
        .func = &ble_mesh_ctl_temperature_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_temperature_get_cmd));

    const esp_console_cmd_t ble_mesh_ctl_temperature_range_get_cmd = {
        .command = "ble_mesh_ctl_temperature_range_get",
        .help = "Ctl temperature Range Get",
        .hint = NULL,
        .func = &ble_mesh_ctl_temperature_range_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_temperature_range_get_cmd));

    const esp_console_cmd_t ble_mesh_ctl_get_cmd = {
        .command = "ble_mesh_ctl_get",
        .help = "Ctl  Get",
        .hint = NULL,
        .func = &ble_mesh_ctl_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_get_cmd));

    ctl_temperature_set_args.temperature = arg_int1("t", "temp", "<color_temp>", "Temperature of the color in K");
    ctl_temperature_set_args.end = arg_end(2);

    const esp_console_cmd_t ble_mesh_ctl_temperature_set_cmd = {
        .command = "ble_mesh_ctl_temperature_set",
        .help = "Ctl temperature Set",
        .hint = NULL,
        .func = &ble_mesh_ctl_temperature_set,
        .argtable = &ctl_temperature_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_temperature_set_cmd));

    ctl_bool_set_args.truefalse = arg_int1("v", "value", "<true_false>", "0 for false, anything else for true");
    ctl_bool_set_args.end = arg_end(2);

    const esp_console_cmd_t ble_mesh_toggle_provisioning_cmd = {
        .command = "ble_mesh_set_provisioning_enabled",
        .help = "Toggle the provisioning functionality",
        .hint = NULL,
        .func = &ble_mesh_set_provisioning_enabled,
        .argtable = &ctl_bool_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_toggle_provisioning_cmd));
}