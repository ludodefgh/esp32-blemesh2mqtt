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

std::string get_bridge_mac_identifier()
{
    // Get WiFi MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    
    // Format MAC address as hex string
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Create identifier with MAC address
    char identifier[33];
    snprintf(identifier, sizeof(identifier), "blemesh2mqtt_bridge_%s", mac_str);
    
    return std::string(identifier);
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

CJsonPtr create_provisioning_json()
{
    CJsonPtr root(cJSON_CreateObject(), cJSON_Delete);

    // Top-level fields
    cJSON_AddStringToObject(root.get(), "name", "BLE Mesh Provisioning");
    std::string unique_id = get_bridge_mac_identifier();
    unique_id += "_provisioning";
    cJSON_AddStringToObject(root.get(), "unique_id", unique_id.c_str());
    cJSON_AddStringToObject(root.get(), "state_topic", "blemesh2mqtt/bridge/provisioning/state");
    cJSON_AddStringToObject(root.get(), "command_topic", "blemesh2mqtt/bridge/provisioning/set");
    cJSON_AddStringToObject(root.get(), "state_on", "ON");
    cJSON_AddStringToObject(root.get(), "state_off", "OFF");
    cJSON_AddStringToObject(root.get(), "availability_topic", "blemesh2mqtt/bridge/state");
    cJSON_AddStringToObject(root.get(), "payload_available", "on");
    cJSON_AddStringToObject(root.get(), "payload_not_available", "offline");

    // Device object
    cJSON *device = create_bridge_device_object();
    //cJSON_AddStringToObject(device, "configuration_url", get_ip_address());

    // Add device to root
    cJSON_AddItemToObject(root.get(), "device", device);

    cJSON *origin = cJSON_CreateObject();
    cJSON_AddStringToObject(origin, "name", "blemesh2mqtt");
    cJSON_AddStringToObject(origin, "sw", "0.1.0");
    //cJSON_AddStringToObject(origin, "url", get_ip_address());

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
    cJSON_AddStringToObject(root.get(), "state_topic", "blemesh2mqtt/bridge/info");
    cJSON_AddStringToObject(root.get(), "unit_of_measurement", "s");
    cJSON_AddStringToObject(root.get(), "value_template", "{{ value_json.uptime }}");
    cJSON_AddStringToObject(root.get(), "entity_category", "diagnostic");
    cJSON_AddStringToObject(root.get(), "availability_topic", "blemesh2mqtt/bridge/state");
    cJSON_AddStringToObject(root.get(), "payload_available", "on");
    cJSON_AddStringToObject(root.get(), "payload_not_available", "offline");
    cJSON_AddStringToObject(root.get(), "platform", "sensor");
    

    // Device object
    cJSON *device = create_bridge_device_object();
    cJSON_AddItemToObject(root.get(), "device", device);

    // cJSON *origin = cJSON_CreateObject();
    // cJSON_AddStringToObject(origin, "name", "blemesh2mqtt");
    // cJSON_AddStringToObject(origin, "sw", "0.1.0");
    // cJSON_AddStringToObject(origin, "url", get_ip_address());

    // cJSON_AddItemToObject(root.get(), "origin", origin);

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
    cJSON_AddStringToObject(root.get(), "state_topic", "blemesh2mqtt/bridge/info");
    cJSON_AddStringToObject(root.get(), "unit_of_measurement", "kb");
    cJSON_AddStringToObject(root.get(), "value_template", "{{ value_json.heap_free }}");
    cJSON_AddStringToObject(root.get(), "entity_category", "diagnostic");
    cJSON_AddStringToObject(root.get(), "availability_topic", "blemesh2mqtt/bridge/state");
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
    cJSON_AddStringToObject(root.get(), "state_topic", "blemesh2mqtt/bridge/info");
    cJSON_AddStringToObject(root.get(), "value_template", "{{ value_json.ip_address }}");
    cJSON_AddStringToObject(root.get(), "entity_category", "diagnostic");
    cJSON_AddStringToObject(root.get(), "availability_topic", "blemesh2mqtt/bridge/state");
    cJSON_AddStringToObject(root.get(), "payload_available", "on");
    cJSON_AddStringToObject(root.get(), "payload_not_available", "offline");
    cJSON_AddStringToObject(root.get(), "icon", "mdi:ip-network");

    // Device object
    cJSON *device = create_bridge_device_object();
    cJSON_AddItemToObject(root.get(), "device", device);

    return root;
}

CJsonPtr create_bridge_info_json(int devices_provisioned, const char *version)
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

    cJSON_AddNumberToObject(root.get(), "devices_provisioned", devices_provisioned);
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

void publish_bridge_info(int devices_provisioned, const char *version)
{
    CJsonPtr bridge_info_json = create_bridge_info_json(devices_provisioned, version);
    char *json_data = cJSON_PrintUnformatted(bridge_info_json.get());
    int msg_id = esp_mqtt_client_publish(get_mqtt_client(), "blemesh2mqtt/bridge/info", json_data, 0, 0, 0);
    ESP_LOGV(TAG, "sent bridge info publish successful, msg_id=%d", msg_id);
    cJSON_free(json_data);
}

// FIX-ME : make it accesible globally
extern bool enable_provisioning;
void send_bridge_discovery()
{
    {
        CJsonPtr discovery_json = create_provisioning_json();
        char *json_data = cJSON_PrintUnformatted(discovery_json.get());
        int msg_id = esp_mqtt_client_publish(get_mqtt_client(), "homeassistant/switch/blemesh2mqtt/provisioning/config", json_data, 0, 0, 0);
        ESP_LOGI(TAG, "sent bridge discovery publish successful, msg_id=%d", msg_id);
        cJSON_free(json_data);
    }

    {
        CJsonPtr uptime_json = create_uptime_json();
        char *json_data = cJSON_PrintUnformatted(uptime_json.get());
        int msg_id = esp_mqtt_client_publish(get_mqtt_client(), "homeassistant/sensor/blemesh2mqtt/uptime/config", json_data, 0, 0, 0);
        ESP_LOGI(TAG, "sent uptime discovery publish successful, msg_id=%d", msg_id);
        cJSON_free(json_data);
    }

    {
        CJsonPtr mem_json = create_mem_json();
        char *json_data = cJSON_PrintUnformatted(mem_json.get());
        int msg_id = esp_mqtt_client_publish(get_mqtt_client(), "homeassistant/sensor/blemesh2mqtt/memory/config", json_data, 0, 0, 0);
        ESP_LOGI(TAG, "sent memory discovery publish successful, msg_id=%d", msg_id);
        cJSON_free(json_data);
    }

    {
        CJsonPtr ip_json = create_ip_json();
        char *json_data = cJSON_PrintUnformatted(ip_json.get());
        int msg_id = esp_mqtt_client_publish(get_mqtt_client(), "homeassistant/sensor/blemesh2mqtt/ip_address/config", json_data, 0, 0, 0);
        ESP_LOGI(TAG, "sent IP address discovery publish successful, msg_id=%d", msg_id);
        cJSON_free(json_data);
    }

    {
       publish_bridge_info(69, "0.1.0");
    }
    {
        mqtt_publish_provisioning_enabled(enable_provisioning);
    }
}

#define PUBLISH_INTERVAL_MS 10000

esp_timer_handle_t publish_timer;

void periodic_publish_callback(void *arg) {
    // FIX-Me : add real value
    int devices_provisioned = 4;  // Replace with your actual count
    const char *version = "0.1.0";
    publish_bridge_info(devices_provisioned, version);
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
    int msg_id = esp_mqtt_client_publish(get_mqtt_client(), "blemesh2mqtt/bridge/provisioning/state", enable_provisioning ? "ON" : "OFF", 0, 0, 0);
}

void mqtt_bridge_subscribe(esp_mqtt_client_handle_t client)
{
    int msg_id = esp_mqtt_client_subscribe(client, "blemesh2mqtt/bridge/provisioning/set", 0);
    //send_status(node_info);
    ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
}