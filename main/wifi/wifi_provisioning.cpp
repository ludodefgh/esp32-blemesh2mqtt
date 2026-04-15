// Standard C/C++ libraries
#include <stdio.h>
#include <string.h>

// ESP-IDF core includes
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"

// ESP-IDF component includes
#include "nvs.h"
#include "nvs_flash.h"

// Third-party libraries
#include "cJSON.h"
#include <dhcpserver/dhcpserver.h>

// LWIP includes
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"

// Project includes
#include "common/log_common.h"
#include "dns_server.h"
#include "security/credential_encryption.h"
#include "wifi_provisioning.h"

static const char *TAG = "wifi_provisioning";

static wifi_provisioning_state_t s_provisioning_state = WIFI_PROV_STATE_IDLE;
static wifi_provisioning_event_cb_t s_event_callback = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static wifi_ap_record_extended_t *s_scan_results = NULL;
static uint16_t s_scan_count = 0;
static EventGroupHandle_t s_wifi_event_group;
static bool s_scan_in_progress = false;

// WiFi reconnection management
static TaskHandle_t s_reconnect_task_handle = NULL;
static bool s_normal_operation_mode = false;
static uint32_t s_reconnect_attempt = 0;

// Configuration-based reconnection parameters
#ifdef CONFIG_WIFI_RECONNECT_UNLIMITED_ATTEMPTS
static const bool UNLIMITED_ATTEMPTS = true;
static const uint32_t FIXED_RECONNECT_DELAY_MS = CONFIG_WIFI_RECONNECT_DELAY_MS;
static const uint32_t MAX_RECONNECT_ATTEMPTS = 1; // Not used in unlimited mode
static const uint32_t INITIAL_RECONNECT_DELAY_MS = 1000; // Not used in unlimited mode
static const uint32_t MAX_RECONNECT_DELAY_MS = CONFIG_WIFI_RECONNECT_DELAY_MS;
#else
static const bool UNLIMITED_ATTEMPTS = false;
static const uint32_t FIXED_RECONNECT_DELAY_MS = CONFIG_WIFI_RECONNECT_DELAY_MS; // Not used in limited mode
static const uint32_t MAX_RECONNECT_ATTEMPTS = CONFIG_WIFI_RECONNECT_MAX_ATTEMPTS;
static const uint32_t INITIAL_RECONNECT_DELAY_MS = CONFIG_WIFI_RECONNECT_INITIAL_DELAY_MS;
static const uint32_t MAX_RECONNECT_DELAY_MS = CONFIG_WIFI_RECONNECT_DELAY_MS;
#endif

static char ip_address[16] = {0};

