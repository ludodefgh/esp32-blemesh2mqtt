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

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (httpd_ws_get_fd_info(ws_server, httpd_req_to_sockfd(req)) != HTTPD_WS_CLIENT_WEBSOCKET)
        return ESP_OK;

    int fd = httpd_req_to_sockfd(req);
    std::lock_guard<std::mutex> lock(ws_mutex);
    if (std::find(ws_clients.begin(), ws_clients.end(), fd) == ws_clients.end())
    {
        ws_clients.push_back(fd);
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
            std::lock_guard<std::mutex> lock(ws_mutex);
            for (auto it = ws_clients.begin(); it != ws_clients.end();)
            {
                httpd_ws_frame_t frame;
                frame.type = HTTPD_WS_TYPE_TEXT;
                frame.payload = (uint8_t *)line;
                frame.len = strlen(line);
                frame.final = true;

                if (httpd_ws_send_frame_async(ws_server, *it, &frame) != ESP_OK)
                {
                    it = ws_clients.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            vRingbufferReturnItem(log_ringbuf, (void *)line);
        }
    }
}

void websocket_logger_install()
{
    log_ringbuf = xRingbufferCreate(1024, RINGBUF_TYPE_NOSPLIT);
    assert(log_ringbuf);

    xTaskCreate(ws_log_sender_task, "ws_log_sender", 4096, nullptr, 5, nullptr);
    esp_log_set_vprintf(log_ws_vprintf);
}

void websocket_logger_send_ping()
{
    ESP_LOGE(TAG, "websocket_logger_send_ping called");
    std::lock_guard<std::mutex> lock(ws_mutex);
    for (int fd : ws_clients)
    {
        httpd_ws_frame_t frame;
        frame.type = HTTPD_WS_TYPE_PING;
        frame.final = true;
        frame.payload = nullptr;
        frame.len = 0;
        httpd_ws_send_frame_async(ws_server, fd, &frame);
    }
}
