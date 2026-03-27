#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>

#include "ErrorLog.h"
#include "Devices.h"

static error_log_entry_t log_entries[ERROR_LOG_MAX_ENTRIES];
static uint8_t log_head = 0;   // next write position
static uint8_t log_count = 0;
static SemaphoreHandle_t log_mutex = NULL;

// save the original vprintf so we can still output to serial
static vprintf_like_t original_vprintf = NULL;

/**
 * @brief Custom vprintf hook. Intercepts all log output.
 * If the formatted message starts with 'E ' or 'W ' (error/warning prefix),
 * captures it into the ring buffer with the current RTC timestamp.
 * Always forwards to the original vprintf for serial output.
 */
static int error_log_vprintf(const char *fmt, va_list args)
{
    // copy va_list so we can use it twice
    va_list args_copy;
    va_copy(args_copy, args);

    // format into a temp buffer to check the log level prefix
    char buf[ERROR_LOG_MSG_LEN + 20];
    vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    // ESP_LOGE/W formatted output starts with: "\033[0;31mE (...) tag: message\n"
    // or, without colors: "E (12345) tag: message\n"
    // check for 'E' or 'W' after any ANSI escape sequence
    char *p = buf;

    // skip ANSI color escape: \033[0;31m
    if (p[0] == '\033') {
        char *m = strchr(p, 'm');
        if (m) p = m + 1;
    }

    char level = p[0];

    if ((level == 'E' || level == 'W') && p[1] == ' ')
    {
        // find the tag:message part - skip past "E (12345) "
        char *msg_start = strchr(p, ')');
        if (msg_start) {
            msg_start += 2; // skip ") "
        } else {
            msg_start = p + 2; // fallback
        }

        // remove trailing newline
        size_t len = strlen(msg_start);
        if (len > 0 && msg_start[len - 1] == '\n') {
            msg_start[len - 1] = '\0';
        }

        // grab timestamp from sys_time (best effort, no blocking)
        uint8_t h = 0, m = 0, s = 0;
        if (sys_time_mutex != NULL && xSemaphoreTake(sys_time_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            h = (uint8_t)sys_time.tm_hour;
            m = (uint8_t)sys_time.tm_min;
            s = (uint8_t)sys_time.tm_sec;
            xSemaphoreGive(sys_time_mutex);
        }

        // write to ring buffer (mutex protected)
        if (log_mutex != NULL && xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            error_log_entry_t *entry = &log_entries[log_head];
            strncpy(entry->message, msg_start, ERROR_LOG_MSG_LEN - 1);
            entry->message[ERROR_LOG_MSG_LEN - 1] = '\0';
            entry->hour = h;
            entry->minute = m;
            entry->second = s;
            entry->level = level;
            entry->valid = true;

            log_head = (log_head + 1) % ERROR_LOG_MAX_ENTRIES;
            if (log_count < ERROR_LOG_MAX_ENTRIES) log_count++;

            xSemaphoreGive(log_mutex);
        }
    }

    // forward to original vprintf for serial output
    if (original_vprintf) {
        return original_vprintf(fmt, args);
    }
    return vprintf(fmt, args);
}

void clear_error_buffer(void)
{
    if (log_mutex != NULL && xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        memset(log_entries, 0, sizeof(log_entries));
        log_head = 0;
        log_count = 0;
        xSemaphoreGive(log_mutex);
    }   
}

void error_log_init(void)
{
    // clear the ring buffer
    memset(log_entries, 0, sizeof(log_entries));
    log_head = 0;
    log_count = 0;

    // create mutex
    log_mutex = xSemaphoreCreateMutex();

    // install the custom vprintf hook, saving the original
    original_vprintf = esp_log_set_vprintf(error_log_vprintf);
}

uint8_t error_log_get_count(void)
{
    return log_count;
}

const error_log_entry_t* error_log_get_entry(uint8_t index)
{
    if (index >= log_count) return NULL;

    // index 0 = newest
    // newest entry is at (log_head - 1), next newest at (log_head - 2), etc.
    int pos = (int)log_head - 1 - (int)index;
    if (pos < 0) pos += ERROR_LOG_MAX_ENTRIES;

    return &log_entries[pos];
}
