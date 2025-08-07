#include "websocket_logger.h"

#include "esp_log_write.h"
#include <vector>
#include <mutex>
#include <algorithm>
#include <freertos/ringbuf.h>
#include "common/log_common.h"

static httpd_handle_t ws_server = nullptr;
static std::vector<int> ws_clients;
static std::mutex ws_mutex;
static RingbufHandle_t log_ringbuf = nullptr;
static const char *TAG = "ws_logger";
static vprintf_like_t original_vprintf = nullptr;

// WebSocket connection management constants
static constexpr size_t MAX_WS_CLIENTS = 4;

// Helper function to safely add a client with connection limit
static bool add_ws_client(int fd) {
    std::lock_guard<std::mutex> lock(ws_mutex);
    if (ws_clients.size() >= MAX_WS_CLIENTS) {
        // WebSocket client limit reached, rejecting connection
        return false;
    }
    if (std::find(ws_clients.begin(), ws_clients.end(), fd) == ws_clients.end()) {
        ws_clients.push_back(fd);
        // WebSocket client connected
        return true;
    } else {
        // WebSocket client already exists, not adding again
        return false;
    }
}

// Helper function to safely remove a client
static void remove_ws_client(int fd) {
    std::lock_guard<std::mutex> lock(ws_mutex);
    auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
    if (it != ws_clients.end()) {
        ws_clients.erase(it);
        // WebSocket client disconnected
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    httpd_ws_client_info_t ws_info = httpd_ws_get_fd_info(ws_server, fd);
    
    LOG_DEBUG(TAG, "ws_handler called: fd=%d, ws_info=%d", fd, ws_info);
    
    if (ws_info == HTTPD_WS_CLIENT_INVALID) {
        // Invalid WebSocket client
        return ESP_FAIL;
    }
    
    if (ws_info == HTTPD_WS_CLIENT_WEBSOCKET) {
        // Check if this is a new connection or an existing one (without double-locking)
        bool already_exists = false;
        {
            std::lock_guard<std::mutex> lock(ws_mutex);
            already_exists = std::find(ws_clients.begin(), ws_clients.end(), fd) != ws_clients.end();
        }
        
        // Checking WebSocket client status
        
        if (!already_exists) {
            // New WebSocket connection - use safe helper
            if (!add_ws_client(fd)) {
                // Failed to add WebSocket client
                return ESP_FAIL;
            }
        }
        // If client already exists, this is just a regular WebSocket frame, continue normally
    } else if (ws_info == HTTPD_WS_CLIENT_HTTP) {
        // HTTP to WebSocket upgrade
        // Upgrade to WebSocket
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        
        if (ret != ESP_OK) {
            LOG_ERROR(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
            remove_ws_client(fd);
            return ret;
        }
    }
    
    return ESP_OK;
}

void websocket_logger_register_uri(httpd_handle_t server)
{
    LOG_INFO(TAG, "WebSocket logger URI registered at /ws/logs");
    httpd_uri_t uri = {
        .uri = "/ws/logs",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = nullptr,
        .is_websocket = true};
    ws_server = server;
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &uri));
}

int log_ws_vprintf(const char *fmt, va_list args)
{
    char line[256];
    int len = vsnprintf(line, sizeof(line), fmt, args);
    
    // Use original vprintf to avoid recursive logging
    if (original_vprintf) {
        original_vprintf(fmt, args);
    }

    // Prevent recursive logging: don't send ws_logger messages to WebSocket
    if (log_ringbuf && !strstr(line, "ws_logger"))
    {
        BaseType_t result = xRingbufferSend(log_ringbuf, line, len + 1, 0); // include null terminator
        if (result != pdTRUE)
        {
            // Use printf directly for error logging to avoid recursion
            printf("Failed to send log line to ringbuffer\n");
        }
    }

    return len;
}

void ws_log_sender_task(void *arg)
{
    while (true)
    {
        size_t len = 0;
        char *line = nullptr;
        while ((line = (char *)xRingbufferReceive(log_ringbuf, &len, portMAX_DELAY)) != nullptr)
        {
            // Create a local copy of client list to minimize lock time
            std::vector<int> clients_copy;
            std::vector<int> failed_clients;
            
            {
                std::lock_guard<std::mutex> lock(ws_mutex);
                clients_copy = ws_clients; // Copy for safe iteration
            }
            
            // Send to clients without holding the main mutex
            httpd_ws_frame_t frame;
            frame.type = HTTPD_WS_TYPE_TEXT;
            frame.payload = (uint8_t *)line;
            frame.len = strlen(line);
            frame.final = true;

            // Sending log to WebSocket clients

            for (int fd : clients_copy) {
                // Check if client is still valid before sending
                httpd_ws_client_info_t ws_info = httpd_ws_get_fd_info(ws_server, fd);
                if (ws_info == HTTPD_WS_CLIENT_INVALID) {
                    // Client is invalid, marking for removal
                    failed_clients.push_back(fd);
                    continue;
                }
                
                // Sending to WebSocket client
                esp_err_t ret = httpd_ws_send_frame_async(ws_server, fd, &frame);
                if (ret != ESP_OK) {
                    // Failed to send to WebSocket client
                    failed_clients.push_back(fd);
                }
            }
            
            // Remove failed clients in a separate critical section
            if (!failed_clients.empty()) {
                std::lock_guard<std::mutex> lock(ws_mutex);
                for (int fd : failed_clients) {
                    auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
                    if (it != ws_clients.end()) {
                        ws_clients.erase(it);
                        // Removed failed WebSocket client
                    }
                }
            }
            
            vRingbufferReturnItem(log_ringbuf, (void *)line);
        }
    }
}

void ws_cleanup_task(void *arg)
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(30000)); // Check every 30 seconds
        
        std::vector<int> clients_copy;
        std::vector<int> stale_clients;
        
        {
            std::lock_guard<std::mutex> lock(ws_mutex);
            clients_copy = ws_clients; // Safe copy for validation
        }
        
        // Check clients without holding mutex
        for (int fd : clients_copy) {
            httpd_ws_client_info_t ws_info = httpd_ws_get_fd_info(ws_server, fd);
            if (ws_info == HTTPD_WS_CLIENT_INVALID) {
                stale_clients.push_back(fd);
            }
        }
        
        // Remove stale clients
        if (!stale_clients.empty()) {
            std::lock_guard<std::mutex> lock(ws_mutex);
            for (int fd : stale_clients) {
                auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
                if (it != ws_clients.end()) {
                    ws_clients.erase(it);
                    // Cleanup: Removed stale WebSocket client
                }
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(ws_mutex);
            if (!ws_clients.empty()) {
                // WebSocket cleanup running
            }
        }
    }
}

void websocket_logger_install()
{
    log_ringbuf = xRingbufferCreate(1024, RINGBUF_TYPE_NOSPLIT);
    assert(log_ringbuf);

    // Reserve capacity for WebSocket clients vector to prevent reallocations
    {
        std::lock_guard<std::mutex> lock(ws_mutex);
        ws_clients.reserve(MAX_WS_CLIENTS);
    }

    xTaskCreate(ws_log_sender_task, "ws_log_sender", 3072, nullptr, 5, nullptr);  // Reduced from 4096
    xTaskCreate(ws_cleanup_task, "ws_cleanup", 2048, nullptr, 2, nullptr);        // Increased stack size to prevent overflow
    
    // Store original vprintf function before replacing it
    original_vprintf = esp_log_set_vprintf(log_ws_vprintf);
}

