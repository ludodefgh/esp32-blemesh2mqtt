#include "mqtt_bridge.h"
#include "mqtt_control.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <wifi/wifi_station.h>

#define TAG "APP_MQTT_BRIDGE"

static std::string get_wifi_mac_string()
{
    // Get WiFi MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    
    // Format MAC address as hex string
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return std::string(mac_str);
}

std::string get_bridge_mac_identifier()
{
    // Create identifier with MAC address
    char identifier[40];
    snprintf(identifier, sizeof(identifier), "blemesh2mqtt_bridge_%s", get_wifi_mac_string().c_str());
    
    return std::string(identifier);
}

std::string get_bridge_base_topic()
{
    // Create base topic with MAC address
    char topic[32];
    snprintf(topic, sizeof(topic), "blemesh2mqtt_%s", get_wifi_mac_string().c_str());
    
    return std::string(topic);
}

const char* get_bridge_availability_topic()
{
    static const std::string topic{get_bridge_base_topic() + "/bridge/state"};
    return topic.c_str();
}

const char* get_bridge_state_topic()
{
    static const std::string topic{get_bridge_base_topic() + "/bridge/info"};
    return topic.c_str();
}

static cJSON* create_bridge_device_object()
{
    cJSON *device = cJSON_CreateObject();
    cJSON *identifiers = cJSON_CreateArray();
    
    std::string identifier = get_bridge_mac_identifier();
    cJSON_AddItemToArray(identifiers, cJSON_CreateString(identifier.c_str()));

    cJSON_AddItemToObject(device, "identifiers", identifiers);
    cJSON_AddStringToObject(device, "manufacturer", "YourName");
    cJSON_AddStringToObject(device, "model", "BLEMesh2MQTT Bridge");
    cJSON_AddStringToObject(device, "name", "BLE Mesh Bridge");
    cJSON_AddStringToObject(device, "sw_version", "0.1.0");
    
    return device;
}
const char* get_bridge_provisioning_state_topic()
{
    static const std::string topic {get_bridge_base_topic() + "/bridge/provisioning/state"};
    return topic.c_str();
}

const char* get_bridge_provisioning_set_topic()
{
    static const std::string topic {get_bridge_base_topic() + "/bridge/provisioning/set"};
    return topic.c_str();
}

const char* get_bridge_restart_set_topic()
{
    static const std::string topic {get_bridge_base_topic() + "/bridge/restart/set"};
    return topic.c_str();
}

CJsonPtr create_provisioning_json()
{
    CJsonPtr root(cJSON_CreateObject(), cJSON_Delete);

    // Top-level fields
    cJSON_AddStringToObject(root.get(), "name", "BLE Mesh Provisioning");
    std::string unique_id = get_bridge_mac_identifier();
    unique_id += "_provisioning";
    cJSON_AddStringToObject(root.get(), "unique_id", unique_id.c_str());
    cJSON_AddStringToObject(root.get(), "state_topic", get_bridge_provisioning_state_topic());
    cJSON_AddStringToObject(root.get(), "command_topic", get_bridge_provisioning_set_topic());
    cJSON_AddStringToObject(root.get(), "state_on", "ON");
    cJSON_AddStringToObject(root.get(), "state_off", "OFF");
    cJSON_AddStringToObject(root.get(), "availability_topic", get_bridge_availability_topic());
    cJSON_AddStringToObject(root.get(), "payload_available", "on");
    cJSON_AddStringToObject(root.get(), "payload_not_available", "offline");

    // Device object
    cJSON *device = create_bridge_device_object();

    // Add device to root
    cJSON_AddItemToObject(root.get(), "device", device);

    cJSON *origin = cJSON_CreateObject();
    cJSON_AddStringToObject(origin, "name", "blemesh2mqtt");
    cJSON_AddStringToObject(origin, "sw", "0.1.0");

    cJSON_AddItemToObject(root.get(), "origin", origin);

    return root;
}

