#include "ble_mesh_commands.h"
#include "ble_mesh_node.h"
#include "ble_mesh_provisioning.h"
#include <esp_log.h>
#include <esp_err.h>
#include <esp_console.h>
#include <argtable3/argtable3.h>
#include "debug_console_common.h"
#include "debug/debug_commands_registry.h"
#include "message_queue.h"
#include "Uui128.h"

#define TAG "MESH_COMMANDS"


extern esp_ble_mesh_client_t config_client;
extern esp_ble_mesh_client_t onoff_client;
extern esp_ble_mesh_client_t level_client;
extern esp_ble_mesh_client_t lightness_cli;
extern esp_ble_mesh_client_t hsl_cli;
extern esp_ble_mesh_client_t ctl_cli;

extern struct example_info_store store;

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void ble_mesh_ctl_set(bm2mqtt_node_info *node_info)
{
    node_info->color_mode = color_mode_t::color_temp;
    message_queue().enqueue(node_info->unicast,
                            message_payload{
                                .send = [node_info]()
                                {
                                    ESP_LOGW(TAG, "[ble_mesh_ctl_set] Setting CTL for node 0x%04X", __func__, node_info->unicast);
                                    esp_ble_mesh_client_common_param_t common = {0};
                                    esp_ble_mesh_light_client_set_state_t set_state_light = {0};

                                    node_manager().example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET);

                                    set_state_light.ctl_set.ctl_temperature = node_info->curr_temp;
                                    set_state_light.ctl_set.ctl_lightness = node_info->hsl_l;
                                    set_state_light.ctl_set.ctl_delta_uv = 0;
                                    set_state_light.ctl_set.op_en = false;
                                    set_state_light.ctl_set.delay = 0;
                                    set_state_light.ctl_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
                                    int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
                                },
                                .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET,
                                .retries_left = 3,
                            });
}

void ble_mesh_ctl_temperature_set(bm2mqtt_node_info *node_info)
{
    ESP_LOGI(TAG, "[%s] Setting CTL Temperature for node 0x%04X", __func__, node_info->unicast);
    node_info->color_mode = color_mode_t::color_temp;

    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_light_client_set_state_t set_state_light = {0};

    node_manager().example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET_UNACK);
    common.ctx.addr = node_info->unicast + node_info->light_ctl_temp_offset;

    set_state_light.ctl_temperature_set.ctl_temperature = node_info->curr_temp;
    set_state_light.ctl_temperature_set.ctl_delta_uv = 0;
    set_state_light.ctl_temperature_set.op_en = false;
    set_state_light.ctl_temperature_set.delay = 0;
    set_state_light.ctl_temperature_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
    int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
    if (err)
    {
        ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET Set failed", __func__);
    }
}

void light_hsl_set(bm2mqtt_node_info *node_info)
{
    message_queue().enqueue(node_info->unicast,
                            message_payload{
                                .send = [node_info]()
                                {
                                    ESP_LOGW(TAG, "[light_hsl_set] Setting HSL for node 0x%04X", __func__, node_info->unicast);
                                    esp_ble_mesh_client_common_param_t common = {0};
                                    esp_ble_mesh_light_client_set_state_t set_state = {0};

                                    node_manager().example_ble_mesh_set_msg_common(&common, node_info, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET);

                                    node_info->color_mode = color_mode_t::hs;
                                    set_state.hsl_set.hsl_hue = node_info->hsl_h;
                                    set_state.hsl_set.hsl_saturation = node_info->hsl_s;
                                    set_state.hsl_set.hsl_lightness = node_info->hsl_l;
                                    set_state.hsl_set.op_en = false;
                                    set_state.hsl_set.delay = 0;
                                    set_state.hsl_set.tid = store.tid++; // Transaction ID (should increment on each new transaction)
                                    esp_err_t err = esp_ble_mesh_light_client_set_state(&common, &set_state);
                                    if (err)
                                    {
                                        ESP_LOGE(TAG, "%s: call to esp_ble_mesh_light_client_set_state failed", __func__);
                                    }
                                },
                                .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET,
                                .retries_left = 3,
                            });
}