char *get_ip_address()
{
    return ip_address;
}

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static void wifi_reconnect_task(void *pvParameters)
{
    LOG_INFO(TAG, "WiFi reconnect task started - will persist for multiple disconnections");
    if (UNLIMITED_ATTEMPTS)
    {
        LOG_INFO(TAG, "Reconnection mode: UNLIMITED attempts with %d ms fixed delay", FIXED_RECONNECT_DELAY_MS);
    }
    else
    {
        LOG_INFO(TAG, "Reconnection mode: %d max attempts, %d-%d ms exponential backoff", 
                 MAX_RECONNECT_ATTEMPTS, INITIAL_RECONNECT_DELAY_MS, MAX_RECONNECT_DELAY_MS);
    }
    
    while (s_normal_operation_mode)
    {
        // Wait for disconnection notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        if (!s_normal_operation_mode)
        {
            LOG_DEBUG(TAG, "Not in normal operation mode, exiting reconnect task");
            break;
        }
        
        // Reset attempt counter for each new disconnection event
        s_reconnect_attempt = 0;
        
        // Check if WiFi has already reconnected before starting reconnection attempts
        wifi_ap_record_t ap_info;
        esp_err_t initial_wifi_status = esp_wifi_sta_get_ap_info(&ap_info);
        if (initial_wifi_status == ESP_OK)
        {
            LOG_INFO(TAG, "WiFi already reconnected to AP: %s, no reconnection needed", ap_info.ssid);
            s_provisioning_state = WIFI_PROV_STATE_STA_CONNECTED;
            if (s_event_callback)
            {
                s_event_callback(s_provisioning_state, NULL);
            }
            continue; // Skip to next disconnection event
        }
        
        LOG_INFO(TAG, "Starting reconnection attempts for disconnection event");
        
        // Reconnection loop for this disconnection event
        while (s_normal_operation_mode && (UNLIMITED_ATTEMPTS || s_reconnect_attempt < MAX_RECONNECT_ATTEMPTS))
        {
            if (UNLIMITED_ATTEMPTS)
            {
                LOG_INFO(TAG, "WiFi reconnection attempt %d (unlimited mode)", s_reconnect_attempt + 1);
            }
            else
            {
                LOG_INFO(TAG, "WiFi reconnection attempt %d/%d", s_reconnect_attempt + 1, MAX_RECONNECT_ATTEMPTS);
            }
            
            uint32_t delay_ms;
            if (UNLIMITED_ATTEMPTS)
            {
                // Use fixed delay for unlimited attempts
                delay_ms = FIXED_RECONNECT_DELAY_MS;
            }
            else
            {
                // Calculate exponential backoff delay for limited attempts
                delay_ms = INITIAL_RECONNECT_DELAY_MS * (1 << s_reconnect_attempt);
                if (delay_ms > MAX_RECONNECT_DELAY_MS)
                {
                    delay_ms = MAX_RECONNECT_DELAY_MS;
                }
            }
            
            LOG_INFO(TAG, "Waiting %d ms before reconnection attempt", delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            
            if (!s_normal_operation_mode)
            {
                LOG_DEBUG(TAG, "Normal operation mode disabled during delay, stopping reconnect");
                break;
            }
            
            // Check if WiFi is already connected before attempting reconnection
            wifi_ap_record_t ap_info;
            esp_err_t wifi_status = esp_wifi_sta_get_ap_info(&ap_info);
            if (wifi_status == ESP_OK)
            {
                LOG_INFO(TAG, "WiFi already connected to AP: %s, stopping reconnection attempts", ap_info.ssid);
                s_provisioning_state = WIFI_PROV_STATE_STA_CONNECTED;
                if (s_event_callback)
                {
                    s_event_callback(s_provisioning_state, NULL);
                }
                break; // Exit reconnection loop, connection is restored
            }
            
            // Set state to reconnecting
            s_provisioning_state = WIFI_PROV_STATE_STA_RECONNECTING;
            if (s_event_callback)
            {
                s_event_callback(s_provisioning_state, NULL);
            }
            
            // Final check before attempting reconnection
            wifi_ap_record_t final_ap_info;
            esp_err_t final_wifi_status = esp_wifi_sta_get_ap_info(&final_ap_info);
            if (final_wifi_status == ESP_OK)
            {
                LOG_INFO(TAG, "WiFi connected during reconnection attempt, stopping");
                s_provisioning_state = WIFI_PROV_STATE_STA_CONNECTED;
                if (s_event_callback)
                {
                    s_event_callback(s_provisioning_state, NULL);
                }
                break; // Exit reconnection loop
            }
            
            // Attempt to reconnect
            LOG_INFO(TAG, "Attempting WiFi reconnection...");
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK)
            {
                LOG_WARN(TAG, "Failed to initiate WiFi connection: %s", esp_err_to_name(err));
            }
            
            // Wait for connection result
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                   WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                   pdTRUE, pdFALSE,
                                                   pdMS_TO_TICKS(10000));
            
            if (bits & WIFI_CONNECTED_BIT)
            {
                LOG_INFO(TAG, "WiFi reconnected successfully - task continues running for future disconnections");
                s_reconnect_attempt = 0; // Reset attempt counter
                break; // Exit reconnection loop, but keep task running
            }
            else
            {
                LOG_WARN(TAG, "WiFi reconnection failed");
                s_reconnect_attempt++;
            }
        }
        
        // Check if we exhausted all attempts for this disconnection (only applies to limited attempts mode)
        if (!UNLIMITED_ATTEMPTS && s_reconnect_attempt >= MAX_RECONNECT_ATTEMPTS)
        {
            LOG_ERROR(TAG, "Max reconnection attempts (%d) reached for this disconnection event", MAX_RECONNECT_ATTEMPTS);
            s_provisioning_state = WIFI_PROV_STATE_STA_FAILED;
            if (s_event_callback)
            {
                s_event_callback(s_provisioning_state, NULL);
            }
            // Continue running the task for future disconnection events
        }
    }
    
    LOG_INFO(TAG, "WiFi reconnect task ending");
    s_reconnect_task_handle = NULL;
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_AP_START:
            LOG_INFO(TAG, "WiFi AP started");
            s_provisioning_state = WIFI_PROV_STATE_AP_STARTED;
            if (s_event_callback)
            {
                s_event_callback(s_provisioning_state, NULL);
            }
            break;

        case WIFI_EVENT_AP_STOP:
            LOG_INFO(TAG, "WiFi AP stopped");
            break;

        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            LOG_INFO(TAG, "Station connected: MAC=" MACSTR ", waiting for DHCP IP assignment", MAC2STR(event->mac));
        }
        break;

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            LOG_INFO(TAG, "Station disconnected: MAC=" MACSTR, MAC2STR(event->mac));
        }
        break;

        case WIFI_EVENT_STA_START:
            LOG_INFO(TAG, "WiFi STA started");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            LOG_INFO(TAG, "WiFi STA connected");
            s_provisioning_state = WIFI_PROV_STATE_STA_CONNECTING;
            if (s_event_callback)
            {
                s_event_callback(s_provisioning_state, event_data);
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            LOG_INFO(TAG, "WiFi STA disconnected");
            
            if (s_normal_operation_mode)
            {
                LOG_INFO(TAG, "In normal operation mode, triggering automatic reconnection");
                // Trigger reconnection task if it exists and we have credentials
                if (s_reconnect_task_handle != NULL && wifi_provisioning_is_configured())
                {
                    s_provisioning_state = WIFI_PROV_STATE_STA_RECONNECTING;
                    xTaskNotifyGive(s_reconnect_task_handle);
                }
                else
                {
                    LOG_WARN(TAG, "Cannot reconnect: task=%p, configured=%d", 
                             s_reconnect_task_handle, wifi_provisioning_is_configured());
                    s_provisioning_state = WIFI_PROV_STATE_STA_FAILED;
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
            }
            else
            {
                LOG_INFO(TAG, "In provisioning mode, not attempting reconnection");
                s_provisioning_state = WIFI_PROV_STATE_STA_FAILED;
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            
            if (s_event_callback)
            {
                s_event_callback(s_provisioning_state, event_data);
            }
            break;

        case WIFI_EVENT_SCAN_DONE:
        {
            // Ignore duplicate SCAN_DONE events (can happen in APSTA mode)
            if (!s_scan_in_progress)
            {
                LOG_DEBUG(TAG, "Ignoring duplicate WIFI_EVENT_SCAN_DONE");
                break;
            }

            LOG_INFO(TAG, "WiFi scan completed");
            s_scan_in_progress = false;

            // Process scan results asynchronously
            uint16_t max_aps = 20;
            wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * max_aps);
            if (ap_records)
            {
                esp_err_t err = esp_wifi_scan_get_ap_records(&max_aps, ap_records);
                if (err == ESP_OK)
                {
                    // Free previous results
                    if (s_scan_results)
                    {
                        free(s_scan_results);
                    }

                    s_scan_results = (wifi_ap_record_extended_t *)malloc(sizeof(wifi_ap_record_extended_t) * max_aps);
                    if (s_scan_results)
                    {
                        s_scan_count = max_aps;
                        for (int i = 0; i < max_aps; i++)
                        {
                            strlcpy(s_scan_results[i].ssid, (char *)ap_records[i].ssid, sizeof(s_scan_results[i].ssid));
                            s_scan_results[i].rssi = ap_records[i].rssi;
                            s_scan_results[i].authmode = ap_records[i].authmode;
                        }
                        LOG_INFO(TAG, "Processed %d WiFi networks from scan", s_scan_count);
                    }
                    else
                    {
                        LOG_ERROR(TAG, "Failed to allocate memory for scan results");
                        s_scan_count = 0;
                    }
                }
                else
                {
                    LOG_ERROR(TAG, "Failed to get scan results: %s", esp_err_to_name(err));
                }
                free(ap_records);
            }
            else
            {
                LOG_ERROR(TAG, "Failed to allocate memory for AP records");
            }
        }
        break;

        default:
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            LOG_INFO(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
            snprintf(ip_address, sizeof(ip_address), "%d.%d.%d.%d", IP2STR(&event->ip_info.ip));

            s_provisioning_state = WIFI_PROV_STATE_STA_CONNECTED;
            s_reconnect_attempt = 0; // Reset reconnection counter on successful connection
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            if (s_event_callback)
            {
                s_event_callback(s_provisioning_state, event_data);
            }
        }
        break;

        case IP_EVENT_AP_STAIPASSIGNED:
        {
            ip_event_ap_staipassigned_t *event = (ip_event_ap_staipassigned_t *)event_data;
            LOG_INFO(TAG, "DHCP assigned IP " IPSTR " to station MAC=" MACSTR,
                     IP2STR(&event->ip), MAC2STR(event->mac));

            // Now it's safe to switch to APSTA mode - client has IP and can use WiFi scanning
            wifi_mode_t current_mode;
            esp_err_t err = esp_wifi_get_mode(&current_mode);
            if (err == ESP_OK && current_mode == WIFI_MODE_AP)
            {
                LOG_INFO(TAG, "Switching to APSTA mode now that client has IP address");
                err = esp_wifi_set_mode(WIFI_MODE_APSTA);
                if (err != ESP_OK)
                {
                    LOG_WARN(TAG, "Failed to switch to APSTA mode: %s", esp_err_to_name(err));
                }
                else
                {
                    LOG_INFO(TAG, "Successfully switched to APSTA mode - WiFi scanning available on demand");
                    // Note: WiFi scanning will only be performed when explicitly requested
                    // to avoid interference with client connections
                }
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
    if (s_wifi_event_group == NULL)
    {
        LOG_ERROR(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    LOG_INFO(TAG, "WiFi provisioning initialized");
    return ESP_OK;
}

esp_err_t wifi_provisioning_start_captive_portal(void)
{
    if (s_provisioning_state == WIFI_PROV_STATE_AP_STARTED)
    {
        LOG_WARN(TAG, "Captive portal already started");
        return ESP_OK;
    }

    static bool event_handlers_registered = false;
    if (!event_handlers_registered)
    {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, NULL));
        event_handlers_registered = true;
    }

    uint8_t base_mac[6];
    esp_read_mac(base_mac, ESP_MAC_WIFI_STA);

    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X%02X",
             CAPTIVE_PORTAL_SSID_PREFIX, base_mac[3], base_mac[4], base_mac[5]);

    wifi_config_t ap_config = {};
    strlcpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(ap_ssid);
    strlcpy((char *)ap_config.ap.password, CAPTIVE_PORTAL_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.channel = CAPTIVE_PORTAL_CHANNEL;
    ap_config.ap.max_connection = CAPTIVE_PORTAL_MAX_CONNECTIONS;
    ap_config.ap.authmode = strlen(CAPTIVE_PORTAL_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ap_config.ap.beacon_interval = 100; // Default beacon interval for faster detection

    // -----------------------------------------------------------------------
    // Initialisation order matters: netif BEFORE esp_wifi_init().
    // The WiFi driver attaches to the netif packet-input function during init;
    // if the netif doesn't exist yet, DHCP DISCOVER frames from stations never
    // reach lwIP. This mirrors the official ESP-IDF captive portal example.
    // -----------------------------------------------------------------------
    if (s_ap_netif != NULL)
    {
        LOG_INFO(TAG, "Destroying existing AP netif to clear DHCP leases...");
        esp_netif_dhcps_stop(s_ap_netif);
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    s_ap_netif = esp_netif_create_default_wifi_ap(); // MUST come before esp_wifi_init()

    static bool wifi_initialized = false;
    if (!wifi_initialized)
    {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        wifi_initialized = true;
    }

    // No DHCP/IP manipulation before esp_wifi_start(). Let the netif
    // auto-start DHCP via WIFI_EVENT_AP_START → esp_netif_action_start().
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    // Force HT20: without this the C3 advertises HT40, causing phones to
    // fail auth/assoc (rm mis) on the first connection attempt.
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
    ESP_ERROR_CHECK(esp_wifi_start());

    // -----------------------------------------------------------------------
    // DHCP option 114 — set AFTER esp_wifi_start(), same as the example's
    // dhcp_set_captiveportal_url(). Use non-fatal checks: stop may fail if
    // auto-start hasn't fired yet; start may fail if it already ran. Both
    // outcomes leave the server running with the option correctly applied.
    // -----------------------------------------------------------------------
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ap_ip_info;
    esp_netif_get_ip_info(ap_netif, &ap_ip_info);
    char captive_portal_uri[32];
    snprintf(captive_portal_uri, sizeof(captive_portal_uri),
             "http://" IPSTR, IP2STR(&ap_ip_info.ip));

    esp_netif_dhcps_stop(ap_netif); // non-fatal
    esp_err_t dhcp_opt_err = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                                                     ESP_NETIF_CAPTIVEPORTAL_URI,
                                                     captive_portal_uri,
                                                     strlen(captive_portal_uri));
    if (dhcp_opt_err != ESP_OK)
    {
        LOG_WARN(TAG, "DHCP option 114 failed: %s (DNS detection still active)",
                 esp_err_to_name(dhcp_opt_err));
    }
    else
    {
        LOG_INFO(TAG, "DHCP option 114 set: %s", captive_portal_uri);
    }
    esp_netif_dhcps_start(ap_netif); // non-fatal

    LOG_INFO(TAG, "Captive portal started: SSID=%s IP=" IPSTR,
             ap_ssid, IP2STR(&ap_ip_info.ip));

    esp_ip4_addr_t captive_ip;
    esp_netif_str_to_ip4(CAPTIVE_PORTAL_IP, &captive_ip);
    dns_server_config_t dns_config = {
        .num_of_entries = 2,
        .item = {
            {.name = "*", .ip = captive_ip},
            {.name = "captiveportal.local", .ip = captive_ip}
        }};
    esp_err_t dns_start_err = dns_server_start_with_config(&dns_config);
    if (dns_start_err != ESP_OK)
    {
        LOG_WARN(TAG, "DNS server failed to start: %s", esp_err_to_name(dns_start_err));
    }
    else
    {
        LOG_INFO(TAG, "DNS server started");
    }

    return ESP_OK;
}

esp_err_t wifi_provisioning_stop_captive_portal(void)
{
    if (s_provisioning_state != WIFI_PROV_STATE_AP_STARTED)
    {
        LOG_WARN(TAG, "Captive portal not running");
        return ESP_OK;
    }

    LOG_INFO(TAG, "Stopping captive portal...");

    // Stop DHCP server first
    LOG_INFO(TAG, "Stopping DHCP server...");
    esp_err_t dhcp_stop_err = esp_netif_dhcps_stop(s_ap_netif);
    if (dhcp_stop_err != ESP_OK && dhcp_stop_err != ESP_ERR_ESP_NETIF_DHCP_NOT_STOPPED)
    {
        LOG_WARN(TAG, "DHCP server stop failed: %s", esp_err_to_name(dhcp_stop_err));
    }
    else
    {
        LOG_INFO(TAG, "DHCP server stopped");
    }

    // Stop DNS server
    dns_server_stop();

    // Give some time for any ongoing operations to complete
    vTaskDelay(pdMS_TO_TICKS(500));

    // Stop WiFi to ensure clean state
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK)
    {
        LOG_WARN(TAG, "WiFi stop failed: %s", esp_err_to_name(err));
    }

    // Set to STA mode for next boot
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        LOG_WARN(TAG, "WiFi mode set failed: %s", esp_err_to_name(err));
    }

    // Destroy the AP netif to ensure clean state on next captive portal start
    if (s_ap_netif != NULL)
    {
        LOG_INFO(TAG, "Destroying AP netif to clear all DHCP lease data...");
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }

    s_provisioning_state = WIFI_PROV_STATE_IDLE;

    LOG_INFO(TAG, "Captive portal stopped");
    return ESP_OK;
}

static bool is_valid_ssid(const char *ssid)
{
    if (!ssid)
        return false;

    size_t len = strlen(ssid);
    if (len == 0 || len > 32)
    {
        LOG_WARN(TAG, "SSID length invalid: %zu (must be 1-32)", len);
        return false;
    }

    // Check for valid UTF-8 characters
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = ssid[i];
        // Allow printable ASCII and common UTF-8 characters
        if (c < 32 || c == 127)
        {
            LOG_WARN(TAG, "SSID contains invalid character at position %zu: 0x%02x", i, c);
            return false;
        }
    }

    return true;
}

static bool is_valid_password(const char *password)
{
    if (!password)
        return false;

    size_t len = strlen(password);
    if (len > 64)
    {
        LOG_WARN(TAG, "Password too long: %zu (max 64)", len);
        return false;
    }

    // Password can be empty for open networks
    if (len == 0)
    {
        LOG_DEBUG(TAG, "Empty password - assuming open network");
        return true;
    }

    // For WPA/WPA2, minimum length is 8
    if (len < 8)
    {
        LOG_WARN(TAG, "Password too short for WPA: %zu (min 8)", len);
        return false;
    }

    // Check for printable ASCII characters
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = password[i];
        if (c < 32 || c == 127)
        {
            LOG_WARN(TAG, "Password contains invalid character at position %zu: 0x%02x", i, c);
            return false;
        }
    }

    return true;
}

