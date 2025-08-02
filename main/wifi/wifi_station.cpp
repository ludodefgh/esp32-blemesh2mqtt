#include "wifi_station.h"
/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <cstring>

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi_provisioning.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;
static char ip_address[16] = {0};
static SemaphoreHandle_t s_wifi_mutex = NULL;

char* get_ip_address()
{
    // Return a copy to prevent race conditions with IP address updates
    static char ip_copy[16];
    if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strncpy(ip_copy, ip_address, sizeof(ip_copy) - 1);
        ip_copy[sizeof(ip_copy) - 1] = '\0';
        xSemaphoreGive(s_wifi_mutex);
        return ip_copy;
    }
    return "0.0.0.0";  // Fallback if mutex unavailable
}


void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        
        // Safely update IP address with mutex protection
        if (s_wifi_mutex && xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            snprintf(ip_address, sizeof(ip_address), "%d.%d.%d.%d", IP2STR(&event->ip_info.ip));
            xSemaphoreGive(s_wifi_mutex);
        }
        
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta_with_stored_credentials(void)
{
    char ssid[32] = {0};
    char password[64] = {0};
    
    if (!wifi_provisioning_is_configured()) {
        ESP_LOGI(TAG, "No stored WiFi credentials found");
        return ESP_ERR_NOT_FOUND;
    }
    
    esp_err_t err = wifi_provisioning_get_credentials(ssid, password, sizeof(ssid), sizeof(password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get stored WiFi credentials: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize synchronization primitives
    if (s_wifi_mutex == NULL) {
        s_wifi_mutex = xSemaphoreCreateMutex();
        if (s_wifi_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create WiFi mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Take mutex to prevent concurrent initialization
    if (xSemaphoreTake(s_wifi_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take WiFi mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            xSemaphoreGive(s_wifi_mutex);
            ESP_LOGE(TAG, "Failed to create event group");
            return ESP_ERR_NO_MEM;
        }
    }

    static bool wifi_initialized = false;
    if (!wifi_initialized) {
        esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
        if (sta_netif == NULL) {
            xSemaphoreGive(s_wifi_mutex);
            ESP_LOGE(TAG, "Failed to create default WiFi STA");
            return ESP_ERR_NO_MEM;
        }
        wifi_initialized = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    static bool event_handlers_registered = false;
    if (!event_handlers_registered) {
        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        
        esp_err_t ret = esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &event_handler,
                                                            NULL,
                                                            &instance_any_id);
        if (ret != ESP_OK) {
            xSemaphoreGive(s_wifi_mutex);
            ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ret = esp_event_handler_instance_register(IP_EVENT,
                                                  IP_EVENT_STA_GOT_IP,
                                                  &event_handler,
                                                  NULL,
                                                  &instance_got_ip);
        if (ret != ESP_OK) {
            xSemaphoreGive(s_wifi_mutex);
            ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
            return ret;
        }
        
        event_handlers_registered = true;
    }

    wifi_config_t wifi_config {};
    strlcpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *) wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
    wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_wifi_mutex);
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_wifi_mutex);
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        xSemaphoreGive(s_wifi_mutex);
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Release mutex after successful initialization
    xSemaphoreGive(s_wifi_mutex);

    ESP_LOGI(TAG, "wifi_init_sta finished, attempting to connect to: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Successfully connected to SSID: %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

void wifi_init_sta(void)
{
    esp_err_t ret = wifi_init_sta_with_stored_credentials();
    if (ret == ESP_OK) {
        return;
    }
    
    ESP_LOGI(TAG, "No stored WiFi credentials found, captive portal required");
}
