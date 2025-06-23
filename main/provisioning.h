#pragma once
#include "esp_ble_mesh_provisioning_api.h"
#include "nodesManager.h"

#define PROV_OWN_ADDR 0x0001
#define COMP_DATA_PAGE_0 0x00

// static struct example_info_store
// {
//     uint16_t net_idx; /* NetKey Index */
//     uint16_t app_idx; /* AppKey Index */
//     uint8_t onoff;    /* Remote OnOff */
//     uint8_t tid;      /* Message TID */
// } __attribute__((packed)) store = {
//     .net_idx = ESP_BLE_MESH_KEY_UNUSED,
//     .app_idx = ESP_BLE_MESH_KEY_UNUSED,
//     .onoff = LED_OFF,
//     .tid = 0x0,
// };

esp_err_t prov_complete(int node_idx, const esp_ble_mesh_octet16_t uuid,
                               uint16_t unicast, uint8_t elem_num, uint16_t net_idx);

void prov_link_open(esp_ble_mesh_prov_bearer_t bearer);
void prov_link_close(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason);
void recv_unprov_adv_pkt(uint8_t dev_uuid[16], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer);

void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param);

void process_composition_data(esp_ble_mesh_cfg_client_cb_param_t *param);