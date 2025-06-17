#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_example_init.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "debugUART.h"
#include "debugGPIO.h"
#include "modelsConfig.h"
#include "provisioning.h"
#include "nodesManager.h"

#define TAG "EXAMPLE"

#define CID_ESP 0x02E5

#define MSG_ROLE ROLE_PROVISIONER

#define APP_KEY_IDX 0x0000
#define APP_KEY_OCTET 0x12

#define APP_USE_ONOFF_CLIENT

extern struct example_info_store store ;

static int16_t NormalizedLevelValue = 0;
static int16_t LastRaw = 0;
/// @brief Provisioning stuff
static uint8_t dev_uuid[16];

static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "onoff_client";
static bool init_done = true;

static int ShitShowAppKeyBind = 0;

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
esp_ble_mesh_client_t hsl_cli;
esp_ble_mesh_client_t ctl_cli;

//ESP_BLE_MESH_MODEL_PUB_DEFINE(level_pub, 2 + 4, ROLE_NODE);
//ESP_BLE_MESH_MODEL_PUB_DEFINE(lightness_cli_pub, 2 + 3, ROLE_NODE);

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
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
typedef struct {
    int16_t cid;
    int16_t pid;
    int16_t vid;
    int16_t crpl;
    int16_t features;
    int16_t all_models;
    uint8_t sig_models;
    uint8_t vnd_models;
} esp_ble_mesh_composition_head;

typedef struct {
    uint16_t model_id;
    uint16_t vendor_id;
} tsModel;

typedef struct {
    // reserve space for up to 20 SIG models
    uint16_t SIG_models[20];
    uint8_t numSIGModels;

    // reserve space for up to 4 vendor models
    tsModel Vendor_models[4];
    uint8_t numVendorModels;
} esp_ble_mesh_composition_decode;

int decode_comp_data(esp_ble_mesh_composition_head *head, esp_ble_mesh_composition_decode *data, uint8_t *mystr, int size)
{
    int pos_sig_base;
    int pos_vnd_base;
    int i;

    memcpy(head, mystr, sizeof(*head));

    if(size < sizeof(*head) + head->sig_models * 2 + head->vnd_models * 4) {
        return -1;
    }

    pos_sig_base = sizeof(*head) - 1;

    for(i = 1; i < head->sig_models * 2; i = i + 2) {
        data->SIG_models[i/2] = mystr[i + pos_sig_base] | (mystr[i + pos_sig_base + 1] << 8);
        printf("%d: %4.4x\n", i/2, data->SIG_models[i/2]);
    }

    pos_vnd_base = head->sig_models * 2 + pos_sig_base;

    for(i = 1; i < head->vnd_models * 2; i = i + 2) {
        data->Vendor_models[i/2].model_id = mystr[i + pos_vnd_base] | (mystr[i + pos_vnd_base + 1] << 8);
        printf("%d: %4.4x\n", i/2, data->Vendor_models[i/2].model_id);

        data->Vendor_models[i/2].vendor_id = mystr[i + pos_vnd_base + 2] | (mystr[i + pos_vnd_base + 3] << 8);
        printf("%d: %4.4x\n", i/2, data->Vendor_models[i/2].vendor_id);
    }

    return 0;
}

////////////////////////////////////////////////////////

