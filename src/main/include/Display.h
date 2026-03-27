#pragma once

#include <esp_err.h>
#include <freertos/semphr.h>

#include <u8g2.h>
#include <stdatomic.h>

extern u8g2_t u8g2;
extern SemaphoreHandle_t u8g2_mutex;
extern _Atomic bool inverse_color;

esp_err_t init_display();
void switch_inverse_color(void);