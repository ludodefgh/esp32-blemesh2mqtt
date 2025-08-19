#include "web_server.h"

// ESP-IDF includes
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <esp_ble_mesh_networking_api.h>

// Third-party libraries
#include <cJSON.h>

// Project includes
#include "ble_mesh/ble_mesh_commands.h"
#include "ble_mesh/ble_mesh_control.h"
#include "ble_mesh/ble_mesh_node.h"
#include "ble_mesh/ble_mesh_provisioning.h"
#include "common/log_common.h"
#include "common/version.h"
#include "debug/console_cmd.h"
#include "debug/websocket_logger.h"
#include "mqtt/mqtt_bridge.h"
#include "mqtt/mqtt_control.h"
#include "mqtt/mqtt_credentials.h"
#include "ota/ota_manager.h"
#include "sig_companies/company_map.h"
#include "wifi/wifi_provisioning.h"

#define TAG "WEB_SERVER"

// Store large JavaScript strings in Flash to save DRAM
static const char nodes_javascript[] =
    "<script>\n"
    "let lastSend = 0;\n"
    "let throttleDelay = 200;\n"
    "let scheduled = false;\n"
    "let pending = {};\n"
    "\n"
    "function onSliderInput(uuid, el) {\n"
    "  el.nextElementSibling.value = el.value;\n"
    "  pending.uuid = uuid;\n"
    "  pending.value = el.value;\n"
    "  scheduleSend();\n"
    "}\n"
    "\n"
    "function scheduleSend() {\n"
    "  if (scheduled) return;\n"
    "  const now = Date.now();\n"
    "  const timeSinceLast = now - lastSend;\n"
    "  const wait = Math.max(0, throttleDelay - timeSinceLast);\n"
    "  scheduled = true;\n"
    "  setTimeout(() => {\n"
    "    sendLightness(pending.uuid, pending.value);\n"
    "    lastSend = Date.now();\n"
    "    scheduled = false;\n"
    "  }, wait);\n"
    "}\n"
    "\n"
    "function sendLightness(uuid, value) {\n"
    "  fetch('/node/set_lightness', {\n"
    "    method: 'POST',\n"
    "    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },\n"
    "    body: 'uuid=' + encodeURIComponent(uuid) + '&lightness=' + encodeURIComponent(value)\n"
    "  }).catch(err => console.error('Failed to send lightness:', err));\n"
    "}\n"
    "function unprovisionNode(uuid) {"
    "fetch('/node/unprovision', {"
    "method: 'POST',"
    " headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
    "body: 'uuid=' + encodeURIComponent(uuid)"
    "}).then(() => location.reload());"
    "}"
    "\n"
    "function provisionNode(uuid) {"
    "fetch('/node/provision', {"
    " method: 'POST',"
    "  headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
    "   body: 'uuid=' + encodeURIComponent(uuid)"
    "  }).then(() => location.reload());"
    "}"
    "</script>\n";

// Global server handle for dynamic handler registration
static httpd_handle_t g_server = NULL;

esp_err_t setup_handler(httpd_req_t *req);
esp_err_t list_console_commands_handler(httpd_req_t *req);
esp_err_t reset_wifi_handler(httpd_req_t *req);
esp_err_t rename_node_handler(httpd_req_t *req);
esp_err_t node_send_mqtt_status_handler(httpd_req_t *req);
esp_err_t node_send_mqtt_discovery_handler(httpd_req_t *req);
esp_err_t ota_upload_handler(httpd_req_t *req);
esp_err_t storage_upload_handler(httpd_req_t *req);
esp_err_t ota_status_handler(httpd_req_t *req);
esp_err_t ota_restart_handler(httpd_req_t *req);