void Bind_App_Key(esp_ble_mesh_node_info_t *node)
{
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
        ESP_LOGW(TAG, "[LIGHTNESS] Bound Gen Level model");
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
            ESP_LOGE(TAG, "%s: Config Model App (4) Bind failed", __func__);
            return;
        }
        ESP_LOGW(TAG, "[HSL] Bound Light HSL model");
    }
    // else if (ShitShowAppKeyBind == 2)
    // {
    //     // [CTL]
    //     ++ShitShowAppKeyBind;
    //     esp_ble_mesh_cfg_client_set_state_t set_state = {0};
    //     example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
    //     common.ctx.addr = node->unicast;// + 2;
    //     set_state.model_app_bind.element_addr = node->unicast;// + 1;// + 2;
    //     set_state.model_app_bind.model_app_idx = store.app_idx;
    //     set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_SRV;
    //     set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
    //     err = esp_ble_mesh_config_client_set_state(&common, &set_state);
    //     if (err)
    //     {
    //         ESP_LOGE(TAG, "%s: Config Model App (3) Bind failed", __func__);
    //         return;
    //     }
    //     ESP_LOGW(TAG, "[CTL] Bound Light CTL model");
    // }
    //  else if (ShitShowAppKeyBind == 3)
    // {
    //     // [CTL]
    //     ++ShitShowAppKeyBind;
    //     esp_ble_mesh_cfg_client_set_state_t set_state = {0};
    //     example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
    //     common.ctx.addr = node->unicast;// + 2;
    //     set_state.model_app_bind.element_addr = node->unicast + 1;// + 1;// + 2;
    //     set_state.model_app_bind.model_app_idx = store.app_idx;
    //     set_state.model_app_bind.model_id = ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_TEMP_SRV;
    //     set_state.model_app_bind.company_id = ESP_BLE_MESH_CID_NVAL;
    //     err = esp_ble_mesh_config_client_set_state(&common, &set_state);
    //     if (err)
    //     {
    //         ESP_LOGE(TAG, "%s: Config Model App (3) Bind failed", __func__);
    //         return;
    //     }
    //     ESP_LOGW(TAG, "[CTL] Bound Light CTL model");
    // }
   
    else
    {
        init_done = true;
    }
}

////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
static void example_ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                              esp_ble_mesh_cfg_client_cb_param_t *param)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_node_info_t *node = NULL;
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
            init_done = false;

            ESP_LOGI(TAG, "composition data %s", bt_hex(param->status_cb.comp_data_status.composition_data->data, param->status_cb.comp_data_status.composition_data->len));

             process_composition_data(param);
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
            //esp_ble_mesh_composition_head head = {0};
            //esp_ble_mesh_composition_decode data = {0};

            // int ret = decode_comp_data(&head, &data, param->status_cb.comp_data_status.composition_data->data, param->status_cb.comp_data_status.composition_data->len);
            // if (ret == -1) {
            //     ESP_LOGE(TAG, "Failed to decode composition");
            // }
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
            // switch (opcode)
            // {
            // }
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

            mesh_example_info_store();
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

