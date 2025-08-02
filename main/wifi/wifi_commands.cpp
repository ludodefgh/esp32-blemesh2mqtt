#include "wifi_commands.h"
#include "wifi_provisioning.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "debug/console_cmd.h"
#include "debug/debug_commands_registry.h"

static const char* TAG = "wifi_commands";

static int wifi_clear_cmd(int argc, char **argv)
{
    ESP_LOGI(TAG, "Clearing WiFi credentials...");
    esp_err_t err = wifi_provisioning_clear_credentials();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials cleared successfully");
        ESP_LOGI(TAG, "Restarting device to enter captive portal mode...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to clear WiFi credentials: %s", esp_err_to_name(err));
    }
    return 0;
}

static int wifi_status_cmd(int argc, char **argv)
{
    if (wifi_provisioning_is_configured()) {
        char ssid[32] = {0};
        char password[64] = {0};
        esp_err_t err = wifi_provisioning_get_credentials(ssid, password, sizeof(ssid), sizeof(password));
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi configured - SSID: %s", ssid);
        } else {
            ESP_LOGI(TAG, "WiFi configured but failed to read credentials");
        }
    } else {
        ESP_LOGI(TAG, "WiFi not configured - will start captive portal on boot");
    }
    
    wifi_provisioning_state_t state = wifi_provisioning_get_state();
    const char* state_str;
    switch (state) {
        case WIFI_PROV_STATE_IDLE: state_str = "IDLE"; break;
        case WIFI_PROV_STATE_AP_STARTED: state_str = "AP_STARTED"; break;
        case WIFI_PROV_STATE_STA_CONNECTING: state_str = "STA_CONNECTING"; break;
        case WIFI_PROV_STATE_STA_CONNECTED: state_str = "STA_CONNECTED"; break;
        case WIFI_PROV_STATE_STA_FAILED: state_str = "STA_FAILED"; break;
        default: state_str = "UNKNOWN"; break;
    }
    ESP_LOGI(TAG, "Current state: %s", state_str);
    
    return 0;
}

void register_wifi_commands(void)
{
    const esp_console_cmd_t wifi_clear_cmd_def = {
        .command = "wifi_clear",
        .help = "Clear stored WiFi credentials and restart in captive portal mode",
        .hint = NULL,
        .func = &wifi_clear_cmd,
    };
    ESP_ERROR_CHECK(register_console_command(&wifi_clear_cmd_def));

    const esp_console_cmd_t wifi_status_cmd_def = {
        .command = "wifi_status",
        .help = "Show WiFi provisioning status and stored credentials",
        .hint = NULL,
        .func = &wifi_status_cmd,
    };
    ESP_ERROR_CHECK(register_console_command(&wifi_status_cmd_def));
}

REGISTER_DEBUG_COMMAND(register_wifi_commands);