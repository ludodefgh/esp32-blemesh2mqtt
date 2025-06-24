#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_defs.h"
#include "esp_console.h"
#include "ble_mesh_example_nvs.h"
#include "ble_mesh_example_init.h"

#include "ble_mesh_control.h"
#include "debug/debugUART.h"
#include "debug/debugGPIO.h"
#include "ble_mesh_provisioning.h"
#include "ble_mesh_node.h"
#include "mqtt_control.h"
#include "_config.h"

#define TAG "EXAMPLE"

extern bool init_done;

/// @brief Provisioning stuff

static nvs_handle_t NVS_HANDLE;

extern void wifi_init_sta(void);

static int heap_size(int argc, char **argv)
{
    uint32_t heap_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "min heap size: %" PRIu32, heap_size);
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
    ESP_ERROR_CHECK(esp_console_cmd_register(&heap_cmd));

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
#pragma endregion Debug

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Main

extern "C" void app_main()
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing...");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err)
    {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    /* Open nvs namespace for storing/restoring mesh example info */
    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err)
    {
        return;
    }

    RegisterDebugCommands();

    RegisterProvisioningDebugCommands();
    RegisterBleMeshDebugCommands();
    RegisterMQTTDebugCommands();

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err)
    {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL)
    {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", static_cast<esp_log_level_t>(CONFIG_LOG_MAXIMUM_LEVEL));
    }

    // ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

#if defined(DEBUG_USE_GPIO)
    initDebugGPIO();
#endif

    RefreshNodes();

    /// MQTT START
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    mqtt5_app_start();
    /// MQTT END
}
#pragma endregion Main