#pragma once
#include <functional>
#include <mutex>
#include <queue>

#include "esp_timer.h"

// Serializes outgoing BLE Mesh sends for external mesh nodes (ble_mesh_control.h).
// Separate from message_queue_manager: that one's built around per-node DevKey ack/retry,
// which doesn't fit group-addressed, uncorrelated, often-unacknowledged external sends.
class external_node_queue_t
{
public:
    // Runs `send` once earlier sends have dispatched and the gap has elapsed. No
    // synchronous send result — only "accepted into the queue".
    void enqueue(std::function<void()> send);

    size_t size() const;

private:
    // Caller must hold `lock`; released while invoking send() to avoid a self-deadlock
    // if a send re-enters enqueue().
    void try_send_next(std::unique_lock<std::mutex> &lock);
    static void gap_timer_cb(void *arg);

    mutable std::mutex mutex_;
    std::queue<std::function<void()>> queue_;
    esp_timer_handle_t gap_timer_ = nullptr;
    bool waiting_ = false;
};

external_node_queue_t &external_node_queue();
