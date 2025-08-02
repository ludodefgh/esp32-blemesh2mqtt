#include   "web_server.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include <esp_ble_mesh_networking_api.h>
#include <ble_mesh/ble_mesh_provisioning.h>
#include <ble_mesh/ble_mesh_commands.h>
#include "debug/console_cmd.h"
#include <debug/websocket_logger.h>
#include <cJSON.h>
#include <mqtt/mqtt_control.h>
#include <mqtt/mqtt_bridge.h>
#include "wifi/wifi_provisioning.h"

#define TAG "WEB_SERVER"

esp_err_t setup_handler(httpd_req_t *req);

esp_err_t nodes_handler(httpd_req_t *req)
{
    //
    // Provisioned nodes
    //
    // Start HTML + JavaScript
    httpd_resp_send_chunk(req,
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
                          "  fetch('/set_lightness', {\n"
                          "    method: 'POST',\n"
                          "    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },\n"
                          "    body: 'uuid=' + encodeURIComponent(uuid) + '&lightness=' + encodeURIComponent(value)\n"
                          "  }).catch(err => console.error('Failed to send lightness:', err));\n"
                          "}\n"
                          "function unprovisionNode(uuid) {"
                          "fetch('/unprovision', {"
                          "method: 'POST',"
                          " headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
                          "body: 'uuid=' + encodeURIComponent(uuid)"
                          "}).then(() => location.reload());"
                          "}"

                          "function provisionNode(uuid) {"
                          "fetch('/provision', {"
                          " method: 'POST',"
                          "  headers: { 'Content-Type': 'application/x-www-form-urlencoded' },"
                          "   body: 'uuid=' + encodeURIComponent(uuid)"
                          "  }).then(() => location.reload());"
                          "}"
                          "</script>\n",
                          -1);

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

esp_err_t provision_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    if (sscanf(buf, "uuid=%32s", uuid_str) != 1 || strlen(uuid_str) != 32) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
        return ESP_FAIL;
    }

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++) {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    const Uuid128 uuid128{uuid};
    provision_device(uuid128.raw());  // Starts provisioning

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t set_lightness_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    int lightness = -1;

    if (sscanf(buf, "uuid=%32s&lightness=%d", uuid_str, &lightness) != 2) {
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
    const Uuid128 dev_uuid{uuid};
    ble_mesh_ctl_lightness_set(lightness, dev_uuid); // Extend this to accept node?

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t unprovision_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    if (sscanf(buf, "uuid=%32s", uuid_str) != 1 || strlen(uuid_str) != 32) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
        return ESP_FAIL;
    }

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    // Call your function to re-provision the node
    const Uuid128 dev_uuid{uuid};
    unprovision_device(dev_uuid); // Your unprovisioning function

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t send_mqtt_status_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    if (sscanf(buf, "uuid=%32s", uuid_str) != 1 || strlen(uuid_str) != 32) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
        return ESP_FAIL;
    }

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    // Find the node by UUID and send MQTT status
    const Uuid128 dev_uuid{uuid};
    if (bm2mqtt_node_info *node_info = node_manager().get_node(dev_uuid))
    {
        mqtt_node_send_status(node_info);
    }

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t send_mqtt_discovery_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int recv_len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (recv_len <= 0 || recv_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[recv_len] = '\0';

    char uuid_str[33] = {0};
    if (sscanf(buf, "uuid=%32s", uuid_str) != 1 || strlen(uuid_str) != 32) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
        return ESP_FAIL;
    }

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    // Find the node by UUID and send MQTT discovery
    const Uuid128 dev_uuid{uuid};
    if (bm2mqtt_node_info *node_info = node_manager().get_node(dev_uuid))
    {
        mqtt_send_discovery(node_info);
    }

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t nodes_json_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{ \"provisioned\": [");

    // List provisioned nodes
    for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++) {
        const esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_table_entry()[i];
        if (!node) continue;

        char uuid_str[33];
        for (int j = 0; j < 16; j++) sprintf(&uuid_str[j * 2], "%02X", node->dev_uuid[j]);

        char unicast_str[6];
        sprintf(unicast_str, "%04X", node->unicast_addr);

        char buf[256];
        snprintf(buf, sizeof(buf),
            "%s{ \"uuid\": \"%s\", \"name\": \"%s\", \"unicast\": \"%s\" }",
            i > 0 ? "," : "", uuid_str, node->name,unicast_str);
        httpd_resp_sendstr_chunk(req, buf);
    }

    httpd_resp_sendstr_chunk(req, "], \"unprovisioned\": [");

    // List unprovisioned nodes
    bool first = true;
    for_each_unprovisioned_node([&](const ble2mqtt_unprovisioned_device& dev) {
        char uuid_str[33];
        for (int j = 0; j < 16; j++) sprintf(&uuid_str[j * 2], "%02X", dev.dev_uuid[j]);

        char buf[256];
        snprintf(buf, sizeof(buf),
            "%s{ \"uuid\": \"%s\", \"rssi\": %d }",
            first ? "" : ",", uuid_str, dev.rssi);

        httpd_resp_sendstr_chunk(req, buf);
        first = false;
    });

    httpd_resp_sendstr_chunk(req, "] }");
    httpd_resp_sendstr_chunk(req, NULL);  // end of response
    return ESP_OK;
}

