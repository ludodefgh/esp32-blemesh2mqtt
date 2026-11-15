#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include <esp_timer.h>

#include "ble_mesh_node.h"

enum class message_type_t : uint8_t
{
    ble_mesh_message,
    mqtt_message,
};

// Optimized memory layout: 4-byte aligned members grouped together
// Reduces padding from ~10 bytes to ~2 bytes
struct message_payload
{
    std::function<void(std::shared_ptr<bm2mqtt_node_info> &node)> send;  // 32 bytes (std::function on ESP32)
    uint32_t opcode;              // 4 bytes
    uint8_t retries_left = 3;     // 1 byte
    message_type_t type = message_type_t::ble_mesh_message;  // 1 byte (uint8_t enum)
    // 2 bytes padding to align to 4-byte boundary (38 bytes total, aligned to 40)
};

// Verify structure size optimization at compile time
static_assert(sizeof(message_payload) <= 40, "message_payload has unexpected padding");

// mqtt_message opcode of a node status publish: only the newest queued one is kept.
constexpr uint32_t STATUS_PUBLISH_OPCODE = 0xFFFF0001;

struct node_message_queue
{
    node_message_queue() = default;
    node_message_queue(const node_message_queue &) = delete;
    node_message_queue &operator=(const node_message_queue &) = delete;
    ~node_message_queue();

    std::deque<message_payload> queue;
    esp_timer_handle_t failsafe_timer = nullptr;
    uint32_t id = 0;              // failsafe timer arg: survives the queue being erased, unlike `this`
    uint32_t send_generation = 0; // bumped per send, so a stale failsafe can be told apart
    uint32_t failsafe_generation = 0;
    bool waiting = false;
};

// Called from the MQTT task, the BTC task (client callbacks) and esp_timer: every access
// holds mutex, and sends / node resets run with it released (they can block or re-enter).
class message_queue_manager
{
public:
    void enqueue(std::shared_ptr<bm2mqtt_node_info> node, const message_payload &msg);
    void handle_ack(const std::shared_ptr<bm2mqtt_node_info>& node, uint32_t opcode);
    void handle_timeout(const std::shared_ptr<bm2mqtt_node_info>& node, uint32_t opcode);

    void print_debug() const;
    void clear_queue(const std::shared_ptr<bm2mqtt_node_info>& node);

private:
    void dispatch(std::unique_lock<std::mutex> &lock, const std::shared_ptr<bm2mqtt_node_info> &node);
    void on_timeout(std::unique_lock<std::mutex> &lock, const std::shared_ptr<bm2mqtt_node_info> &node, uint32_t opcode);
    void unlock_and_reset_if_done(std::unique_lock<std::mutex> &lock, const std::shared_ptr<bm2mqtt_node_info> &node, uint32_t opcode);
    static void failsafe_callback(void *arg);

    mutable std::mutex mutex;
    std::map<std::shared_ptr<bm2mqtt_node_info>, node_message_queue> node_queues;
    uint32_t next_id = 0;
};

message_queue_manager &message_queue();
