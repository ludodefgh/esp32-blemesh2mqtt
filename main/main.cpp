#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <memory>

#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_ble_mesh_defs.h"
#include "esp_console.h"
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_example_init.h"

//// MQTT includes start
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "mqtt_client.h"
//// MQTT includes end

#include "cJSON.h"
#include "ble_mesh.h"
#include "debugUART.h"
#include "debugGPIO.h"
#include "modelsConfig.h"
#include "provisioning.h"
#include "nodesManager.h"
#include "_config.h"

#define TAG "EXAMPLE"

// static int16_t NormalizedLevelValue = 0;
// static int16_t LastRaw = 0;
extern bool init_done;

/// @brief Provisioning stuff

static nvs_handle_t NVS_HANDLE;

extern void wifi_init_sta(void);

static int heap_size(int argc, char **argv)
{
    uint32_t heap_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "min heap size: %" PRIu32, heap_size);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MQTT Setup
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region MQTTSetup
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static esp_mqtt5_publish_property_config_t publish_property = {
    .payload_format_indicator = 1,
    .message_expiry_interval = 1000,
    .topic_alias = 0,
    .response_topic = "/topic/test/response",
    .correlation_data = "123456",
    .correlation_data_len = 6,
};

static esp_mqtt5_subscribe_property_config_t subscribe_property = {
    .subscribe_id = 25555,
    .no_local_flag = false,
    .retain_as_published_flag = false,
    .retain_handle = 0,
    // .is_share_subscribe = true,
    // .share_name = "group1",
};

static esp_mqtt5_unsubscribe_property_config_t unsubscribe_property = {
    .is_share_subscribe = false,
    //.share_name = "group1",
};

static esp_mqtt5_disconnect_property_config_t disconnect_property = {
    .session_expiry_interval = 60,
    .disconnect_reason = 0,
};

// FWD
void ReadJsonResponse(const char *input);


std::string get_command_topic(esp_ble_mesh_node_info_t* node_info)
{
    if (esp_ble_mesh_node_t *mesh_node = esp_ble_mesh_provisioner_get_node_with_uuid(node_info->uuid))
    {
        char buf[64] = {0};
        snprintf(buf, sizeof(buf), "blemesh2mqtt_%s", bt_hex(mesh_node->addr, BD_ADDR_LEN));

        const std::string root_publish = "blemesh2mqtt/" + std::string{buf} + "/set";

        return root_publish;
    }
    return {};
}