CJsonPtr create_restart_json()
{
    CJsonPtr root(cJSON_CreateObject(), cJSON_Delete);

    // Top-level fields for Home Assistant button entity
    cJSON_AddStringToObject(root.get(), "name", "Restart");
    std::string unique_id = get_bridge_mac_identifier();
    unique_id += "_restart";
    cJSON_AddStringToObject(root.get(), "unique_id", unique_id.c_str());
    cJSON_AddStringToObject(root.get(), "command_topic", get_bridge_restart_set_topic());
    cJSON_AddStringToObject(root.get(), "payload_press", "RESTART");
    cJSON_AddStringToObject(root.get(), "availability_topic", get_bridge_availability_topic());
    cJSON_AddStringToObject(root.get(), "payload_available", "on");
    cJSON_AddStringToObject(root.get(), "payload_not_available", "offline");
    cJSON_AddStringToObject(root.get(), "device_class", "restart");
    cJSON_AddStringToObject(root.get(), "entity_category", "config");
    cJSON_AddStringToObject(root.get(), "icon", "mdi:restart");

    // Device object
    cJSON *device = create_bridge_device_object();
    cJSON_AddItemToObject(root.get(), "device", device);

    cJSON *origin = cJSON_CreateObject();
    cJSON_AddStringToObject(origin, "name", "blemesh2mqtt");
    cJSON_AddStringToObject(origin, "sw", "0.1.0");

    cJSON_AddItemToObject(root.get(), "origin", origin);

    return root;
}

CJsonPtr create_uptime_json()
{
    CJsonPtr root(cJSON_CreateObject(), cJSON_Delete);

    // Top-level fields
    cJSON_AddStringToObject(root.get(), "name", "BLE Mesh Bridge Uptime");
    std::string unique_id = get_bridge_mac_identifier();
    unique_id += "_uptime";
    cJSON_AddStringToObject(root.get(), "unique_id", unique_id.c_str());
    cJSON_AddStringToObject(root.get(), "state_topic", get_bridge_state_topic());
    cJSON_AddStringToObject(root.get(), "unit_of_measurement", "s");
    cJSON_AddStringToObject(root.get(), "value_template", "{{ value_json.uptime }}");
    cJSON_AddStringToObject(root.get(), "entity_category", "diagnostic");
    cJSON_AddStringToObject(root.get(), "availability_topic", get_bridge_availability_topic());
    cJSON_AddStringToObject(root.get(), "payload_available", "on");
    cJSON_AddStringToObject(root.get(), "payload_not_available", "offline");
    cJSON_AddStringToObject(root.get(), "platform", "sensor");
    

    // Device object
    cJSON *device = create_bridge_device_object();
    cJSON_AddItemToObject(root.get(), "device", device);

    return root;
}

CJsonPtr create_mem_json()
{
    CJsonPtr root(cJSON_CreateObject(), cJSON_Delete);

    // Top-level fields
    cJSON_AddStringToObject(root.get(), "name", "BLE Mesh Bridge Memory");
    std::string unique_id = get_bridge_mac_identifier();
    unique_id += "_memory";
    cJSON_AddStringToObject(root.get(), "unique_id", unique_id.c_str());
    cJSON_AddStringToObject(root.get(), "state_topic", get_bridge_state_topic());
    cJSON_AddStringToObject(root.get(), "unit_of_measurement", "kb");
    cJSON_AddStringToObject(root.get(), "value_template", "{{ value_json.heap_free }}");
    cJSON_AddStringToObject(root.get(), "entity_category", "diagnostic");
    cJSON_AddStringToObject(root.get(), "availability_topic", get_bridge_availability_topic());
    cJSON_AddStringToObject(root.get(), "payload_available", "on");
    cJSON_AddStringToObject(root.get(), "payload_not_available", "offline");
       

    // Device object
    cJSON *device = create_bridge_device_object();
    cJSON_AddItemToObject(root.get(), "device", device);

    return root;
}

