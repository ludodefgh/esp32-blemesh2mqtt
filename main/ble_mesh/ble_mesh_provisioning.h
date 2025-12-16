#pragma once
#include <functional>

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "ble_mesh_node.h"

#define PROV_OWN_ADDR 0x0001
#define COMP_DATA_PAGE_0 0x00

struct mesh_network_info_store
{
    uint16_t net_idx; /* NetKey Index */
    uint16_t app_idx; /* AppKey Index */
    uint8_t onoff;    /* Remote OnOff */
    uint8_t tid;      /* Message TID */
};

// Optimized memory layout to reduce padding
// Members reordered: 16-byte arrays, 4-byte enums, 6-byte arrays, 2-byte types, 1-byte types
// Reduces size from 36 bytes to 32 bytes (11% reduction)
struct ble2mqtt_unprovisioned_device
{
    uint8_t dev_uuid[16];               /*!< Device UUID of the unprovisioned device */
    esp_ble_mesh_prov_bearer_t bearer;  /*!< Bearer of the unprovisioned device (4 bytes enum) */
    esp_ble_mesh_bd_addr_t addr;        /*!< Device address of the unprovisioned device (6 bytes) */
    uint16_t oob_info;                  /*!< OOB Info of the unprovisioned device (2 bytes, completes 4-byte alignment) */
    esp_ble_mesh_addr_type_t addr_type; /*!< Device address type (1 byte) */
    uint8_t adv_type;                   /*!< Advertising type of the unprovisioned device (1 byte) */
    int8_t rssi;                        /*!< RSSI of the received advertising packet (1 byte) */
    // 1 byte padding to align to 4-byte boundary
    // Total: 16 + 4 + 6 + 2 + 3 + 1 padding = 32 bytes (vs 36 bytes)
};

static_assert(sizeof(ble2mqtt_unprovisioned_device) <= 32, "ble2mqtt_unprovisioned_device has unexpected padding");

esp_err_t prov_complete(int node_idx, const esp_ble_mesh_octet16_t uuid,
                        uint16_t unicast, uint8_t elem_num, uint16_t net_idx);

void prov_link_open(esp_ble_mesh_prov_bearer_t bearer);
void prov_link_close(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason);
void recv_unprov_adv_pkt(uint8_t dev_uuid[16], uint8_t addr[BD_ADDR_LEN],
                         esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                         uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer);
void recv_unprov_adv_pkt(const ble2mqtt_unprovisioned_device &unprov_device);

void for_each_unprovisioned_node(std::function<void(const ble2mqtt_unprovisioned_device &unprov_device)> func);

void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                      esp_ble_mesh_prov_cb_param_t *param);

int list_provisioned_nodes_esp(int argc = 0, char **argv = nullptr);

void for_each_provisioned_node(std::function<void(const esp_ble_mesh_node_t *, int node_index)> func);

void register_provisioning_commands();

void ble_mesh_unprovision_device(const device_uuid128 &dev_uuid);
void ble_mesh_provision_device(const uint8_t uuid[16]);

void ble_mesh_reset_node(std::shared_ptr<bm2mqtt_node_info> node);