void gen_onoff_set(bm2mqtt_node_info *node_info)
{
    message_queue().enqueue(node_info->unicast,
                            message_payload{
                                .send = [node_info]()
                                {
                                    ESP_LOGW(TAG, "[gen_onoff_set] Generic on/off model for node 0x%04X", node_info->unicast);
                                    esp_ble_mesh_client_common_param_t common = {0};
                                    esp_ble_mesh_generic_client_set_state_t set_state = {0};

                                    node_manager().example_ble_mesh_set_msg_common(&common, node_info, onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET);
                                    set_state.onoff_set.op_en = false;
                                    set_state.onoff_set.onoff = node_info->onoff;
                                    set_state.onoff_set.tid = store.tid++;
                                    esp_err_t err = esp_ble_mesh_generic_client_set_state(&common, &set_state);
                                    if (err)
                                    {
                                        ESP_LOGE(TAG, "%s: call to esp_ble_mesh_config_client_set_state failed", __func__);
                                    }
                                },
                                .opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET,
                                .retries_left = 3,
                            });
}

typedef struct
{
    struct arg_int *temperature;
    struct arg_end *end;
} ble_mesh_ctl_temperature_set_args_t;
ble_mesh_ctl_temperature_set_args_t ctl_temperature_set_args;

typedef struct
{
    struct arg_int *lightness;
    struct arg_end *end;
} ble_mesh_ctl_lightness_set_args_t;
ble_mesh_ctl_lightness_set_args_t ctl_lightness_set_args;


int ble_mesh_hsl_range_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ble_mesh_hsl_range_get(node_info);
    }
    return 0;
}

void ble_mesh_hsl_range_get(bm2mqtt_node_info *node_info)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_light_client_get_state_t get_state_light = {0};

    node_manager().example_ble_mesh_set_msg_common(&common, node_info, hsl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET);
    int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
    if (err)
    {
        ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_RANGE_GET Get failed", __func__);
    }
}

int ble_mesh_lightness_range_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state_light = {0};

        node_manager().example_ble_mesh_set_msg_common(&common, node_info, lightness_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET);
        int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_RANGE_GET Get failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_ctl_temperature_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ble_mesh_ctl_temperature_get(node_info);
    }
    return 0;
}

void ble_mesh_ctl_temperature_get(bm2mqtt_node_info *node_info)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_light_client_get_state_t get_state_light = {0};

    node_manager().example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_GET);
    common.ctx.addr = node_info->unicast + node_info->light_ctl_temp_offset;
    int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
    if (err)
    {
        ESP_LOGE(TAG, "%s: failed", __func__);
    }
}

int ble_mesh_ctl_temperature_range_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        ble_mesh_ctl_temperature_range_get(node_info);
    }
    return 0;
}

void ble_mesh_ctl_temperature_range_get(bm2mqtt_node_info *node_info)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_light_client_get_state_t get_state_light = {0};

    node_manager().example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_RANGE_GET);
    common.ctx.addr = node_info->unicast;

    int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
    if (err)
    {
        ESP_LOGE(TAG, "%s: failed", __func__);
    }
}

int ble_mesh_ctl_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_get_state_t get_state_light = {0};

        node_manager().example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET);
        int err = esp_ble_mesh_light_client_get_state(&common, &get_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_GET Get failed", __func__);
        }
    }
    return 0;
}

int ble_mesh_ctl_lightness_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ctl_lightness_set_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, ctl_lightness_set_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(0); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
            message_queue().enqueue(node_info->unicast,
                            message_payload{
                                .send = [node_info]()
                                {
                                    ESP_LOGW(TAG, "[ble_mesh_ctl_lightness_set] Setting Lightness for node 0x%04X", __func__, node_info->unicast);
                                    esp_ble_mesh_client_common_param_t common = {0};
                                    esp_ble_mesh_light_client_set_state_t set_state_light = {0};

                                    node_manager().example_ble_mesh_set_msg_common(&common, node_info, lightness_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET);
                                    common.ctx.addr = node_info->unicast;

                                    set_state_light.lightness_set.lightness = ctl_lightness_set_args.lightness->ival[0];
                                    set_state_light.lightness_set.op_en = false;
                                    set_state_light.lightness_set.delay = 0;
                                    set_state_light.lightness_set.tid = store.tid++;
                                    int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
                                    if (err)
                                    {
                                        ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET Set failed", __func__);
                                    }
                                },
                                .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET,
                                .retries_left = 3,
                            });

        
    }
    return 0;
}