static void example_ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                               esp_ble_mesh_generic_client_cb_param_t *param)
{
    //esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_node_info_t *node = NULL;
    uint32_t opcode;
    uint16_t addr;
    //int err;

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
#if defined(APP_USE_ONOFF_CLIENT)
           // esp_ble_mesh_generic_client_set_state_t set_state = {0};
            node->onoff = param->status_cb.onoff_status.present_onoff;
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET onoff: 0x%02x", node->onoff);

#endif
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
            //esp_ble_mesh_generic_client_set_state_t set_state = {0};
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
    switch (event) {
    case ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT:
        ESP_LOGI("LIGHT_CLI", "Get response: ");
        break;

    case ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT:
        ESP_LOGI("LIGHT_CLI", "Set response: Error Code : %i", param->error_code);
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

static esp_err_t ble_mesh_init(void)
{
    // uint8_t match[2] = {0xdd, 0xdd};
    esp_err_t err = ESP_OK;

    store.net_idx = ESP_BLE_MESH_KEY_PRIMARY;
    store.app_idx = APP_KEY_IDX;
    memset(prov_key.app_key, APP_KEY_OCTET, sizeof(prov_key.app_key));
    
    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_client_callback(example_ble_mesh_config_client_cb);
    esp_ble_mesh_register_generic_client_callback(example_ble_mesh_generic_client_cb);
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

    // err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
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

    const uint8_t Bulduuid[16] = {0xF4, 0xA2, 0x56, 0x8B, 0x1E, 0x42, 0x4A, 0xF0, 0xA0, 0xB9, 0x39, 0x8B, 0x0E, 0x5E, 0xFE, 0x82};
    // esp_ble_mesh_bd_addr_t addr = {0xb0, 0xce, 0x18, 0xa8, 0xae, 0x87};
    // esp_ble_mesh_addr_type_t addr_type = 0;
    // esp_ble_mesh_prov_bearer_t bearer = (esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_GATT);
    // uint16_t oob_info = 2;
    uint16_t unicast_addr = 0x05;
    // int err2 = esp_ble_mesh_provisioner_prov_device_with_addr(Bulduuid, addr, addr_type, bearer, oob_info,unicast_addr);
    // if (err2 != ESP_OK)
    // {
    //    ESP_LOGE(TAG, "Failed to pre-provision ? (err %d)", err);
    // }
    // else
    {
        memcpy(GetNode(0)->uuid, Bulduuid, 16);
        GetNode(0)->unicast = unicast_addr;
        GetNode(0)->elem_num = 4;
        GetNode(0)->onoff = 1;
    }

    return err;
}

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#pragma region CLI UART


void cli_task(void *arg)
{
    uint8_t *data = (uint8_t *)malloc(UART_BUFFER_SIZE);
    if (data == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate UART buffer");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "CLI ready. Type 'help' for commands.");

    while (1)
    {
        int len = uart_read_bytes(UART_PORT_NUM, data, UART_BUFFER_SIZE - 1, pdMS_TO_TICKS(100));
        if (len > 0)
        {
            data[len] = '\0'; // Null-terminate input
            char *input = (char *)data;

            if (strncmp(input, "help", 4) == 0)
            {
                printf("\nAvailable commands:\n");
                printf("  Unprovision              - List provisioned nodes\n");
                printf("  help              - Show this help message\n\n");
            }
            else if (strncmp(input, "Unprovision", 11) == 0 || strncmp(input, "0", 1) == 0)
            {
                if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
                {
                    esp_ble_mesh_node_info_t *node = GetNode(0);
                    esp_ble_mesh_client_common_param_t common = {0};
                    esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                    example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_NODE_RESET);
                    
                    ShitShowAppKeyBind = 0;
                    int err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Failed to delete node [err=%d] [Node=5s]", err, bt_hex(node->uuid, 16));
                    }
                }
            }
            else
            {
                printf("Unknown command. Type 'help' for a list of commands.\n");
            }
        }
    }
    free(data);
    vTaskDelete(NULL);
}
#pragma endregion

enum ModeType
{
 BRIGHTNESS,
//  LIGHT_CTL,
//  LIGHT_CTL_TEMP,
 TEMPERATURE,
 HUE_H,
 HUE_S,
 HUE_L,
 MAX_VALUE
};

enum ModeType CurrentModeType = BRIGHTNESS;

static const char* ModeName[] = {"BRIGHTNESS", "TEMPERATURE","HUE_H","HUE_S","HUE_L", "Light HSL Hue Server",
                                    "Light HSL Saturation Server"};
void NextMode()
{
    CurrentModeType = (ModeType)(CurrentModeType + 1);
    if (CurrentModeType == MAX_VALUE)
    {
        CurrentModeType = (ModeType)0;
    }
    
    ESP_LOGW(TAG, "%s: %d : %s", __func__, CurrentModeType, ModeName[CurrentModeType]);
}

void SendBrightness()
{
    if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ESP_LOGI(TAG, "Sending brightness level : %d", LastRaw);
        esp_ble_mesh_node_info_t *node = GetNode(0);
        esp_ble_mesh_client_common_param_t common = {};
        esp_ble_mesh_generic_client_set_state_t set_state = {};

        node->level = (int16_t)map(LastRaw, 0, 4095, -32768, 32767);

        example_ble_mesh_set_msg_common(&common, node, level_client.model, ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET);
        set_state.level_set.level = node->level;
        set_state.level_set.op_en = false;
        set_state.level_set.delay = 0;
        set_state.level_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
        int err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: [BRIGHTNESS] : Set failed : %i", __func__, err);
            return;
        }
    }
}

void SendTemperature()
{
    if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ESP_LOGI(TAG, "Sending temperature level : %d", LastRaw);
        esp_ble_mesh_node_info_t *node = GetNode(0);
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_generic_client_set_state_t set_state = {0};

        example_ble_mesh_set_msg_common(&common, node, level_client.model, ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET);
        common.ctx.addr = node->unicast + 1;
        set_state.level_set.level = (int16_t)map(LastRaw, 0, 4095, -32768, 32767);
        set_state.level_set.op_en = false;
        set_state.level_set.delay = 0;
        set_state.level_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
        int err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: [TEMPERATURE] : Set failed : %i", __func__, err);
            return;
        }
    }
}