std::string get_discovery_id(esp_ble_mesh_node_info_t* node_info)
{
    if (esp_ble_mesh_node_t *mesh_node = esp_ble_mesh_provisioner_get_node_with_uuid(node_info->uuid))
    {
        char buf[64] = {0};
        snprintf(buf, sizeof(buf), "homeassistant/light/blemesh2mqtt_%s", bt_hex(mesh_node->addr, BD_ADDR_LEN));

        std::string uniq_id = std::string{buf} + "_light/config";

        return uniq_id;
    }
    return {};
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt5_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id = 0;

    ESP_LOGD(TAG, "free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt5_client_set_publish_property(client, &publish_property);
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 1);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        esp_mqtt5_client_set_subscribe_property(client, &subscribe_property);
        if (esp_ble_mesh_node_info_t* node_info = GetNode(0); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
        {
            msg_id = esp_mqtt_client_subscribe(client, get_command_topic(node_info).c_str(), 0);
        }
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // esp_mqtt5_client_set_unsubscribe_property(client, &unsubscribe_property);
        // msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos0");
        // ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        // esp_mqtt5_client_delete_user_property(unsubscribe_property.user_property);
        // unsubscribe_property.user_property = NULL;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // esp_mqtt5_client_set_publish_property(client, &publish_property);
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        esp_mqtt5_client_set_disconnect_property(client, &disconnect_property);
        esp_mqtt5_client_delete_user_property(disconnect_property.user_property);
        disconnect_property.user_property = NULL;
        esp_mqtt_client_disconnect(client);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        // ESP_LOGI(TAG, "payload_format_indicator is %d", event->property->payload_format_indicator);
        // ESP_LOGI(TAG, "response_topic is %.*s", event->property->response_topic_len, event->property->response_topic);
        // ESP_LOGI(TAG, "correlation_data is %.*s", event->property->correlation_data_len, event->property->correlation_data);
        // ESP_LOGI(TAG, "content_type is %.*s", event->property->content_type_len, event->property->content_type);
        ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);

        ReadJsonResponse(event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        ESP_LOGI(TAG, "MQTT5 return code is %d", event->error_handle->connect_return_code);
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}
esp_mqtt_client_handle_t mqtt_client;

static void mqtt5_app_start(void)
{
    ESP_LOGI(TAG, "mqtt5_app_start");

    esp_mqtt5_connection_property_config_t connect_property = {
        .session_expiry_interval = 10,
        .maximum_packet_size = 1024,
        .receive_maximum = 65535,
        .topic_alias_maximum = 2,
        .request_resp_info = true,
        .request_problem_info = true,
        .will_delay_interval = 10,
        .message_expiry_interval = 10,
        .payload_format_indicator = true,
        .response_topic = "/test/response",
        .correlation_data = "123456",
        .correlation_data_len = 6,
    };

    esp_mqtt_client_config_t mqtt5_cfg{}; // = {

    mqtt5_cfg.broker.address.hostname = "192.168.2.194"; // CONFIG_BROKER_URL;
    mqtt5_cfg.broker.address.transport = esp_mqtt_transport_t::MQTT_TRANSPORT_OVER_TCP;
    mqtt5_cfg.broker.address.port = 1883;
    mqtt5_cfg.session.protocol_ver = MQTT_PROTOCOL_V_5;
    mqtt5_cfg.network.disable_auto_reconnect = true;
    mqtt5_cfg.credentials.username = config_mqtt_user;
    mqtt5_cfg.credentials.authentication.password = config_mqtt_pwd;
    mqtt5_cfg.session.last_will.topic = "/topic/will";
    mqtt5_cfg.session.last_will.msg = "i will leave";
    mqtt5_cfg.session.last_will.msg_len = 12;
    mqtt5_cfg.session.last_will.qos = 1;
    mqtt5_cfg.session.last_will.retain = true;

    mqtt_client = esp_mqtt_client_init(&mqtt5_cfg);

    /* Set connection properties and user properties */
    esp_mqtt5_client_set_connect_property(mqtt_client, &connect_property);

    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(mqtt_client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt5_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}
#pragma endregion MQTTSetup

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MQTT messages
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region MQttMessages

namespace std
{
    template <>
    struct default_delete<cJSON>
    {
        void operator()(cJSON *ptr) const
        {
            cJSON_Delete(ptr);
        }
    };
}

/*

To remove the component, publish an empty string to the discovery topic. This will remove the component and clear the published discovery payload. It will also remove the device entry if there are no further references to it.
*/
std::unique_ptr<cJSON> make_discovery_message(esp_ble_mesh_node_info_t *node)
{
    cJSON *root = cJSON_CreateObject();

    if (esp_ble_mesh_node_t *mesh_node = esp_ble_mesh_provisioner_get_node_with_uuid(node->uuid))
    {
        char buf[64] = {0};
        snprintf(buf, sizeof(buf), "blemesh2mqtt_%s", bt_hex(mesh_node->addr, BD_ADDR_LEN));

        // Set Device section
        {
            cJSON *dev = nullptr;
            cJSON_AddItemToObject(root, "dev", dev = cJSON_CreateObject());
            if (dev != nullptr)
            {
                cJSON_AddItemToObject(dev, "name", cJSON_CreateString("light"));
                cJSON_AddItemToObject(dev, "ids", cJSON_CreateString(buf));
            }
        }

        // set origin section
        {
            cJSON *origin = nullptr;
            cJSON_AddItemToObject(root, "o", origin = cJSON_CreateObject());
            if (origin)
            {
                cJSON_AddItemToObject(origin, "name", cJSON_CreateString("blemesh2mqtt"));
                cJSON_AddItemToObject(origin, "sw", cJSON_CreateString("0.0.1"));
            }
        }

        const std::string root_publish = "blemesh2mqtt/" + std::string{buf};
        cJSON_AddItemToObject(root, "~", cJSON_CreateString(root_publish.c_str()));
        cJSON_AddItemToObject(root, "name", cJSON_CreateNull());
        const std::string uniq_id = std::string{buf} + "_light";
        cJSON_AddItemToObject(root, "uniq_id", cJSON_CreateString(uniq_id.c_str()));
        cJSON_AddItemToObject(root, "cmd_t", cJSON_CreateString("~/set"));
        cJSON_AddItemToObject(root, "stat_t", cJSON_CreateString("~/state"));
        cJSON_AddItemToObject(root, "schema", cJSON_CreateString("json"));
        cJSON_AddItemToObject(root, "brightness", cJSON_CreateBool(1));
        cJSON_AddItemToObject(root, "sup_clrm", cJSON_CreateString("hs"));
    }

    return std::unique_ptr<cJSON>{root};
}

std::unique_ptr<cJSON> make_status_message(esp_ble_mesh_node_info_t* node_info)
{
    cJSON *root, *color;
    root = cJSON_CreateObject();

    if (node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        cJSON_AddNumberToObject(root, "brightness", (uint16_t)map(node_info->hsl_l, 0, 32767, 0, 255));
        cJSON_AddStringToObject(root, "color_mode", "hs");
        cJSON_AddStringToObject(root, "state", node_info->onoff ? "ON" : "OFF");

        cJSON_AddItemToObject(root, "color", color = cJSON_CreateObject());

        cJSON_AddNumberToObject(color, "h", (uint16_t)map(node_info->hsl_h, 0, 65535, 0, 360));
        cJSON_AddNumberToObject(color, "s", (uint16_t)map(node_info->hsl_s, 0, 65535, 0, 100));
    }

    return std::unique_ptr<cJSON>{root};
    ;
}

// FWD
int send_status(int argc, char **argv);

void ReadJsonResponse(const char *input)
{
    if (GetNode(0)->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        if (cJSON *response = cJSON_Parse(input))
        {
            if (const cJSON *name = cJSON_GetObjectItemCaseSensitive(response, "state"))
            {
                if (cJSON_IsString(name) && (name->valuestring != NULL))
                {
                    if (strcmp(name->valuestring, "ON") == 0)
                    {
                        ESP_LOGI(TAG, "SendGenericOnOff(true)");
                        SendGenericOnOff(true);
                    }
                    else if (strcmp(name->valuestring, "OFF") == 0)
                    {
                        ESP_LOGI(TAG, "SendGenericOnOff(false)");
                        SendGenericOnOff(false);
                    }
                }

                bool update_hsl = false;
                if (cJSON *brightness = cJSON_GetObjectItemCaseSensitive(response, "brightness"))
                {
                    if (cJSON_IsNumber(brightness))
                    {
                        // FIX-ME : might be related to my buld, or check server config for value range
                        uint16_t filteredValue = (uint16_t)map(brightness->valuedouble, 0, 255, 0, 32767);
                        GetNode(0)->hsl_l = filteredValue;
                        update_hsl = true;
                    }
                }

                if (cJSON *color = cJSON_GetObjectItemCaseSensitive(response, "color"))
                {
                    if (cJSON_IsObject(color))
                    {
                        cJSON *hue = cJSON_GetObjectItemCaseSensitive(color, "h");
                        cJSON *saturation = cJSON_GetObjectItemCaseSensitive(color, "s");

                        if (cJSON_IsNumber(hue))
                        {
                            uint16_t filteredValue = (uint16_t)map(hue->valuedouble, 0, 360, 0, 65535);
                            GetNode(0)->hsl_h = filteredValue;
                            update_hsl = true;
                        }
                        if (cJSON_IsNumber(saturation))
                        {
                            uint16_t filteredValue = (uint16_t)map(saturation->valuedouble, 0, 100, 0, 65535);
                            GetNode(0)->hsl_s = filteredValue;
                            update_hsl = true;
                        }
                    }
                }

                if (update_hsl)
                {
                    SendHSL();
                }
            }
            cJSON_Delete(response);
        }

        send_status(0, nullptr);
    }
}

int send_discovery(int argc, char **argv)
{
    if (esp_ble_mesh_node_info_t* node_info = GetNode(0); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        std::unique_ptr<cJSON> discovery_message = make_discovery_message(GetNode(0));
        char *json_data = cJSON_PrintUnformatted(discovery_message.get());
        int msg_id = 0;
        msg_id = esp_mqtt_client_publish(mqtt_client, get_discovery_id(node_info).c_str(), json_data, 0, 0, 0);
        ESP_LOGI(TAG, "sent discovery publish successful, msg_id=%d", msg_id);

        cJSON_free(json_data);
    }

    return 0;
}

int delete_entity(int argc, char **argv)
{
    if (esp_ble_mesh_node_info_t* node_info = GetNode(0); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        const char *json_data = "";
        int msg_id = 0;
        msg_id = esp_mqtt_client_publish(mqtt_client, get_discovery_id(node_info).c_str(), json_data, 0, 0, 0);
        ESP_LOGI(TAG, "sent discovery publish successful, msg_id=%d", msg_id);
    }

    return 0;
}

int send_status(int argc, char **argv)
{
    if (esp_ble_mesh_node_info_t* node_info = GetNode(0); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        if (esp_ble_mesh_node_t *mesh_node = esp_ble_mesh_provisioner_get_node_with_uuid(node_info->uuid))
        {
            char buf[64] = {0};
            snprintf(buf, sizeof(buf), "blemesh2mqtt_%s", bt_hex(mesh_node->addr, BD_ADDR_LEN));
            const std::string root_publish = "blemesh2mqtt/" + std::string{buf} + "/state";

            std::unique_ptr<cJSON> status_message = make_status_message(node_info);
            char *json_data = cJSON_PrintUnformatted(status_message.get());
            int msg_id = 0;

            msg_id = esp_mqtt_client_publish(mqtt_client, root_publish.c_str(), json_data, 0, 0, 0);
            ESP_LOGI(TAG, "sent status publish successful, msg_id=%d", msg_id);

            cJSON_free(json_data);
        }
        
    }

    return 0;
}

#pragma endregion MQttMessages

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DEBUG
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Debug
// fwd decl
static int toggle_on_off(int argc, char **argv)
{
    SendGenericOnOffToggle();
    return 0;
}

void RegisterDebugCommands()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    // init console REPL environment
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    /* Register commands */

    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "get min free heap size during test",
        .hint = NULL,
        .func = &heap_size,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap_cmd));

    const esp_console_cmd_t toggleLight_cmd = {
        .command = "toggle",
        .help = "toggle connected lights on/off",
        .hint = NULL,
        .func = &toggle_on_off,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&toggleLight_cmd));

    const esp_console_cmd_t discovery_cmd = {
        .command = "mqtt_discovery",
        .help = "[MQTT] send discovery message",
        .hint = NULL,
        .func = &send_discovery,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&discovery_cmd));

    const esp_console_cmd_t delete_entity_cmd = {
        .command = "mqtt_delete",
        .help = "[MQTT] delete entity",
        .hint = NULL,
        .func = &delete_entity,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&delete_entity_cmd));

    const esp_console_cmd_t mqtt_status_cmd = {
        .command = "mqtt_status",
        .help = "[MQTT] send status message",
        .hint = NULL,
        .func = &send_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mqtt_status_cmd));

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
#pragma endregion Debug

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Main

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

    RegisterDebugCommands();

    RegisterProvisioningDebugCommands();

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err)
    {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL)
    {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", static_cast<esp_log_level_t>(CONFIG_LOG_MAXIMUM_LEVEL));
    }

    // ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