esp_err_t wifi_provisioning_set_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password)
    {
        LOG_ERROR(TAG, "SSID or password is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!is_valid_ssid(ssid))
    {
        LOG_ERROR(TAG, "Invalid SSID format");
        return ESP_ERR_INVALID_ARG;
    }

    if (!is_valid_password(password))
    {
        LOG_ERROR(TAG, "Invalid password format");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Encrypt and store SSID
    std::string encrypted_ssid;
    if (credential_encryption::instance().is_initialized())
    {
        esp_err_t encrypt_err = credential_encryption::instance().encrypt_string(ssid, encrypted_ssid);
        if (encrypt_err != ESP_OK)
        {
            LOG_WARN(TAG, "Failed to encrypt SSID, storing as plain text");
            encrypted_ssid = ssid;
        }
        else
        {
            LOG_INFO(TAG, "SSID encrypted successfully");
        }
    }
    else
    {
        LOG_WARN(TAG, "Encryption not initialized, storing SSID as plain text");
        encrypted_ssid = ssid;
    }

    err = nvs_set_str(nvs_handle, WIFI_SSID_KEY, encrypted_ssid.c_str());
    if (err != ESP_OK)
    {
        nvs_close(nvs_handle);
        return err;
    }

    // Encrypt and store password
    std::string encrypted_password;
    if (credential_encryption::instance().is_initialized())
    {
        esp_err_t encrypt_err = credential_encryption::instance().encrypt_string(password, encrypted_password);
        if (encrypt_err != ESP_OK)
        {
            LOG_WARN(TAG, "Failed to encrypt password, storing as plain text");
            encrypted_password = password;
        }
        else
        {
            LOG_INFO(TAG, "Password encrypted successfully");
        }
    }
    else
    {
        LOG_WARN(TAG, "Encryption not initialized, storing password as plain text");
        encrypted_password = password;
    }

    err = nvs_set_str(nvs_handle, WIFI_PASSWORD_KEY, encrypted_password.c_str());
    if (err != ESP_OK)
    {
        nvs_close(nvs_handle);
        return err;
    }

    uint8_t configured = 1;
    err = nvs_set_u8(nvs_handle, WIFI_CONFIGURED_KEY, configured);
    if (err != ESP_OK)
    {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to commit WiFi credentials to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    // Add a small delay to ensure NVS operations complete
    vTaskDelay(pdMS_TO_TICKS(100));

    LOG_INFO(TAG, "WiFi credentials saved and committed: SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t wifi_provisioning_get_credentials(char *ssid, char *password, size_t ssid_len, size_t password_len)
{
    if (!ssid || !password)
    {
        LOG_ERROR(TAG, "Invalid parameters: ssid=%p, password=%p", ssid, password);
        return ESP_ERR_INVALID_ARG;
    }

    LOG_INFO(TAG, "Loading WiFi credentials from NVS namespace: %s", WIFI_CREDENTIALS_NAMESPACE);

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to open NVS namespace '%s': %s", WIFI_CREDENTIALS_NAMESPACE, esp_err_to_name(err));
        return err;
    }

    // Get encrypted SSID
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, WIFI_SSID_KEY, nullptr, &required_size);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to get SSID size from NVS key '%s': %s", WIFI_SSID_KEY, esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    LOG_INFO(TAG, "Found SSID in NVS, size: %zu bytes", required_size);

    char *encrypted_ssid_buf = new char[required_size];
    err = nvs_get_str(nvs_handle, WIFI_SSID_KEY, encrypted_ssid_buf, &required_size);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to read SSID from NVS: %s", esp_err_to_name(err));
        delete[] encrypted_ssid_buf;
        nvs_close(nvs_handle);
        return err;
    }
    LOG_INFO(TAG, "Read SSID from NVS: %zu bytes", required_size);

    // Decrypt SSID
    std::string decrypted_ssid;
    if (!credential_encryption::instance().is_initialized())
    {
        LOG_ERROR(TAG, "Encryption not initialized");
        delete[] encrypted_ssid_buf;
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_STATE;
    }

    LOG_INFO(TAG, "Attempting to decrypt SSID...");
    esp_err_t decrypt_err = credential_encryption::instance().decrypt_string(encrypted_ssid_buf, decrypted_ssid);
    if (decrypt_err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to decrypt SSID: %s", esp_err_to_name(decrypt_err));
        delete[] encrypted_ssid_buf;
        nvs_close(nvs_handle);
        return decrypt_err;
    }

    LOG_INFO(TAG, "SSID decrypted successfully: '%s'", decrypted_ssid.c_str());

    if (decrypted_ssid.length() >= ssid_len)
    {
        LOG_ERROR(TAG, "SSID too long: %zu bytes, buffer size: %zu", decrypted_ssid.length(), ssid_len);
        delete[] encrypted_ssid_buf;
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    }
    strlcpy(ssid, decrypted_ssid.c_str(), ssid_len);
    delete[] encrypted_ssid_buf;

    // Get encrypted password
    required_size = 0;
    err = nvs_get_str(nvs_handle, WIFI_PASSWORD_KEY, nullptr, &required_size);
    if (err != ESP_OK)
    {
        nvs_close(nvs_handle);
        return err;
    }

    char *encrypted_password_buf = new char[required_size];
    err = nvs_get_str(nvs_handle, WIFI_PASSWORD_KEY, encrypted_password_buf, &required_size);
    if (err != ESP_OK)
    {
        delete[] encrypted_password_buf;
        nvs_close(nvs_handle);
        return err;
    }

    // Decrypt password
    std::string decrypted_password;
    esp_err_t password_decrypt_err = credential_encryption::instance().decrypt_string(encrypted_password_buf, decrypted_password);
    if (password_decrypt_err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to decrypt password: %s", esp_err_to_name(password_decrypt_err));
        delete[] encrypted_password_buf;
        nvs_close(nvs_handle);
        return password_decrypt_err;
    }

    LOG_INFO(TAG, "Password decrypted successfully");

    if (decrypted_password.length() >= password_len)
    {
        LOG_ERROR(TAG, "Password too long: %zu bytes, buffer size: %zu", decrypted_password.length(), password_len);
        delete[] encrypted_password_buf;
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_SIZE;
    }
    strlcpy(password, decrypted_password.c_str(), password_len);

    // Clear sensitive data from memory
    memset(encrypted_password_buf, 0, required_size);
    delete[] encrypted_password_buf;

    nvs_close(nvs_handle);
    return ESP_OK;
}

bool wifi_provisioning_is_configured(void)
{
    LOG_INFO(TAG, "Checking if WiFi is configured...");

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        LOG_INFO(TAG, "WiFi not configured: failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t configured = 0;
    err = nvs_get_u8(nvs_handle, WIFI_CONFIGURED_KEY, &configured);
    nvs_close(nvs_handle);

    bool is_configured = (err == ESP_OK && configured == 1);
    LOG_INFO(TAG, "WiFi configured check: %s (configured flag: %d, error: %s)",
             is_configured ? "YES" : "NO", configured, esp_err_to_name(err));

    return is_configured;
}

esp_err_t wifi_provisioning_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        return err;
    }

    nvs_erase_key(nvs_handle, WIFI_SSID_KEY);
    nvs_erase_key(nvs_handle, WIFI_PASSWORD_KEY);
    nvs_erase_key(nvs_handle, WIFI_CONFIGURED_KEY);

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    LOG_INFO(TAG, "WiFi credentials cleared");
    return err;
}

esp_err_t wifi_provisioning_scan_start(void)
{
    static uint32_t last_scan_time = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Only scan if more than 30 seconds have passed since last scan (increased to reduce interference)
    if (s_scan_results && (current_time - last_scan_time) < 30000)
    {
        LOG_INFO(TAG, "Using cached scan results (%d networks) - last scan was %d ms ago",
                 s_scan_count, (int)(current_time - last_scan_time));
        return ESP_OK;
    }

    // Check if scan is already in progress
    if (s_scan_in_progress)
    {
        LOG_DEBUG(TAG, "WiFi scan already in progress");
        return ESP_OK;
    }

    // Check if we're in APSTA mode for scanning
    wifi_mode_t current_mode;
    esp_err_t err = esp_wifi_get_mode(&current_mode);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        return err;
    }

    if (current_mode != WIFI_MODE_APSTA)
    {
        LOG_WARN(TAG, "WiFi scan requested but not in APSTA mode (mode=%d). Scan may not work.", current_mode);
        LOG_WARN(TAG, "This usually means client hasn't connected yet to trigger mode switch");
        // Don't switch modes during active connections as it can disrupt the AP
        return ESP_ERR_INVALID_STATE;
    }
    else
    {
        LOG_INFO(TAG, "WiFi scanning in APSTA mode");
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 30, // Very short minimum scan time
                .max = 80  // Reduced maximum scan time to minimize client disconnections
            }}};

    // Delay to ensure AP operations are stable and no client activity is ongoing
    vTaskDelay(pdMS_TO_TICKS(500));

    // Start non-blocking scan with minimal disruption
    LOG_INFO(TAG, "Starting gentle non-blocking WiFi scan...");
    s_scan_in_progress = true;
    err = esp_wifi_scan_start(&scan_config, false); // false = non-blocking
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to start WiFi scan: %s", esp_err_to_name(err));
        s_scan_in_progress = false;
        return err;
    }

    last_scan_time = current_time;
    LOG_INFO(TAG, "Non-blocking WiFi scan started successfully");
    return ESP_OK;
}