esp_err_t nodes_handler(httpd_req_t *req)
{
    //
    // Provisioned nodes
    //
    // Send JavaScript from Flash memory to save DRAM
    httpd_resp_send_chunk(req, nodes_javascript, -1);

    // Loop through all nodes
    for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++)
    {
        const esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_table_entry()[i];
        if (!node)
            continue;

        char uuid_str[33] = {0};
        for (int j = 0; j < 16; j++)
        {
            sprintf(uuid_str + j * 2, "%02X", node->dev_uuid[j]);
        }

        char chunk[512];
        size_t len = snprintf(chunk, sizeof(chunk),
                              "<div class='node'>"
                              "<strong>Node %d:</strong> %s<br>"
                              "Lightness: "
                              "<input type='range' min='0' max='65535' step='500' value='0' "
                              "oninput='onSliderInput(\"%s\", this)'>"
                              "<output>0</output><br>"
                              "<button onclick='unprovisionNode(\"%s\")'>Unprovision</button>"
                              "</div>",
                              i, node->name, uuid_str, uuid_str);

        httpd_resp_send_chunk(req, chunk, len);
    }

    //
    // Unprovisioned nodes
    //
    httpd_resp_send_chunk(req, "<h2>Unprovisioned Devices</h2>", -1);

    for_each_unprovisioned_node([&req](const ble2mqtt_unprovisioned_device &dev)
                                {
        char uuid_str[33] = {0};
        for (int j = 0; j < 16; j++) {
            sprintf(uuid_str + j * 2, "%02X", dev.dev_uuid[j]);
        }

        char line[512];
        snprintf(line, sizeof(line),
            "<div class='node'>"
            "<strong>UUID:</strong> %s<br>"
            "<strong>RSSI:</strong> %d<br>"
            "<button onclick='provisionNode(\"%s\")'>Provision</button>"
            "</div>", uuid_str, dev.rssi, uuid_str);

        httpd_resp_send_chunk(req, line, -1); });

    // End HTML
    httpd_resp_send_chunk(req, "</body></html>", -1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t node_provision_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    if (sscanf(buf, "uuid=%32s", uuid_str) != 1 || strlen(uuid_str) != 32)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
        return ESP_FAIL;
    }

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    const device_uuid128 uuid128{uuid};
    ble_mesh_provision_device(uuid128.raw());

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t node_unprovision_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    if (sscanf(buf, "uuid=%32s", uuid_str) != 1 || strlen(uuid_str) != 32)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
        return ESP_FAIL;
    }

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    const device_uuid128 uuid128{uuid};
    ble_mesh_unprovision_device(uuid128);

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t node_send_mqtt_status_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    if (sscanf(buf, "uuid=%32s", uuid_str) != 1 || strlen(uuid_str) != 32)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
        return ESP_FAIL;
    }

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    const device_uuid128 uuid128{uuid};
    if (auto node_info = node_manager().get_node(uuid128))
    {
        mqtt_node_send_status(node_info);
    }

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t node_send_mqtt_discovery_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    if (sscanf(buf, "uuid=%32s", uuid_str) != 1 || strlen(uuid_str) != 32)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
        return ESP_FAIL;
    }

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    const device_uuid128 uuid128{uuid};
    if (auto node_info = node_manager().get_node(uuid128))
    {
        mqtt_send_discovery(node_info);
    }

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t set_lightness_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    int lightness = -1;

    if (sscanf(buf, "uuid=%32s&lightness=%d", uuid_str, &lightness) != 2)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid parameters");
        return ESP_FAIL;
    }

    if (strlen(uuid_str) != 32 || lightness < 0 || lightness > 65535)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid params");
        return ESP_FAIL;
    }

    // Convert hex UUID string back to bytes
    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    // Find the node by UUID
    const esp_ble_mesh_node_t *node = NULL;
    for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++)
    {
        const esp_ble_mesh_node_t *n = esp_ble_mesh_provisioner_get_node_table_entry()[i];
        if (n && memcmp(n->dev_uuid, uuid, 16) == 0)
        {
            node = n;
            break;
        }
    }

    if (!node)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Node not found");
        return ESP_FAIL;
    }

    // Optional: use node->unicast_addr or other data
    printf("Lightness for UUID: %s → %d\n", uuid_str, lightness);
    const device_uuid128 dev_uuid{uuid};
    ble_mesh_ctl_lightness_set(lightness, dev_uuid); // Extend this to accept node?

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t system_info_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    uint32_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    int64_t uptime_us = esp_timer_get_time();

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{ \"memory\": { \"free\": %lu, \"minimum\": %lu, \"total\": %lu, \"used\": %lu }, \"uptime\": %lld, \"version\": \"%s\" }",
             free_heap, min_heap, total_heap, total_heap - free_heap, uptime_us, FIRMWARE_VERSION);

    httpd_resp_send(req, buf, -1);
    return ESP_OK;
}

esp_err_t wifi_info_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    wifi_ap_record_t ap_info;
    wifi_config_t wifi_config;
    esp_netif_ip_info_t ip_info;
    uint8_t mac[6];
    char ssid[33] = "--";
    char ip_str[16] = "--";
    char netmask_str[16] = "--";
    char gateway_str[16] = "--";
    char mac_str[18] = "--";
    const char* status = "disconnected";

    // Get WiFi status
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        strncpy(ssid, (char*)ap_info.ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        status = "connected";
        
        // Get IP information
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            esp_ip4addr_ntoa(&ip_info.netmask, netmask_str, sizeof(netmask_str));
            esp_ip4addr_ntoa(&ip_info.gw, gateway_str, sizeof(gateway_str));
        }
    } else if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK && 
               strlen((char*)wifi_config.sta.ssid) > 0) {
        strncpy(ssid, (char*)wifi_config.sta.ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        status = "configured";
    }

    // Get MAC address
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{ \"status\": \"%s\", \"ssid\": \"%s\", \"ip\": \"%s\", \"netmask\": \"%s\", \"gateway\": \"%s\", \"mac\": \"%s\" }",
             status, ssid, ip_str, netmask_str, gateway_str, mac_str);

    httpd_resp_send(req, buf, -1);
    return ESP_OK;
}

esp_err_t api_wildcard_handler(httpd_req_t *req)
{
    // Route based on URI path
    if (strstr(req->uri, "/api/system_info"))
    {
        return system_info_handler(req);
    }
    else if (strstr(req->uri, "/api/wifi_info"))
    {
        return wifi_info_handler(req);
    }
    else if (strstr(req->uri, "/api/console_commands"))
    {
        return list_console_commands_handler(req);
    }
    else if (strstr(req->uri, "/api/auto_provisioning"))
    {
        if (req->method == HTTP_GET)
        {
            return auto_provisioning_get_handler(req);
        }
        else if (req->method == HTTP_POST)
        {
            return auto_provisioning_set_handler(req);
        }
        else
        {
            httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
            return ESP_FAIL;
        }
    }
    else if (strstr(req->uri, "/api/ota/upload"))
    {
        return ota_upload_handler(req);
    }
    else if (strstr(req->uri, "/api/storage/upload"))
    {
        return storage_upload_handler(req);
    }
    else if (strstr(req->uri, "/api/ota/status"))
    {
        return ota_status_handler(req);
    }
    else if (strstr(req->uri, "/api/ota/restart"))
    {
        return ota_restart_handler(req);
    }
    else if (strstr(req->uri, "/api/reset_wifi"))
    {
        return reset_wifi_handler(req);
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "API endpoint not found");
        return ESP_FAIL;
    }
}