CJsonPtr create_ip_json()
{
    CJsonPtr root(cJSON_CreateObject(), cJSON_Delete);

    // Top-level fields
    cJSON_AddStringToObject(root.get(), "name", "BLE Mesh Bridge IP Address");
    std::string unique_id = get_bridge_mac_identifier();
    unique_id += "_ip_address";
    cJSON_AddStringToObject(root.get(), "unique_id", unique_id.c_str());
    cJSON_AddStringToObject(root.get(), "state_topic", get_bridge_state_topic());
    cJSON_AddStringToObject(root.get(), "value_template", "{{ value_json.ip_address }}");
    cJSON_AddStringToObject(root.get(), "entity_category", "diagnostic");
    cJSON_AddStringToObject(root.get(), "availability_topic", get_bridge_availability_topic());
    cJSON_AddStringToObject(root.get(), "payload_available", "on");
    cJSON_AddStringToObject(root.get(), "payload_not_available", "offline");
    cJSON_AddStringToObject(root.get(), "icon", "mdi:ip-network");

    // Device object
    cJSON *device = create_bridge_device_object();
    cJSON_AddItemToObject(root.get(), "device", device);

    return root;
}

CJsonPtr create_bridge_info_json(const char *version)
{
    CJsonPtr root(cJSON_CreateObject(), cJSON_Delete);

    // Uptime in seconds
    int64_t uptime_us = esp_timer_get_time();
    int uptime_sec = static_cast<int>(uptime_us / 1000000);
    cJSON_AddNumberToObject(root.get(), "uptime", uptime_sec);
    // Free heap size
    cJSON_AddNumberToObject(root.get(), "heap_free", esp_get_free_heap_size()/1024); // Convert to KB
    // IP address
    cJSON_AddStringToObject(root.get(), "ip_address", get_ip_address());

    cJSON_AddStringToObject(root.get(), "version", version);
    
    // Build date/time
    std::string build_datetime = std::string(__DATE__) + " " + __TIME__;
    cJSON_AddStringToObject(root.get(), "build_date", build_datetime.c_str());

    // Origin
    cJSON *origin = cJSON_CreateObject();
    cJSON_AddStringToObject(origin, "name", "blemesh2mqtt");
    cJSON_AddStringToObject(origin, "sw", "0.1.0");
    cJSON_AddStringToObject(origin, "url", get_ip_address());

    cJSON_AddItemToObject(root.get(), "origin", origin);

    return root;
}

void publish_bridge_info(const char *version)
{
    CJsonPtr bridge_info_json = create_bridge_info_json(version);
    char *json_data = cJSON_PrintUnformatted(bridge_info_json.get());
    int msg_id = esp_mqtt_client_publish(get_mqtt_client(), get_bridge_state_topic(), json_data, 0, 0, 0);
    ESP_LOGV(TAG, "sent bridge info publish successful, msg_id=%d", msg_id);
    cJSON_free(json_data);
}

