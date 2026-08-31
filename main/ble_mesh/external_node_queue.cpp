#include "external_node_queue.h"

#include "common/log_common.h"

static const char *TAG = "EXT_NODE_Q";

// Mesh stack has shown instability with no gap between back-to-back sends.
static constexpr uint64_t SEND_GAP_US = 300 * 1000;

external_node_queue_t &external_node_queue()
{
    static external_node_queue_t instance;
    return instance;
}

// Beyond this, something's likely stuck rather than just briefly queued.
static constexpr size_t BACKLOG_WARN_THRESHOLD = 10;

void external_node_queue_t::enqueue(std::function<void()> send)
{
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.push(std::move(send));
    if (queue_.size() > BACKLOG_WARN_THRESHOLD)
    {
        LOG_WARN(TAG, "Backlog: %u sends queued", (unsigned)queue_.size());
    }
    if (!waiting_)
    {
        try_send_next(lock);
    }
}

size_t external_node_queue_t::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void external_node_queue_t::try_send_next(std::unique_lock<std::mutex> &lock)
{
    if (queue_.empty())
    {
        waiting_ = false;
        return;
    }

    auto send = std::move(queue_.front());
    queue_.pop();
    waiting_ = true;

    lock.unlock();
    send();
    lock.lock();

    if (!gap_timer_)
    {
        const esp_timer_create_args_t args = {
            .callback = &external_node_queue_t::gap_timer_cb,
            .arg = this,
            .name = "ext_node_q_gap",
        };
        esp_timer_create(&args, &gap_timer_);
    }
    esp_timer_start_once(gap_timer_, SEND_GAP_US);
}

void external_node_queue_t::gap_timer_cb(void *arg)
{
    auto *self = static_cast<external_node_queue_t *>(arg);
    std::unique_lock<std::mutex> lock(self->mutex_);
    self->try_send_next(lock);
}
