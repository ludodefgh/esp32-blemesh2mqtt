#include "ble_mesh_provisioning.h"

// Standard C/C++ libraries
#include <inttypes.h>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <vector>

// ESP-IDF includes
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_console.h"
#include "esp_mac.h"
#include "nvs_flash.h"

// Project includes
#include "ble_mesh_control.h"
#include "ble_mesh_node.h"
#include "common/log_common.h"
#include "debug/console_cmd.h"
#include "debug/debug_commands_registry.h"
#include "debug_console_common.h"
#include "message_queue.h"
#include "mqtt/mqtt_control.h"

#define TAG "APP_PROV"

extern esp_ble_mesh_client_t config_client;
std::vector<ble2mqtt_unprovisioned_device> unprovisioned_devices;

void remove_unprovisioned_device(const device_uuid128 &uuid)
{
    for (auto it = unprovisioned_devices.begin(); it != unprovisioned_devices.end(); ++it)
    {
        if (memcmp(it->dev_uuid, uuid.raw(), 16) == 0)
        {
            unprovisioned_devices.erase(it);
            break;
        }
    }
}

struct mesh_network_info_store store = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .onoff = 0,
    .tid = 0x0,
};

void print_model_name(uint16_t model_id)
{
    switch (model_id)
    {
    case ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV:
        LOG_INFO(TAG, "Model: Generic OnOff Server");
        break;
    case ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI:
        LOG_INFO(TAG, "Model: Generic OnOff Client");
        break;
    case ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_SRV:
        LOG_INFO(TAG, "Model: Generic Level Server");
        break;
    case ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_CLI:
        LOG_INFO(TAG, "Model: Generic Level Client");
        break;
    case ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_SRV:
        LOG_INFO(TAG, "Model: Light Lightness Server");
        break;
    case ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_CLI:
        LOG_INFO(TAG, "Model: Light Lightness Client");
        break;
    case ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_SRV:
        LOG_INFO(TAG, "Model: Light HSL Server");
        break;
    case ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_CLI:
        LOG_INFO(TAG, "Model: Light HSL Client");
        break;
    default:
        LOG_INFO(TAG, "Model: Unknown (0x%04X)", model_id);
        break;
    }
}
esp_err_t prov_complete(esp_ble_mesh_prov_cb_param_t::ble_mesh_provisioner_prov_comp_param &node_aparam)
{
    int node_idx = node_aparam.node_idx;
    const device_uuid128 uuid128{node_aparam.device_uuid};
    uint16_t unicast = node_aparam.unicast_addr;
    uint8_t elem_num = node_aparam.element_num;
    uint16_t net_idx = node_aparam.netkey_idx;
    uint16_t node_index = node_aparam.node_idx;

    std::shared_ptr<bm2mqtt_node_info> node = nullptr;
    char name[11] = {0};
    int err;

    LOG_INFO(TAG, "node index: 0x%x, unicast address: 0x%02x, element num: %d, netkey index: 0x%02x",
             node_idx, unicast, elem_num, net_idx);
    LOG_INFO(TAG, "device uuid: %s", bt_hex(uuid128.raw(), 16));

    snprintf(name, sizeof(name), "%s%d", "NODE-", node_idx);
    err = esp_ble_mesh_provisioner_set_node_name(node_idx, name);
    if (err)
    {
        LOG_ERROR(TAG, "Set node name failed");
        return ESP_FAIL;
    }

    err = node_manager().store_node_info(uuid128, unicast, elem_num, node_index);
    if (err)
    {
        LOG_ERROR(TAG, "Store node info failed");
        return ESP_FAIL;
    }

    node = node_manager().get_node(unicast);
    if (!node)
    {
        LOG_ERROR(TAG, "Get node info failed");
        return ESP_FAIL;
    }

    message_queue().enqueue(node,
                            message_payload{
                                .send = [](std::shared_ptr<bm2mqtt_node_info> &node) // shared_ptr captured by value, keeps node alive
                                {
                                    esp_ble_mesh_client_common_param_t common = {0};
                                    esp_ble_mesh_cfg_client_get_state_t get_state = {0};
                                    LOG_WARN(TAG, "Setting CTL for node 0x%04X", node->unicast);
                                    node_manager().ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
                                    get_state.comp_data_get.page = COMP_DATA_PAGE_0;
                                    esp_err_t err = esp_ble_mesh_config_client_get_state(&common, &get_state);
                                    if (err)
                                    {
                                        LOG_ERROR(TAG, "Send config comp data get failed");
                                    }
                                },
                                .opcode = ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET,
                                .retries_left = 3,
                            });

    store.net_idx = net_idx;
    /* mesh_example_info_store() shall not be invoked here, because if the device
     * is restarted and goes into a provisioned state, then the following events
     * will come:
     * 1st: ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT
     * 2nd: ESP_BLE_MESH_PROV_REGISTER_COMP_EVT
     * So the store.net_idx will be updated here, and if we store the mesh example
     * info here, the wrong app_idx (initialized with 0xFFFF) will be stored in nvs
     * just before restoring it.
     */

    remove_unprovisioned_device(uuid128);

    return ESP_OK;
}

