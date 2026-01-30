#pragma once

#include <esp_err.h>
#include <u8g2_esp32_hal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern u8g2_t u8g2;
extern SemaphoreHandle_t u8g2_mutex;

esp_err_t init_display();