esp_err_t mqtt_api_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // Extract action from URI path
    const char *action = nullptr;
    if (strstr(req->uri, "/status"))
    {
        action = "status";
    }
    else if (strstr(req->uri, "/config"))
    {
        action = "config";
    }
    else if (strstr(req->uri, "/clear"))
    {
        action = "clear";
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid MQTT API endpoint");
        return ESP_FAIL;
    }

    // Handle GET requests (status)
    if (req->method == HTTP_GET)
    {
        if (strcmp(action, "status") != 0)
        {
            httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
            return ESP_FAIL;
        }

        const auto &creds = mqtt_credentials().get_credentials();
        std::string last_error = mqtt_credentials().get_last_error();
        std::string state_str = mqtt_credentials().get_connection_state_string();

        // Escape JSON strings to prevent malformed JSON
        auto escape_json_string = [](const std::string& str) -> std::string {
            std::string escaped;
            escaped.reserve(str.length() * 1.2);
            for (char c : str) {
                switch (c) {
                    case '"': escaped += "\\\""; break;
                    case '\\': escaped += "\\\\"; break;
                    case '\b': escaped += "\\b"; break;
                    case '\f': escaped += "\\f"; break;
                    case '\n': escaped += "\\n"; break;
                    case '\r': escaped += "\\r"; break;
                    case '\t': escaped += "\\t"; break;
                    default:
                        if (c < 0x20) {
                            char hex[7];
                            snprintf(hex, sizeof(hex), "\\u%04x", c);
                            escaped += hex;
                        } else {
                            escaped += c;
                        }
                        break;
                }
            }
            return escaped;
        };

        std::string state = escape_json_string(mqtt_credentials().get_connection_state_string());
        std::string broker_host = creds.is_valid() ? escape_json_string(creds.broker_host) : "";
        std::string username = creds.is_valid() ? escape_json_string(creds.username) : "";
        std::string escaped_last_error = escape_json_string(last_error);

        char buf[1024];
        snprintf(buf, sizeof(buf),
                 "{ \"state\": \"%s\", \"configured\": %s, \"broker_host\": \"%s\", \"broker_port\": %d, \"use_ssl\": %s, \"username\": \"%s\", \"last_error\": \"%s\" }",
                 state.c_str(),
                 creds.is_valid() ? "true" : "false",
                 broker_host.c_str(),
                 creds.broker_port,
                 creds.use_ssl ? "true" : "false",
                 username.c_str(),
                 escaped_last_error.c_str());

        esp_err_t result = httpd_resp_send(req, buf, strlen(buf));
        return result;
    }

    // Handle POST requests (config, clear)
    if (req->method == HTTP_POST)
    {
        if (strcmp(action, "clear") == 0)
        {
            esp_err_t err = mqtt_credentials().clear_credentials();

            // Stop MQTT client since credentials are cleared
            if (err == ESP_OK)
            {
                mqtt5_app_stop();
            }

            if (err == ESP_OK)
            {
                httpd_resp_send(req, "{ \"success\": true, \"message\": \"Credentials cleared successfully\" }", -1);
            }
            else
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to clear credentials");
            }
            return ESP_OK;
        }

        if (strcmp(action, "config") == 0)
        {
            char buf[768] = {0};
            int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (recv_len <= 0 || recv_len >= sizeof(buf))
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
                return ESP_FAIL;
            }
            buf[recv_len] = '\0';

            // Parse JSON
            cJSON *json = cJSON_Parse(buf);
            if (!json)
            {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
                return ESP_FAIL;
            }

            mqtt_credentials_t new_creds;

            // Parse broker host
            cJSON *broker_host = cJSON_GetObjectItem(json, "broker_host");
            if (!broker_host || !cJSON_IsString(broker_host))
            {
                cJSON_Delete(json);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid broker_host");
                return ESP_FAIL;
            }
            new_creds.broker_host = broker_host->valuestring;

            // Parse broker port
            cJSON *broker_port = cJSON_GetObjectItem(json, "broker_port");
            if (broker_port && cJSON_IsNumber(broker_port))
            {
                new_creds.broker_port = (uint16_t)broker_port->valueint;
            }
            else
            {
                new_creds.broker_port = 1883; // Default
            }

            // Parse username
            cJSON *username = cJSON_GetObjectItem(json, "username");
            if (!username || !cJSON_IsString(username))
            {
                cJSON_Delete(json);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid username");
                return ESP_FAIL;
            }
            new_creds.username = username->valuestring;

            // Parse password
            cJSON *password = cJSON_GetObjectItem(json, "password");
            if (!password || !cJSON_IsString(password))
            {
                cJSON_Delete(json);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid password");
                return ESP_FAIL;
            }
            new_creds.password = password->valuestring;

            // Parse SSL flag
            cJSON *use_ssl = cJSON_GetObjectItem(json, "use_ssl");
            if (use_ssl && cJSON_IsBool(use_ssl))
            {
                new_creds.use_ssl = cJSON_IsTrue(use_ssl);
            }
            else
            {
                new_creds.use_ssl = false; // Default
            }

            cJSON_Delete(json);

            // Validate credentials
            std::string error_msg;
            if (!mqtt_credentials().validate_credentials(new_creds, error_msg))
            {
                char error_response[256];
                snprintf(error_response, sizeof(error_response),
                         "{ \"error\": \"Invalid credentials: %s\" }", error_msg.c_str());
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error_response);
                return ESP_FAIL;
            }

            // Save credentials
            esp_err_t err = mqtt_credentials().save_credentials(new_creds);
            if (err != ESP_OK)
            {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
                return ESP_FAIL;
            }

            // Restart MQTT client with new credentials
            mqtt5_app_restart();

            // Send success response
            httpd_resp_send(req, "{ \"success\": true, \"message\": \"Credentials saved successfully\" }", -1);
            return ESP_OK;
        }
    }

    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    return ESP_FAIL;
}

