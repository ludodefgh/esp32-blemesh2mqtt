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
#include "security/credential_encryption.h"
#include <string.h>
#include <stdio.h>
#include <dhcpserver/dhcpserver.h>

static const char* TAG = "wifi_provisioning";

static wifi_provisioning_state_t s_provisioning_state = WIFI_PROV_STATE_IDLE;
static wifi_provisioning_event_cb_t s_event_callback = NULL;
static esp_netif_t* s_ap_netif = NULL;
static esp_netif_t* s_sta_netif = NULL;
static wifi_ap_record_extended_t* s_scan_results = NULL;
static uint16_t s_scan_count = 0;
static EventGroupHandle_t s_wifi_event_group;

static char ip_address[16] = {0};

char* get_ip_address()
{
    return ip_address;
}

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
                    snprintf(ip_address, sizeof(ip_address), "%d.%d.%d.%d", IP2STR(&event->ip_info.ip));

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
    ap_config.ap.beacon_interval = 100;  // Default beacon interval for faster detection

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
    
    uint32_t retry_time = 5;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_IP_REQUEST_RETRY_TIME, 
                                           &retry_time, sizeof(retry_time))); // 5 seconds retry
    
    // Set DHCP Option 114 for modern captive portal detection (RFC 8910)
    char captive_portal_uri[] = "http://192.168.4.1/setup";
    esp_err_t dhcp_opt_err = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, 
                                                   ESP_NETIF_CAPTIVEPORTAL_URI, 
                                                   captive_portal_uri, 
                                                   strlen(captive_portal_uri));
    if (dhcp_opt_err == ESP_OK) {
        ESP_LOGI(TAG, "DHCP Option 114 (Captive Portal URI) set successfully");
    } else {
        ESP_LOGW(TAG, "Failed to set DHCP Option 114: %s", esp_err_to_name(dhcp_opt_err));
        ESP_LOGW(TAG, "Fallback to DNS-based captive portal detection");
    }
    

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));


    // Configure DNS server for captive portal
    esp_ip4_addr_t captive_ip;
    esp_netif_str_to_ip4(CAPTIVE_PORTAL_IP, &captive_ip);
    
    esp_netif_dns_info_t dns_info;
    dns_info.ip.u_addr.ip4.addr = captive_ip.addr;
    dns_info.ip.type = ESP_IPADDR_TYPE_V4;

    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);

    dns_server_config_t dns_config = {
        .num_of_entries = 2,
        .item = {
            {.name = "*", .ip = captive_ip},  // Wildcard catch-all
            {.name = "captiveportal.local", .ip = captive_ip}  // Common portal domain
        }
    };
    ESP_ERROR_CHECK(dns_server_start_with_config(&dns_config));

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

static bool is_valid_ssid(const char* ssid) {
    if (!ssid) return false;
    
    size_t len = strlen(ssid);
    if (len == 0 || len > 32) {
        ESP_LOGW(TAG, "SSID length invalid: %zu (must be 1-32)", len);
        return false;
    }
    
    // Check for valid UTF-8 characters
    for (size_t i = 0; i < len; i++) {
        unsigned char c = ssid[i];
        // Allow printable ASCII and common UTF-8 characters
        if (c < 32 || c == 127) {
            ESP_LOGW(TAG, "SSID contains invalid character at position %zu: 0x%02x", i, c);
            return false;
        }
    }
    
    return true;
}

static bool is_valid_password(const char* password) {
    if (!password) return false;
    
    size_t len = strlen(password);
    if (len > 64) {
        ESP_LOGW(TAG, "Password too long: %zu (max 64)", len);
        return false;
    }
    
    // Password can be empty for open networks
    if (len == 0) {
        ESP_LOGD(TAG, "Empty password - assuming open network");
        return true;
    }
    
    // For WPA/WPA2, minimum length is 8
    if (len < 8) {
        ESP_LOGW(TAG, "Password too short for WPA: %zu (min 8)", len);
        return false;
    }
    
    // Check for printable ASCII characters
    for (size_t i = 0; i < len; i++) {
        unsigned char c = password[i];
        if (c < 32 || c == 127) {
            ESP_LOGW(TAG, "Password contains invalid character at position %zu: 0x%02x", i, c);
            return false;
        }
    }
    
    return true;
}

