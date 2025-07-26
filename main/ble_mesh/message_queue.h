#pragma once
#include <queue>
#include <functional>
#include <cstdint>
#include <map>
#include <esp_timer.h>
#include "ble_mesh_node.h"

enum class message_type_t : uint8_t
{
    ble_mesh_message,
    mqtt_message,
};

struct message_payload {
    std::function<void()> send;
    uint32_t opcode;
    uint8_t retries_left = 3;
    message_type_t type = message_type_t::ble_mesh_message;
};

class message_queue {
public:
    void enqueue(const message_payload &msg);
    void handle_ack(uint32_t opcode);
    void handle_timeout(uint32_t opcode);

    size_t size() const { return queue.size(); }
    bool is_waiting() const { return waiting; }
    const std::queue<message_payload> &get_queue() const { return queue; }
private:
    void try_send_next();

     void ensure_failsafe_timer();
    static void failsafe_callback(void *arg);
    void on_failsafe_trigger();

    std::queue<message_payload> queue;
    esp_timer_handle_t failsafe_timer = nullptr;
    bool waiting = false;
};

class message_queue_manager {
public:
    void enqueue(bm2mqtt_node_info* node, const message_payload &msg);
    void handle_ack(bm2mqtt_node_info* node, uint32_t opcode);
    void handle_timeout(bm2mqtt_node_info* node, uint32_t opcode);

    void print_debug() const;
    void clear_queue(bm2mqtt_node_info* node);

private:
    std::map<bm2mqtt_node_info*, message_queue> node_queues;
};


message_queue_manager& message_queue();

