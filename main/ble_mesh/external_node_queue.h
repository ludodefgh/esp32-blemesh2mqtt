#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>

#include "esp_err.h"
#include "esp_timer.h"

// Serializes outgoing BLE Mesh sends for External Mesh Nodes (ble_mesh_control.h) —
// the counterpart of message_queue() (which is keyed by provisioned node and DevKey).
// Acknowledged unicast sends wait for their ack and are retried on timeout/error, so
// a lost ack neither drops the command nor makes the next one fail with "Busy";
// group/unacknowledged sends just keep a gap between sends.
// Client msg_timeout for acknowledged unicast external sends. Acks normally land in
// ~0.1-0.3 s; shorter than MSG_TIMEOUT (4 s) so a lost ack is retried quickly.
static constexpr int32_t EXTERNAL_SEND_ACK_TIMEOUT_MS = 2000;

struct external_send_t
{
    std::function<esp_err_t()> send; // performs the send; returns the API result
    uint16_t ack_addr = 0;           // unicast destination if acknowledged, else 0
    uint32_t ack_opcode = 0;         // opcode the client callback reports for this send
    uint8_t retries_left = 3;
    bool orphaned = false;           // its node was forgotten while in flight: ignore the reply
};

class external_node_queue_t
{
public:
    // A still-queued acknowledged send to the same addr+opcode is dropped in favour of
    // this one, queued at the back (latest value wins — e.g. an HA slider).
    void enqueue(external_send_t item);

    // Drops every queued send to `addr` — e.g. a node being forgotten — and orphans the
    // one in flight, so its late reply doesn't bring the node back.
    void drop_pending_for(uint16_t addr);

    // From the client model callbacks, for every response, timeout or send error.
    // Ignored unless it matches the send currently awaiting its ack. Returns true if
    // that send was orphaned (its reply should be dropped).
    bool on_send_complete(uint16_t addr, uint32_t opcode, bool acked);

private:
    // All take the caller's lock; dispatch() releases it while calling send().
    void dispatch(std::unique_lock<std::mutex> &lock);
    void finish(std::unique_lock<std::mutex> &lock, external_send_t item, bool acked);
    void start_gap(std::unique_lock<std::mutex> &lock);
    static void gap_timer_cb(void *arg);
    static void guard_timer_cb(void *arg);

    std::mutex mutex_;
    std::deque<external_send_t> queue_;
    std::optional<external_send_t> inflight_; // sent, awaiting its ack
    uint32_t inflight_gen_ = 0;               // which dispatch() inflight_ belongs to
    uint32_t send_gen_ = 0;
    uint32_t guard_gen_ = 0;                  // which send the ack guard timer is armed for
    std::optional<external_send_t> retry_;    // to resend after the gap
    bool busy_ = false;                       // a send is in flight or a gap is pending
    esp_timer_handle_t gap_timer_ = nullptr;
    esp_timer_handle_t guard_timer_ = nullptr;
};

external_node_queue_t &external_node_queue();
