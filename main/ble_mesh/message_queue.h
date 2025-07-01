#pragma once
#include <queue>
#include <functional>
#include <cstdint>
#include <map>
#include <esp_timer.h>

struct message_payload {
    std::function<void()> send;
    uint32_t opcode;
    uint8_t retries_left = 3;
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
    void enqueue(uint16_t addr, const message_payload &msg);
    void handle_ack(uint16_t addr, uint32_t opcode);
    void handle_timeout(uint16_t addr, uint32_t opcode);

    void print_debug() const;

private:
    std::map<uint16_t, message_queue> node_queues;
};


message_queue_manager& message_queue();

