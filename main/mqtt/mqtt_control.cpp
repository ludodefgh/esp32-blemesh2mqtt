#include "mqtt_control.h"

//// MQTT includes start
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "esp_console.h"
//// MQTT includes end

#include "ble_mesh/ble_mesh_node.h"
#include "ble_mesh/ble_mesh_control.h"
#include "debug_console_common.h"
#include "mqtt_bridge.h"
#include <memory>
#include <string>
#include "cJSON.h"
#include "_config.h"
#include <ble_mesh/ble_mesh_commands.h>

#define TAG "APP_MQTT"

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

std::string get_command_topic(const bm2mqtt_node_info *node_info)
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

std::string get_discovery_id(bm2mqtt_node_info *node_info)
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

void subscribe_nodes(esp_mqtt_client_handle_t client)
{
    node_manager().for_each_node([&client](const bm2mqtt_node_info *node_info)
    {
        int msg_id = esp_mqtt_client_subscribe(client, get_command_topic(node_info).c_str(), 0);
        //send_status(node_info);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
    });
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
       
        subscribe_nodes(client);
        mqtt_bridge_subscribe(client);

         start_periodic_publish_timer();

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

        parse_mqtt_event_data(event);
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

esp_mqtt_client_handle_t get_mqtt_client()
{
    return mqtt_client;
}

void mqtt5_app_start(void)
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
    mqtt5_cfg.session.last_will.topic = "blemesh2mqtt/bridge/state";
    mqtt5_cfg.session.last_will.msg = "offline";
    mqtt5_cfg.session.last_will.msg_len = 8;
    mqtt5_cfg.session.last_will.qos = 1;
    mqtt5_cfg.session.last_will.retain = true;

    mqtt_client = esp_mqtt_client_init(&mqtt5_cfg);

    /* Set connection properties and user properties */
    esp_mqtt5_client_set_connect_property(mqtt_client, &connect_property);

    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(mqtt_client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt5_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    {
        int msg_id = esp_mqtt_client_publish(get_mqtt_client(), "blemesh2mqtt/bridge/state", "on", 0, 0, 0);
        ESP_LOGI(TAG, "Sent state message, msg_id=%d", msg_id);
    }

    {
        send_bridge_discovery();
    }
    // {
    //     mqtt_init_periodic_info();
    // }
    
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
std::unique_ptr<cJSON> make_discovery_message(bm2mqtt_node_info *node)
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
        cJSON *sup_clrm = nullptr;
        cJSON_AddItemToObject(root, "sup_clrm", sup_clrm = cJSON_CreateArray());
        if (sup_clrm != nullptr)
        {
            cJSON_AddItemToArray(sup_clrm, cJSON_CreateString("hs"));
            cJSON_AddItemToArray(sup_clrm, cJSON_CreateString("color_temp"));
        }

        cJSON_AddItemToObject(root, "min_kelvin", cJSON_CreateNumber(node->min_temp));
        cJSON_AddItemToObject(root, "max_kelvin", cJSON_CreateNumber(node->max_temp));

        cJSON_AddItemToObject(root, "color_temp_kelvin", cJSON_CreateBool(1));
    }

    return std::unique_ptr<cJSON>{root};
}

std::unique_ptr<cJSON> make_status_message(const bm2mqtt_node_info *node_info)
{
    cJSON *root, *color;
    root = cJSON_CreateObject();

    if (node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        cJSON_AddNumberToObject(root, "brightness", (uint16_t)map(node_info->hsl_l, 0, 32767, 0, 255));
        cJSON_AddStringToObject(root, "color_mode", node_info->color_mode == color_mode_t::hs ? "hs" : "color_temp");
        cJSON_AddStringToObject(root, "state", node_info->onoff ? "ON" : "OFF");
        cJSON_AddNumberToObject(root, "color_temp", node_info->curr_temp);

        cJSON_AddItemToObject(root, "color", color = cJSON_CreateObject());
        cJSON_AddNumberToObject(color, "h", (uint16_t)map(node_info->hsl_h, 0, 65535, 0, 360));
        cJSON_AddNumberToObject(color, "s", (uint16_t)map(node_info->hsl_s, 0, 65535, 0, 100));
    }

    return std::unique_ptr<cJSON>{root};
}