esp_err_t wifi_provisioning_get_scan_results(wifi_ap_record_extended_t **ap_records, uint16_t *count)
{
    if (!ap_records || !count)
    {
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
    LOG_INFO(TAG, "Attempting to connect to WiFi using stored credentials...");

    char ssid[32] = {0};
    char password[64] = {0};

    esp_err_t err = wifi_provisioning_get_credentials(ssid, password, sizeof(ssid), sizeof(password));
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to get WiFi credentials: %s", esp_err_to_name(err));
        return err;
    }

    LOG_INFO(TAG, "Retrieved credentials for SSID: %s", ssid);

    // Initialize WiFi if not already done
    static bool wifi_initialized = false;
    if (!wifi_initialized)
    {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        wifi_initialized = true;
    }

    // Register event handlers if not already done
    static bool event_handlers_registered = false;
    if (!event_handlers_registered)
    {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, NULL));
        event_handlers_registered = true;
    }

    if (s_sta_netif == NULL)
    {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t sta_config = {};
    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // esp_wifi_start() automatically triggers connection in STA mode
    // No need to call esp_wifi_connect() explicitly

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(120000));

    if (bits & WIFI_CONNECTED_BIT)
    {
        LOG_INFO(TAG, "Connected to WiFi network: %s", ssid);
        return ESP_OK;
    }
    else
    {
        LOG_ERROR(TAG, "Failed to connect to WiFi network: %s", ssid);
        return ESP_FAIL;
    }
}

