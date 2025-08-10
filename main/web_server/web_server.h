#pragma once

#include "esp_http_server.h"

void start_webserver(void);
void register_captive_portal_handlers(httpd_handle_t server);
void unregister_captive_portal_handlers(httpd_handle_t server);
void register_bridge_handlers(httpd_handle_t server);
void unregister_bridge_handlers(httpd_handle_t server);
esp_err_t auto_provisioning_get_handler(httpd_req_t *req);
esp_err_t auto_provisioning_set_handler(httpd_req_t *req);