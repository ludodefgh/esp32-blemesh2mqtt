#pragma once

#include "esp_log.h"

// Standardized logging macros with function name and line number
#define LOG_ERROR(tag, format, ...) ESP_LOGE(tag, "[%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(tag, format, ...)  ESP_LOGW(tag, "[%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(tag, format, ...)  ESP_LOGI(tag, "[%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(tag, format, ...) ESP_LOGD(tag, "[%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_VERBOSE(tag, format, ...) ESP_LOGV(tag, "[%s:%d] " format, __func__, __LINE__, ##__VA_ARGS__)

// Conditional logging - only log errors and warnings
#define LOG_ERROR_IF(condition, tag, format, ...) do { \
    if (condition) LOG_ERROR(tag, format, ##__VA_ARGS__); \
} while(0)

#define LOG_WARN_IF(condition, tag, format, ...) do { \
    if (condition) LOG_WARN(tag, format, ##__VA_ARGS__); \
} while(0)

// Progress logging for operations with known total steps
#define LOG_PROGRESS(tag, current, total, format, ...) \
    LOG_INFO(tag, "Progress %d/%d " format, current, total, ##__VA_ARGS__)
    