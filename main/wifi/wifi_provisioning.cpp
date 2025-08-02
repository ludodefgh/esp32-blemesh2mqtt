#include "wifi_provisioning.h"
#include "dns_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "wifi_provisioning";

static wifi_provisioning_state_t s_provisioning_state = WIFI_PROV_STATE_IDLE;
static wifi_provisioning_event_cb_t s_event_callback = NULL;
static esp_netif_t* s_ap_netif = NULL;
static esp_netif_t* s_sta_netif = NULL;
static wifi_ap_record_extended_t* s_scan_results = NULL;
static uint16_t s_scan_count = 0;
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WiFi AP started");
                s_provisioning_state = WIFI_PROV_STATE_AP_STARTED;
                if (s_event_callback) {
                    s_event_callback(s_provisioning_state, NULL);
                }
                break;
                
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "WiFi AP stopped");
                break;
                
            case WIFI_EVENT_AP_STACONNECTED:
                {
                    wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                    ESP_LOGI(TAG, "Station connected: MAC=" MACSTR, MAC2STR(event->mac));
                }
                break;
                
            case WIFI_EVENT_AP_STADISCONNECTED:
                {
                    wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                    ESP_LOGI(TAG, "Station disconnected: MAC=" MACSTR, MAC2STR(event->mac));
                }
                break;
                
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi STA connected");
                s_provisioning_state = WIFI_PROV_STATE_STA_CONNECTING;
                if (s_event_callback) {
                    s_event_callback(s_provisioning_state, event_data);
                }
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi STA disconnected");
                s_provisioning_state = WIFI_PROV_STATE_STA_FAILED;
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                if (s_event_callback) {
                    s_event_callback(s_provisioning_state, event_data);
                }
                break;
                
            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "WiFi scan completed");
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                {
                    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                    ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
                    s_provisioning_state = WIFI_PROV_STATE_STA_CONNECTED;
                    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                    if (s_event_callback) {
                        s_event_callback(s_provisioning_state, event_data);
                    }
                }
                break;
                
            default:
                break;
        }
    }
}

esp_err_t wifi_provisioning_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WiFi provisioning initialized");
    return ESP_OK;
}

esp_err_t wifi_provisioning_start_captive_portal(void)
{
    if (s_provisioning_state == WIFI_PROV_STATE_AP_STARTED) {
        ESP_LOGW(TAG, "Captive portal already started");
        return ESP_OK;
    }

    static bool event_handlers_registered = false;
    if (!event_handlers_registered) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
        event_handlers_registered = true;
    }

    uint8_t base_mac[6];
    esp_read_mac(base_mac, ESP_MAC_WIFI_STA);
    
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X%02X", 
             CAPTIVE_PORTAL_SSID_PREFIX, base_mac[3], base_mac[4], base_mac[5]);

    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, ap_ssid);
    ap_config.ap.ssid_len = strlen(ap_ssid);
    strcpy((char*)ap_config.ap.password, CAPTIVE_PORTAL_PASSWORD);
    ap_config.ap.channel = CAPTIVE_PORTAL_CHANNEL;
    ap_config.ap.max_connection = CAPTIVE_PORTAL_MAX_CONNECTIONS;
    ap_config.ap.authmode = strlen(CAPTIVE_PORTAL_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    static bool wifi_initialized = false;
    if (!wifi_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        wifi_initialized = true;
    }

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_str_to_ip4(CAPTIVE_PORTAL_IP, &ip_info.ip);
    esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask);
    esp_netif_str_to_ip4(CAPTIVE_PORTAL_IP, &ip_info.gw);
    
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(dns_server_start());

    ESP_LOGI(TAG, "Captive portal started with SSID: %s", ap_ssid);
    return ESP_OK;
}

esp_err_t wifi_provisioning_stop_captive_portal(void)
{
    if (s_provisioning_state != WIFI_PROV_STATE_AP_STARTED) {
        ESP_LOGW(TAG, "Captive portal not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping captive portal...");
    
    // Stop DNS server first
    dns_server_stop();
    
    // Give some time for any ongoing operations to complete
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Stop WiFi to ensure clean state
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi stop failed: %s", esp_err_to_name(err));
    }
    
    // Set to STA mode for next boot
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi mode set failed: %s", esp_err_to_name(err));
    }
    
    s_provisioning_state = WIFI_PROV_STATE_IDLE;
    
    ESP_LOGI(TAG, "Captive portal stopped");
    return ESP_OK;
}