esp_err_t nodes_json_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{ \"provisioned\": [");

    // List provisioned nodes
    for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++)
    {
        const esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_table_entry()[i];
        if (!node)
            continue;

        char uuid_str[33];
        for (int j = 0; j < 16; j++)
            sprintf(&uuid_str[j * 2], "%02X", node->dev_uuid[j]);

        char unicast_str[6];
        sprintf(unicast_str, "%04X", node->unicast_addr);

        // Get company name from node manager
        const char *company_name = "Unknown";
        if (auto node_info = node_manager().get_node(device_uuid128{node->dev_uuid}))
        {
            if (node_info->company_id != 0)
            {
                company_name = lookup_company_name(node_info->company_id);
                if (!company_name)
                    company_name = "Unknown";
            }
        }

        char buf[512];
        snprintf(buf, sizeof(buf),
                 "%s{ \"uuid\": \"%s\", \"name\": \"%s\", \"unicast\": \"%s\", \"company\": \"%s\" }",
                 i > 0 ? "," : "", uuid_str, node->name, unicast_str, company_name);
        httpd_resp_sendstr_chunk(req, buf);
    }

    httpd_resp_sendstr_chunk(req, "], \"unprovisioned\": [");

    // List unprovisioned nodes
    bool first = true;
    for_each_unprovisioned_node([&](const ble2mqtt_unprovisioned_device &dev)
                                {
        char uuid_str[33];
        for (int j = 0; j < 16; j++) sprintf(&uuid_str[j * 2], "%02X", dev.dev_uuid[j]);

        char buf[256];
        snprintf(buf, sizeof(buf),
            "%s{ \"uuid\": \"%s\", \"rssi\": %d }",
            first ? "" : ",", uuid_str, dev.rssi);

        httpd_resp_sendstr_chunk(req, buf);
        first = false; });

    httpd_resp_sendstr_chunk(req, "] }");
    httpd_resp_sendstr_chunk(req, NULL); // end of response
    return ESP_OK;
}

esp_err_t list_console_commands_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    httpd_resp_sendstr_chunk(req, "[");
    for (size_t i = 0; i < get_registered_commands().size(); ++i)
    {
        const auto &cmd = get_registered_commands()[i];
        std::string json = "{ \"name\": \"" + cmd.name + "\", \"help\": \"" + cmd.help + "\" }";
        if (i < get_registered_commands().size() - 1)
        {
            json += ",";
        }
        httpd_resp_sendstr_chunk(req, json.c_str());
    }
    httpd_resp_sendstr_chunk(req, "]");

    return httpd_resp_sendstr_chunk(req, NULL);
}