// FIX-ME : make it accesible globally
extern bool enable_provisioning;
void send_bridge_discovery()
{
    // Publish provisioning switch
    {
        CJsonPtr discovery_json = create_provisioning_json();
        if (cJSON *unique_id = cJSON_GetObjectItemCaseSensitive(discovery_json.get(), "unique_id"))
        {
            if (cJSON_IsString(unique_id) && (unique_id->valuestring != nullptr))
            {
                ESP_LOGI(TAG, "[send_bridge_discovery] Publishing switch discovery for %s", unique_id->valuestring);

                char *json_data = cJSON_PrintUnformatted(discovery_json.get());
                const std::string topic = "homeassistant/switch/" + std::string{unique_id->valuestring} + "/config";
                int msg_id = esp_mqtt_client_publish(get_mqtt_client(), topic.c_str(), json_data, 0, 0, 0);
                cJSON_free(json_data);
            }
            else{
                ESP_LOGE(TAG, "[send_bridge_discovery] Invalid unique_id in switch discovery JSON");
            }
        }
        else{
            ESP_LOGE(TAG, "[send_bridge_discovery] No unique_id found in switch discovery JSON");
        }
    }

    // Publish restart button
    {
        CJsonPtr discovery_json = create_restart_json();
        if (cJSON *unique_id = cJSON_GetObjectItemCaseSensitive(discovery_json.get(), "unique_id"))
        {
            if (cJSON_IsString(unique_id) && (unique_id->valuestring != nullptr))
            {
                ESP_LOGI(TAG, "[send_bridge_discovery] Publishing button discovery for %s", unique_id->valuestring);

                char *json_data = cJSON_PrintUnformatted(discovery_json.get());
                const std::string topic = "homeassistant/button/" + std::string{unique_id->valuestring} + "/config";
                int msg_id = esp_mqtt_client_publish(get_mqtt_client(), topic.c_str(), json_data, 0, 0, 0);
                cJSON_free(json_data);
            }
            else{
                ESP_LOGE(TAG, "[send_bridge_discovery] Invalid unique_id in button discovery JSON");
            }
        }
        else{
            ESP_LOGE(TAG, "[send_bridge_discovery] No unique_id found in button discovery JSON");
        }
    }

    for (const auto sensor_json_func : {create_uptime_json, create_mem_json, create_ip_json} )
    {
        CJsonPtr sensor_json = sensor_json_func();
        if (cJSON *unique_id = cJSON_GetObjectItemCaseSensitive(sensor_json.get(), "unique_id"))
        {
            if (cJSON_IsString(unique_id) && (unique_id->valuestring != nullptr))
            {
                ESP_LOGI(TAG, "[send_bridge_discovery] Publishing discovery for %s", unique_id->valuestring);

                char *json_data = cJSON_PrintUnformatted(sensor_json.get());
                const std::string topic = "homeassistant/sensor/" + std::string{unique_id->valuestring} + "/config";
                int msg_id = esp_mqtt_client_publish(get_mqtt_client(), topic.c_str(), json_data, 0, 0, 0);
                cJSON_free(json_data);
            }
            else {
                ESP_LOGE(TAG, "[send_bridge_discovery] Invalid unique_id in sensor JSON");
            }
        }
        else{
            ESP_LOGE(TAG, "[send_bridge_discovery] No unique_id found in sensor JSON");
        }
    }
    
    {
       publish_bridge_info("0.1.0");
    }
    {
        mqtt_publish_provisioning_enabled(enable_provisioning);
    }
}

#define PUBLISH_INTERVAL_MS 10000

esp_timer_handle_t publish_timer;

void periodic_publish_callback(void *arg)
{
    const char *version = "0.1.0";
    publish_bridge_info(version);
}

void start_periodic_publish_timer()
{
    const esp_timer_create_args_t timer_args = {
        .callback = &periodic_publish_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mqtt_info_pub"
    };

    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &publish_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(publish_timer, PUBLISH_INTERVAL_MS * 1000));
}

void mqtt_publish_provisioning_enabled(bool enable_provisioning)
{
    ESP_LOGI(TAG, "[%s] Publish : %s", __func__, enable_provisioning ? "ON" : "OFF");
    int msg_id = esp_mqtt_client_publish(get_mqtt_client(), get_bridge_provisioning_state_topic(), enable_provisioning ? "ON" : "OFF", 0, 0, 0);
}

void mqtt_bridge_subscribe(esp_mqtt_client_handle_t client)
{
    int msg_id = esp_mqtt_client_subscribe(client, get_bridge_provisioning_set_topic(), 0);
    ESP_LOGI(TAG, "sent provisioning subscribe successful, msg_id=%d", msg_id);
    
    msg_id = esp_mqtt_client_subscribe(client, get_bridge_restart_set_topic(), 0);
    ESP_LOGI(TAG, "sent restart subscribe successful, msg_id=%d", msg_id);
}