bool wifi_provisioning_should_start_captive_portal(void)
{
    LOG_INFO(TAG, "Determining if captive portal should start...");

    if (!wifi_provisioning_is_configured())
    {
        LOG_INFO(TAG, "No WiFi credentials configured, starting captive portal");
        return true;
    }

    LOG_INFO(TAG, "WiFi credentials found, attempting to connect...");
    esp_err_t connect_result = wifi_provisioning_try_connect_sta();
    if (connect_result != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to connect with stored credentials: %s, starting captive portal", esp_err_to_name(connect_result));
        return true;
    }

    LOG_INFO(TAG, "Successfully connected with stored credentials, no need for captive portal");
    
    // Start reconnect task for automatic reconnection if connection is lost
    esp_err_t reconnect_err = wifi_provisioning_start_reconnect_task();
    if (reconnect_err != ESP_OK)
    {
        LOG_WARN(TAG, "Failed to start WiFi reconnect task: %s", esp_err_to_name(reconnect_err));
    }
    else
    {
        LOG_INFO(TAG, "WiFi reconnect task started for automatic reconnection");
    }
    
    return false;
}

esp_err_t wifi_provisioning_start_reconnect_task(void)
{
    if (s_reconnect_task_handle != NULL)
    {
        LOG_WARN(TAG, "Reconnect task already running");
        return ESP_OK;
    }
    
    s_normal_operation_mode = true;
    s_reconnect_attempt = 0;
    
    BaseType_t task_created = xTaskCreate(
        wifi_reconnect_task,
        "wifi_reconnect",
        4096,
        NULL,
        5,
        &s_reconnect_task_handle
    );
    
    if (task_created != pdPASS)
    {
        LOG_ERROR(TAG, "Failed to create WiFi reconnect task");
        s_normal_operation_mode = false;
        return ESP_FAIL;
    }
    
    LOG_INFO(TAG, "WiFi reconnect task started successfully");
    return ESP_OK;
}