void prov_link_open(esp_ble_mesh_prov_bearer_t bearer)
{
    LOG_INFO(TAG, "%s link open", bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
}

void prov_link_close(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason)
{
    LOG_INFO(TAG, "%s link close, reason 0x%02x",
             bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", reason);
}

void for_each_unprovisioned_node(std::function<void(const ble2mqtt_unprovisioned_device &unprov_device)> func)
{
    for (const auto &unprov_dev : unprovisioned_devices)
    {
        func(unprov_dev);
    }
}

void recv_unprov_adv_pkt(const ble2mqtt_unprovisioned_device &unprov_device)
{
    bool already_registered = false;
    for (auto index = 0; index < unprovisioned_devices.size(); ++index)
    {
        if (memcmp(unprovisioned_devices[index].dev_uuid, unprov_device.dev_uuid, 16) == 0)
        {
            already_registered = true;
            break;
        }
    }

    if (!already_registered)
    {
        LOG_INFO(TAG, "Received unprovisioned device: %s, address: %s, address type: %d, adv type: %d",
                 bt_hex(unprov_device.dev_uuid, 16), bt_hex(unprov_device.addr, BD_ADDR_LEN),
                 unprov_device.addr_type, unprov_device.adv_type);
        unprovisioned_devices.emplace_back(unprov_device);
        
        // Auto-provision if enabled
        if (ble_mesh_get_auto_provisioning_enabled())
        {
            LOG_INFO(TAG, "Auto-provisioning enabled - automatically provisioning device: %s", 
                     bt_hex(unprov_device.dev_uuid, 16));
            ble_mesh_provision_device(unprov_device.dev_uuid);
        }
    }
}

void ble_mesh_provision_device(const uint8_t uuid[16])
{
    ble2mqtt_unprovisioned_device *device = nullptr;
    for (auto index = 0; index < unprovisioned_devices.size(); ++index)
    {
        if (memcmp(unprovisioned_devices[index].dev_uuid, uuid, 16) == 0)
        {
            device = &unprovisioned_devices[index];
            break;
        }
    }

    if (device != nullptr)
    {
        recv_unprov_adv_pkt(device->dev_uuid, device->addr,
                            device->addr_type, device->oob_info,
                            device->adv_type, device->bearer);
    }
}
void recv_unprov_adv_pkt(uint8_t dev_uuid[16], uint8_t addr[BD_ADDR_LEN],
                         esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                         uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    esp_ble_mesh_unprov_dev_add_t add_dev = {0};
    int err;

    /* Due to the API esp_ble_mesh_provisioner_set_dev_uuid_match, Provisioner will only
     * use this callback to report the devices, whose device UUID starts with 0xdd & 0xdd,
     * to the application layer.
     */

    LOG_INFO(TAG, "address: %s, address type: %d, adv type: %d", bt_hex(addr, BD_ADDR_LEN), addr_type, adv_type);
    LOG_INFO(TAG, "device uuid: %s", bt_hex(dev_uuid, 16));
    LOG_INFO(TAG, "oob info: %d, bearer: %s", oob_info, (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");

    memcpy(add_dev.addr, addr, BD_ADDR_LEN);
    add_dev.addr_type = (esp_ble_mesh_addr_type_t)addr_type;
    memcpy(add_dev.uuid, dev_uuid, 16);
    add_dev.oob_info = oob_info;
    add_dev.bearer = (esp_ble_mesh_prov_bearer_t)bearer;
    /* Note: If unprovisioned device adv packets have not been received, we should not add
             device with ADD_DEV_START_PROV_NOW_FLAG set. */
    err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev,
                                                  (esp_ble_mesh_dev_add_flag_t)(ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG));
    if (err)
    {
        LOG_ERROR(TAG, "Add unprovisioned device into queue failed");
    }

    return;
}

void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                      esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        LOG_INFO(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        break;

    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        LOG_INFO(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT, err_code %d", param->provisioner_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
        LOG_INFO(TAG, "ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT, err_code %d", param->provisioner_prov_disable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:

        ble2mqtt_unprovisioned_device new_entry;
        // Manual field copy to handle optimized structure layout
        memcpy(new_entry.dev_uuid, param->provisioner_recv_unprov_adv_pkt.dev_uuid, 16);
        new_entry.bearer = param->provisioner_recv_unprov_adv_pkt.bearer;
        memcpy(new_entry.addr, param->provisioner_recv_unprov_adv_pkt.addr, 6);
        new_entry.oob_info = param->provisioner_recv_unprov_adv_pkt.oob_info;
        new_entry.addr_type = param->provisioner_recv_unprov_adv_pkt.addr_type;
        new_entry.adv_type = param->provisioner_recv_unprov_adv_pkt.adv_type;
        new_entry.rssi = param->provisioner_recv_unprov_adv_pkt.rssi;

        recv_unprov_adv_pkt(new_entry);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        prov_link_open(param->provisioner_prov_link_open.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        prov_link_close(param->provisioner_prov_link_close.bearer, param->provisioner_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        prov_complete(param->provisioner_prov_complete);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        LOG_INFO(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT, err_code %d", param->provisioner_add_unprov_dev_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        LOG_INFO(TAG, "ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT, err_code %d", param->provisioner_set_dev_uuid_match_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
    {
        LOG_INFO(TAG, "ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT, err_code %d", param->provisioner_set_node_name_comp.err_code);
        if (param->provisioner_set_node_name_comp.err_code == ESP_OK)
        {
            const char *name = NULL;
            name = esp_ble_mesh_provisioner_get_node_name(param->provisioner_set_node_name_comp.node_index);
            if (!name)
            {
                LOG_ERROR(TAG, "Get node name failed");
                return;
            }
            LOG_INFO(TAG, "Node %d name is: %s", param->provisioner_set_node_name_comp.node_index, name);
        }
        break;
    }
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
    {
        LOG_INFO(TAG, "ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT, err_code %d", param->provisioner_add_app_key_comp.err_code);
        if (param->provisioner_add_app_key_comp.err_code == ESP_OK)
        {
            esp_err_t err = 0;
            store.app_idx = param->provisioner_add_app_key_comp.app_idx;

            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, store.app_idx,
                                                                       ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_CLI, ESP_BLE_MESH_CID_NVAL);

            if (err != ESP_OK)
            {
                LOG_ERROR(TAG, "Provisioner bind local level model [ESP_BLE_MESH_MODEL_ID_GEN_LEVEL_CLI] appkey failed");
                return;
            }

            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, store.app_idx,
                                                                       ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_CLI, ESP_BLE_MESH_CID_NVAL);

            if (err != ESP_OK)
            {
                LOG_ERROR(TAG, "Provisioner bind local level model [ESP_BLE_MESH_MODEL_ID_LIGHT_CTL_CLI] appkey failed");
                return;
            }

            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, store.app_idx,
                                                                       ESP_BLE_MESH_MODEL_ID_LIGHT_HSL_CLI, ESP_BLE_MESH_CID_NVAL);

            if (err != ESP_OK)
            {
                LOG_ERROR(TAG, "Provisioner bind local level model [ESP_BLE_MESH_MODEL_LIGHT_HSL_CLI] appkey failed");
                return;
            }

            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, store.app_idx,
                                                                       ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_CLI, ESP_BLE_MESH_CID_NVAL);

            if (err != ESP_OK)
            {
                LOG_ERROR(TAG, "Provisioner bind local level model [ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_CLI] appkey failed");
                return;
            }

            err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, store.app_idx,
                                                                       ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI, ESP_BLE_MESH_CID_NVAL);
            if (err != ESP_OK)
            {
                LOG_ERROR(TAG, "Provisioner bind local on-off model [ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI] appkey failed");
                return;
            }
        }
        break;
    }
    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        LOG_INFO(TAG, "ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT, err_code %d", param->provisioner_bind_app_key_to_model_comp.err_code);
        break;
    default:

        // LOG_INFO(TAG, "Other err_code %d", event);
        break;
    }

    return;
}

void for_each_provisioned_node(std::function<void(const esp_ble_mesh_node_t *, int node_index)> func)
{
    for (int i = 0; i < CONFIG_BLE_MESH_MAX_PROV_NODES; i++)
    {
        const esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_table_entry()[i];
        if (node != nullptr)
        {
            func(node, i);
        }
    }
}

int list_provisioned_nodes_esp(int argc, char **argv)
{
    uint16_t node_count = esp_ble_mesh_provisioner_get_prov_node_count();
    LOG_INFO(TAG, "Provisioned nodes: %d", node_count);

    for_each_provisioned_node([](const esp_ble_mesh_node_t *node, int node_index)
                              {
                                  LOG_INFO(TAG, "==device uuid: %s", bt_hex(node->dev_uuid, 16));
                                  LOG_INFO(TAG, "  device name: %s", node->name);
                                  LOG_INFO(TAG, "  node index(ESP): %d", node_index);
                                  LOG_INFO(TAG, "  Primary Address: 0x%04X", node->unicast_addr);
                                  LOG_INFO(TAG, "  Address: %s, address type: %d", bt_hex(node->addr, BD_ADDR_LEN), node->addr_type);
                                  LOG_INFO(TAG, "  Element Count: %d", node->element_num);
                                  LOG_INFO(TAG, "  NetKey Index: %d", node->net_idx);
                              });

    return 0;
}

void ble_mesh_unprovision_device(const device_uuid128 &uuid)
{
    if (auto node_info = node_manager().get_node(uuid))
    {
        message_queue().enqueue(node_info,
                                message_payload{
                                    .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info)
                                    {
                                        LOG_WARN(TAG, "unprovision_device for node 0x%04X", node_info->unicast);
                                        esp_ble_mesh_client_common_param_t common = {0};
                                        esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                        node_manager().ble_mesh_set_msg_common(&common, node_info, config_client.model, ESP_BLE_MESH_MODEL_OP_NODE_RESET);

                                        int err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                        if (err != ESP_OK)
                                        {
                                            LOG_ERROR(TAG, "Failed to delete node [err=%d] [Node=%s]", err, bt_hex(node_info->uuid.raw(), 16));
                                        }
                                    },
                                    .opcode = ESP_BLE_MESH_MODEL_OP_NODE_RESET,
                                    .retries_left = 2,
                                });
    }
    else
    {
        esp_ble_mesh_provisioner_delete_node_with_uuid(uuid.raw());
    }
}

int unprovision_all_nodes(int argc, char **argv)
{
    std::vector<device_uuid128> uuids_to_remove;

    for_each_provisioned_node([&](const esp_ble_mesh_node_t *node, int node_index)
                              {
            LOG_INFO(TAG, "  device uuid: %s", bt_hex(node->dev_uuid, 16));
            LOG_INFO(TAG, "  device name: %s", node->name);
            LOG_INFO(TAG, "  Primary Address: 0x%04X", node->unicast_addr);
            LOG_INFO(TAG, "  Address: %s, address type: %d", bt_hex(node->addr, BD_ADDR_LEN), node->addr_type);
            LOG_INFO(TAG, "  Element Count: %d", node->element_num);
            LOG_INFO(TAG, "  NetKey Index: %d", node->net_idx);

            uuids_to_remove.emplace_back(device_uuid128{node->dev_uuid}); });

    for (device_uuid128 &bla : uuids_to_remove)
    {
        if (auto node_info = node_manager().get_node(bla))
        {
            message_queue().enqueue(node_info, message_payload{
                                                   .send = [](std::shared_ptr<bm2mqtt_node_info> &node_info)
                                                   {
                                                       esp_ble_mesh_client_common_param_t common = {0};
                                                       esp_ble_mesh_cfg_client_set_state_t set_state = {0};
                                                       node_manager().ble_mesh_set_msg_common(&common, node_info, config_client.model, ESP_BLE_MESH_MODEL_OP_NODE_RESET);

                                                       int err = esp_ble_mesh_config_client_set_state(&common, &set_state);
                                                       if (err != ESP_OK)
                                                       {
                                                           LOG_ERROR(TAG, "Failed to delete node [err=%d] [Node=5s]", err, bt_hex(node_info->uuid.raw(), 16));
                                                       }
                                                   },
                                                   .opcode = ESP_BLE_MESH_MODEL_OP_NODE_RESET,
                                                   .retries_left = 3,
                                               });
        }

        esp_ble_mesh_provisioner_delete_node_with_uuid(bla.raw());
    }

    return 0;
}

extern bool get_composition_data_debug;
int get_composition_data(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }
    if (auto node_info = node_manager().get_node(node_index_args.node_index->ival[0]); node_info && node_info->unicast != ESP_BLE_MESH_ADDR_UNASSIGNED)
    {

        get_composition_data_debug = true;
        esp_ble_mesh_client_common_param_t common = {0};
        esp_ble_mesh_cfg_client_get_state_t get_state = {0};
        node_manager().ble_mesh_set_msg_common(&common, node_info, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
        get_state.comp_data_get.page = COMP_DATA_PAGE_0;
        auto err = esp_ble_mesh_config_client_get_state(&common, &get_state);
        if (err)
        {
            LOG_ERROR(TAG, "Send config comp data get failed");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

int provision_device_index(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&node_index_args);

    if (nerrors != 0)
    {
        arg_print_errors(stderr, node_index_args.end, argv[0]);
        return 1;
    }

    if (node_index_args.node_index->ival[0] < 0 || node_index_args.node_index->ival[0] >= unprovisioned_devices.size())
    {
        LOG_ERROR(TAG, "Invalid node index: %d", node_index_args.node_index->ival[0]);
        return 1;
    }

    const ble2mqtt_unprovisioned_device &unprov_device = unprovisioned_devices[node_index_args.node_index->ival[0]];
    ble_mesh_provision_device(unprov_device.dev_uuid);

    return 0;
}
int list_unprovisionned_devices(int argc, char **argv)
{
    LOG_INFO(TAG, "Unprovisionned devices: %d", unprovisioned_devices.size());

    for_each_unprovisioned_node([](const ble2mqtt_unprovisioned_device &unprov_device)
                                {
        LOG_INFO(TAG, "==device uuid: %s", bt_hex(unprov_device.dev_uuid, 16));
        LOG_INFO(TAG, "  address: %s, address type: %d, adv type: %d",
                 bt_hex(unprov_device.addr, BD_ADDR_LEN), unprov_device.addr_type, unprov_device.adv_type); });

    return 0;
}

void register_provisioning_commands()
{
    /* Register commands */

    const esp_console_cmd_t list_prov_nodes_cmd = {
        .command = "list_provisionned_devices",
        .help = "List provisioned nodes at esp level",
        .hint = NULL,
        .func = &list_provisioned_nodes_esp,
    };
    ESP_ERROR_CHECK(register_console_command(&list_prov_nodes_cmd));

    const esp_console_cmd_t unprovision_cmd = {
        .command = "prov_unprovision_all_nodes",
        .help = "Unprovisionned all paired nodes",
        .hint = NULL,
        .func = &unprovision_all_nodes,
    };
    ESP_ERROR_CHECK(register_console_command(&unprovision_cmd));

    node_index_args.node_index = arg_int1("n", "node", "<node_index>", "Node index as reported by prov_list_nodes command");
    node_index_args.end = arg_end(2);

    const esp_console_cmd_t get_composition_data_cmd = {
        .command = "get_composition_data",
        .help = "Get the composition data of a node",
        .hint = NULL,
        .func = &get_composition_data,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(register_console_command(&get_composition_data_cmd));

    const esp_console_cmd_t list_unprovisionned_cmd = {
        .command = "list_unprovisionned_devices",
        .help = "List unprovisionned devices",
        .hint = NULL,
        .func = &list_unprovisionned_devices,
    };
    ESP_ERROR_CHECK(register_console_command(&list_unprovisionned_cmd));

    const esp_console_cmd_t provision_device_index_cmd = {
        .command = "provision_device_index",
        .help = "Provision a device by index",
        .hint = NULL,
        .func = &provision_device_index,
        .argtable = &node_index_args,
    };
    ESP_ERROR_CHECK(register_console_command(&provision_device_index_cmd));
}

void ble_mesh_reset_node(std::shared_ptr<bm2mqtt_node_info> node)
{
    if (node != nullptr)
    {
        LOG_INFO(TAG, "Node reset successfully");
        LOG_INFO(TAG, "Resetting node 0x%04X", node->unicast);
        mqtt_remove_node(node);
        esp_ble_mesh_provisioner_delete_node_with_uuid(node->uuid.raw());
        node_manager().remove_node(node->uuid);
        message_queue().clear_queue(node);
        node_manager().mark_node_info_dirty();
    }
}

REGISTER_DEBUG_COMMAND(register_provisioning_commands);