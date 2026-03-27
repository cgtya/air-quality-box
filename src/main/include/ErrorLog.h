#pragma once

#include <stdint.h>
#include <stdbool.h>

#define ERROR_LOG_MAX_ENTRIES    16
#define ERROR_LOG_MSG_LEN        64

typedef struct {
    char message[ERROR_LOG_MSG_LEN];
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    char level;     // 'E' for error, 'W' for warning
    bool valid;
} error_log_entry_t;

/**
 * @brief Initializes the error log system.
 * Creates mutex and installs custom vprintf hook via esp_log_set_vprintf().
 * Must be called after sys_time_mutex is created (after set_up_devices).
 */
void error_log_init(void);

/**
 * @brief Returns the number of stored error log entries.
 */
uint8_t error_log_get_count(void);

/**
 * @brief Gets a log entry by index.
 * @param index 0 = newest, (count-1) = oldest
 * @return Pointer to the entry, or NULL if index is out of range.
 */
const error_log_entry_t* error_log_get_entry(uint8_t index);

/**
 * @brief Clears the error log buffer.
 */
void clear_error_buffer(void);