void wifi_provisioning_stop_reconnect_task(void)
{
    s_normal_operation_mode = false;
    
    if (s_reconnect_task_handle != NULL)
    {
        // Wake up the task so it can exit cleanly
        xTaskNotifyGive(s_reconnect_task_handle);
        
        // Wait a bit for the task to exit
        for (int i = 0; i < 10 && s_reconnect_task_handle != NULL; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        // Force delete if it didn't exit cleanly
        if (s_reconnect_task_handle != NULL)
        {
            LOG_WARN(TAG, "Force deleting WiFi reconnect task");
            vTaskDelete(s_reconnect_task_handle);
            s_reconnect_task_handle = NULL;
        }
    }
    
    s_reconnect_attempt = 0;
    LOG_INFO(TAG, "WiFi reconnect task stopped");
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
    LOG_INFO(TAG, "Android captive portal detection: %s", req->uri);

    // Handle specific Android connectivity checks differently
    if (strstr(req->uri, "/generate_204") || strstr(req->uri, "/gen_204"))
    {
        // Android expects 204 (No Content) for successful connectivity
        // Return different status to trigger captive portal
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // For other Android endpoints, return modified content
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    httpd_resp_set_hdr(req, "Content-Type", "text/html");

    // Send modified HTML that signals captive portal
    const char *response = "<html><head><title>Captive Portal</title></head>"
                           "<body><script>window.location.href='http://192.168.4.1/setup';</script>"
                           "<h1>WiFi Setup Required</h1>"
                           "<p><a href='http://192.168.4.1/setup'>Click here to configure WiFi</a></p>"
                           "</body></html>";

    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

static esp_err_t captive_ios_handler(httpd_req_t *req)
{
    LOG_INFO(TAG, "iOS captive portal detection: %s", req->uri);
    
    // Handle different iOS connectivity check endpoints specifically
    if (strstr(req->uri, "/hotspot-detect.html"))
    {
        // iOS hotspot-detect.html expects specific HTML content
        // Return modified content to trigger captive portal
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Content-Type", "text/html");
        
        // iOS expects different content than the default "Success"
        const char *response = "<HTML><HEAD><TITLE>WiFi Setup Required</TITLE></HEAD>"
                               "<BODY><H1>WiFi Configuration Needed</H1>"
                               "<P>To access the internet, configure WiFi settings.</P>"
                               "<P><A HREF='http://192.168.4.1/setup'>Configure WiFi</A></P>"
                               "</BODY></HTML>";
        
        httpd_resp_sendstr(req, response);
        return ESP_OK;
    }
    
    // For other iOS endpoints, use redirect
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
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
    // Check if scan is in progress
    if (s_scan_in_progress)
    {
        // Return a "scan in progress" response
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "status", "scanning");
        cJSON_AddStringToObject(response, "message", "WiFi scan in progress, try again in a moment");

        char *json_string = cJSON_Print(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_string);

        free(json_string);
        cJSON_Delete(response);
        return ESP_OK;
    }

    esp_err_t err = wifi_provisioning_scan_start();
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to start WiFi scan: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    // For non-blocking scan, we need to wait a bit or return cached results
    // If scan just started, return cached results (if any) or empty array
    wifi_ap_record_extended_t *ap_records;
    uint16_t count;
    wifi_provisioning_get_scan_results(&ap_records, &count);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < count; i++)
    {
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
    if (ret <= 0 || ret >= sizeof(buf))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Basic JSON structure validation
    if (buf[0] != '{' || buf[ret - 1] != '}')
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON structure");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(buf);
    if (!json)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON format");
        return ESP_FAIL;
    }

    cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(json, "password");

    if (!ssid_json || !cJSON_IsString(ssid_json) ||
        !password_json || !cJSON_IsString(password_json))
    {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid SSID/password fields");
        return ESP_FAIL;
    }

    const char *ssid = cJSON_GetStringValue(ssid_json);
    const char *password = cJSON_GetStringValue(password_json);

    // Validate credentials before processing
    if (!ssid || !password)
    {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "NULL SSID or password");
        return ESP_FAIL;
    }

    esp_err_t err = wifi_provisioning_set_credentials(ssid, password);
    cJSON_Delete(json);

    if (err == ESP_ERR_INVALID_ARG)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID or password format");
        return ESP_FAIL;
    }
    else if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
        return ESP_FAIL;
    }

    // Give extra time for NVS operations to complete and verify the write
    LOG_INFO(TAG, "Verifying credentials were saved correctly...");
    vTaskDelay(pdMS_TO_TICKS(500)); // Allow NVS operations to complete

    // Verify the credentials were actually saved by reading them back
    char verify_ssid[32] = {0};
    char verify_password[64] = {0};
    esp_err_t verify_err = wifi_provisioning_get_credentials(verify_ssid, verify_password, sizeof(verify_ssid), sizeof(verify_password));
    if (verify_err == ESP_OK && strcmp(verify_ssid, ssid) == 0 && strcmp(verify_password, password) == 0)
    {
        LOG_INFO(TAG, "Credentials verified successfully in NVS");
    }
    else
    {
        LOG_WARN(TAG, "Credential verification failed: %s", esp_err_to_name(verify_err));
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
    LOG_INFO(TAG, "Final synchronization before restart...");
    vTaskDelay(pdMS_TO_TICKS(500));

    LOG_INFO(TAG, "Restarting ESP32 to connect with new credentials...");
    esp_restart();

    return ESP_OK;
}

