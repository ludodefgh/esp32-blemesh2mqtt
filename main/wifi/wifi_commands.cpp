#include "wifi_commands.h"

// ESP-IDF includes
#include "esp_console.h"
#include "esp_system.h"

// Project includes
#include "common/log_common.h"
#include "debug/console_cmd.h"
#include "debug/debug_commands_registry.h"
#include "mesh_config.h"
#include "wifi_provisioning.h"

static const char *TAG = "wifi_commands";

static int wifi_clear_cmd(int argc, char **argv)
{
    LOG_INFO(TAG, "Clearing WiFi credentials...");
    esp_err_t err = wifi_provisioning_clear_credentials();
    if (err == ESP_OK)
    {
        LOG_INFO(TAG, "WiFi credentials cleared successfully");
        LOG_INFO(TAG, "Restarting device to enter captive portal mode...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    else
    {
        LOG_ERROR(TAG, "Failed to clear WiFi credentials: %s", esp_err_to_name(err));
    }
    return 0;
}

static int wifi_status_cmd(int argc, char **argv)
{
    if (wifi_provisioning_is_configured())
    {
        char ssid[32] = {0};
        char password[64] = {0};
        esp_err_t err = wifi_provisioning_get_credentials(ssid, password, sizeof(ssid), sizeof(password));
        if (err == ESP_OK)
        {
            LOG_INFO(TAG, "WiFi configured - SSID: %s", ssid);
        }
        else
        {
            LOG_INFO(TAG, "WiFi configured but failed to read credentials");
        }
    }
    else
    {
        LOG_INFO(TAG, "WiFi not configured - will start captive portal on boot");
    }

    wifi_provisioning_state_t state = wifi_provisioning_get_state();
    const char *state_str;
    switch (state)
    {
    case WIFI_PROV_STATE_IDLE:
        state_str = "IDLE";
        break;
    case WIFI_PROV_STATE_AP_STARTED:
        state_str = "AP_STARTED";
        break;
    case WIFI_PROV_STATE_STA_CONNECTING:
        state_str = "STA_CONNECTING";
        break;
    case WIFI_PROV_STATE_STA_CONNECTED:
        state_str = "STA_CONNECTED";
        break;
    case WIFI_PROV_STATE_STA_FAILED:
        state_str = "STA_FAILED";
        break;
    default:
        state_str = "UNKNOWN";
        break;
    }
    LOG_INFO(TAG, "Current state: %s", state_str);

    return 0;
}

// Sets WiFi + mesh mode over serial in one shot and restarts — for driving setup from
// a script instead of the captive portal UI (which isn't reachable over the network
// from a devcontainer with no path to the bridge's temporary AP).
static int wifi_set_cmd(int argc, char **argv)
{
    if (argc != 3)
    {
        LOG_ERROR(TAG, "Usage: wifi_set <ssid> <password>");
        return 1;
    }

    esp_err_t err = wifi_provisioning_set_credentials(argv[1], argv[2]);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
        return 1;
    }

#ifdef CONFIG_BLE_MESH_NODE
    mesh_config_update([](mesh_config_t *cfg, void *) { cfg->mode = MESH_MODE_JOIN_EXISTING; }, nullptr);

    LOG_INFO(TAG, "WiFi credentials saved, mesh mode set to join-existing, restarting...");
#else
    // Provisioner-only SKU can't run join-existing mode — leave the mesh mode alone.
    LOG_INFO(TAG, "WiFi credentials saved, restarting...");
#endif
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return 0;
}

void register_wifi_commands(void)
{
    const esp_console_cmd_t wifi_set_cmd_def = {
        .command = "wifi_set",
        .help = "Set WiFi credentials (+ join-existing mesh mode on a Node SKU) and restart: wifi_set <ssid> <password>",
        .hint = NULL,
        .func = &wifi_set_cmd,
    };
    ESP_ERROR_CHECK(register_console_command(&wifi_set_cmd_def));

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