esp_err_t wifi_provisioning_set_credentials(const char* ssid, const char* password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, WIFI_SSID_KEY, ssid);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, WIFI_PASSWORD_KEY, password);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    uint8_t configured = 1;
    err = nvs_set_u8(nvs_handle, WIFI_CONFIGURED_KEY, configured);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WiFi credentials saved: SSID=%s", ssid);
    return err;
}

esp_err_t wifi_provisioning_get_credentials(char* ssid, char* password, size_t ssid_len, size_t password_len)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required_size = ssid_len;
    err = nvs_get_str(nvs_handle, WIFI_SSID_KEY, ssid, &required_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    required_size = password_len;
    err = nvs_get_str(nvs_handle, WIFI_PASSWORD_KEY, password, &required_size);
    nvs_close(nvs_handle);

    return err;
}

bool wifi_provisioning_is_configured(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t configured = 0;
    err = nvs_get_u8(nvs_handle, WIFI_CONFIGURED_KEY, &configured);
    nvs_close(nvs_handle);

    return (err == ESP_OK && configured == 1);
}

esp_err_t wifi_provisioning_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_key(nvs_handle, WIFI_SSID_KEY);
    nvs_erase_key(nvs_handle, WIFI_PASSWORD_KEY);
    nvs_erase_key(nvs_handle, WIFI_CONFIGURED_KEY);
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WiFi credentials cleared");
    return err;
}

esp_err_t wifi_provisioning_scan_start(void)
{
    static uint32_t last_scan_time = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Only scan if more than 30 seconds have passed since last scan
    if (s_scan_results && (current_time - last_scan_time) < 30000) {
        ESP_LOGD(TAG, "Using cached scan results (%d networks)", s_scan_count);
        return ESP_OK;
    }

    if (s_scan_results) {
        free(s_scan_results);
        s_scan_results = NULL;
        s_scan_count = 0;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 200  // Reduced scan time for faster response
            }
        }
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    last_scan_time = current_time;

    uint16_t max_aps = 20;
    wifi_ap_record_t* ap_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * max_aps);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&max_aps, ap_records));

    s_scan_results = (wifi_ap_record_extended_t*)malloc(sizeof(wifi_ap_record_extended_t) * max_aps);
    s_scan_count = max_aps;

    for (int i = 0; i < max_aps; i++) {
        strcpy(s_scan_results[i].ssid, (char*)ap_records[i].ssid);
        s_scan_results[i].rssi = ap_records[i].rssi;
        s_scan_results[i].authmode = ap_records[i].authmode;
    }

    free(ap_records);
    ESP_LOGI(TAG, "WiFi scan completed, found %d networks", s_scan_count);
    return ESP_OK;
}

esp_err_t wifi_provisioning_get_scan_results(wifi_ap_record_extended_t** ap_records, uint16_t* count)
{
    if (!ap_records || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    *ap_records = s_scan_results;
    *count = s_scan_count;
    return ESP_OK;
}

wifi_provisioning_state_t wifi_provisioning_get_state(void)
{
    return s_provisioning_state;
}

esp_err_t wifi_provisioning_set_event_callback(wifi_provisioning_event_cb_t callback)
{
    s_event_callback = callback;
    return ESP_OK;
}

esp_err_t wifi_provisioning_try_connect_sta(void)
{
    char ssid[32] = {0};
    char password[64] = {0};
    
    esp_err_t err = wifi_provisioning_get_credentials(ssid, password, sizeof(ssid), sizeof(password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi credentials");
        return err;
    }

    // Initialize WiFi if not already done
    static bool wifi_initialized = false;
    if (!wifi_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        wifi_initialized = true;
    }

    // Register event handlers if not already done
    static bool event_handlers_registered = false;
    if (!event_handlers_registered) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
        event_handlers_registered = true;
    }

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t sta_config = {};
    strcpy((char*)sta_config.sta.ssid, ssid);
    strcpy((char*)sta_config.sta.password, password);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // esp_wifi_start() automatically triggers connection in STA mode
    // No need to call esp_wifi_connect() explicitly

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE, pdFALSE,
                                          pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi network: %s", ssid);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi network: %s", ssid);
        return ESP_FAIL;
    }
}

