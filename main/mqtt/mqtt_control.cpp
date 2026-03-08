#include "mqtt_control.h"

// Standard C/C++ libraries
#include <memory>
#include <string>

// ESP-IDF includes
#include "cJSON.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"

// Project includes
#include "ble_mesh/ble_mesh_commands.h"
#include "ble_mesh/ble_mesh_control.h"
#include "ble_mesh/ble_mesh_node.h"
#include "ble_mesh/message_queue.h"
#include "common/log_common.h"
#include "common/version.h"
#include "debug/console_cmd.h"
#include "debug/debug_commands_registry.h"
#include "debug_console_common.h"
#include "mqtt_bridge.h"
#include "mqtt_credentials.h"
#include "sig_companies/company_map.h"

#define TAG "APP_MQTT"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MQTT Setup
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region MQTTSetup
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        LOG_ERROR(TAG, "Last error %s: 0x%x", message, error_code);
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

// static esp_mqtt5_unsubscribe_property_config_t unsubscribe_property = {
//     .is_share_subscribe = false,
//     //.share_name = "group1",
// };

static esp_mqtt5_disconnect_property_config_t disconnect_property = {
    .session_expiry_interval = 60,
    .disconnect_reason = 0,
};

static std::string get_node_base_topic(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    if (!node_info)
    {
        LOG_WARN(TAG, "Failed to get node base topic - node_info is null");
        return {};
    }

    if (esp_ble_mesh_node_t *mesh_node = esp_ble_mesh_provisioner_get_node_with_uuid(node_info->uuid.raw()))
    {
        std::string node_addr{bt_hex(mesh_node->addr, BD_ADDR_LEN)};
        return get_bridge_base_topic() + "/node_" + node_addr;
    }
    LOG_WARN(TAG, "Failed to get node base topic - node not found");
    return {};
}

std::string mqtt_get_node_root_topic(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    return get_node_base_topic(node_info);
}

std::string mqtt_get_node_set_topic(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    const std::string base = get_node_base_topic(node_info);
    return base.empty() ? std::string{} : base + "/set";
}

std::string mqtt_get_node_state_topic(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    const std::string base = get_node_base_topic(node_info);
    return base.empty() ? std::string{} : base + "/state";
}

std::string mqtt_get_node_discovery_id(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    if (!node_info)
        return {};

    if (esp_ble_mesh_node_t *mesh_node = esp_ble_mesh_provisioner_get_node_with_uuid(node_info->uuid.raw()))
    {
        char buf[64] = {0};
        snprintf(buf, sizeof(buf), "homeassistant/light/blemesh2mqtt_%s", bt_hex(mesh_node->addr, BD_ADDR_LEN));

        std::string uniq_id = std::string{buf} + "_light/config";

        return uniq_id;
    }
    return {};
}

void mqtt_subscribe_all_nodes(esp_mqtt_client_handle_t client)
{
    node_manager().for_each_node([&client](std::shared_ptr<bm2mqtt_node_info>& node_info)
                                 { mqtt_subscribe_node(client, node_info); });
}

