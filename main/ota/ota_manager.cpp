#include "ota_manager.h"

// Standard C/C++ libraries
#include <cstring>

// ESP-IDF includes
#include "esp_app_desc.h"
#include "esp_image_format.h"
#include "esp_system.h"

// Project includes
#include "common/log_common.h"

static const char *TAG = "OTA_MANAGER";

ota_manager &ota_manager::instance()
{
    static ota_manager instance;
    return instance;
}

esp_err_t ota_manager::begin_ota_update(size_t firmware_size)
{
    if (ota_in_progress_)
    {
        set_error("OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    LOG_INFO(TAG, "Starting OTA update, firmware size: %zu bytes", firmware_size);

    // Find next available OTA partition
    update_partition_ = esp_ota_get_next_update_partition(nullptr);
    if (update_partition_ == nullptr)
    {
        set_error("No available OTA partition");
        LOG_ERROR(TAG, "No available OTA partition found");
        return ESP_ERR_NOT_FOUND;
    }

    LOG_INFO(TAG, "Writing to partition subtype %d at offset 0x%lx",
             update_partition_->subtype, update_partition_->address);

    // Begin OTA operation
    esp_err_t err = esp_ota_begin(update_partition_, firmware_size, &ota_handle_);
    if (err != ESP_OK)
    {
        set_error("Failed to begin OTA");
        LOG_ERROR(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return err;
    }

    // Initialize progress tracking
    progress_info_.total_size = firmware_size;
    progress_info_.written_size = 0;
    progress_info_.progress_percent = 0;
    progress_info_.status_message = "OTA update started";
    progress_info_.update_type = update_type_t::FIRMWARE;

    ota_in_progress_ = true;
    storage_update_ = false;
    update_progress("OTA update started");

    return ESP_OK;
}

esp_err_t ota_manager::begin_storage_update(size_t storage_size)
{
    if (ota_in_progress_)
    {
        set_error("OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    LOG_INFO(TAG, "Starting storage update, size: %zu bytes", storage_size);

    // Find storage partition
    storage_partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (storage_partition_ == nullptr)
    {
        set_error("Storage partition not found");
        LOG_ERROR(TAG, "Storage partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    LOG_INFO(TAG, "Writing to storage partition at offset 0x%lx, size %lu bytes",
             storage_partition_->address, storage_partition_->size);

    // Validate size doesn't exceed partition
    if (storage_size > storage_partition_->size)
    {
        set_error("Storage size exceeds partition capacity");
        LOG_ERROR(TAG, "Storage size %zu exceeds partition size %lu", storage_size, storage_partition_->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Erase storage partition
    esp_err_t err = esp_partition_erase_range(storage_partition_, 0, storage_partition_->size);
    if (err != ESP_OK)
    {
        set_error("Failed to erase storage partition");
        LOG_ERROR(TAG, "esp_partition_erase_range failed (%s)", esp_err_to_name(err));
        return err;
    }

    // Initialize progress tracking
    progress_info_.total_size = storage_size;
    progress_info_.written_size = 0;
    progress_info_.progress_percent = 0;
    progress_info_.status_message = "Storage update started";
    progress_info_.update_type = update_type_t::STORAGE;

    ota_in_progress_ = true;
    storage_update_ = true;
    update_progress("Storage update started");

    return ESP_OK;
}

esp_err_t ota_manager::write_ota_data(const uint8_t *data, size_t size)
{
    if (!ota_in_progress_)
    {
        set_error("OTA not in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (data == nullptr || size == 0)
    {
        set_error("Invalid data parameters");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    if (storage_update_)
    {
        // Write to storage partition
        err = esp_partition_write(storage_partition_, progress_info_.written_size, data, size);
        if (err != ESP_OK)
        {
            set_error("Failed to write storage data");
            LOG_ERROR(TAG, "esp_partition_write failed (%s)", esp_err_to_name(err));
            abort_ota_update();
            return err;
        }
    }
    else
    {
        // Validate first chunk contains valid ESP32 firmware header
        if (progress_info_.written_size == 0)
        {
            if (!validate_firmware_header(data, size))
            {
                set_error("Invalid firmware header");
                LOG_ERROR(TAG, "Firmware validation failed");
                abort_ota_update();
                return ESP_ERR_INVALID_ARG;
            }
        }

        err = esp_ota_write(ota_handle_, data, size);
        if (err != ESP_OK)
        {
            set_error("Failed to write OTA data");
            LOG_ERROR(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
            abort_ota_update();
            return err;
        }
    }

    // Update progress
    progress_info_.written_size += size;
    if (progress_info_.total_size > 0)
    {
        progress_info_.progress_percent = (progress_info_.written_size * 100) / progress_info_.total_size;
        progress_info_.progress_percent = (progress_info_.progress_percent > 100) ? 100 : progress_info_.progress_percent;
    }

    char progress_msg[64];
    snprintf(progress_msg, sizeof(progress_msg), "Written %zu/%zu bytes (%d%%)",
             progress_info_.written_size, progress_info_.total_size, progress_info_.progress_percent);
    update_progress(progress_msg);

    LOG_DEBUG(TAG, "OTA write: %zu bytes, total: %zu/%zu (%d%%)",
              size, progress_info_.written_size, progress_info_.total_size, progress_info_.progress_percent);

    return ESP_OK;
}

esp_err_t ota_manager::end_ota_update()
{
    if (!ota_in_progress_)
    {
        set_error("OTA not in progress");
        return ESP_ERR_INVALID_STATE;
    }

    LOG_INFO(TAG, "Finalizing update");
    
    esp_err_t err = ESP_OK;

    if (storage_update_)
    {
        update_progress("Storage update complete");
        LOG_INFO(TAG, "Storage update completed successfully");
        
        // Reset flags
        ota_in_progress_ = false;
        storage_update_ = false;
        progress_info_.status_message = "Storage update completed";
        progress_info_.progress_percent = 100;
        
        return ESP_OK;
    }
    else
    {
        update_progress("Validating firmware...");

        err = esp_ota_end(ota_handle_);
        if (err != ESP_OK)
        {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED)
            {
                set_error("Firmware validation failed");
                LOG_ERROR(TAG, "Firmware validation failed");
            }
            else
            {
                set_error("Failed to finalize OTA");
                LOG_ERROR(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
            }
            ota_in_progress_ = false;
            return err;
        }

        // Set boot partition
        err = esp_ota_set_boot_partition(update_partition_);
        if (err != ESP_OK)
        {
            set_error("Failed to set boot partition");
            LOG_ERROR(TAG, "esp_ota_set_boot_partition failed (%s)", esp_err_to_name(err));
            ota_in_progress_ = false;
            return err;
        }

        ota_in_progress_ = false;
        progress_info_.progress_percent = 100;
        update_progress("OTA update completed successfully");

        LOG_INFO(TAG, "OTA update completed successfully. Restart required.");
        return ESP_OK;
    }
}

esp_err_t ota_manager::abort_ota_update()
{
    if (!ota_in_progress_)
    {
        return ESP_OK;
    }

    LOG_WARN(TAG, "Aborting update");
    
    // Store update type before resetting flags
    bool was_storage_update = storage_update_;
    
    if (!storage_update_)
    {
        esp_ota_abort(ota_handle_);
    }
    
    ota_in_progress_ = false;
    storage_update_ = false;

    progress_info_.status_message = was_storage_update ? "Storage update aborted" : "OTA update aborted";
    if (progress_callback_)
    {
        progress_callback_(progress_info_);
    }

    return ESP_OK;
}

void ota_manager::set_progress_callback(ota_progress_callback_t callback)
{
    progress_callback_ = callback;
}

bool ota_manager::is_ota_in_progress() const
{
    return ota_in_progress_;
}

const ota_progress_info_t &ota_manager::get_progress_info() const
{
    return progress_info_;
}

esp_err_t ota_manager::mark_app_valid()
{
    LOG_INFO(TAG, "Marking current app as valid");
    return esp_ota_mark_app_valid_cancel_rollback();
}

esp_err_t ota_manager::rollback_if_possible()
{
    const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
    const esp_partition_t *currently_running = esp_ota_get_running_partition();

    if (last_invalid_app != nullptr && last_invalid_app != currently_running)
    {
        LOG_WARN(TAG, "Rolling back to previous partition");
        esp_err_t err = esp_ota_set_boot_partition(last_invalid_app);
        if (err == ESP_OK)
        {
            esp_restart();
        }
        return err;
    }

    LOG_WARN(TAG, "No valid partition available for rollback");
    return ESP_ERR_NOT_FOUND;
}

bool ota_manager::validate_firmware_header(const uint8_t *data, size_t size)
{
    if (size < sizeof(esp_image_header_t))
    {
        LOG_ERROR(TAG, "Data too small for image header");
        return false;
    }

    const esp_image_header_t *header = (const esp_image_header_t *)data;

    // Check magic number
    if (header->magic != ESP_IMAGE_HEADER_MAGIC)
    {
        LOG_ERROR(TAG, "Invalid image magic: 0x%02x", header->magic);
        return false;
    }

    // Check chip ID
    if (header->chip_id != ESP_CHIP_ID_ESP32)
    {
        LOG_ERROR(TAG, "Invalid chip ID: %d", header->chip_id);
        return false;
    }

    LOG_INFO(TAG, "Firmware header validation passed");
    return true;
}

const char *ota_manager::get_last_error() const
{
    return last_error_;
}

void ota_manager::update_progress(const char *message)
{
    progress_info_.status_message = message;
    if (progress_callback_)
    {
        progress_callback_(progress_info_);
    }
}

void ota_manager::set_error(const char *error)
{
    strncpy(last_error_, error, sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = '\0';
    LOG_ERROR(TAG, "%s", error);
}

// C-style API implementation
extern "C"
{
    esp_err_t ota_manager_begin(size_t firmware_size)
    {
        return ota_manager::instance().begin_ota_update(firmware_size);
    }

    esp_err_t ota_manager_begin_storage(size_t storage_size)
    {
        return ota_manager::instance().begin_storage_update(storage_size);
    }

    esp_err_t ota_manager_write(const uint8_t *data, size_t size)
    {
        return ota_manager::instance().write_ota_data(data, size);
    }

    esp_err_t ota_manager_end()
    {
        return ota_manager::instance().end_ota_update();
    }

    esp_err_t ota_manager_abort()
    {
        return ota_manager::instance().abort_ota_update();
    }

    bool ota_manager_is_in_progress()
    {
        return ota_manager::instance().is_ota_in_progress();
    }

    const ota_progress_info_t *ota_manager_get_progress()
    {
        return &ota_manager::instance().get_progress_info();
    }

    void ota_manager_set_progress_callback(ota_progress_callback_t callback)
    {
        ota_manager::instance().set_progress_callback(callback);
    }

    esp_err_t ota_manager_mark_app_valid()
    {
        return ota_manager::instance().mark_app_valid();
    }
}