void parse_mqtt_event_data(esp_mqtt_event_handle_t event)
{
    if (strncmp(event->topic, "blemesh2mqtt/bridge/auto_provision/set", event->topic_len) == 0)
    {
        //buffer_length = strlen(event->data) + sizeof("");
        ESP_LOGI(TAG, "Received auto_provision command from MQTT: [%.*s]", event->data_len, event->data  );

        ble_mesh_set_provisioning_enabled(strncmp(event->data, "ON",event->data_len) == 0);
   
        return;
    }

    // FIX-ME : likely slow af
    const std::string topic{event->topic, static_cast<std::string::size_type>(event->topic_len)};
    if (auto index_pos = topic.find("blemesh2mqtt_"); index_pos != std::string::npos)
    {
        // +13 : sizeof blemesh2mqtt_
        // 12 sizeof mac address string
        const std::string mac{topic, index_pos + 13, 12};
        if (bm2mqtt_node_info *node_info = node_manager().get_node(mac))
        {
            if (cJSON *response = cJSON_Parse(event->data))
            {
                if (const cJSON *name = cJSON_GetObjectItemCaseSensitive(response, "state"))
                {
                    if (cJSON_IsString(name) && (name->valuestring != NULL))
                    {
                        if (strcmp(name->valuestring, "ON") == 0)
                        {
                            node_info->onoff = true;
                            gen_onoff_set(node_info);
                        }
                        else if (strcmp(name->valuestring, "OFF") == 0)
                        {
                            node_info->onoff = false;
                            gen_onoff_set(node_info);
                        }
                    }

                    bool light_value_changed = false;
                    color_mode_t current_mode = node_info->color_mode;
                    if (cJSON *brightness = cJSON_GetObjectItemCaseSensitive(response, "brightness"))
                    {
                        if (cJSON_IsNumber(brightness))
                        {
                            // FIX-ME : might be related to my buld, or check server config for value range
                            uint16_t filteredValue = (uint16_t)map(brightness->valuedouble, 0, 255, 0, 32767);
                            node_info->hsl_l = filteredValue;
                            light_value_changed = true;
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
                                node_info->hsl_h = filteredValue;
                                current_mode = color_mode_t::hs;
                                light_value_changed = true;
                            }
                            if (cJSON_IsNumber(saturation))
                            {
                                uint16_t filteredValue = (uint16_t)map(saturation->valuedouble, 0, 100, 0, 65535);
                                node_info->hsl_s = filteredValue;
                                current_mode = color_mode_t::hs;
                                light_value_changed = true;
                            }
                        }
                    }

                    if (cJSON *color_temp = cJSON_GetObjectItemCaseSensitive(response, "color_temp"))
                    {
                        if (cJSON_IsNumber(color_temp))
                        {
                            // FIX-ME : might be related to my buld, or check server config for value range
                            // uint16_t filteredValue = (uint16_t)map(brightness->valuedouble, 0, 255, 0, 32767);
                            node_info->curr_temp = color_temp->valuedouble;
                            current_mode = color_mode_t::color_temp;
                            light_value_changed = true;
                        }
                    }

                    if (light_value_changed)
                    {
                        if (current_mode == color_mode_t::color_temp)
                        {
                            // ble_mesh_ctl_temperature_set(node_info);
                            ble_mesh_ctl_set(node_info);
                        }
                        else if (current_mode == color_mode_t::hs)
                        {
                            light_hsl_set(node_info);
                        }
                    }
                }
                cJSON_Delete(response);

                send_status(node_info);
            }
        }
    }
}

int send_discovery(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &node_index_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        std::unique_ptr<cJSON> discovery_message = make_discovery_message(node_info);
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
    int nerrors = arg_parse(argc, argv, (void **) &node_index_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        const char *json_data = "";
        int msg_id = 0;
        msg_id = esp_mqtt_client_publish(mqtt_client, get_discovery_id(node_info).c_str(), json_data, 0, 0, 0);
        ESP_LOGI(TAG, "sent discovery publish successful, msg_id=%d", msg_id);
    }

    return 0;
}

void send_status(const bm2mqtt_node_info *node_info)
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

int send_status(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &node_index_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
       send_status(node_info);
    }

    return 0;
}

#pragma endregion MQttMessages

void RegisterMQTTDebugCommands()
{
    node_index_args.node_index = arg_int1("n", "node", "<node_index>", "Node index as reported by prov_list_nodes command");
    node_index_args.end = arg_end(2);

    const esp_console_cmd_t discovery_cmd = {
        .command = "mqtt_discovery",
        .help = "[MQTT] send discovery message",
        .hint = NULL,
        .func = &send_discovery,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&discovery_cmd));

    const esp_console_cmd_t delete_entity_cmd = {
        .command = "mqtt_delete",
        .help = "[MQTT] delete entity",
        .hint = NULL,
        .func = &delete_entity,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&delete_entity_cmd));

    const esp_console_cmd_t mqtt_status_cmd = {
        .command = "mqtt_status",
        .help = "[MQTT] send status message",
        .hint = NULL,
        .func = &send_status,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mqtt_status_cmd));
}