void mqtt_subscribe_node(esp_mqtt_client_handle_t client, const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    int msg_id = esp_mqtt_client_subscribe(client, mqtt_get_node_set_topic(node_info).c_str(), 0);
    LOG_INFO(TAG, "sent subscribe successful, msg_id=%d", msg_id);
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
    LOG_DEBUG(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;

    LOG_DEBUG(TAG, "free heap size is %" PRIu32 ", minimum %" PRIu32, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        LOG_INFO(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_credentials().set_connection_state(mqtt_connection_state_t::CONNECTED);
        esp_mqtt5_client_set_publish_property(client, &publish_property);
        esp_mqtt5_client_set_subscribe_property(client, &subscribe_property);

        mqtt_subscribe_all_nodes(client);
        mqtt_bridge_subscribe(client);

        {
            send_bridge_discovery();
            esp_mqtt_client_publish(mqtt_get_client(), get_bridge_availability_topic(), "on", 0, 0, 0);
        }

        start_periodic_publish_timer();

        break;
    case MQTT_EVENT_DISCONNECTED:
    {
        LOG_INFO(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_credentials().set_connection_state(mqtt_connection_state_t::DISCONNECTED);
    }
    break;
    case MQTT_EVENT_SUBSCRIBED:
        LOG_DEBUG(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        LOG_INFO(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        esp_mqtt5_client_set_disconnect_property(client, &disconnect_property);
        esp_mqtt5_client_delete_user_property(disconnect_property.user_property);
        disconnect_property.user_property = NULL;
        esp_mqtt_client_disconnect(client);
        break;
    case MQTT_EVENT_PUBLISHED:
        LOG_INFO(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        LOG_INFO(TAG, "[MQTT_EVENT_DATA] TOPIC=%.*s", event->topic_len, event->topic);
        LOG_INFO(TAG, "[MQTT_EVENT_DATA] DATA=%.*s", event->data_len, event->data);

        mqtt_parse_event_data(event);
        break;
    case MQTT_EVENT_ERROR:
        LOG_INFO(TAG, "MQTT_EVENT_ERROR");
        LOG_INFO(TAG, "MQTT5 return code is %d", event->error_handle->connect_return_code);

        // Set appropriate error state based on the error type
        if (event->error_handle->connect_return_code == 4 || event->error_handle->connect_return_code == 5)
        {
            mqtt_credentials().set_connection_state(mqtt_connection_state_t::ERROR_AUTH);
            mqtt_credentials().set_last_error("Authentication failed");
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            mqtt_credentials().set_connection_state(mqtt_connection_state_t::ERROR_NETWORK);
            mqtt_credentials().set_last_error("Network connection failed");
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            LOG_INFO(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        else
        {
            mqtt_credentials().set_connection_state(mqtt_connection_state_t::ERROR_TIMEOUT);
            mqtt_credentials().set_last_error("Connection timeout or unknown error");
        }
        break;
    default:
        LOG_INFO(TAG, "Other event id:%d", event->event_id);
        break;
    }
}
esp_mqtt_client_handle_t mqtt_client;

esp_mqtt_client_handle_t mqtt_get_client()
{
    return mqtt_client;
}

std::string host{};
uint16_t port{};
std::string user{};
std::string pass{};
bool mqtt5_app_start(void)
{
    LOG_DEBUG(TAG, "mqtt5_app_start");

    // Load credentials from NVS
    if (mqtt_credentials().load_credentials() != ESP_OK)
    {
        LOG_WARN(TAG, "No valid MQTT credentials found. MQTT will not start.");
        return false;
    }

    if (!mqtt_credentials().has_valid_credentials())
    {
        LOG_ERROR(TAG, "Invalid MQTT credentials. MQTT will not start.");
        return false;
    }

    const auto &creds = mqtt_credentials().get_credentials();
    mqtt_credentials().set_connection_state(mqtt_connection_state_t::CONNECTING);

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

    host = creds.broker_host;
    port = creds.broker_port;
    user = creds.username;
    pass = creds.password;
    mqtt5_cfg.broker.address.hostname = host.c_str();
    mqtt5_cfg.broker.address.transport = creds.use_ssl ? esp_mqtt_transport_t::MQTT_TRANSPORT_OVER_SSL : esp_mqtt_transport_t::MQTT_TRANSPORT_OVER_TCP;
    mqtt5_cfg.broker.address.port = port;
    mqtt5_cfg.session.protocol_ver = MQTT_PROTOCOL_V_5;
    mqtt5_cfg.network.disable_auto_reconnect = false;
    mqtt5_cfg.credentials.username = user.c_str();
    mqtt5_cfg.credentials.authentication.password = pass.c_str();
    mqtt5_cfg.session.last_will.topic = get_bridge_availability_topic();
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

    LOG_INFO(TAG, "MQTT client started successfully with broker: %s:%d",
             creds.broker_host.c_str(), creds.broker_port);
    return true;
}

void mqtt5_app_stop(void)
{
    LOG_INFO(TAG, "Stopping MQTT client");

    if (mqtt_client)
    {
        mqtt_credentials().set_connection_state(mqtt_connection_state_t::DISCONNECTED);
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = nullptr;
        LOG_INFO(TAG, "MQTT client stopped and destroyed");
    }
    else
    {
        LOG_WARN(TAG, "MQTT client was not running");
    }
}

void mqtt5_app_restart(void)
{
    LOG_INFO(TAG, "Restarting MQTT client");

    // Stop existing client if running
    mqtt5_app_stop();

    // Start with new credentials
    mqtt5_app_start();
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
std::unique_ptr<cJSON> make_node_discovery_message(std::shared_ptr<bm2mqtt_node_info> node)
{
    if (!node)
        return nullptr;

    cJSON *root = cJSON_CreateObject();

    if (esp_ble_mesh_node_t *mesh_node = esp_ble_mesh_provisioner_get_node_with_uuid(node->uuid.raw()))
    {
        char buf[64] = {0};
        snprintf(buf, sizeof(buf), "node_%s", bt_hex(mesh_node->addr, BD_ADDR_LEN));

        // Set Device section
        {
            cJSON *dev = nullptr;
            cJSON_AddItemToObject(root, "dev", dev = cJSON_CreateObject());
            if (dev != nullptr)
            {
                uint16_t node_index = get_node_index(node->uuid);
                const char *node_name = esp_ble_mesh_provisioner_get_node_name(node_index);
                cJSON_AddItemToObject(dev, "name", cJSON_CreateString(node_name ? node_name : "light"));
                cJSON_AddItemToObject(dev, "ids", cJSON_CreateString(buf));
                std::string identifier = get_bridge_mac_identifier();
                cJSON_AddItemToObject(dev, "via_device", cJSON_CreateString(identifier.c_str()));

                // Add company information if available
                if (node->company_id != 0)
                {
                    const char *company_name = lookup_company_name(node->company_id);
                    cJSON_AddItemToObject(dev, "mf", cJSON_CreateString(company_name ? company_name : "Unknown"));
                }
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

        // const std::string root_publish = "blemesh2mqtt/" + std::string{buf};
        const std::string root_publish = mqtt_get_node_root_topic(node);

        cJSON_AddItemToObject(root, "~", cJSON_CreateString(root_publish.c_str()));
        cJSON_AddItemToObject(root, "name", cJSON_CreateNull());
        const std::string uniq_id = std::string{buf} + "_light";
        cJSON_AddItemToObject(root, "uniq_id", cJSON_CreateString(uniq_id.c_str()));
        cJSON_AddItemToObject(root, "cmd_t", cJSON_CreateString("~/set"));
        cJSON_AddItemToObject(root, "stat_t", cJSON_CreateString("~/state"));
        cJSON_AddItemToObject(root, "schema", cJSON_CreateString("json"));
        cJSON_AddItemToObject(root, "brightness", cJSON_CreateBool(1));
        cJSON_AddNumberToObject(root, "brightness_scale", node->max_lightness);
        cJSON *sup_clrm = nullptr;
        cJSON_AddItemToObject(root, "sup_clrm", sup_clrm = cJSON_CreateArray());
        if (sup_clrm != nullptr)
        {
            bool has_color = false;
            if (node->features & FEATURE_LIGHT_CTL)
            {
                cJSON_AddItemToArray(sup_clrm, cJSON_CreateString("color_temp"));
                cJSON_AddItemToObject(root, "color_temp_kelvin", cJSON_CreateBool(1));
                cJSON_AddItemToObject(root, "min_kelvin", cJSON_CreateNumber(node->min_temp));
                cJSON_AddItemToObject(root, "max_kelvin", cJSON_CreateNumber(node->max_temp));
                has_color = true;
            }
            if (node->features & FEATURE_LIGHT_HSL)
            {
                cJSON_AddItemToArray(sup_clrm, cJSON_CreateString("hs"));
                has_color = true;
            }

            if (!has_color && (node->features & FEATURE_LIGHT_LIGHTNESS))
            {
                cJSON_AddItemToArray(sup_clrm, cJSON_CreateString("brightness"));
            }
        }
    }

    return std::unique_ptr<cJSON>{root};
}

std::unique_ptr<cJSON> make_status_message(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    if (!node_info)
        return nullptr;

    cJSON *root, *color;
    root = cJSON_CreateObject();

    if (node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        cJSON_AddStringToObject(root, "state", node_info->onoff ? "ON" : "OFF");

        if (node_info->color_mode == color_mode_t::brightness)
        {
            cJSON_AddStringToObject(root, "color_mode", "brightness");
            cJSON_AddNumberToObject(root, "brightness", node_info->hsl_l);
        }
        else if (node_info->color_mode == color_mode_t::color_temp)
        {
            cJSON_AddStringToObject(root, "color_mode", "color_temp");
            cJSON_AddNumberToObject(root, "brightness", node_info->hsl_l);
            cJSON_AddNumberToObject(root, "color_temp", node_info->curr_temp);
        }
        else if (node_info->color_mode == color_mode_t::hs)
        {
            cJSON_AddStringToObject(root, "color_mode", "hs");
            cJSON_AddNumberToObject(root, "brightness", node_info->hsl_l);
            cJSON_AddItemToObject(root, "color", color = cJSON_CreateObject());
            cJSON_AddNumberToObject(color, "h", (uint16_t)map(node_info->hsl_h, node_info->min_hue, node_info->max_hue, 0, 360));
            cJSON_AddNumberToObject(color, "s", (uint16_t)map(node_info->hsl_s, node_info->min_saturation, node_info->max_saturation, 0, 100));
        }
    }

    return std::unique_ptr<cJSON>{root};
}

esp_timer_handle_t home_assistant_restart_timer = nullptr;
static uint8_t num_try = 3;
void on_home_assistant_restart_timer(void *arg)
{
    if (mqtt_credentials().get_connection_state() == mqtt_connection_state_t::CONNECTED)
    {
        --num_try;

        mqtt_subscribe_all_nodes(mqtt_get_client());
        mqtt_bridge_subscribe(mqtt_get_client());
        send_bridge_discovery();
        esp_mqtt_client_publish(mqtt_get_client(), get_bridge_availability_topic(), "on", 0, 0, 0);
        publish_bridge_info();
        ble_mesh_republish_all_nodes_to_mqtt();
    }
    else
    {
        LOG_WARN(TAG, "MQTT client not connected, skipping resubscribe");
    }
    if (num_try == 0)
    {
        LOG_WARN(TAG, "home_assistant_restart_timer expired - stopping timer and cleaning up");
        if (home_assistant_restart_timer)
        {
            esp_timer_stop(home_assistant_restart_timer);
            esp_timer_delete(home_assistant_restart_timer);
            home_assistant_restart_timer = nullptr;
        }
    }
}

void mqtt_parse_event_data(esp_mqtt_event_handle_t event)
{
    if (strncmp(event->topic, "homeassistant/status", event->topic_len) == 0)
    {
        if (strncmp(event->data, "online", event->data_len) == 0)
        {
            LOG_INFO(TAG, "Home Assistant is online - scheduling discovery republish");

            // Stop existing timer if running
            if (home_assistant_restart_timer)
            {
                esp_timer_stop(home_assistant_restart_timer);
                esp_timer_delete(home_assistant_restart_timer);
                home_assistant_restart_timer = nullptr;
            }

            esp_timer_create_args_t args = {
                .callback = &on_home_assistant_restart_timer,
                .arg = nullptr,
                .name = "ha_restart_timer"};
            esp_err_t err = esp_timer_create(&args, &home_assistant_restart_timer);
            if (err == ESP_OK)
            {
                err = esp_timer_start_periodic(home_assistant_restart_timer, 60 * 1000 * 1000); // 60 second intervals
                if (err == ESP_OK)
                {
                    num_try = 3;
                    LOG_INFO(TAG, "Started Home Assistant restart timer (60s intervals, 3 retries)");
                }
                else
                {
                    LOG_ERROR(TAG, "Failed to start Home Assistant restart timer: %s", esp_err_to_name(err));
                }
            }
            else
            {
                LOG_ERROR(TAG, "Failed to create Home Assistant restart timer: %s", esp_err_to_name(err));
            }
        }
        else if (strncmp(event->data, "offline", event->data_len) == 0)
        {
            LOG_INFO(TAG, "Home Assistant is offline - doing nothing");
        }
    }

    if (strncmp(event->topic, get_bridge_provisioning_set_topic(), event->topic_len) == 0)
    {
        LOG_INFO(TAG, "Received provisioning command from MQTT: [%.*s]", event->data_len, event->data);
        ble_mesh_set_provisioning_enabled(strncmp(event->data, "ON", event->data_len) == 0);

        return;
    }

    if (strncmp(event->topic, get_bridge_auto_provisioning_set_topic(), event->topic_len) == 0)
    {
        LOG_INFO(TAG, "Received auto-provisioning command from MQTT: [%.*s]", event->data_len, event->data);
        ble_mesh_set_auto_provisioning_enabled(strncmp(event->data, "ON", event->data_len) == 0);

        return;
    }

    if (strncmp(event->topic, get_bridge_restart_set_topic(), event->topic_len) == 0)
    {
        LOG_INFO(TAG, "Received restart command from MQTT: [%.*s]", event->data_len, event->data);

        if (strncmp(event->data, "RESTART", event->data_len) == 0)
        {
            LOG_WARN(TAG, "Bridge restart requested via MQTT - restarting in 2 seconds...");

            // Give time for MQTT response to be sent before restarting
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }

        return;
    }

    // FIX-ME : likely slow af
    const std::string topic{event->topic, static_cast<std::string::size_type>(event->topic_len)};
    if (auto index_pos = topic.find("node_"); index_pos != std::string::npos)
    {
        // +5 : sizeof node_
        // 12 sizeof mac address string
        const std::string mac{topic, index_pos + 5, 12};
        if (auto node_info = node_manager().get_node(mac))
        {
            // Use RAII wrapper to prevent memory leaks
            CJsonPtr response(cJSON_Parse(event->data), cJSON_Delete);
            if (response)
            {
                if (const cJSON *name = cJSON_GetObjectItemCaseSensitive(response.get(), "state"))
                {
                    if (cJSON_IsString(name) && (name->valuestring != NULL))
                    {
                        if (strcmp(name->valuestring, "ON") == 0)
                        {
                            node_info->onoff = true;
                            ble_mesh_gen_onoff_set(node_info);
                        }
                        else if (strcmp(name->valuestring, "OFF") == 0)
                        {
                            node_info->onoff = false;
                            ble_mesh_gen_onoff_set(node_info);
                        }
                    }

                    bool light_value_changed = false;
                    color_mode_t current_mode = node_info->color_mode;

                    if ((node_info->features & FEATURE_LIGHT_LIGHTNESS) || (node_info->features & FEATURE_LIGHT_HSL))
                    {
                        LOG_WARN(TAG, "[mqtt_parse_event_data] Light Lightness feature supported");
                        if (cJSON *brightness = cJSON_GetObjectItemCaseSensitive(response.get(), "brightness"))
                        {
                            if (cJSON_IsNumber(brightness))
                            {
                                uint16_t filteredValue = MAX(0, MIN(node_info->max_lightness, (uint16_t)brightness->valuedouble));
                                node_info->hsl_l = filteredValue;
                                light_value_changed = true;
                            }
                        }
                    }

                    if (node_info->features & FEATURE_LIGHT_HSL)
                    {
                        LOG_WARN(TAG, "[mqtt_parse_event_data] Light HSL feature supported");
                        if (cJSON *color = cJSON_GetObjectItemCaseSensitive(response.get(), "color"))
                        {
                            if (cJSON_IsObject(color))
                            {
                                cJSON *hue = cJSON_GetObjectItemCaseSensitive(color, "h");
                                cJSON *saturation = cJSON_GetObjectItemCaseSensitive(color, "s");

                                if (cJSON_IsNumber(hue))
                                {
                                    uint16_t filteredValue = (uint16_t)map(hue->valuedouble, 0, 360, node_info->min_hue, node_info->max_hue);
                                    node_info->hsl_h = filteredValue;
                                    current_mode = color_mode_t::hs;
                                    light_value_changed = true;
                                }
                                if (cJSON_IsNumber(saturation))
                                {
                                    uint16_t filteredValue = (uint16_t)map(saturation->valuedouble, 0, 100, node_info->min_saturation, node_info->max_saturation);
                                    node_info->hsl_s = filteredValue;
                                    current_mode = color_mode_t::hs;
                                    light_value_changed = true;
                                }
                            }
                        }
                    }

                    if (node_info->features & FEATURE_LIGHT_CTL)
                    {
                        LOG_WARN(TAG, "[mqtt_parse_event_data] Light CTL feature supported");
                        if (cJSON *color_temp = cJSON_GetObjectItemCaseSensitive(response.get(), "color_temp"))
                        {
                            if (cJSON_IsNumber(color_temp))
                            {
                                // Discovery uses color_temp_kelvin=true with min_kelvin/max_kelvin, so HA sends
                                // color_temp directly in Kelvin, matching the BLE Mesh CTL temperature unit.
                                uint16_t filteredValue = (uint16_t)MAX((double)node_info->min_temp, MIN((double)node_info->max_temp, color_temp->valuedouble));
                                node_info->curr_temp = filteredValue;
                                current_mode = color_mode_t::color_temp;
                                light_value_changed = true;
                            }
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
                            ble_mesh_light_hsl_set(node_info);
                        }
                        else if (current_mode == color_mode_t::brightness)
                        {
                            ble_mesh_lightness_set(node_info);
                        }
                    }

                    message_queue().enqueue(node_info, message_payload{
                                                           .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info)
                                                           {
                                                               mqtt_node_send_status(node_info);
                                                           },
                                                           .opcode = 0x0000, // No specific opcode, just a marker
                                                           .retries_left = 0,
                                                           .type = message_type_t::mqtt_message, // Indicate this is a MQTT message
                                                       });
                }
                // cJSON automatically deleted by smart pointer
            }
        }
    }
}

int send_discovery(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (auto node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        std::unique_ptr<cJSON> discovery_message = make_node_discovery_message(node_info);
        char *json_data = cJSON_PrintUnformatted(discovery_message.get());
        int msg_id = 0;
        msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_get_node_discovery_id(node_info).c_str(), json_data, 0, 0, 0);
        LOG_INFO(TAG, "sent discovery publish successful, msg_id=%d", msg_id);

        cJSON_free(json_data);
    }

    return 0;
}

int delete_entity(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (auto node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        const char *json_data = "";
        int msg_id = 0;
        msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_get_node_discovery_id(node_info).c_str(), json_data, 0, 0, 0);
        LOG_INFO(TAG, "sent discovery publish successful, msg_id=%d", msg_id);
    }

    return 0;
}

void mqtt_node_send_status(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    if (!node_info)
    {
        LOG_ERROR(TAG, "mqtt_node_send_status: node_info is null");
        return;
    }

    const std::string root_publish = mqtt_get_node_state_topic(node_info);
    if (root_publish.empty())
    {
        LOG_ERROR(TAG, "mqtt_node_send_status: failed to get state topic for node");
        return;
    }

    std::unique_ptr<cJSON> status_message = make_status_message(node_info);
    if (!status_message)
    {
        LOG_ERROR(TAG, "mqtt_node_send_status: failed to create status message");
        return;
    }

    char *json_data = cJSON_PrintUnformatted(status_message.get());
    if (!json_data)
    {
        LOG_ERROR(TAG, "mqtt_node_send_status: failed to format JSON");
        return;
    }

    int msg_id = esp_mqtt_client_publish(mqtt_client, root_publish.c_str(), json_data, 0, 0, 0);
    LOG_INFO(TAG, "sent status publish successful, msg_id=%d", msg_id);
    LOG_INFO(TAG, "TOPIC=%s", root_publish.c_str());
    LOG_INFO(TAG, "DATA=%s", json_data);

    cJSON_free(json_data);
}

void mqtt_send_discovery(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    std::unique_ptr<cJSON> discovery_message = make_node_discovery_message(node_info);
    char *json_data = cJSON_PrintUnformatted(discovery_message.get());
    int msg_id = 0;
    msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_get_node_discovery_id(node_info).c_str(), json_data, 0, 0, 0);
    LOG_INFO(TAG, "sent discovery publish successful, msg_id=%d", msg_id);

    cJSON_free(json_data);
}

void mqtt_remove_node(const std::shared_ptr<bm2mqtt_node_info>& node_info)
{
    if (!node_info)
    {
        LOG_ERROR(TAG, "mqtt_remove_node: node_info is null");
        return;
    }

    const std::string discovery_topic = mqtt_get_node_discovery_id(node_info);
    if (discovery_topic.empty())
    {
        LOG_ERROR(TAG, "mqtt_remove_node: failed to get discovery topic for node");
        return;
    }

    // Publishing empty string to discovery topic removes the entity from Home Assistant
    const char *empty_payload = "";
    int msg_id = esp_mqtt_client_publish(mqtt_client, discovery_topic.c_str(), empty_payload, 0, 0, 0);
    LOG_INFO(TAG, "Removed node from MQTT discovery: %s", discovery_topic.c_str());
    msg_id = esp_mqtt_client_unsubscribe(mqtt_client, mqtt_get_node_set_topic(node_info).c_str());
    LOG_INFO(TAG, "Unsubscribed from node set topic: %s, msg_id=%d", mqtt_get_node_set_topic(node_info).c_str(), msg_id);
}

int mqtt_send_status(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (auto node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        mqtt_node_send_status(node_info);
    }

    return 0;
}

#pragma endregion MQttMessages

void register_mqtt_commands()
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
    ESP_ERROR_CHECK(register_console_command(&discovery_cmd));

    const esp_console_cmd_t delete_entity_cmd = {
        .command = "mqtt_delete",
        .help = "[MQTT] delete entity",
        .hint = NULL,
        .func = &delete_entity,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(register_console_command(&delete_entity_cmd));

    const esp_console_cmd_t mqtt_status_cmd = {
        .command = "mqtt_status",
        .help = "[MQTT] send status message",
        .hint = NULL,
        .func = &mqtt_send_status,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(register_console_command(&mqtt_status_cmd));
}

REGISTER_DEBUG_COMMAND(register_mqtt_commands);