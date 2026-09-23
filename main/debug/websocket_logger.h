#pragma once

#include "esp_http_server.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void websocket_logger_register_uri(httpd_handle_t server);
    void websocket_logger_install();

    // Fills buf with the retained log history (oldest first, newline-terminated lines,
    // truncated to fit) and null-terminates it. Returns the number of bytes written,
    // excluding the null terminator. Unlike /ws/logs (live stream only, nothing to see
    // if you connect after the fact), this covers what happened before any client
    // connected. Empty unless history retention is enabled (see below).

    // Off by default — retaining history costs RAM continuously for a feature only
    // needed while actively debugging. Disabling drops whatever was already retained.
    void websocket_logger_set_history_enabled(bool enabled);
    bool websocket_logger_is_history_enabled(void);
    size_t websocket_logger_get_history(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
