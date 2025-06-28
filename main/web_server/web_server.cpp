#include   "web_server.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include <esp_ble_mesh_networking_api.h>
#include <ble_mesh/ble_mesh_provisioning.h>
#include <ble_mesh/ble_mesh_control.h>

#define TAG "WEB_SERVER"

esp_err_t hello_get_handler(httpd_req_t *req)
{
    const char *resp = "Hello from ESP32!";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_uri_t hello = {
    .uri = "/hello",
    .method = HTTP_GET,
    .handler = hello_get_handler,
};

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
    uint16_t count = esp_ble_mesh_provisioner_get_prov_node_count();
    for (int i = 0; i < count; i++)
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
    char buf[64] = {0};
    httpd_req_recv(req, buf, sizeof(buf) - 1);

    char uuid_str[33] = {0};
    sscanf(buf, "uuid=%32s", uuid_str);

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++) {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    provision_device(uuid);  // Starts provisioning

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t set_lightness_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    httpd_req_recv(req, buf, sizeof(buf) - 1);

    char uuid_str[33] = {0};
    int lightness = -1;

    sscanf(buf, "uuid=%32s&lightness=%d", uuid_str, &lightness);

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
    uint16_t count = esp_ble_mesh_provisioner_get_prov_node_count();
    const esp_ble_mesh_node_t *node = NULL;
    for (int i = 0; i < count; i++)
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
    ble_mesh_ctl_lightness_set(lightness, uuid); // Extend this to accept node?

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t unprovision_handler(httpd_req_t *req)
{
    char buf[64] = {0};
    httpd_req_recv(req, buf, sizeof(buf) - 1);

    char uuid_str[33] = {0};
    sscanf(buf, "uuid=%32s", uuid_str);

    uint8_t uuid[16] = {0};
    for (int i = 0; i < 16; i++)
    {
        sscanf(uuid_str + i * 2, "%2hhx", &uuid[i]);
    }

    // Call your function to re-provision the node
    unprovision_device(uuid); // Your unprovisioning function

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t nodes_json_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{ \"provisioned\": [");

    // List provisioned nodes
    uint16_t count = esp_ble_mesh_provisioner_get_prov_node_count();
    for (int i = 0; i < count; i++) {
        const esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_table_entry()[i];
        if (!node) continue;

        char uuid_str[33];
        for (int j = 0; j < 16; j++) sprintf(&uuid_str[j * 2], "%02X", node->dev_uuid[j]);

        char buf[256];
        snprintf(buf, sizeof(buf),
            "%s{ \"uuid\": \"%s\", \"name\": \"%s\" }",
            i > 0 ? "," : "", uuid_str, node->name);
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

httpd_uri_t json_nodes_uri = {
    .uri = "/nodes.json",
    .method = HTTP_GET,
    .handler = nodes_json_handler,
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

esp_err_t static_handler(httpd_req_t *req)
{
    char filepath[640];
    //snprintf(filepath, sizeof(filepath), "/littlefs%s", req->uri[0] == '/' ? req->uri : "/index.html");
    snprintf(filepath, sizeof(filepath), "/littlefs%s", req->uri[0] == '/' ? req->uri : "/index.html");

    FILE *file = fopen(filepath, "r");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        ESP_LOGI(TAG, "Failed to open file: %s", filepath);
        ESP_LOGE(TAG, "FUCK ME: [%s]", req->uri);
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


// void register_static_routes(httpd_handle_t server)
// {
//     httpd_uri_t static_uri = {
//         .uri = "/*",
//         .method = HTTP_GET,
//         .handler = static_handler,
//     };
//     httpd_register_uri_handler(server, &static_uri);
// }

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

    if (httpd_start(&server, &config) == ESP_OK)
    {
        // httpd_register_uri_handler(server, &hello);
        httpd_register_uri_handler(server, &nodes_uri);
        httpd_register_uri_handler(server, &set_lightness_uri);
        httpd_register_uri_handler(server, &set_provision_uri);
        httpd_register_uri_handler(server, &set_unprovision_uri);
        httpd_register_uri_handler(server, &json_nodes_uri);
        //register_static_routes(server);
        httpd_register_uri_handler(server, &static_uri);
    }
}
