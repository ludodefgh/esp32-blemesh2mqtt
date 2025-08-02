#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DNS_PORT 53
#define DNS_MAX_HOSTNAME_LEN 128
#define DNS_RESPONSE_BUFFER_SIZE 256

esp_err_t dns_server_start(void);
esp_err_t dns_server_stop(void);

#ifdef __cplusplus
}
#endif