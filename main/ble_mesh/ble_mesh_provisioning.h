#pragma once
#include <functional>
#include "esp_ble_mesh_provisioning_api.h"
#include "ble_mesh_node.h"
#include "esp_ble_mesh_defs.h"

#define PROV_OWN_ADDR 0x0001
#define COMP_DATA_PAGE_0 0x00


struct example_info_store
{
    uint16_t net_idx; /* NetKey Index */
    uint16_t app_idx; /* AppKey Index */
    uint8_t onoff;    /* Remote OnOff */
    uint8_t tid;      /* Message TID */
};

struct ble2mqtt_unprovisioned_device {
        uint8_t  dev_uuid[16];                  /*!< Device UUID of the unprovisioned device */
        esp_ble_mesh_bd_addr_t addr;            /*!< Device address of the unprovisioned device */
        esp_ble_mesh_addr_type_t addr_type;     /*!< Device address type */
        uint16_t oob_info;                      /*!< OOB Info of the unprovisioned device */
        uint8_t  adv_type;                      /*!< Advertising type of the unprovisioned device */
        esp_ble_mesh_prov_bearer_t bearer;      /*!< Bearer of the unprovisioned device */
        int8_t   rssi;                          /*!< RSSI of the received advertising packet */
    } ;

esp_err_t prov_complete(int node_idx, const esp_ble_mesh_octet16_t uuid,
                               uint16_t unicast, uint8_t elem_num, uint16_t net_idx);

void prov_link_open(esp_ble_mesh_prov_bearer_t bearer);
void prov_link_close(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason);
void recv_unprov_adv_pkt(uint8_t dev_uuid[16], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer);
void recv_unprov_adv_pkt(const ble2mqtt_unprovisioned_device& unprov_device);

void for_each_unprovisioned_node(std::function<void( const ble2mqtt_unprovisioned_device& unprov_device)> func);

void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param);

int list_provisioned_nodes(int argc = 0, char **argv = nullptr);

void for_each_provisioned_node(std::function<void( const esp_ble_mesh_node_t *)> func);

void RegisterProvisioningDebugCommands();

void unprovision_device(uint8_t uuid[16]);
void provision_device(uint8_t uuid[16]);