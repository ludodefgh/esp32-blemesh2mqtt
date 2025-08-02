#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

char* get_ip_address();
void wifi_init_sta(void);
esp_err_t wifi_init_sta_with_stored_credentials(void);

#ifdef __cplusplus
}
#endif