static esp_err_t wifi_status_handler(httpd_req_t *req)
{
    cJSON *response = cJSON_CreateObject();

    switch (s_provisioning_state)
    {
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
    case WIFI_PROV_STATE_STA_RECONNECTING:
        cJSON_AddStringToObject(response, "status", "reconnecting");
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

namespace
{
constexpr httpd_uri_t captive_uris[] = {
        // Android captive portal detection (modern versions)
        {.uri = "/generate_204", .method = HTTP_GET, .handler = captive_android_handler},
        {.uri = "/gen_204", .method = HTTP_GET, .handler = captive_android_handler},
        {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_android_handler},
        {.uri = "/connectivity-check.html", .method = HTTP_GET, .handler = captive_android_handler},
        {.uri = "/connectivitycheck.gstatic.com/generate_204", .method = HTTP_GET, .handler = captive_android_handler},

        // iOS captive portal detection
        {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_ios_handler},
        {.uri = "/library/test/success.html", .method = HTTP_GET, .handler = captive_ios_handler},
        {.uri = "/captive.apple.com/hotspot-detect.html", .method = HTTP_GET, .handler = captive_ios_handler},

        // Windows captive portal detection
        {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_windows_handler},
        {.uri = "/redirect", .method = HTTP_GET, .handler = captive_redirect_handler},
        {.uri = "/msftconnecttest.com/connecttest.txt", .method = HTTP_GET, .handler = captive_windows_handler},
        {.uri = "/msftncsi.com/ncsi.txt", .method = HTTP_GET, .handler = captive_windows_handler},

        // Additional modern connectivity checks
        {.uri = "/mobile/status.php", .method = HTTP_GET, .handler = captive_redirect_handler},
        {.uri = "/canonical.html", .method = HTTP_GET, .handler = captive_redirect_handler},
        {.uri = "/success.txt", .method = HTTP_GET, .handler = captive_redirect_handler},
        {.uri = "/kindle-wifi/wifiredirect.html", .method = HTTP_GET, .handler = captive_redirect_handler},
        {.uri = "/kindle-wifi/wifistub.html", .method = HTTP_GET, .handler = captive_redirect_handler},

        // API endpoints
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_handler},
        {.uri = "/api/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler},
        {.uri = "/api/wifi/status", .method = HTTP_GET, .handler = wifi_status_handler}};
}
void wifi_provisioning_register_captive_portal_handlers(httpd_handle_t server)
{
    for (int i = 0; i < sizeof(captive_uris) / sizeof(captive_uris[0]); i++)
    {
        httpd_register_uri_handler(server, &captive_uris[i]);
    }
}