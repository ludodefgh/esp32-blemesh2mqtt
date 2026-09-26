/*
 * HSL light test node for the blemesh2mqtt bridge's External Mesh Node tests.
 *
 * One element: Generic OnOff, Generic Level, Light Lightness (+Setup) and Light HSL
 * (+Setup) Servers, all auto-responding. The app keeps the bound states consistent
 * the way a real bulb does (OnOff <-> Lightness <-> HSL lightness) and logs one
 * "STATE ..." line after every change, so tests can read the bulb's real state.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "board.h"
#include "ble_mesh_example_init.h"

#define TAG "HSL_NODE"

#define CID_ESP 0x02E5

// Deliberately not the full 0..0xFFFF, so the bridge's Range Get handling is exercised.
#define LIGHTNESS_RANGE_MIN 0x0001
#define LIGHTNESS_RANGE_MAX 0xFFFF
#define HUE_RANGE_MIN       0x0000
#define HUE_RANGE_MAX       0xFFFF
#define SAT_RANGE_MIN       0x0000
#define SAT_RANGE_MAX       0xFFFF

static uint8_t dev_uuid[16] = { 0xdd, 0xdd };

static esp_ble_mesh_cfg_srv_t config_server = {
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
    .default_ttl = 7,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_pub, 2 + 3, ROLE_NODE);
static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(level_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_gen_level_srv_t level_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
};

static esp_ble_mesh_light_lightness_state_t lightness_state;

ESP_BLE_MESH_MODEL_PUB_DEFINE(lightness_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_light_lightness_srv_t lightness_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &lightness_state,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(lightness_setup_pub, 2 + 5, ROLE_NODE);
static esp_ble_mesh_light_lightness_setup_srv_t lightness_setup_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &lightness_state,
};

static esp_ble_mesh_light_hsl_state_t hsl_state;

ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_hsl_srv_t hsl_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &hsl_state,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(hsl_setup_pub, 2 + 9, ROLE_NODE);
static esp_ble_mesh_light_hsl_setup_srv_t hsl_setup_server = {
    .rsp_ctrl = {
        .get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
        .set_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    },
    .state = &hsl_state,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_pub, &onoff_server),
    ESP_BLE_MESH_MODEL_GEN_LEVEL_SRV(&level_pub, &level_server),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SRV(&lightness_pub, &lightness_server),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_SETUP_SRV(&lightness_setup_pub, &lightness_setup_server),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SRV(&hsl_pub, &hsl_server),
    ESP_BLE_MESH_MODEL_LIGHT_HSL_SETUP_SRV(&hsl_setup_pub, &hsl_setup_server),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

static void log_state(const char *cause)
{
    ESP_LOGI(TAG, "STATE (%s) onoff=%u lightness=%u hsl(h=%u s=%u l=%u)",
             cause, onoff_server.state.onoff, lightness_state.lightness_actual,
             hsl_state.hue, hsl_state.saturation, hsl_state.lightness);
}

// Keep OnOff, Light Lightness Actual and HSL Lightness consistent, like a real bulb.
static void apply_lightness(uint16_t lightness)
{
    lightness_state.lightness_actual = lightness;
    hsl_state.lightness = lightness;
    onoff_server.state.onoff = lightness > 0;
    if (lightness > 0) {
        lightness_state.lightness_last = lightness;
    }
}

static void apply_onoff(uint8_t onoff)
{
    if (onoff) {
        apply_lightness(lightness_state.lightness_last ? lightness_state.lightness_last : LIGHTNESS_RANGE_MAX);
    } else {
        apply_lightness(0);
    }
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Provisioned: net_idx 0x%04x, addr 0x%04x",
                 param->node_prov_complete.net_idx, param->node_prov_complete.addr);
        board_led_operation(LED_G, LED_OFF);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGW(TAG, "Node reset");
        esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        break;
    default:
        break;
    }
}

static void generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                              esp_ble_mesh_generic_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT) {
        return;
    }
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK:
        apply_onoff(param->value.state_change.onoff_set.onoff);
        log_state("onoff set");
        break;
    case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
    case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET_UNACK:
        ESP_LOGI(TAG, "level set %d", param->value.state_change.level_set.level);
        break;
    default:
        break;
    }
}

static void lighting_server_cb(esp_ble_mesh_lighting_server_cb_event_t event,
                               esp_ble_mesh_lighting_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_LIGHTING_SERVER_STATE_CHANGE_EVT) {
        return;
    }
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET:
    case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET_UNACK:
        apply_lightness(param->value.state_change.lightness_set.lightness);
        log_state("lightness set");
        break;
    case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET:
    case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET_UNACK:
        apply_lightness(param->value.state_change.hsl_set.lightness);
        log_state("hsl set");
        break;
    default:
        break;
    }
}

static void config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                             esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        return;
    }
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        ESP_LOGI(TAG, "AppKey added, app_idx 0x%04x", param->value.state_change.appkey_add.app_idx);
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        ESP_LOGI(TAG, "Model 0x%04x bound to app_idx 0x%04x",
                 param->value.state_change.mod_app_bind.model_id, param->value.state_change.mod_app_bind.app_idx);
        break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
        ESP_LOGI(TAG, "Model 0x%04x subscribed to 0x%04x",
                 param->value.state_change.mod_sub_add.model_id, param->value.state_change.mod_sub_add.sub_addr);
        break;
    default:
        break;
    }
}

static esp_err_t ble_mesh_init(void)
{
    lightness_state.lightness_range_min = LIGHTNESS_RANGE_MIN;
    lightness_state.lightness_range_max = LIGHTNESS_RANGE_MAX;
    lightness_state.lightness_last = LIGHTNESS_RANGE_MAX;
    hsl_state.hue_range_min = HUE_RANGE_MIN;
    hsl_state.hue_range_max = HUE_RANGE_MAX;
    hsl_state.saturation_range_min = SAT_RANGE_MIN;
    hsl_state.saturation_range_max = SAT_RANGE_MAX;
    apply_lightness(0); // boots off

    esp_ble_mesh_register_prov_callback(provisioning_cb);
    esp_ble_mesh_register_config_server_callback(config_server_cb);
    esp_ble_mesh_register_generic_server_callback(generic_server_cb);
    esp_ble_mesh_register_lighting_server_callback(lighting_server_cb);

    esp_err_t err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
        return err;
    }

    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
        return err;
    }

    ESP_LOGI(TAG, "HSL test node initialized");
    log_state("boot");
    board_led_operation(LED_G, LED_ON);
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing...");
    board_init();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }
}
