#pragma once
#include <queue>
#include <functional>
#include <cstdint>
#include <map>


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
private:
    void try_send_next();

    std::queue<message_payload> queue;
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

