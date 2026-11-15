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
#include "ble_mesh_provisioning.h"

static const char *TAG = "MESS_QUEUE";

message_queue_manager &message_queue()
{
    static message_queue_manager instance;
    return instance;
}

// State Sets carry absolute values and status publishes read live state, so only the
// newest queued one matters.
static bool is_superseded_by_newer(uint32_t opcode)
{
    switch (opcode)
    {
    case STATUS_PUBLISH_OPCODE:
    case ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET:
    case ESP_BLE_MESH_MODEL_OP_GEN_LEVEL_SET:
    case ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET:
    case ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET:
    case ESP_BLE_MESH_MODEL_OP_LIGHT_CTL_SET:
        return true;
    default:
        return false;
    }
}

// The front message is in flight and this opcode answers it.
static bool awaits_ack(const node_message_queue &node_queue, uint32_t opcode)
{
    return node_queue.waiting && !node_queue.queue.empty() && node_queue.queue.front().opcode == opcode;
}

node_message_queue::~node_message_queue()
{
    if (failsafe_timer)
    {
        esp_timer_stop(failsafe_timer);
        esp_timer_delete(failsafe_timer);
    }
}

void message_queue_manager::enqueue(std::shared_ptr<bm2mqtt_node_info> node, const message_payload &msg)
{
    if (!node)
        return;

    LOG_DEBUG(TAG, "Enqueueing message for node 0x%04X, opcode 0x%08X", node->unicast, msg.opcode);
    std::unique_lock<std::mutex> lock(mutex);
    auto [it, inserted] = node_queues.try_emplace(node);
    if (inserted)
    {
        it->second.id = ++next_id;
    }
    auto &queue = it->second.queue;
    if (is_superseded_by_newer(msg.opcode))
    {
        // Drop the still-queued older one and queue the new one at the back: replacing it in
        // place would reorder it against other commands (HA slider bursts).
        const auto first_unsent = queue.begin() + (it->second.waiting ? 1 : 0);
        for (auto queued = first_unsent; queued != queue.end(); ++queued)
        {
            if (queued->type == msg.type && queued->opcode == msg.opcode)
            {
                queue.erase(queued);
                break;
            }
        }
    }
    queue.push_back(msg);
    dispatch(lock, node);
}

// Sends the node's next message(s) unless one is already awaiting its ack. Returns with
// the lock held; the queue may have been acked, cleared or erased while it was released.
void message_queue_manager::dispatch(std::unique_lock<std::mutex> &lock, const std::shared_ptr<bm2mqtt_node_info> &node)
{
    while (true)
    {
        auto it = node_queues.find(node);
        if (it == node_queues.end() || it->second.waiting || it->second.queue.empty())
            return;

        node_message_queue &node_queue = it->second;
        const message_payload &msg = node_queue.queue.front();
        auto send = msg.send;
        const uint32_t opcode = msg.opcode;
        const uint8_t retries_left = msg.retries_left;
        auto send_node = node;

        if (msg.type == message_type_t::mqtt_message)
        {
            node_queue.queue.pop_front();
            lock.unlock();
            send(send_node);
            lock.lock();
            continue;
        }

        // Marked in flight before sending: the ack can arrive before the lock is retaken.
        node_queue.waiting = true;
        const uint32_t id = node_queue.id;
        const uint32_t generation = ++node_queue.send_generation;
        lock.unlock();
        send(send_node);
        lock.lock();
        LOG_INFO(TAG, "Sent message with opcode 0x%08X, retries left: %u", opcode, retries_left);

        it = node_queues.find(node);
        if (it == node_queues.end() || it->second.id != id || !it->second.waiting || it->second.send_generation != generation)
            return; // already acked / timed out / cleared

        node_message_queue &in_flight = it->second;
        if (!in_flight.failsafe_timer)
        {
            esp_timer_create_args_t args = {
                .callback = &message_queue_manager::failsafe_callback,
                .arg = reinterpret_cast<void *>(static_cast<uintptr_t>(id)),
                .name = "msgqueue_failsafe"};
            esp_timer_create(&args, &in_flight.failsafe_timer);
        }
        if (in_flight.failsafe_timer)
        {
            esp_timer_stop(in_flight.failsafe_timer);
            in_flight.failsafe_generation = generation;
            esp_timer_start_once(in_flight.failsafe_timer, 10 * 1000000); // 10 sec
        }
        return;
    }
}