esp_err_t bridge_wildcard_handler(httpd_req_t *req)
{
    // All bridge operations require POST method
    if (req->method != HTTP_POST)
    {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
        return ESP_FAIL;
    }

    // Route bridge operations
    if (strstr(req->uri, "/bridge/reset_wifi"))
    {
        return reset_wifi_handler(req);
    }
    else if (strstr(req->uri, "/bridge/restart"))
    {
        // Handle restart bridge functionality
        httpd_resp_send(req, NULL, 0);
        esp_restart();
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Bridge operation not found");
    return ESP_FAIL;
}

esp_err_t mqtt_wildcard_handler(httpd_req_t *req)
{
    // Route MQTT operations
    if (strstr(req->uri, "/mqtt/status") || strstr(req->uri, "/mqtt/config") || strstr(req->uri, "/mqtt/clear"))
    {
        return mqtt_api_handler(req);
    }
    else if (strstr(req->uri, "/mqtt/bridge_status"))
    {
        // All bridge MQTT operations require POST method
        if (req->method != HTTP_POST)
        {
            httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
            return ESP_FAIL;
        }
        publish_bridge_info();
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    else if (strstr(req->uri, "/mqtt/bridge_discovery"))
    {
        // All bridge MQTT operations require POST method
        if (req->method != HTTP_POST)
        {
            httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
            return ESP_FAIL;
        }
        send_bridge_discovery();
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "MQTT operation not found");
    return ESP_FAIL;
}

esp_err_t auto_provisioning_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    
    char buf[128];
    snprintf(buf, sizeof(buf), 
             "{ \"enable_provisioning\": %s, \"enable_auto_provisioning\": %s }",
             ble_mesh_get_provisioning_enabled() ? "true" : "false",
             ble_mesh_get_auto_provisioning_enabled() ? "true" : "false");
    
    httpd_resp_send(req, buf, -1);
    return ESP_OK;
}

esp_err_t auto_provisioning_set_handler(httpd_req_t *req)
{
    char buf[256];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Request body required");
        return ESP_FAIL;
    }
    buf[received] = '\0';
    
    // Parse JSON to get auto_provisioning value
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *auto_prov_item = cJSON_GetObjectItem(json, "enable_auto_provisioning");
    if (cJSON_IsBool(auto_prov_item))
    {
        bool new_value = cJSON_IsTrue(auto_prov_item);
        ble_mesh_set_auto_provisioning_enabled(new_value);
        
        httpd_resp_set_type(req, "application/json");
        snprintf(buf, sizeof(buf), "{ \"success\": true, \"enable_auto_provisioning\": %s }", 
                 new_value ? "true" : "false");
        httpd_resp_send(req, buf, -1);
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid enable_auto_provisioning field");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t node_wildcard_handler(httpd_req_t *req)
{
    // All node operations require POST method
    if (req->method != HTTP_POST)
    {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
        return ESP_FAIL;
    }

    // Route node operations
    if (strstr(req->uri, "/node/provision"))
    {
        return node_provision_handler(req);
    }
    else if (strstr(req->uri, "/node/unprovision"))
    {
        return node_unprovision_handler(req);
    }
    else if (strstr(req->uri, "/node/set_lightness"))
    {
        return set_lightness_handler(req);
    }
    else if (strstr(req->uri, "/node/rename"))
    {
        return rename_node_handler(req);
    }
    else if (strstr(req->uri, "/node/send_mqtt_status"))
    {
        return node_send_mqtt_status_handler(req);
    }
    else if (strstr(req->uri, "/node/send_mqtt_discovery"))
    {
        return node_send_mqtt_discovery_handler(req);
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Node operation not found");
    return ESP_FAIL;
}

esp_err_t reset_wifi_handler(httpd_req_t *req)
{
    LOG_INFO(TAG, "Clearing WiFi credentials via web interface...");

    // Send response first
    httpd_resp_send(req, NULL, 0);

    // Stop WiFi completely first
    esp_wifi_disconnect();
    esp_wifi_stop();

    // Clear WiFi credentials from NVS
    esp_err_t err = wifi_provisioning_clear_credentials();
    if (err == ESP_OK)
    {
        LOG_INFO(TAG, "WiFi credentials cleared successfully");

        // Also clear any WiFi configuration in RAM
        wifi_config_t wifi_config = {};
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

        LOG_INFO(TAG, "Restarting device to enter captive portal mode...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    else
    {
        LOG_ERROR(TAG, "Failed to clear WiFi credentials: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

esp_err_t rename_node_handler(httpd_req_t *req)
{
    LOG_WARN(TAG, "rename_node_handler called");
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0 || len >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json)
        return httpd_resp_send_500(req);

    const cJSON *name_json = cJSON_GetObjectItemCaseSensitive(json, "name");
    const cJSON *uuid_json = cJSON_GetObjectItemCaseSensitive(json, "uuid");

    if (!name_json || !cJSON_IsString(name_json) || !uuid_json || !cJSON_IsString(uuid_json))
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        ;
    }

    // Validate name length and content
    const char *name_str = name_json->valuestring;
    if (!name_str || strlen(name_str) == 0 || strlen(name_str) > 31)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid name length");
    }

    // Validate UUID format
    const char *uuid_str = uuid_json->valuestring;
    if (!uuid_str || strlen(uuid_str) != 32)
    {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
    }

    uint8_t uuid_tmp[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        if (sscanf(uuid_str + i * 2, "%2hhx", &uuid_tmp[i]) != 1)
        {
            cJSON_Delete(json);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID hex format");
        }
    }

    device_uuid128 uuid{uuid_tmp};

    node_manager().set_node_name(uuid, name_json->valuestring);
    if (auto node = node_manager().get_node(uuid))
    {
        mqtt_send_discovery(node);
    }

    cJSON_Delete(json);
    return httpd_resp_sendstr(req, "OK");
}

// Constants for OTA security
#define OTA_MIN_FIRMWARE_SIZE (32 * 1024)       // 32KB minimum
#define OTA_MAX_FIRMWARE_SIZE (2 * 1024 * 1024) // 2MB maximum
#define OTA_BUFFER_SIZE 1024

// ⚠️  SECURITY WARNING: This is a hardcoded key for development only!
// 🚨 CRITICAL: Replace with proper authentication before production deployment
// TODO: Implement certificate-based authentication or device-specific keys
#define OTA_API_KEY "ota_secure_key_2024"

static bool authenticate_ota_request(httpd_req_t *req)
{
    char auth_header[128];
    size_t header_len = httpd_req_get_hdr_value_len(req, "X-OTA-Key");

    if (header_len == 0 || header_len >= sizeof(auth_header))
    {
        LOG_WARN(TAG, "OTA authentication failed: missing or invalid X-OTA-Key header");
        return false;
    }

    if (httpd_req_get_hdr_value_str(req, "X-OTA-Key", auth_header, sizeof(auth_header)) != ESP_OK)
    {
        LOG_WARN(TAG, "OTA authentication failed: could not read X-OTA-Key header");
        return false;
    }

    bool authenticated = (strcmp(auth_header, OTA_API_KEY) == 0);
    if (!authenticated)
    {
        LOG_WARN(TAG, "OTA authentication failed: invalid API key");
    }
    else
    {
        LOG_INFO(TAG, "OTA authentication successful");
    }

    return authenticated;
}

esp_err_t validate_ota_request(httpd_req_t *req)
{
    // Validate content length
    if (req->content_len < OTA_MIN_FIRMWARE_SIZE)
    {
        LOG_WARN(TAG, "Firmware too small: %zu bytes (min %d)", req->content_len, OTA_MIN_FIRMWARE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    if (req->content_len > OTA_MAX_FIRMWARE_SIZE)
    {
        LOG_WARN(TAG, "Firmware too large: %zu bytes (max %d)", req->content_len, OTA_MAX_FIRMWARE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    // Log security event
    LOG_WARN(TAG, "OTA upload attempt - Size: %zu bytes", req->content_len);

    return ESP_OK;
}

esp_err_t ota_upload_handler(httpd_req_t *req)
{
    LOG_INFO(TAG, "OTA upload handler called");

    if (req->method != HTTP_POST)
    {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
        return ESP_FAIL;
    }

    // Authenticate request
    if (!authenticate_ota_request(req))
    {
        LOG_WARN(TAG, "Unauthorized OTA upload attempt");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return ESP_FAIL;
    }

    // Validate request
    esp_err_t validation_err = validate_ota_request(req);
    if (validation_err != ESP_OK)
    {
        if (validation_err == ESP_ERR_INVALID_SIZE)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware size");
        }
        else
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        }
        return ESP_FAIL;
    }

    // Get content length
    size_t content_length = req->content_len;

    LOG_INFO(TAG, "Starting OTA upload, content length: %zu bytes", content_length);

    // Begin OTA update
    esp_err_t err = ota_manager_begin(content_length);
    if (err != ESP_OK)
    {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "Failed to begin OTA: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
        return ESP_FAIL;
    }

    // Read and write firmware data in chunks
    char *buffer = (char *)malloc(OTA_BUFFER_SIZE);
    if (buffer == NULL)
    {
        ota_manager_abort();
        LOG_ERROR(TAG, "Failed to allocate %d bytes for OTA buffer", OTA_BUFFER_SIZE);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    size_t remaining = content_length;
    size_t total_written = 0;
    int recv_len = 0;

    while (remaining > 0)
    {
        size_t chunk_size = (remaining > OTA_BUFFER_SIZE) ? OTA_BUFFER_SIZE : remaining;
        recv_len = httpd_req_recv(req, buffer, chunk_size);

        if (recv_len <= 0)
        {
            LOG_ERROR(TAG, "OTA receive failed after %zu bytes. Error: %d", total_written, recv_len);
            goto cleanup;
        }

        if ((size_t)recv_len > remaining)
        {
            LOG_ERROR(TAG, "Received more data than expected: %d > %zu", recv_len, remaining);
            goto cleanup;
        }

        err = ota_manager_write((const uint8_t *)buffer, (size_t)recv_len);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "OTA write failed at offset %zu: %s", total_written, esp_err_to_name(err));
            goto cleanup;
        }

        remaining -= (size_t)recv_len;
        total_written += (size_t)recv_len;

        // Log progress every 64KB
        if (total_written % (64 * 1024) == 0)
        {
            LOG_INFO(TAG, "OTA progress: %zu / %zu bytes (%.1f%%)",
                     total_written, content_length,
                     (float)total_written / content_length * 100.0);
        }
    }

    ret = ESP_OK;
    LOG_INFO(TAG, "OTA upload completed: %zu bytes", total_written);

cleanup:
    free(buffer);
    if (ret != ESP_OK)
    {
        ota_manager_abort();
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload timeout");
        }
        else
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        }
        return ESP_FAIL;
    }

    // Finalize OTA update
    err = ota_manager_end();
    if (err != ESP_OK)
    {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "OTA finalization failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
        return ESP_FAIL;
    }

    // Send success response
    httpd_resp_set_type(req, "application/json");
    const char *success_response = "{ \"success\": true, \"message\": \"Firmware uploaded successfully. Device will restart in 3 seconds.\" }";
    httpd_resp_send(req, success_response, -1);

    // Restart after a short delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;
}

esp_err_t storage_upload_handler(httpd_req_t *req)
{
    LOG_INFO(TAG, "Storage upload handler called");

    if (req->method != HTTP_POST)
    {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
        return ESP_FAIL;
    }

    // Authenticate request
    if (!authenticate_ota_request(req))
    {
        LOG_WARN(TAG, "Unauthorized storage upload attempt");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return ESP_FAIL;
    }

    // Get content length
    size_t content_length = req->content_len;

    LOG_INFO(TAG, "Starting storage upload, content length: %zu bytes", content_length);

    // Validate storage file size
    if (content_length < 512)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Storage file too small (minimum 512 bytes)");
        return ESP_FAIL;
    }

    if (content_length > 256 * 1024) // 256KB max for storage
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Storage file too large (maximum 256KB)");
        return ESP_FAIL;
    }

    // Begin storage update
    esp_err_t err = ota_manager_begin_storage(content_length);
    if (err != ESP_OK)
    {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "Failed to begin storage update: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
        return ESP_FAIL;
    }

    // Read and write storage data in chunks
    char *buffer = (char *)malloc(OTA_BUFFER_SIZE);
    if (buffer == NULL)
    {
        ota_manager_abort();
        LOG_ERROR(TAG, "Failed to allocate %d bytes for storage buffer", OTA_BUFFER_SIZE);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    size_t remaining = content_length;
    size_t total_written = 0;
    int recv_len = 0;

    while (remaining > 0)
    {
        size_t chunk_size = (remaining > OTA_BUFFER_SIZE) ? OTA_BUFFER_SIZE : remaining;
        recv_len = httpd_req_recv(req, buffer, chunk_size);

        if (recv_len <= 0)
        {
            LOG_ERROR(TAG, "Storage receive failed after %zu bytes. Error: %d", total_written, recv_len);
            goto storage_cleanup;
        }

        if ((size_t)recv_len > remaining)
        {
            LOG_ERROR(TAG, "Received more data than expected: %d > %zu", recv_len, remaining);
            goto storage_cleanup;
        }

        err = ota_manager_write((const uint8_t *)buffer, (size_t)recv_len);
        if (err != ESP_OK)
        {
            LOG_ERROR(TAG, "Storage write failed at offset %zu: %s", total_written, esp_err_to_name(err));
            goto storage_cleanup;
        }

        remaining -= (size_t)recv_len;
        total_written += (size_t)recv_len;

        // Log progress every 32KB
        if (total_written % (32 * 1024) == 0)
        {
            LOG_INFO(TAG, "Storage progress: %zu / %zu bytes (%.1f%%)",
                     total_written, content_length,
                     (float)total_written / content_length * 100.0);
        }
    }

    ret = ESP_OK;
    LOG_INFO(TAG, "Storage upload completed: %zu bytes", total_written);

storage_cleanup:
    free(buffer);
    if (ret != ESP_OK)
    {
        ota_manager_abort();
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload timeout");
        }
        else
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
        }
        return ESP_FAIL;
    }

    // Finalize storage update
    err = ota_manager_end();
    if (err != ESP_OK)
    {
        char error_msg[128];
        snprintf(error_msg, sizeof(error_msg), "Storage finalization failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error_msg);
        return ESP_FAIL;
    }

    // Send success response
    httpd_resp_set_type(req, "application/json");
    const char *success_response = "{ \"success\": true, \"message\": \"Storage uploaded successfully. Please refresh the page to see changes.\" }";
    httpd_resp_send(req, success_response, -1);

    LOG_INFO(TAG, "Storage update completed successfully");

    return ESP_OK;
}

esp_err_t ota_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    const ota_progress_info_t *progress = ota_manager_get_progress();
    bool in_progress = ota_manager_is_in_progress();

    char response[256];
    snprintf(response, sizeof(response),
             "{ \"in_progress\": %s, \"progress_percent\": %d, \"written_size\": %zu, \"total_size\": %zu, \"status_message\": \"%s\" }",
             in_progress ? "true" : "false",
             progress->progress_percent,
             progress->written_size,
             progress->total_size,
             progress->status_message ? progress->status_message : "");

    httpd_resp_send(req, response, -1);
    return ESP_OK;
}

esp_err_t ota_restart_handler(httpd_req_t *req)
{
    if (req->method != HTTP_POST)
    {
        httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
        return ESP_FAIL;
    }

    // Send response first
    httpd_resp_set_type(req, "application/json");
    const char *response = "{ \"success\": true, \"message\": \"Device restarting...\" }";
    httpd_resp_send(req, response, -1);

    // Restart after short delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

httpd_uri_t setup_uri = {
    .uri = "/setup",
    .method = HTTP_GET,
    .handler = setup_handler,
};

esp_err_t root_handler(httpd_req_t *req)
{
    if (wifi_provisioning_get_state() == WIFI_PROV_STATE_AP_STARTED)
    {
        return setup_handler(req);
    }
    // Normal operation - serve index.html from littlefs
    char filepath[640];
    snprintf(filepath, sizeof(filepath), "/littlefs/index.html");

    FILE *file = fopen(filepath, "r");
    if (!file)
    {
        LOG_ERROR(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    char chunk[512];
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        httpd_resp_send_chunk(req, chunk, read_bytes);
    }
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

const char *get_content_type(const char *filename)
{
    if (strstr(filename, ".html"))
        return "text/html";
    if (strstr(filename, ".js"))
        return "application/javascript";
    if (strstr(filename, ".css"))
        return "text/css";
    if (strstr(filename, ".png"))
        return "image/png";
    if (strstr(filename, ".ico"))
        return "image/x-icon";
    if (strstr(filename, ".json"))
        return "application/json";
    return "text/plain";
}

esp_err_t setup_handler(httpd_req_t *req)
{
    // Add headers to help with captive portal detection
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");

    // Serve setup.html from filesystem
    const char *filepath = "/littlefs/setup.html";
    FILE *file = fopen(filepath, "r");
    if (!file)
    {
        LOG_ERROR(TAG, "Failed to open setup file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Setup file not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    char chunk[512];
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        httpd_resp_send_chunk(req, chunk, read_bytes);
    }
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t static_handler(httpd_req_t *req)
{
    wifi_provisioning_state_t state = wifi_provisioning_get_state();
    LOG_DEBUG(TAG, "Static handler: URI=%s, WiFi state=%d", req->uri, state);

    if (state == WIFI_PROV_STATE_AP_STARTED)
    {
        // In AP mode, redirect everything to setup page except API calls
        if (strncmp(req->uri, "/api/", 5) == 0)
        {
            // Let API calls through
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "API endpoint not found");
            return ESP_FAIL;
        }
        LOG_INFO(TAG, "Serving setup page in AP mode for URI: %s", req->uri);
        return setup_handler(req);
    }

    char filepath[640];
    if (strcmp(req->uri, "/") == 0)
    {
        snprintf(filepath, sizeof(filepath), "/littlefs/index.html");
    }
    else
    {
        snprintf(filepath, sizeof(filepath), "/littlefs%s", req->uri);
    }

    FILE *file = fopen(filepath, "r");
    if (!file)
    {
        if (wifi_provisioning_get_state() == WIFI_PROV_STATE_AP_STARTED)
        {
            return setup_handler(req);
        }
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        LOG_INFO(TAG, "Failed to open file: %s", filepath);
        return ESP_FAIL;
    }

    const char *content_type = get_content_type(filepath);
    httpd_resp_set_type(req, content_type);

    char chunk[512];
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        httpd_resp_send_chunk(req, chunk, read_bytes);
    }
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t static_uri = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = static_handler,
};

// Logically grouped bridge handlers array using wildcards
static httpd_uri_t bridge_handlers[] = {
    // Bridge operations handler - /bridge/*
    {
        .uri = "/bridge/*",
        .method = HTTP_POST,
        .handler = bridge_wildcard_handler,
        .user_ctx = NULL},
    // MQTT operations handler - /mqtt/*
    {
        .uri = "/mqtt/*",
        .method = HTTP_GET,
        .handler = mqtt_wildcard_handler,
        .user_ctx = NULL},
    {.uri = "/mqtt/*",
     .method = HTTP_POST,
     .handler = mqtt_wildcard_handler,
     .user_ctx = NULL},
    // Node operations handler - /node/*
    {
        .uri = "/node/*",
        .method = HTTP_POST,
        .handler = node_wildcard_handler,
        .user_ctx = NULL},
    // Remaining API endpoints - /api/*
    {
        .uri = "/api/*",
        .method = HTTP_GET,
        .handler = api_wildcard_handler,
        .user_ctx = NULL},
    {.uri = "/api/*",
     .method = HTTP_POST,
     .handler = api_wildcard_handler,
     .user_ctx = NULL},
    // Specific GET endpoints that remain as-is
    {
        .uri = "/nodes",
        .method = HTTP_GET,
        .handler = nodes_handler,
    },
    {
        .uri = "/nodes.json",
        .method = HTTP_GET,
        .handler = nodes_json_handler,
    },
    {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
    }};

static const size_t bridge_handlers_count = sizeof(bridge_handlers) / sizeof(bridge_handlers[0]);

// WiFi provisioning state change callback
static void wifi_state_change_callback(wifi_provisioning_state_t state, void *event_data)
{
    if (!g_server)
    {
        LOG_WARN(TAG, "Web server not initialized, cannot change handlers");
        return;
    }

    LOG_INFO(TAG, "WiFi state changed to: %d", state);

    switch (state)
    {
    case WIFI_PROV_STATE_AP_STARTED:
        LOG_INFO(TAG, "Switching to captive portal mode");
        unregister_bridge_handlers(g_server);
        register_captive_portal_handlers(g_server);
        break;

    case WIFI_PROV_STATE_STA_CONNECTED:
        LOG_INFO(TAG, "Switching to bridge operation mode");
        unregister_captive_portal_handlers(g_server);
        register_bridge_handlers(g_server);
        break;

    case WIFI_PROV_STATE_STA_CONNECTING:
    case WIFI_PROV_STATE_STA_FAILED:
    case WIFI_PROV_STATE_IDLE:
    default:
        // No handler changes needed for these states
        LOG_DEBUG(TAG, "No handler changes needed for state: %d", state);
        break;
    }
}

void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 30;
    config.stack_size = 5120; // Still need 5120 or we can have stack overflow issues when the bridge connects to mqtt, or some other reason ?

    // Improve connection management to prevent file descriptor leaks
    config.max_open_sockets = 4;      // Conservative: 4 client sockets (7 total - 3 internal)
    config.lru_purge_enable = true;   // Enable automatic cleanup of old connections
    config.keep_alive_enable = false; // Disable keep-alive to force connection closure
    config.close_fn = NULL;           // Use default close function
    config.recv_wait_timeout = 3;     // Reduced from 5 - more aggressive cleanup
    config.send_wait_timeout = 3;     // Reduced from 5 - more aggressive cleanup

    if (httpd_start(&server, &config) == ESP_OK)
    {
        wifi_provisioning_state_t current_state = wifi_provisioning_get_state();
        LOG_INFO(TAG, "Starting web server in state: %d", current_state);

        // Register handlers based on current state
        if (current_state == WIFI_PROV_STATE_AP_STARTED)
        {
            register_captive_portal_handlers(server);
        }
        else
        {
            register_bridge_handlers(server);
        }

        // Static file handler is always registered (handles both modes)
        httpd_register_uri_handler(server, &static_uri);

        // Store server handle for dynamic registration
        g_server = server;

        // Register callback for WiFi state changes to dynamically switch handlers
        esp_err_t callback_err = wifi_provisioning_set_event_callback(wifi_state_change_callback);
        if (callback_err != ESP_OK)
        {
            LOG_WARN(TAG, "Failed to register WiFi state callback: %s", esp_err_to_name(callback_err));
        }
        else
        {
            LOG_INFO(TAG, "WiFi state change callback registered successfully");
        }

        LOG_INFO(TAG, "Web server started successfully");
    }
    else
    {
        LOG_ERROR(TAG, "Failed to start web server");
    }
}

void register_captive_portal_handlers(httpd_handle_t server)
{
    if (!server)
    {
        LOG_ERROR(TAG, "Server handle is NULL");
        return;
    }

    LOG_INFO(TAG, "Registering captive portal handlers");
    wifi_provisioning_register_captive_portal_handlers(server);
    httpd_register_uri_handler(server, &setup_uri);
}

void unregister_captive_portal_handlers(httpd_handle_t server)
{
    if (!server)
    {
        LOG_ERROR(TAG, "Server handle is NULL");
        return;
    }

    LOG_INFO(TAG, "Unregistering captive portal handlers");

    // Array of captive portal URIs that need to be unregistered (matching wifi_provisioning.cpp)
    struct
    {
        const char *uri;
        httpd_method_t method;
    } captive_uris[] = {
        // Android captive portal detection (modern versions)
        {"/generate_204", HTTP_GET},
        {"/gen_204", HTTP_GET},
        {"/ncsi.txt", HTTP_GET},
        {"/connectivity-check.html", HTTP_GET},
        {"/connectivitycheck.gstatic.com/generate_204", HTTP_GET},

        // iOS captive portal detection
        {"/hotspot-detect.html", HTTP_GET},
        {"/library/test/success.html", HTTP_GET},
        {"/captive.apple.com/hotspot-detect.html", HTTP_GET},

        // Windows captive portal detection
        {"/connecttest.txt", HTTP_GET},
        {"/redirect", HTTP_GET},
        {"/msftconnecttest.com/connecttest.txt", HTTP_GET},
        {"/msftncsi.com/ncsi.txt", HTTP_GET},

        // Additional modern connectivity checks
        {"/mobile/status.php", HTTP_GET},
        {"/canonical.html", HTTP_GET},
        {"/success.txt", HTTP_GET},
        {"/kindle-wifi/wifiredirect.html", HTTP_GET},
        {"/kindle-wifi/wifistub.html", HTTP_GET},

        // API endpoints
        {"/api/wifi/scan", HTTP_GET},
        {"/api/wifi/connect", HTTP_POST},
        {"/api/wifi/status", HTTP_GET}};

    const size_t captive_uris_count = sizeof(captive_uris) / sizeof(captive_uris[0]);

    // Unregister all captive portal handlers
    for (size_t i = 0; i < captive_uris_count; i++)
    {
        esp_err_t err = httpd_unregister_uri_handler(server, captive_uris[i].uri, captive_uris[i].method);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
        {
            LOG_WARN(TAG, "Failed to unregister captive handler %s: %s",
                     captive_uris[i].uri, esp_err_to_name(err));
        }
    }

    // Unregister setup page
    httpd_unregister_uri_handler(server, "/setup", HTTP_GET);
}

void register_bridge_handlers(httpd_handle_t server)
{
    if (!server)
    {
        LOG_ERROR(TAG, "Server handle is NULL");
        return;
    }

    LOG_INFO(TAG, "Registering %zu bridge operation handlers", bridge_handlers_count);

    // Register all bridge handlers from array
    for (size_t i = 0; i < bridge_handlers_count; i++)
    {
        esp_err_t err = httpd_register_uri_handler(server, &bridge_handlers[i]);
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Failed to register handler %s: %s",
                     bridge_handlers[i].uri, esp_err_to_name(err));
        }
    }

    // Register websocket logger separately as it's not in the array
    websocket_logger_register_uri(server);
    websocket_logger_install();
}

void unregister_bridge_handlers(httpd_handle_t server)
{
    if (!server)
    {
        LOG_ERROR(TAG, "Server handle is NULL");
        return;
    }

    LOG_INFO(TAG, "Unregistering %zu bridge operation handlers", bridge_handlers_count);

    // Unregister all bridge handlers from array
    for (size_t i = 0; i < bridge_handlers_count; i++)
    {
        esp_err_t err = httpd_unregister_uri_handler(server, bridge_handlers[i].uri, bridge_handlers[i].method);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
        {
            LOG_WARN(TAG, "Failed to unregister handler %s: %s",
                     bridge_handlers[i].uri, esp_err_to_name(err));
        }
    }

    // Note: websocket_logger handlers are managed separately and don't need manual unregistration
}