bool wifi_provisioning_should_start_captive_portal(void)
{
    if (!wifi_provisioning_is_configured()) {
        ESP_LOGI(TAG, "No WiFi credentials configured, starting captive portal");
        return true;
    }

    esp_err_t connect_result = wifi_provisioning_try_connect_sta();
    if (connect_result != ESP_OK) {
        ESP_LOGI(TAG, "Failed to connect with stored credentials, starting captive portal");
        return true;
    }

    return false;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t captive_android_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Android captive portal detection: %s", req->uri);
    
    // Modern Android captive portal detection optimization
    // Return 200 with captive portal content for faster detection
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_set_hdr(req, "Content-Type", "text/html");
    
    // Send minimal HTML that triggers captive portal detection
    const char* response = "<html><head><title>Captive Portal</title></head>"
                          "<body><script>window.location.href='http://192.168.4.1/setup';</script>"
                          "<h1>WiFi Setup Required</h1>"
                          "<p><a href='http://192.168.4.1/setup'>Click here to configure WiFi</a></p>"
                          "</body></html>";
    
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

static esp_err_t captive_ios_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "iOS captive portal detection: %s", req->uri);
    // iOS expects specific content for hotspot-detect.html
    // Return different content to trigger captive portal
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t captive_windows_handler(httpd_req_t *req)
{
    // Windows expects specific content for connecttest.txt
    // Return different content to trigger captive portal
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    esp_err_t err = wifi_provisioning_scan_start();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    wifi_ap_record_extended_t* ap_records;
    uint16_t count;
    wifi_provisioning_get_scan_results(&ap_records, &count);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", ap_records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(ap, "authmode", ap_records[i].authmode);
        cJSON_AddItemToArray(root, ap);
    }

    char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free(json_string);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(json, "password");

    if (!ssid_json || !cJSON_IsString(ssid_json) || 
        !password_json || !cJSON_IsString(password_json)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID or password");
        return ESP_FAIL;
    }

    const char* ssid = cJSON_GetStringValue(ssid_json);
    const char* password = cJSON_GetStringValue(password_json);

    esp_err_t err = wifi_provisioning_set_credentials(ssid, password);
    cJSON_Delete(json);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddStringToObject(response, "message", "Credentials saved, restarting device...");

    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response_string);

    free(response_string);
    cJSON_Delete(response);

    // Schedule restart to allow HTTP response to be sent
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Stop captive portal cleanly before restart
    wifi_provisioning_stop_captive_portal();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "Restarting ESP32 to connect with new credentials...");
    esp_restart();

    return ESP_OK;
}

static esp_err_t wifi_status_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();
    
    switch (s_provisioning_state) {
        case WIFI_PROV_STATE_IDLE:
            cJSON_AddStringToObject(response, "status", "idle");
            break;
        case WIFI_PROV_STATE_AP_STARTED:
            cJSON_AddStringToObject(response, "status", "ap_started");
            break;
        case WIFI_PROV_STATE_STA_CONNECTING:
            cJSON_AddStringToObject(response, "status", "connecting");
            break;
        case WIFI_PROV_STATE_STA_CONNECTED:
            cJSON_AddStringToObject(response, "status", "connected");
            break;
        case WIFI_PROV_STATE_STA_FAILED:
            cJSON_AddStringToObject(response, "status", "failed");
            break;
    }

    char *response_string = cJSON_Print(response);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response_string);

    free(response_string);
    cJSON_Delete(response);
    return ESP_OK;
}

void wifi_provisioning_register_captive_portal_handlers(httpd_handle_t server)
{
    httpd_uri_t captive_uris[] = {
        // Android captive portal detection (modern versions)
        { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_android_handler },
        { .uri = "/gen_204", .method = HTTP_GET, .handler = captive_android_handler },
        { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_android_handler },
        { .uri = "/connectivity-check.html", .method = HTTP_GET, .handler = captive_android_handler },
        
        // iOS captive portal detection  
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_ios_handler },
        { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = captive_ios_handler },
        
        // Windows captive portal detection
        { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_windows_handler },
        { .uri = "/redirect", .method = HTTP_GET, .handler = captive_redirect_handler },
        
        // Additional common captive portal endpoints
        { .uri = "/mobile/status.php", .method = HTTP_GET, .handler = captive_redirect_handler },
        { .uri = "/canonical.html", .method = HTTP_GET, .handler = captive_redirect_handler },
        { .uri = "/success.txt", .method = HTTP_GET, .handler = captive_redirect_handler },
        
        // API endpoints
        { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_handler },
        { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler },
        { .uri = "/api/wifi/status", .method = HTTP_GET, .handler = wifi_status_handler }
    };

    for (int i = 0; i < sizeof(captive_uris) / sizeof(captive_uris[0]); i++) {
        httpd_register_uri_handler(server, &captive_uris[i]);
    }
}