esp_err_t list_console_commands_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    
    httpd_resp_sendstr_chunk(req, "[");
    for (size_t i = 0; i < get_registered_commands().size(); ++i)
    {
        const auto &cmd = get_registered_commands()[i];
        std::string json = "{ \"name\": \"" + cmd.name + "\", \"help\": \"" + cmd.help + "\" }";
        if (i < get_registered_commands().size() - 1) {json += ",";}
        httpd_resp_sendstr_chunk(req, json.c_str());
    }
    httpd_resp_sendstr_chunk(req, "]");

    return httpd_resp_sendstr_chunk(req, NULL);
}

esp_err_t send_bridge_mqtt_discovery_handler(httpd_req_t *req)
{
    send_bridge_discovery();
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t send_bridge_mqtt_status_handler(httpd_req_t *req)
{
    publish_bridge_info("0.1.0");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t restart_bridge_handler(httpd_req_t *req)
{
    httpd_resp_send(req, NULL, 0);
    esp_restart();
    return ESP_OK;
}

esp_err_t reset_wifi_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Clearing WiFi credentials via web interface...");
    
    // Send response first
    httpd_resp_send(req, NULL, 0);
    
    // Stop WiFi completely first
    esp_wifi_disconnect();
    esp_wifi_stop();
    
    // Clear WiFi credentials from NVS
    esp_err_t err = wifi_provisioning_clear_credentials();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials cleared successfully");
        
        // Also clear any WiFi configuration in RAM
        wifi_config_t wifi_config = {};
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        
        ESP_LOGI(TAG, "Restarting device to enter captive portal mode...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to clear WiFi credentials: %s", esp_err_to_name(err));
    }
    
    return ESP_OK;
}

esp_err_t rename_node_handler(httpd_req_t *req) {
    ESP_LOGW(TAG, "rename_node_handler called");
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0 || len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) return httpd_resp_send_500(req);

   const cJSON *name_json = cJSON_GetObjectItemCaseSensitive(json, "name");
   const cJSON *uuid_json = cJSON_GetObjectItemCaseSensitive(json, "uuid");

    if (!name_json || !cJSON_IsString(name_json) || !uuid_json || !cJSON_IsString(uuid_json)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");;
    }
    
    // Validate name length and content
    const char* name_str = name_json->valuestring;
    if (!name_str || strlen(name_str) == 0 || strlen(name_str) > 31) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid name length");
    }
    
    // Validate UUID format
    const char* uuid_str = uuid_json->valuestring;
    if (!uuid_str || strlen(uuid_str) != 32) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID format");
    }

    uint8_t uuid_tmp[16] = {0};
    for (int i = 0; i < 16; i++) {
        if (sscanf(uuid_str + i * 2, "%2hhx", &uuid_tmp[i]) != 1) {
            cJSON_Delete(json);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid UUID hex format");
        }
    }

    Uuid128 uuid{uuid_tmp};

    node_manager().set_node_name(uuid, name_json->valuestring);
    if (bm2mqtt_node_info *node = node_manager().get_node(uuid))
    {
        mqtt_send_discovery(node);
    }

    cJSON_Delete(json);
    return httpd_resp_sendstr(req, "OK");
}

httpd_uri_t nodes_uri = {
    .uri = "/nodes",
    .method = HTTP_GET,
    .handler = nodes_handler,
};

httpd_uri_t set_lightness_uri = {
    .uri = "/set_lightness",
    .method = HTTP_POST,
    .handler = set_lightness_handler,
};

httpd_uri_t set_provision_uri = {
    .uri = "/provision",
    .method = HTTP_POST,
    .handler = provision_handler,
};

httpd_uri_t set_unprovision_uri = {
    .uri = "/unprovision",
    .method = HTTP_POST,
    .handler = unprovision_handler,
};

httpd_uri_t send_mqtt_status_uri = {
    .uri = "/send_mqtt_status",
    .method = HTTP_POST,
    .handler = send_mqtt_status_handler,
};

httpd_uri_t send_mqtt_discovery_uri = {
    .uri = "/send_mqtt_discovery",
    .method = HTTP_POST,
    .handler = send_mqtt_discovery_handler,
};

httpd_uri_t json_nodes_uri = {
    .uri = "/nodes.json",
    .method = HTTP_GET,
    .handler = nodes_json_handler,
};

httpd_uri_t console_cmds_uri = {
    .uri = "/api/console_commands",
    .method = HTTP_GET,
    .handler = list_console_commands_handler,
    .user_ctx = NULL
};

httpd_uri_t rename_uri = {
    .uri = "/api/rename_node",
    .method = HTTP_POST,
    .handler = rename_node_handler,
    .user_ctx = nullptr
};

