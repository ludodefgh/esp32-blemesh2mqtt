// Standard C/C++ libraries
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// ESP-IDF includes
#include "esp_ble_mesh_defs.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_console.h"
#include "esp_littlefs.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_flash.h"

// Project includes
#include "ble_mesh/ble_mesh_commands.h"
#include "ble_mesh/ble_mesh_control.h"
#include "ble_mesh/ble_mesh_node.h"
#include "ble_mesh/ble_mesh_provisioning.h"
#include "common/log_common.h"
#include "debug/console_cmd.h"
#include "debug/debug_commands_registry.h"
#include "mqtt/mqtt_control.h"
#include "security/credential_encryption.h"
#include "web_server/web_server.h"
#include "wifi/wifi_commands.h"
#include "wifi/wifi_provisioning.h"

#define TAG "MAIN"

static int heap_size(int argc, char **argv)
{
    uint32_t heap_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    LOG_INFO(TAG, "min heap size: %" PRIu32, heap_size);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DEBUG
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Debug

void RegisterDebugCommands()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    // init console REPL environment
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    /* Register commands */

    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "get min free heap size during test",
        .hint = NULL,
        .func = &heap_size,
    };
    ESP_ERROR_CHECK(register_console_command(&heap_cmd));

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
REGISTER_DEBUG_COMMAND(RegisterDebugCommands);
#pragma endregion Debug

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Main

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

void mount_littlefs(void)
{
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));
}

// Implement missing functions from ble_mesh_example_init.h
esp_err_t bluetooth_init(void)
{
    esp_err_t err;

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err) {
        LOG_ERROR("BLUETOOTH", "Bluetooth controller init failed");
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err) {
        LOG_ERROR("BLUETOOTH", "Bluetooth controller enable failed");
        return err;
    }

    err = esp_bluedroid_init();
    if (err) {
        LOG_ERROR("BLUETOOTH", "Bluedroid init failed");
        return err;
    }

    err = esp_bluedroid_enable();
    if (err) {
        LOG_ERROR("BLUETOOTH", "Bluedroid enable failed");
        return err;
    }

    return ESP_OK;
}

void ble_mesh_get_dev_uuid(uint8_t *dev_uuid)
{
    if (dev_uuid == NULL) {
        return;
    }

    /* Copy device address to first 6 bytes of device UUID */
    uint8_t base_mac_addr[6] = {0};
    esp_read_mac(base_mac_addr, ESP_MAC_BT);
    memcpy(dev_uuid, base_mac_addr, 6);

    /* The remaining 10 bytes are filled with static values */
    dev_uuid[6] = 0xdd;
    dev_uuid[7] = 0xdd;
    for (int i = 8; i < 16; i++) {
        dev_uuid[i] = 0x00;
    }
}
extern "C" void app_main()
{
    esp_err_t err;
    
    LOG_INFO(TAG, "Initializing...");

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialize credential encryption system
    err = CredentialEncryption::instance().initialize();
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to initialize credential encryption: %s", esp_err_to_name(err));
        return;
    }

    err = bluetooth_init();
    if (err)
    {
        LOG_ERROR(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    mount_littlefs();

    size_t total = 0, used = 0;
    err = esp_littlefs_info(conf.partition_label, &total, &used);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(err));
        esp_littlefs_format(conf.partition_label);
    } else {
        LOG_INFO(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    register_wifi_commands();
    debug_command_registry::run_all();

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err)
    {
        LOG_ERROR(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL)
    {
        esp_log_level_set("wifi", static_cast<esp_log_level_t>(CONFIG_LOG_MAXIMUM_LEVEL));
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    ESP_ERROR_CHECK(wifi_provisioning_init());

    if (wifi_provisioning_should_start_captive_portal()) {
        LOG_INFO(TAG, "Starting captive portal for WiFi setup");
        ESP_ERROR_CHECK(wifi_provisioning_start_captive_portal());
    } else {
        LOG_INFO(TAG, "WiFi already connected via provisioning");
    }

#if defined(DEBUG_USE_GPIO)
    initDebugGPIO();
#endif

    start_webserver();

    if (wifi_provisioning_get_state() == WIFI_PROV_STATE_STA_CONNECTED ||
        wifi_provisioning_get_state() == WIFI_PROV_STATE_IDLE) {
        
        node_manager().initialize();
        mqtt5_app_start();
        refresh_all_nodes();

        esp_log_level_set("*", ESP_LOG_INFO);
        esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
        esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
        esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
        esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
        esp_log_level_set("transport", ESP_LOG_VERBOSE);
        esp_log_level_set("outbox", ESP_LOG_VERBOSE);
    } else {
        LOG_INFO(TAG, "WiFi not connected, skipping MQTT and BLE mesh initialization");
    }
}
#pragma endregion Main