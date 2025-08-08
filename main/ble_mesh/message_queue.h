#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <queue>

#include <esp_timer.h>

#include "ble_mesh_node.h"

enum class message_type_t : uint8_t
{
    ble_mesh_message,
    mqtt_message,
};

struct message_payload
{
    std::function<void(std::shared_ptr<bm2mqtt_node_info> &node)> send;
    uint32_t opcode;
    uint8_t retries_left = 3;
    message_type_t type = message_type_t::ble_mesh_message;
};

class node_message_queue
{
public:
    void enqueue(const message_payload &msg);
    void handle_ack(uint32_t opcode);
    void handle_timeout(uint32_t opcode);

    size_t size() const { return queue.size(); }
    bool is_waiting() const { return waiting; }
    const std::queue<message_payload> &get_queue() const { return queue; }
    void set_node(std::shared_ptr<bm2mqtt_node_info> &in_node);

private:
    void try_send_next();

    void ensure_failsafe_timer();
    static void failsafe_callback(void *arg);
    void on_failsafe_trigger();

    std::shared_ptr<bm2mqtt_node_info> node;
    std::queue<message_payload> queue;
    esp_timer_handle_t failsafe_timer = nullptr;
    bool waiting = false;
};

class message_queue_manager
{
public:
    void enqueue(std::shared_ptr<bm2mqtt_node_info> node, const message_payload &msg);
    void handle_ack(const std::shared_ptr<bm2mqtt_node_info>& node, uint32_t opcode);
    void handle_timeout(const std::shared_ptr<bm2mqtt_node_info>& node, uint32_t opcode);

    void print_debug() const;
    void clear_queue(const std::shared_ptr<bm2mqtt_node_info>& node);

private:
    std::map<std::shared_ptr<bm2mqtt_node_info>, node_message_queue> node_queues;
};

message_queue_manager &message_queue();
