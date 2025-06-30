#include "message_queue.h"
#include "esp_log.h"
#include <map>
static const char *TAG = "MessageQueue";

message_queue_manager &message_queue()
{
    static message_queue_manager instance;
    return instance;
}

void message_queue::enqueue(const message_payload &msg)
{
    queue_.push(msg);
    if (!waiting_)
    {
        try_send_next();
    }
}

void message_queue::try_send_next()
{
    if (queue_.empty())
        return;

    auto &msg = queue_.front();
    msg.send();
    waiting_ = true;
}

void message_queue::handle_ack(uint32_t opcode)
{
    if (!queue_.empty() && queue_.front().opcode == opcode)
    {
        queue_.pop();
        waiting_ = false;
        try_send_next();
    }
    ESP_LOGW(TAG, "Ack received for opcode 0x%08X", opcode);
    ESP_LOGW(TAG, "Current queue size: %zu", queue_.size());
}

void message_queue::handle_timeout(uint32_t opcode)
{
    if (queue_.empty())
        return;

    auto &msg = queue_.front();
    if (msg.opcode == opcode)
    {
        if (--msg.retries_left > 0)
        {
            ESP_LOGW(TAG, "Retrying opcode 0x%08X", opcode);
            waiting_ = false;
            try_send_next();
        }
        else
        {
            ESP_LOGE(TAG, "Message dropped: opcode 0x%08X", opcode);
            queue_.pop();
            waiting_ = false;
            try_send_next();
        }
    }

    ESP_LOGW(TAG, "Timeout for opcode 0x%08X", opcode);
    ESP_LOGW(TAG, "Current queue size: %zu", queue_.size());
}

void message_queue_manager::enqueue(uint16_t addr, const message_payload &msg)
{
    ESP_LOGI(TAG, "Enqueueing message for node 0x%04X, opcode 0x%08X", addr, msg.opcode);
    node_queues[addr].enqueue(msg);
}

void message_queue_manager::handle_ack(uint16_t addr, uint32_t opcode)
{
    ESP_LOGI(TAG, "Ack for opcode 0x%08X on node 0x%04X", opcode, addr);
    auto it = node_queues.find(addr);
    if (it != node_queues.end())
    {
        it->second.handle_ack(opcode);
    }
    else
    {
        ESP_LOGE(TAG, "No message queue found for node 0x%04X", addr);
    }
}

void message_queue_manager::handle_timeout(uint16_t addr, uint32_t opcode)
{
    ESP_LOGW(TAG, "Timeout for opcode 0x%08X on node 0x%04X", opcode, addr);
    auto it = node_queues.find(addr);
    if (it != node_queues.end())
    {
        it->second.handle_timeout(opcode);
    }
    else
    {
        ESP_LOGE(TAG, "No message queue found for node 0x%04X", addr);
    }
}