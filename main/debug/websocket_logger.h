#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

void websocket_logger_register_uri(httpd_handle_t server);
void websocket_logger_install();

#ifdef __cplusplus
}
#endif
