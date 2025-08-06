/*
 * Demo code showing how to use the new LittleFS-based company lookup
 * This replaces the old company_map.cpp with significant flash savings.
 */

#include "sig_companies/company_map.h"
#include "esp_log.h"

#define TAG "COMPANY_DEMO"

void demo_company_lookup() {
    ESP_LOGI(TAG, "=== Company Lookup Demo ===");
    
    // Test common companies
    uint16_t test_companies[] = {
        0x0001,  // Nokia
        0x0002,  // Intel
        0x0003,  // IBM
        0x02E5,  // Espressif (ESP32)
        0x004C,  // Apple
        0x0006,  // Microsoft
        0x000F,  // Broadcom
        0x0023,  // Sengled (example BLE mesh device)
        0xFFFF   // Invalid ID
    };
    
    int num_tests = sizeof(test_companies) / sizeof(test_companies[0]);
    
    ESP_LOGI(TAG, "Testing %d company IDs:", num_tests);
    
    for (int i = 0; i < num_tests; i++) {
        uint16_t cid = test_companies[i];
        const char* name = lookup_company_name(cid);
        ESP_LOGI(TAG, "  0x%04X: %s", cid, name);
    }
    
    // Print cache statistics
    company_map_print_stats();
    
    ESP_LOGI(TAG, "=== Memory Usage ===");
    ESP_LOGI(TAG, "Flash saved: ~103KB (moved to LittleFS)");
    ESP_LOGI(TAG, "RAM used: ~2KB (32-entry LRU cache)");
    ESP_LOGI(TAG, "LittleFS used: ~109KB (indexed binary file)");
    
    // Optional: cleanup (normally not needed)
    // company_map_cleanup();
}

/*
Usage in your main application:

#include "sig_companies/company_map.h"

void on_composition_received(esp_ble_mesh_cfg_client_cb_param_t *param, 
                           std::shared_ptr<bm2mqtt_node_info> node) {
    // ... existing code ...
    
    // Get company ID from composition data
    const struct net_buf_simple *buf = param->status_cb.comp_data_status.composition_data;
    const uint8_t *data = buf->data;
    if (buf->len >= 2) {
        uint16_t cid = data[0] | (data[1] << 8);
        node->company_id = cid;
        
        // New: Get company name with optimized lookup
        const char* company_name = lookup_company_name(cid);
        ESP_LOGI(TAG, "Node 0x%04X from company: %s (CID: 0x%04X)", 
                 node->unicast, company_name, cid);
    }
}

// In MQTT discovery message
void make_node_discovery_message(std::shared_ptr<bm2mqtt_node_info> node) {
    // ... existing code ...
    
    if (node->company_id != 0) {
        const char* company_name = lookup_company_name(node->company_id);
        cJSON_AddItemToObject(dev, "mf", cJSON_CreateString(company_name));
    }
}
*/