void message_queue_manager::failsafe_callback(void *arg)
{
    const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    auto &self = message_queue();
    std::unique_lock<std::mutex> lock(self.mutex);
    for (const auto &[key, candidate] : self.node_queues)
    {
        if (candidate.id == id)
        {
            // Re-armed while this firing waited for the lock: it belongs to an earlier send.
            if (esp_timer_is_active(candidate.failsafe_timer))
                return;
            if (candidate.waiting && candidate.failsafe_generation == candidate.send_generation && !candidate.queue.empty())
            {
                const auto node = key;
                const uint32_t opcode = candidate.queue.front().opcode;
                LOG_ERROR(TAG, "Failsafe timeout for opcode 0x%08X", opcode);
                // Without releasing the lock: an ack landing in between would let this
                // timeout hit the next in-flight message with the same opcode.
                self.on_timeout(lock, node, opcode);
            }
            return;
        }
    }
}

// A NODE_RESET ack/timeout with nothing left queued: the node goes, queue included.
void message_queue_manager::unlock_and_reset_if_done(std::unique_lock<std::mutex> &lock, const std::shared_ptr<bm2mqtt_node_info> &node, uint32_t opcode)
{
    auto it = node_queues.find(node);
    const bool reset = opcode == ESP_BLE_MESH_MODEL_OP_NODE_RESET && it != node_queues.end() && it->second.queue.empty();
    lock.unlock();
    if (reset)
    {
        LOG_WARN(TAG, "Node reset opcode 0x%08X received, removing node queue", opcode);
        ble_mesh_reset_node(node);
    }
}

void message_queue_manager::handle_ack(const std::shared_ptr<bm2mqtt_node_info>& node, uint32_t opcode)
{
    if (!node)
        return;

    LOG_INFO(TAG, "Ack for opcode 0x%08X on node 0x%04X", opcode, node->unicast);
    std::unique_lock<std::mutex> lock(mutex);
    auto it = node_queues.find(node);
    if (it == node_queues.end())
    {
        LOG_ERROR(TAG, "No message queue found for node 0x%04X", node->unicast);
        return;
    }

    node_message_queue &node_queue = it->second;
    if (awaits_ack(node_queue, opcode))
    {
        esp_timer_stop(node_queue.failsafe_timer);
        node_queue.queue.pop_front();
        node_queue.waiting = false;
        dispatch(lock, node);
    }
    else if (!node_queue.queue.empty())
    {
        LOG_WARN(TAG, "Opcode 0x%08X not found in the front [0x%08X] of the queue, waiting for next message", opcode, node_queue.queue.front().opcode);
    }

    unlock_and_reset_if_done(lock, node, opcode);
}

void message_queue_manager::handle_timeout(const std::shared_ptr<bm2mqtt_node_info>& node, uint32_t opcode)
{
    if (!node)
        return;

    LOG_WARN(TAG, "Timeout for opcode 0x%08X on node 0x%04X", opcode, node->unicast);
    std::unique_lock<std::mutex> lock(mutex);
    on_timeout(lock, node, opcode);
}

// Releases the lock.
void message_queue_manager::on_timeout(std::unique_lock<std::mutex> &lock, const std::shared_ptr<bm2mqtt_node_info> &node, uint32_t opcode)
{
    auto it = node_queues.find(node);
    if (it == node_queues.end())
    {
        lock.unlock();
        LOG_ERROR(TAG, "No message queue found for node 0x%04X", node->unicast);
        return;
    }

    node_message_queue &node_queue = it->second;
    if (awaits_ack(node_queue, opcode))
    {
        esp_timer_stop(node_queue.failsafe_timer);
        auto &msg = node_queue.queue.front();
        if (msg.retries_left > 0 && --msg.retries_left > 0)
        {
            LOG_WARN(TAG, "Retrying opcode 0x%08X", opcode);
        }
        else
        {
            LOG_ERROR(TAG, "Message dropped: opcode 0x%08X", opcode);
            node_queue.queue.pop_front();
        }
        node_queue.waiting = false;
        dispatch(lock, node);
    }

    unlock_and_reset_if_done(lock, node, opcode);
}

void message_queue_manager::clear_queue(const std::shared_ptr<bm2mqtt_node_info>& node)
{
    if (!node)
        return;

    std::lock_guard<std::mutex> lock(mutex);
    node_queues.erase(node);
}

void message_queue_manager::print_debug() const
{
    std::lock_guard<std::mutex> lock(mutex);
    LOG_INFO(TAG, "=== Message Queue Status ===");
    for (const auto &[node, node_queue] : node_queues)
    {
        LOG_INFO(TAG, "Node 0x%04X: %zu message(s), waiting: %s",
                 node->unicast, node_queue.queue.size(), node_queue.waiting ? "yes" : "no");

        // List opcodes in the queue
        if (!node_queue.queue.empty())
        {
            LOG_INFO(TAG, "    opcode: 0x%08X (retries left: %u)",
                     node_queue.queue.front().opcode, node_queue.queue.front().retries_left);
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