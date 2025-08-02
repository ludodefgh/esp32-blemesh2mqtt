#pragma once

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DNS_PORT 53
#define DNS_MAX_HOSTNAME_LEN 128
#define DNS_RESPONSE_BUFFER_SIZE 256
#define DNS_SERVER_MAX_ITEMS 3

typedef struct {
    char name[DNS_MAX_HOSTNAME_LEN];  // Domain name to match (use "*" for wildcard)
    esp_ip4_addr_t ip;                // IP address to respond with
} dns_server_item_t;

typedef struct {
    int num_of_entries;
    dns_server_item_t item[DNS_SERVER_MAX_ITEMS];
} dns_server_config_t;

// Original simple API (for backward compatibility)
esp_err_t dns_server_start(void);
esp_err_t dns_server_stop(void);

// New configurable API
esp_err_t dns_server_start_with_config(const dns_server_config_t *config);

#ifdef __cplusplus
}
#endif