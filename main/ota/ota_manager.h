#pragma once

#include <cstddef>
#include <functional>

#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

enum class update_type_t
{
    FIRMWARE,
    STORAGE
};

struct ota_progress_info_t
{
    size_t total_size;
    size_t written_size;
    uint8_t progress_percent;
    const char *status_message;
    update_type_t update_type;
};

typedef std::function<void(const ota_progress_info_t &)> ota_progress_callback_t;

class ota_manager
{
public:
    static ota_manager &instance();

    esp_err_t begin_ota_update(size_t firmware_size);
    esp_err_t begin_storage_update(size_t storage_size);
    esp_err_t write_ota_data(const uint8_t *data, size_t size);
    esp_err_t end_ota_update();
    esp_err_t abort_ota_update();

    void set_progress_callback(ota_progress_callback_t callback);

    bool is_ota_in_progress() const;
    const ota_progress_info_t &get_progress_info() const;

    esp_err_t mark_app_valid();
    esp_err_t rollback_if_possible();

    // Validation functions
    bool validate_firmware_header(const uint8_t *data, size_t size);
    const char *get_last_error() const;

private:
    ota_manager() = default;
    ~ota_manager() = default;
    ota_manager(const ota_manager &) = delete;
    ota_manager &operator=(const ota_manager &) = delete;

    void update_progress(const char *message);
    void set_error(const char *error);

    esp_ota_handle_t ota_handle_ = 0;
    const esp_partition_t *update_partition_ = nullptr;
    const esp_partition_t *storage_partition_ = nullptr;
    ota_progress_info_t progress_info_ = {};
    ota_progress_callback_t progress_callback_;
    bool ota_in_progress_ = false;
    bool storage_update_ = false;
    char last_error_[256] = {};
};

// C-style API for HTTP handlers
extern "C"
{
    esp_err_t ota_manager_begin(size_t firmware_size);
    esp_err_t ota_manager_begin_storage(size_t storage_size);
    esp_err_t ota_manager_write(const uint8_t *data, size_t size);
    esp_err_t ota_manager_end();
    esp_err_t ota_manager_abort();
    bool ota_manager_is_in_progress();
    const ota_progress_info_t *ota_manager_get_progress();
    void ota_manager_set_progress_callback(ota_progress_callback_t callback);
    esp_err_t ota_manager_mark_app_valid();
}