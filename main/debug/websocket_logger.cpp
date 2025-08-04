#include "websocket_logger.h"
#include "esp_log.h"
#include <vector>
#include <mutex>
#include <algorithm>
#include <freertos/ringbuf.h>

static httpd_handle_t ws_server = nullptr;
static std::vector<int> ws_clients;
static std::mutex ws_mutex;
static RingbufHandle_t log_ringbuf = nullptr;
static const char *TAG = "ws_logger";

// WebSocket connection management constants
static constexpr size_t MAX_WS_CLIENTS = 4;

// Helper function to safely add a client with connection limit
static bool add_ws_client(int fd) {
    std::lock_guard<std::mutex> lock(ws_mutex);
    if (ws_clients.size() >= MAX_WS_CLIENTS) {
        ESP_LOGW(TAG, "WebSocket client limit reached (%zu), rejecting fd: %d", MAX_WS_CLIENTS, fd);
        return false;
    }
    if (std::find(ws_clients.begin(), ws_clients.end(), fd) == ws_clients.end()) {
        ws_clients.push_back(fd);
        ESP_LOGI(TAG, "WebSocket client connected, fd: %d, total clients: %zu", fd, ws_clients.size());
        return true;
    }
    return false;
}

// Helper function to safely remove a client
static void remove_ws_client(int fd) {
    std::lock_guard<std::mutex> lock(ws_mutex);
    auto it = std::find(ws_clients.begin(), ws_clients.end(), fd);
    if (it != ws_clients.end()) {
        ws_clients.erase(it);
        ESP_LOGI(TAG, "WebSocket client disconnected, fd: %d, remaining clients: %zu", fd, ws_clients.size());
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    httpd_ws_client_info_t ws_info = httpd_ws_get_fd_info(ws_server, fd);
    
    if (ws_info == HTTPD_WS_CLIENT_INVALID) {
        ESP_LOGW(TAG, "Invalid WebSocket client fd: %d", fd);
        return ESP_FAIL;
    }
    
    if (ws_info == HTTPD_WS_CLIENT_WEBSOCKET) {
        // New WebSocket connection - use safe helper
        if (!add_ws_client(fd)) {
            ESP_LOGW(TAG, "Failed to add WebSocket client fd: %d", fd);
            return ESP_FAIL;
        }
    } else if (ws_info == HTTPD_WS_CLIENT_HTTP) {
        // Upgrade to WebSocket
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
            remove_ws_client(fd);
            return ret;
        }
    }
    
    return ESP_OK;
}

void websocket_logger_register_uri(httpd_handle_t server)
{
    ESP_LOGE(TAG, "websocket_logger_register_uri");
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
    vprintf(fmt, args); // still output to UART

    if (log_ringbuf)
    {
        BaseType_t result = xRingbufferSend(log_ringbuf, line, len + 1, 0); // include null terminator
        if (result != pdTRUE)
        {
            printf("Failed to send log line to ringbuffer");
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

            for (int fd : clients_copy) {
                // Check if client is still valid before sending
                httpd_ws_client_info_t ws_info = httpd_ws_get_fd_info(ws_server, fd);
                if (ws_info == HTTPD_WS_CLIENT_INVALID) {
                    ESP_LOGW(TAG, "Client fd %d is invalid, marking for removal", fd);
                    failed_clients.push_back(fd);
                    continue;
                }
                
                esp_err_t ret = httpd_ws_send_frame_async(ws_server, fd, &frame);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send to WebSocket client fd: %d, error: %s", fd, esp_err_to_name(ret));
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
                        ESP_LOGW(TAG, "Removed failed WebSocket client fd: %d", fd);
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
                    ESP_LOGW(TAG, "Cleanup: Removed stale WebSocket client fd: %d", fd);
                }
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(ws_mutex);
            if (!ws_clients.empty()) {
                ESP_LOGD(TAG, "WebSocket cleanup: %zu active clients", ws_clients.size());
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
    xTaskCreate(ws_cleanup_task, "ws_cleanup", 1536, nullptr, 2, nullptr);        // Reduced from 2048, lowered priority
    esp_log_set_vprintf(log_ws_vprintf);
}