int ble_mesh_ctl_lightness_set(int lightness_value, const Uuid128& uuid)
{
    if (bm2mqtt_node_info *node_info = node_manager().get_node(uuid); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
         message_queue().enqueue(node_info->unicast,
                            message_payload{
                                .send = [node_info, lightness_value]()
                                {
                                    ESP_LOGW(TAG, "[ble_mesh_ctl_lightness_set] Setting Lightness for node 0x%04X", __func__, node_info->unicast);
                                    esp_ble_mesh_client_common_param_t common = {0};
                                    esp_ble_mesh_light_client_set_state_t set_state_light = {0};

                                    node_manager().example_ble_mesh_set_msg_common(&common, node_info, lightness_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET);
                                    common.ctx.addr = node_info->unicast;

                                    set_state_light.lightness_set.lightness = lightness_value;
                                    set_state_light.lightness_set.op_en = false;
                                    set_state_light.lightness_set.delay = 0;
                                    set_state_light.lightness_set.tid = store.tid++;
                                    int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
                                    if (err)
                                    {
                                        ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET Set failed", __func__);
                                    }
                                },
                                .opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET,
                                .retries_left = 3,
                            });

        
    }
    return 0;
}

int ble_mesh_ctl_temperature_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ctl_temperature_set_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, ctl_temperature_set_args.end, argv[0]);
        return 1;
    }

    if (bm2mqtt_node_info *node_info = node_manager().get_node(0); node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_light_client_set_state_t set_state_light = {0};

        node_manager().example_ble_mesh_set_msg_common(&common, node_info, ctl_cli.model, ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET_UNACK);
        common.ctx.addr = node_info->unicast + node_info->light_ctl_temp_offset;

        set_state_light.ctl_temperature_set.ctl_temperature = ctl_temperature_set_args.temperature->ival[0];
        set_state_light.ctl_temperature_set.ctl_delta_uv = 0;
        set_state_light.ctl_temperature_set.op_en = false;
        set_state_light.ctl_temperature_set.delay = 0;
        set_state_light.ctl_temperature_set.tid = store.tid++;
        int err = esp_ble_mesh_light_client_set_state(&common, &set_state_light);
        if (err)
        {
            ESP_LOGE(TAG, "%s: ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_TEMPERATURE_SET Get failed", __func__);
        }
    }
    return 0;
}


void RegisterBleMeshCommandsDebugCommands()
{
    /* Register commands */

    node_index_args.node_index = arg_int1("n", "node", "<node_index>", "Node index as reported by prov_list_nodes command");
    node_index_args.end = arg_end(2);

    const esp_console_cmd_t ble_mesh_hsl_range_get_cmd = {
        .command = "ble_mesh_hsl_range_get",
        .help = "Light HSL Range Get",
        .hint = NULL,
        .func = &ble_mesh_hsl_range_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_hsl_range_get_cmd));

    const esp_console_cmd_t ble_mesh_lightness_range_get_cmd = {
        .command = "ble_mesh_lightness_range_get",
        .help = "Lightness Range Get",
        .hint = NULL,
        .func = &ble_mesh_lightness_range_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_lightness_range_get_cmd));

    ctl_lightness_set_args.lightness = arg_int1("l", "lightness", "<lightness>", "lightness");
    ctl_lightness_set_args.end = arg_end(2);

    const esp_console_cmd_t ble_mesh_ctl_lightness_set_cmd = {
        .command = "ble_mesh_ctl_lightness_set",
        .help = "Ctl temperature Set",
        .hint = NULL,
        .func = &ble_mesh_ctl_lightness_set,
        .argtable = &ctl_lightness_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_lightness_set_cmd));

    const esp_console_cmd_t ble_mesh_ctl_temperature_get_cmd = {
        .command = "ble_mesh_ctl_temperature_get",
        .help = "Clt Temperature Get",
        .hint = NULL,
        .func = &ble_mesh_ctl_temperature_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_temperature_get_cmd));

    const esp_console_cmd_t ble_mesh_ctl_temperature_range_get_cmd = {
        .command = "ble_mesh_ctl_temperature_range_get",
        .help = "Ctl temperature Range Get",
        .hint = NULL,
        .func = &ble_mesh_ctl_temperature_range_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_temperature_range_get_cmd));

    const esp_console_cmd_t ble_mesh_ctl_get_cmd = {
        .command = "ble_mesh_ctl_get",
        .help = "Ctl  Get",
        .hint = NULL,
        .func = &ble_mesh_ctl_get,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_get_cmd));

    ctl_temperature_set_args.temperature = arg_int1("t", "temp", "<color_temp>", "Temperature of the color in K");
    ctl_temperature_set_args.end = arg_end(2);

    const esp_console_cmd_t ble_mesh_ctl_temperature_set_cmd = {
        .command = "ble_mesh_ctl_temperature_set",
        .help = "Ctl temperature Set",
        .hint = NULL,
        .func = &ble_mesh_ctl_temperature_set,
        .argtable = &ctl_temperature_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ble_mesh_ctl_temperature_set_cmd));

}
REGISTER_DEBUG_COMMAND(RegisterBleMeshCommandsDebugCommands);