void SendLightCtl()
{
    if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ESP_LOGI(TAG, "Sending LightCtl level : %d", LastRaw);
        esp_ble_mesh_node_info_t *node = GetNode(0);
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state = {0};

        example_ble_mesh_set_msg_common(&common, node, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET);
        //common.ctx.addr +=1;// +=3;
        set_state.ctl_set.ctl_temperature = (uint16_t)map(LastRaw, 0, 4095, 0, 65535);
        set_state.ctl_set.ctl_lightness = 16000;
        set_state.ctl_set.ctl_delta_uv = 256;
        set_state.ctl_set.op_en = false;
        set_state.ctl_set.delay = 0;
        set_state.ctl_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
        int err = esp_ble_mesh_light_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: [LIGHT_CTL] Set failed", __func__);
            return;
        }
    }
}

void SendLightCtl_Temp()
{
    if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ESP_LOGI(TAG, "Sending LIghtCtl level : %d", LastRaw);
        esp_ble_mesh_node_info_t *node = GetNode(0);
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state = {0};

        example_ble_mesh_set_msg_common(&common, node, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET);
        common.ctx.addr +=1;// +=3;
        set_state.ctl_temperature_set.ctl_temperature = (uint16_t)map(LastRaw, 0, 4095, 0, 65535);
        //set_state.ctl_temperature_set.ctl_lightness = 32000;
        set_state.ctl_temperature_set.ctl_delta_uv = 256;
        set_state.ctl_temperature_set.op_en = false;
        set_state.ctl_temperature_set.delay = 0;
        set_state.ctl_temperature_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
        int err = esp_ble_mesh_light_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: [LIGHT_CTL] debounce_timer_callback : Set failed", __func__);
            return;
        }
    }
}