esp_err_t wifi_provisioning_set_credentials(const char* ssid, const char* password)
{
    if (!ssid || !password) {
        ESP_LOGE(TAG, "SSID or password is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!is_valid_ssid(ssid)) {
        ESP_LOGE(TAG, "Invalid SSID format");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!is_valid_password(password)) {
        ESP_LOGE(TAG, "Invalid password format");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Encrypt and store SSID
    std::string encrypted_ssid;
    if (CredentialEncryption::instance().is_initialized()) {
        esp_err_t encrypt_err = CredentialEncryption::instance().encrypt_string(ssid, encrypted_ssid);
        if (encrypt_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to encrypt SSID, storing as plain text");
            encrypted_ssid = ssid;
        } else {
            ESP_LOGI(TAG, "SSID encrypted successfully");
        }
    } else {
        ESP_LOGW(TAG, "Encryption not initialized, storing SSID as plain text");
        encrypted_ssid = ssid;
    }
    
    err = nvs_set_str(nvs_handle, WIFI_SSID_KEY, encrypted_ssid.c_str());
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    // Encrypt and store password
    std::string encrypted_password;
    if (CredentialEncryption::instance().is_initialized()) {
        esp_err_t encrypt_err = CredentialEncryption::instance().encrypt_string(password, encrypted_password);
        if (encrypt_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to encrypt password, storing as plain text");
            encrypted_password = password;
        } else {
            ESP_LOGI(TAG, "Password encrypted successfully");
        }
    } else {
        ESP_LOGW(TAG, "Encryption not initialized, storing password as plain text");
        encrypted_password = password;
    }
    
    err = nvs_set_str(nvs_handle, WIFI_PASSWORD_KEY, encrypted_password.c_str());
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
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit WiFi credentials to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    
    // Add a small delay to ensure NVS operations complete
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "WiFi credentials saved and committed: SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t wifi_provisioning_get_credentials(char* ssid, char* password, size_t ssid_len, size_t password_len)
{
    if (!ssid || !password) {
        ESP_LOGE(TAG, "Invalid parameters: ssid=%p, password=%p", ssid, password);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading WiFi credentials from NVS namespace: %s", WIFI_CREDENTIALS_NAMESPACE);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", WIFI_CREDENTIALS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    // Get encrypted SSID
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, WIFI_SSID_KEY, nullptr, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SSID size from NVS key '%s': %s", WIFI_SSID_KEY, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    ESP_LOGI(TAG, "Found SSID in NVS, size: %zu bytes", required_size);
    
    char* encrypted_ssid_buf = new char[required_size];
    err = nvs_get_str(nvs_handle, WIFI_SSID_KEY, encrypted_ssid_buf, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SSID from NVS: %s", esp_err_to_name(err));
        delete[] encrypted_ssid_buf;
        nvs_close(nvs_handle);
        return err;
    }
    ESP_LOGI(TAG, "Read SSID from NVS: %zu bytes", required_size);
    
    // Decrypt SSID
    std::string decrypted_ssid;
    if (!CredentialEncryption::instance().is_initialized()) {
        ESP_LOGE(TAG, "Encryption not initialized");
        delete[] encrypted_ssid_buf;
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Attempting to decrypt SSID...");
    esp_err_t decrypt_err = CredentialEncryption::instance().decrypt_string(encrypted_ssid_buf, decrypted_ssid);
    if (decrypt_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decrypt SSID: %s", esp_err_to_name(decrypt_err));
        delete[] encrypted_ssid_buf;
        nvs_close(nvs_handle);
        return decrypt_err;
    }
    
    ESP_LOGI(TAG, "SSID decrypted successfully: '%s'", decrypted_ssid.c_str());
    
    if (decrypted_ssid.length() >= ssid_len) {
        ESP_LOGE(TAG, "SSID too long: %zu bytes, buffer size: %zu", decrypted_ssid.length(), ssid_len);
        delete[] encrypted_ssid_buf;
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(ssid, decrypted_ssid.c_str());
    delete[] encrypted_ssid_buf;

    // Get encrypted password
    required_size = 0;
    err = nvs_get_str(nvs_handle, WIFI_PASSWORD_KEY, nullptr, &required_size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    
    char* encrypted_password_buf = new char[required_size];
    err = nvs_get_str(nvs_handle, WIFI_PASSWORD_KEY, encrypted_password_buf, &required_size);
    if (err != ESP_OK) {
        delete[] encrypted_password_buf;
        nvs_close(nvs_handle);
        return err;
    }
    
    // Decrypt password
    std::string decrypted_password;
    esp_err_t password_decrypt_err = CredentialEncryption::instance().decrypt_string(encrypted_password_buf, decrypted_password);
    if (password_decrypt_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decrypt password: %s", esp_err_to_name(password_decrypt_err));
        delete[] encrypted_password_buf;
        nvs_close(nvs_handle);
        return password_decrypt_err;
    }
    
    ESP_LOGI(TAG, "Password decrypted successfully");
    
    if (decrypted_password.length() >= password_len) {
        ESP_LOGE(TAG, "Password too long: %zu bytes, buffer size: %zu", decrypted_password.length(), password_len);
        delete[] encrypted_password_buf;
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    }
    strcpy(password, decrypted_password.c_str());
    
    // Clear sensitive data from memory
    memset(encrypted_password_buf, 0, required_size);
    delete[] encrypted_password_buf;
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

bool wifi_provisioning_is_configured(void)
{
    ESP_LOGI(TAG, "Checking if WiFi is configured...");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "WiFi not configured: failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t configured = 0;
    err = nvs_get_u8(nvs_handle, WIFI_CONFIGURED_KEY, &configured);
    nvs_close(nvs_handle);

    bool is_configured = (err == ESP_OK && configured == 1);
    ESP_LOGI(TAG, "WiFi configured check: %s (configured flag: %d, error: %s)", 
             is_configured ? "YES" : "NO", configured, esp_err_to_name(err));
    
    return is_configured;
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
    ESP_LOGI(TAG, "Attempting to connect to WiFi using stored credentials...");
    
    char ssid[32] = {0};
    char password[64] = {0};
    
    esp_err_t err = wifi_provisioning_get_credentials(ssid, password, sizeof(ssid), sizeof(password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi credentials: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Retrieved credentials for SSID: %s", ssid);

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
    ESP_LOGI(TAG, "Determining if captive portal should start...");
    
    if (!wifi_provisioning_is_configured()) {
        ESP_LOGI(TAG, "No WiFi credentials configured, starting captive portal");
        return true;
    }

    ESP_LOGI(TAG, "WiFi credentials found, attempting to connect...");
    esp_err_t connect_result = wifi_provisioning_try_connect_sta();
    if (connect_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect with stored credentials: %s, starting captive portal", esp_err_to_name(connect_result));
        return true;
    }

    ESP_LOGI(TAG, "Successfully connected with stored credentials, no need for captive portal");
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
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0 || ret >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Basic JSON structure validation
    if (buf[0] != '{' || buf[ret-1] != '}') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON structure");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(json, "password");

    if (!ssid_json || !cJSON_IsString(ssid_json) || 
        !password_json || !cJSON_IsString(password_json)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid SSID/password fields");
        return ESP_FAIL;
    }

    const char* ssid = cJSON_GetStringValue(ssid_json);
    const char* password = cJSON_GetStringValue(password_json);
    
    // Validate credentials before processing
    if (!ssid || !password) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "NULL SSID or password");
        return ESP_FAIL;
    }

    esp_err_t err = wifi_provisioning_set_credentials(ssid, password);
    cJSON_Delete(json);

    if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID or password format");
        return ESP_FAIL;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
        return ESP_FAIL;
    }

    // Give extra time for NVS operations to complete and verify the write
    ESP_LOGI(TAG, "Verifying credentials were saved correctly...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Allow NVS operations to complete
    
    // Verify the credentials were actually saved by reading them back
    char verify_ssid[32] = {0};
    char verify_password[64] = {0};
    esp_err_t verify_err = wifi_provisioning_get_credentials(verify_ssid, verify_password, sizeof(verify_ssid), sizeof(verify_password));
    if (verify_err == ESP_OK && strcmp(verify_ssid, ssid) == 0 && strcmp(verify_password, password) == 0) {
        ESP_LOGI(TAG, "Credentials verified successfully in NVS");
    } else {
        ESP_LOGW(TAG, "Credential verification failed: %s", esp_err_to_name(verify_err));
        // Clear sensitive verification data
        memset(verify_ssid, 0, sizeof(verify_ssid));
        memset(verify_password, 0, sizeof(verify_password));
    }
    
    // Clear sensitive verification data
    memset(verify_ssid, 0, sizeof(verify_ssid));
    memset(verify_password, 0, sizeof(verify_password));

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
    
    // Additional delay to ensure all NVS operations are fully completed
    ESP_LOGI(TAG, "Final synchronization before restart...");
    vTaskDelay(pdMS_TO_TICKS(500));
    
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