httpd_uri_t send_bridge_mqtt_discovery_uri = {
    .uri = "/send_bridge_mqtt_discovery",
    .method = HTTP_POST,
    .handler = send_bridge_mqtt_discovery_handler,
};

httpd_uri_t send_bridge_mqtt_status_uri = {
    .uri = "/send_bridge_mqtt_status",
    .method = HTTP_POST,
    .handler = send_bridge_mqtt_status_handler,
};

httpd_uri_t restart_bridge_uri = {
    .uri = "/restart_bridge",
    .method = HTTP_POST,
    .handler = restart_bridge_handler,
};

httpd_uri_t reset_wifi_uri = {
    .uri = "/reset_wifi",
    .method = HTTP_POST,
    .handler = reset_wifi_handler,
};

httpd_uri_t setup_uri = {
    .uri = "/setup",
    .method = HTTP_GET,
    .handler = setup_handler,
};

esp_err_t root_handler(httpd_req_t *req)
{
    if (wifi_provisioning_get_state() == WIFI_PROV_STATE_AP_STARTED) {
        return setup_handler(req);
    }
    // Normal operation - serve index.html from littlefs
    char filepath[640];
    snprintf(filepath, sizeof(filepath), "/littlefs/index.html");
    
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    char chunk[512];
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        httpd_resp_send_chunk(req, chunk, read_bytes);
    }
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
};

const char *get_content_type(const char *filename)
{
    if (strstr(filename, ".html")) return "text/html";
    if (strstr(filename, ".js"))   return "application/javascript";
    if (strstr(filename, ".css"))  return "text/css";
    if (strstr(filename, ".png"))  return "image/png";
    if (strstr(filename, ".ico"))  return "image/x-icon";
    if (strstr(filename, ".json")) return "application/json";
    return "text/plain";
}

esp_err_t setup_handler(httpd_req_t *req)
{
    // Add headers to help with captive portal detection
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    // Serve setup.html from filesystem
    const char* filepath = "/littlefs/setup.html";
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open setup file: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Setup file not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    char chunk[512];
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        httpd_resp_send_chunk(req, chunk, read_bytes);
    }
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t static_handler(httpd_req_t *req)
{
    wifi_provisioning_state_t state = wifi_provisioning_get_state();
    ESP_LOGD(TAG, "Static handler: URI=%s, WiFi state=%d", req->uri, state);
    
    if (state == WIFI_PROV_STATE_AP_STARTED) {
        // In AP mode, redirect everything to setup page except API calls
        if (strncmp(req->uri, "/api/", 5) == 0) {
            // Let API calls through
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "API endpoint not found");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Serving setup page in AP mode for URI: %s", req->uri);
        return setup_handler(req);
    }

    char filepath[640];
    if (strcmp(req->uri, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "/littlefs/index.html");
    } else {
        snprintf(filepath, sizeof(filepath), "/littlefs%s", req->uri);
    }

    FILE *file = fopen(filepath, "r");
    if (!file) {
        if (wifi_provisioning_get_state() == WIFI_PROV_STATE_AP_STARTED) {
            return setup_handler(req);
        }
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        ESP_LOGI(TAG, "Failed to open file: %s", filepath);
        return ESP_FAIL;
    }

    const char *content_type = get_content_type(filepath);
    httpd_resp_set_type(req, content_type);

    char chunk[512];
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0) {
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

void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

     config.uri_match_fn = httpd_uri_match_wildcard;
     config.max_uri_handlers = 30;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        wifi_provisioning_state_t current_state = wifi_provisioning_get_state();
        ESP_LOGI(TAG, "Starting web server in state: %d", current_state);
        
        // Always register captive portal handlers - they'll be used if needed
        wifi_provisioning_register_captive_portal_handlers(server);
        httpd_register_uri_handler(server, &setup_uri);
        
        // Only register normal operation handlers when not in setup mode
        if (current_state != WIFI_PROV_STATE_AP_STARTED) {
            httpd_register_uri_handler(server, &rename_uri);
            httpd_register_uri_handler(server, &nodes_uri);
            httpd_register_uri_handler(server, &set_lightness_uri);
            httpd_register_uri_handler(server, &set_provision_uri);
            httpd_register_uri_handler(server, &set_unprovision_uri);
            httpd_register_uri_handler(server, &send_mqtt_status_uri);
            httpd_register_uri_handler(server, &send_mqtt_discovery_uri);
            httpd_register_uri_handler(server, &send_bridge_mqtt_discovery_uri);
            httpd_register_uri_handler(server, &send_bridge_mqtt_status_uri);
            httpd_register_uri_handler(server, &restart_bridge_uri);
            httpd_register_uri_handler(server, &reset_wifi_uri);
            httpd_register_uri_handler(server, &json_nodes_uri);
            httpd_register_uri_handler(server, &console_cmds_uri);
           
            websocket_logger_register_uri(server);
            websocket_logger_install();
        }

        // Static file handler is always registered (handles both modes)
        httpd_register_uri_handler(server, &static_uri);

        ESP_LOGI(TAG, "Web server started successfully");
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
    }
}
