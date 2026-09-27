#include "external_node_queue.h"

#include <cinttypes>

#include "common/log_common.h"

static const char *TAG = "EXT_NODE_Q";

// Mesh stack has shown instability with no gap between back-to-back sends.
static constexpr uint64_t SEND_GAP_US = 300 * 1000;
// Only fires if the stack never calls back at all — it normally reports its own
// timeout after EXTERNAL_SEND_ACK_TIMEOUT_MS.
static constexpr uint64_t ACK_GUARD_US = (EXTERNAL_SEND_ACK_TIMEOUT_MS + 1500) * 1000ULL;
// Beyond this, something's likely stuck rather than just briefly queued.
static constexpr size_t BACKLOG_WARN_THRESHOLD = 10;

external_node_queue_t &external_node_queue()
{
    static external_node_queue_t instance;
    return instance;
}

static esp_timer_handle_t create_timer(esp_timer_cb_t callback, void *arg, const char *name)
{
    const esp_timer_create_args_t args = {
        .callback = callback,
        .arg = arg,
        .dispatch_method = ESP_TIMER_TASK,
        .name = name,
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer = nullptr;
    return esp_timer_create(&args, &timer) == ESP_OK ? timer : nullptr;
}

void external_node_queue_t::enqueue(external_send_t item)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (item.ack_addr != 0)
    {
        // Superseded before it was even sent: drop the old one and queue the new one at
        // the back. Replacing it in place would reorder it against that node's other
        // queued commands (ON+brightness then OFF would end with the light back on).
        for (auto it = queue_.begin(); it != queue_.end(); ++it)
        {
            if (it->ack_addr == item.ack_addr && it->ack_opcode == item.ack_opcode)
            {
                queue_.erase(it);
                break;
            }
        }
    }
    queue_.push_back(std::move(item));
    if (queue_.size() > BACKLOG_WARN_THRESHOLD)
    {
        LOG_WARN(TAG, "Backlog: %u sends queued", (unsigned)queue_.size());
    }
    if (!busy_)
    {
        dispatch(lock);
    }
}

void external_node_queue_t::drop_pending_for(uint16_t addr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(queue_, [addr](const external_send_t &queued) { return queued.ack_addr == addr; });
    if (retry_ && retry_->ack_addr == addr)
    {
        retry_.reset();
    }
}

void external_node_queue_t::dispatch(std::unique_lock<std::mutex> &lock)
{
    external_send_t item;
    if (retry_)
    {
        item = std::move(*retry_);
        retry_.reset();
    }
    else if (!queue_.empty())
    {
        item = std::move(queue_.front());
        queue_.pop_front();
    }
    else
    {
        busy_ = false;
        return;
    }
    busy_ = true;

    // Mark it in flight before sending: its ack may come back (on_send_complete) before
    // we re-take the lock below.
    auto send = item.send;
    const bool acknowledged = item.ack_addr != 0;
    const uint32_t gen = ++send_gen_;
    if (acknowledged)
    {
        inflight_ = std::move(item);
        inflight_gen_ = gen;
    }

    lock.unlock();
    const esp_err_t err = send();
    lock.lock();

    if (!acknowledged)
    {
        if (err != ESP_OK)
        {
            LOG_WARN(TAG, "Unacknowledged external send failed (err %d)", err);
        }
        start_gap(lock);
        return;
    }
    if (!inflight_ || inflight_gen_ != gen)
    {
        return; // already completed by on_send_complete
    }
    if (err != ESP_OK)
    {
        external_send_t failed = std::move(*inflight_);
        inflight_.reset();
        finish(lock, std::move(failed), false);
        return;
    }
    if (!guard_timer_)
    {
        guard_timer_ = create_timer(&external_node_queue_t::guard_timer_cb, this, "ext_node_q_ack");
    }
    if (guard_timer_)
    {
        esp_timer_stop(guard_timer_);
        guard_gen_ = gen;
        esp_timer_start_once(guard_timer_, ACK_GUARD_US);
    }
}

void external_node_queue_t::finish(std::unique_lock<std::mutex> &lock, external_send_t item, bool acked)
{
    if (!acked)
    {
        if (item.retries_left > 0)
        {
            item.retries_left--;
            LOG_WARN(TAG, "No ack from 0x%04x for opcode 0x%04" PRIx32 ", retrying (%u left)",
                     item.ack_addr, item.ack_opcode, item.retries_left);
            retry_ = std::move(item);
        }
        else
        {
            LOG_ERROR(TAG, "No ack from 0x%04x for opcode 0x%04" PRIx32 ", giving up", item.ack_addr, item.ack_opcode);
        }
    }
    start_gap(lock);
}

void external_node_queue_t::start_gap(std::unique_lock<std::mutex> &lock)
{
    if (!gap_timer_)
    {
        gap_timer_ = create_timer(&external_node_queue_t::gap_timer_cb, this, "ext_node_q_gap");
    }
    // Without the timer nothing would ever dispatch again, stalling the queue for good.
    if (!gap_timer_ || esp_timer_start_once(gap_timer_, SEND_GAP_US) != ESP_OK)
    {
        LOG_ERROR(TAG, "Gap timer unavailable, dispatching next send immediately");
        dispatch(lock);
    }
}

void external_node_queue_t::on_send_complete(uint16_t addr, uint32_t opcode, bool acked)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!inflight_ || inflight_->ack_addr != addr || inflight_->ack_opcode != opcode)
    {
        return;
    }
    if (guard_timer_)
    {
        esp_timer_stop(guard_timer_);
    }
    external_send_t item = std::move(*inflight_);
    inflight_.reset();
    finish(lock, std::move(item), acked);
}

void external_node_queue_t::gap_timer_cb(void *arg)
{
    auto *self = static_cast<external_node_queue_t *>(arg);
    std::unique_lock<std::mutex> lock(self->mutex_);
    self->dispatch(lock);
}

void external_node_queue_t::guard_timer_cb(void *arg)
{
    auto *self = static_cast<external_node_queue_t *>(arg);
    std::unique_lock<std::mutex> lock(self->mutex_);
    if (!self->inflight_ || self->inflight_gen_ != self->guard_gen_)
    {
        return; // stale: that send already completed
    }
    LOG_WARN(TAG, "No callback for send to 0x%04x, treating as a timeout", self->inflight_->ack_addr);
    external_send_t item = std::move(*self->inflight_);
    self->inflight_.reset();
    self->finish(lock, std::move(item), false);
}
