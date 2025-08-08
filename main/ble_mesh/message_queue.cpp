#include "message_queue.h"

// Standard C/C++ libraries
#include <map>

// ESP-IDF includes
#include <esp_ble_mesh_defs.h>
#include <esp_console.h>
#include <esp_timer.h>

// Project includes
#include "common/log_common.h"
#include "debug/console_cmd.h"
#include "debug/debug_commands_registry.h"

static const char *TAG = "MESS_QUEUE";

message_queue_manager &message_queue()
{
    static message_queue_manager instance;
    return instance;
}

void message_queue::enqueue(const message_payload &msg)
{
    queue.push(msg);
    if (!waiting)
    {
        try_send_next();
    }
}
void message_queue::set_node(std::shared_ptr<bm2mqtt_node_info> &in_node)
{
    if (node == nullptr)
    {
        node = in_node;
    }
}

void message_queue::try_send_next()
{
    if (queue.empty())
        return;

    auto &msg = queue.front();
    msg.send(node);
    if (msg.type == message_type_t::ble_mesh_message)
    {
        waiting = true;

        ensure_failsafe_timer();
        esp_timer_start_once(failsafe_timer, 10 * 1000000); // 10 sec
    }
    else if (msg.type == message_type_t::mqtt_message)
    {
        // For MQTT messages, we don't need a failsafe timer
        waiting = false;
        queue.pop();
        try_send_next();
    }

    LOG_INFO(TAG, "Sent message with opcode 0x%08X, retries left: %u", msg.opcode, msg.retries_left);
}

void message_queue::handle_ack(uint32_t opcode)
{
    LOG_WARN(TAG, "Ack received for opcode 0x%08X", opcode);
    if (!queue.empty() && queue.front().opcode == opcode)
    {
        esp_timer_stop(failsafe_timer);
        queue.pop();
        waiting = false;
        try_send_next();
    }
    else if (!queue.empty())
    {
        LOG_WARN(TAG, "Opcode 0x%08X not found in the front [0x%08X] of the queue, waiting for next message", opcode, queue.front().opcode);
    }

    if (queue.size() > 0)
    {
        LOG_WARN(TAG, "Current queue size: %zu", queue.size());
    }
}

void message_queue::handle_timeout(uint32_t opcode)
{
    if (queue.empty())
        return;

    auto &msg = queue.front();
    if (msg.opcode == opcode)
    {
        if (msg.retries_left > 0 && --msg.retries_left > 0)
        {
            LOG_WARN(TAG, "Retrying opcode 0x%08X", opcode);
            waiting = false;
            try_send_next();
        }
        else
        {
            LOG_ERROR(TAG, "Message dropped: opcode 0x%08X", opcode);
            esp_timer_stop(failsafe_timer);
            queue.pop();
            waiting = false;
            try_send_next();
        }
    }

    LOG_WARN(TAG, "Timeout for opcode 0x%08X", opcode);
    LOG_WARN(TAG, "Current queue size: %zu", queue.size());
}

void message_queue::ensure_failsafe_timer()
{
    if (!failsafe_timer)
    {
        esp_timer_create_args_t args = {
            .callback = &message_queue::failsafe_callback,
            .arg = this,
            .name = "msgqueue_failsafe"};
        esp_timer_create(&args, &failsafe_timer);
    }
    else
    {
        esp_timer_stop(failsafe_timer);
    }
}

void message_queue::failsafe_callback(void *arg)
{
    auto *self = static_cast<message_queue *>(arg);
    self->on_failsafe_trigger();
}

void message_queue::on_failsafe_trigger()
{
    if (!queue.empty())
    {
        LOG_ERROR(TAG, "Failsafe timeout for opcode 0x%08X", queue.front().opcode);
        handle_timeout(queue.front().opcode); // acts like timeout handler
    }
}
void message_queue_manager::enqueue(std::shared_ptr<bm2mqtt_node_info> node, const message_payload &msg)
{
    if (!node)
        return;

    LOG_DEBUG(TAG, "Enqueueing message for node 0x%04X, opcode 0x%08X", node->unicast, msg.opcode);
    auto it = node_queues.find(node);
    if (it == node_queues.end())
    {
        // Set the node in the message queue
        auto &new_queue = node_queues[node];
        new_queue.set_node(node);
        new_queue.enqueue(msg);
    }
    else
    {
        it->second.enqueue(msg);
    }
}

void message_queue_manager::handle_ack(std::shared_ptr<bm2mqtt_node_info> node, uint32_t opcode)
{
    if (!node)
        return;

    LOG_INFO(TAG, "Ack for opcode 0x%08X on node 0x%04X", opcode, node->unicast);
    auto it = node_queues.find(node);
    if (it != node_queues.end())
    {
        it->second.handle_ack(opcode);
    }
    else
    {
        LOG_ERROR(TAG, "No message queue found for node 0x%04X", node->unicast);
    }

    if (opcode == ESP_BLE_MESH_MODEL_OP_NODE_RESET && it->second.size() == 0)
    {
        // Special handling for node reset
        LOG_WARN(TAG, "Node reset opcode 0x%08X received, removing node queue", opcode);
        node_queues.erase(node);
    }
}

void message_queue_manager::handle_timeout(std::shared_ptr<bm2mqtt_node_info> node, uint32_t opcode)
{
    if (!node)
        return;

    LOG_WARN(TAG, "Timeout for opcode 0x%08X on node 0x%04X", opcode, node->unicast);
    auto it = node_queues.find(node);
    if (it != node_queues.end())
    {
        it->second.handle_timeout(opcode);
    }
    else
    {
        LOG_ERROR(TAG, "No message queue found for node 0x%04X", node->unicast);
    }

    if (opcode == ESP_BLE_MESH_MODEL_OP_NODE_RESET && it->second.size() == 0)
    {
        // Special handling for node reset
        LOG_WARN(TAG, "Node reset opcode 0x%08X received, removing node queue", opcode);
        node_queues.erase(node);
    }
}

void message_queue_manager::clear_queue(std::shared_ptr<bm2mqtt_node_info> node)
{
    if (!node)
        return;

    node_queues.erase(node);
}

void message_queue_manager::print_debug() const
{
    LOG_INFO(TAG, "=== Message Queue Status ===");
    for (const auto &entry : node_queues)
    {
        const auto &queue = entry.second;
        size_t queue_size = queue.size();
        bool waiting = queue.is_waiting();

        LOG_INFO(TAG, "Node 0x%04X: %zu message(s), waiting: %s",
                 entry.first->unicast, queue_size, waiting ? "yes" : "no");

        // List opcodes in the queue
        if (queue_size > 0)
        {
            const auto &internal_queue = queue.get_queue();
            LOG_INFO(TAG, "    opcode: 0x%08X (retries left: %u)",
                     internal_queue.front().opcode, internal_queue.front().retries_left);
        }
    }
}

int print_debug_cmd(int argc, char **argv)
{
    message_queue().print_debug();
    return 0;
}

void register_mess_queue_commands()
{
    /* Register commands */

    const esp_console_cmd_t message_queue_print_debug_cmd = {
        .command = "message_queue_print_debug",
        .help = "Print the content of the message queue",
        .hint = NULL,
        .func = &print_debug_cmd,
    };
    ESP_ERROR_CHECK(register_console_command(&message_queue_print_debug_cmd));
}

REGISTER_DEBUG_COMMAND(register_mess_queue_commands);