#if defined(DEBUG_USE_GPIO)
    initDebugGPIO();
#endif

    RefreshNodes();

    /// MQTT START
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    mqtt5_app_start();
    /// MQTT END

    // while (1)
    // {
    //     // Read raw ADC value
    //     int16_t raw = adc1_get_raw(POTENTIOMETER_ADC_CHANNEL);
    //     if (abs(raw - LastRaw) > 400 && init_done)
    //     {
    //         // ESP_LOGE(TAG, "RAW MOTHERFUCKER Last : %i  New : %i)",LastRaw, raw);
    //         LastRaw = raw;
    //         // int16_t filteredValue = (int16_t)map(raw, 0, 4095, -32768, 32767);
    //         NormalizedLevelValue = (int16_t)map(raw, 0, 4095, 0, 65535);
    //         switch (CurrentModeType)
    //         {
    //         case BRIGHTNESS:
    //            // SendBrightness();
    //             break;
    //         // case LIGHT_CTL:
    //         //     SendLightCtl();
    //         //     break;
    //         // case LIGHT_CTL_TEMP:
    //         //  SendLightCtl_Temp();
    //         //  break;
    //         case TEMPERATURE:
    //            // SendTemperature();
    //             break;
    //         case HUE_H:
    //            // SendHSL(CurrentModeType);
    //             break;
    //         case HUE_S:
    //            // SendHSL(CurrentModeType);
    //             break;
    //         case HUE_L:
    //            // SendHSL(CurrentModeType);
    //             break;
    //             break;
    //         default:
    //             break;
    //         }
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(100)); // Delay 500msw
    // }
}
#pragma endregion Main