void SendHSL(enum ModeType Mode)
{
    if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ESP_LOGI(TAG, "Sending HSL level : %d", LastRaw);
        esp_ble_mesh_node_info_t *node = GetNode(0);
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state = {0};

        example_ble_mesh_set_msg_common(&common, node, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET);

        if (Mode == HUE_H)
        {
            uint16_t filteredValue= (uint16_t)map(LastRaw, 0, 4095, 0, 65535);
            node->hsl_h = filteredValue;
        }
        if (Mode == HUE_S)
        {
            uint16_t filteredValue= (uint16_t)map(LastRaw, 0, 4095, 0, 65535);
            node->hsl_s = filteredValue;
        }

        if (Mode == HUE_L)
        {
            uint16_t filteredValue= (uint16_t)map(LastRaw, 0, 4095, 0, 65535);
            node->hsl_l = filteredValue;
        }
        set_state.hsl_set.hsl_hue = node->hsl_h;
        set_state.hsl_set.hsl_saturation = node->hsl_s;
        set_state.hsl_set.hsl_lightness = node->hsl_l;
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

//void app_main(void)
extern "C" void app_main()
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing...");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err)
    {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    /* Open nvs namespace for storing/restoring mesh example info */
    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err)
    {
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err)
    {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    initUART();
    initDebugGPIO();
    xTaskCreate(cli_task, "cli_task", 4096, NULL, 5, NULL);

    while (1)
    {
        // Read raw ADC value
        int16_t raw = adc1_get_raw(POTENTIOMETER_ADC_CHANNEL);
        if (abs(raw - LastRaw) > 200 && init_done)
        {
            //ESP_LOGE(TAG, "RAW MOTHERFUCKER Last : %i  New : %i)",LastRaw, raw);
            LastRaw = raw;
            //int16_t filteredValue = (int16_t)map(raw, 0, 4095, -32768, 32767);
            NormalizedLevelValue = (int16_t)map(raw, 0, 4095, 0, 65535);
            switch (CurrentModeType)
            {
            case BRIGHTNESS:
                SendBrightness();
                break;
            // case LIGHT_CTL:
            //     SendLightCtl();
            //     break;
            // case LIGHT_CTL_TEMP:
            //  SendLightCtl_Temp();
            //  break;
            case TEMPERATURE:
                SendTemperature();
                break;
            case HUE_H:
                SendHSL(CurrentModeType);
                break;
            case HUE_S:
                SendHSL(CurrentModeType);
                break;
            case HUE_L:
                SendHSL(CurrentModeType);
                break;
            break;
            default:
                break;
            }
            
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Delay 500msw
    }
}

void SendGenericOnOff()
{
    if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_node_info_t *node = GetNode(0);
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_generic_client_set_state_t set_state = {0};
        ESP_LOGI(TAG, "Callback Before :  onoff: 0x%02x", node->onoff);
        // node->onoff = !node->onoff;
        // ESP_LOGI(TAG, "Callback After :  onoff: 0x%02x", node->onoff);
        example_ble_mesh_set_msg_common(&common, node, onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET);
        set_state.onoff_set.op_en = false;
        set_state.onoff_set.onoff = !node->onoff;
        set_state.onoff_set.tid = store.tid++;
        int err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
        if (err)
        {
            ESP_LOGE(TAG, "%s: debounce_timer_callback : Generic OnOff Set failed", __func__);
            return;
        }
    }
}



// void SEND_LIGHT_HSL_HUE_SERVER()
// {
//  if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
//     {
//         ESP_LOGI(TAG, "SEND_LIGHT_HSL_HUE_SERVER : %d", NormalizedLevelValue);
//         esp_ble_mesh_node_info_t *node = GetNode(0);
//         esp_ble_mesh_client_common_param_t common = {0};
//         //esp_ble_mesh_light_client_set_state_t set_state = {0};

//         // example_ble_mesh_set_msg_common(&common, node, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_HUE_SET);
//         // common.ctx.addr +=4;
//         // set_state.hsl_hue_set.hue = NormalizedLevelValue;
//         // set_state.hsl_hue_set.op_en = false;
//         // set_state.hsl_hue_set.delay = 0;
//         // set_state.hsl_hue_set.tid = store.tid++; 
//         // int err = esp_ble_mesh_light_client_set_state(&common, &set_state);
//         // if (err)
//         // {
//         //     ESP_LOGE(TAG, "%s: Set failed", __func__);
//         //     return;
//         // }

//        // node->level = NormalizedLevelValue;

//         esp_ble_mesh_generic_client_set_state_t set_state = {0};
//         example_ble_mesh_set_msg_common(&common, node, level_client.model, ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET);
//         common.ctx.addr = node->unicast + 2;
//         set_state.level_set.level = NormalizedLevelValue;
//         set_state.level_set.op_en = false;
//         set_state.level_set.delay = 0;
//         set_state.level_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
//         int err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
//         if (err)
//         {
//             ESP_LOGE(TAG, "%s: [TEMPERATURE] : Set failed : %i", __func__, err);
//             return;
//         }
//     }
// }

// void SEND_LIGHT_HSL_SATURATION_SERVER()
// {
// if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
//     {
//         ESP_LOGI(TAG, "SEND_LIGHT_HSL_SATURATION_SERVER : %d", NormalizedLevelValue);
//         esp_ble_mesh_node_info_t *node = GetNode(0);
//         esp_ble_mesh_client_common_param_t common = {0};
//         esp_ble_mesh_light_client_set_state_t set_state = {0};

//         example_ble_mesh_set_msg_common(&common, node, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SATURATION_SET);
//         common.ctx.addr +=3;
//         set_state.hsl_saturation_set.saturation = NormalizedLevelValue;
//         set_state.hsl_saturation_set.op_en = false;
//         set_state.hsl_saturation_set.delay = 0;
//         set_state.hsl_saturation_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
//         int err = esp_ble_mesh_light_client_set_state(&common, &set_state);
//         if (err)
//         {
//             ESP_LOGE(TAG, "%s: Set failed", __func__);
//             return;
//         }
//     }
// }