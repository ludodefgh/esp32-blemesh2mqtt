#pragma once

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAPTIVE_PORTAL_SSID_PREFIX "ESP32-Setup-"
#define CAPTIVE_PORTAL_PASSWORD ""
#define CAPTIVE_PORTAL_MAX_CONNECTIONS 4
#define CAPTIVE_PORTAL_IP "192.168.4.1"
#define CAPTIVE_PORTAL_CHANNEL 1

#define WIFI_CREDENTIALS_NAMESPACE "wifi_creds"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PASSWORD_KEY "password"
#define WIFI_CONFIGURED_KEY "configured"

typedef enum {
    WIFI_PROV_STATE_IDLE,
    WIFI_PROV_STATE_AP_STARTED,
    WIFI_PROV_STATE_STA_CONNECTING,
    WIFI_PROV_STATE_STA_CONNECTED,
    WIFI_PROV_STATE_STA_FAILED
} wifi_provisioning_state_t;

typedef struct {
    char ssid[32];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_extended_t;

typedef void (*wifi_provisioning_event_cb_t)(wifi_provisioning_state_t state, void* event_data);

esp_err_t wifi_provisioning_init(void);
esp_err_t wifi_provisioning_start_captive_portal(void);
esp_err_t wifi_provisioning_stop_captive_portal(void);
esp_err_t wifi_provisioning_set_credentials(const char* ssid, const char* password);
esp_err_t wifi_provisioning_get_credentials(char* ssid, char* password, size_t ssid_len, size_t password_len);
bool wifi_provisioning_is_configured(void);
esp_err_t wifi_provisioning_clear_credentials(void);
esp_err_t wifi_provisioning_scan_start(void);
esp_err_t wifi_provisioning_get_scan_results(wifi_ap_record_extended_t** ap_records, uint16_t* count);
wifi_provisioning_state_t wifi_provisioning_get_state(void);
esp_err_t wifi_provisioning_set_event_callback(wifi_provisioning_event_cb_t callback);

esp_err_t wifi_provisioning_try_connect_sta(void);
bool wifi_provisioning_should_start_captive_portal(void);

void wifi_provisioning_register_captive_portal_handlers(httpd_handle_t server);

#ifdef __cplusplus
}
#endif