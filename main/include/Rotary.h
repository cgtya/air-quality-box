#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/pulse_cnt.h>

#include "Config.h"

extern pcnt_unit_handle_t rot_pcnt_unit;

extern pcnt_unit_handle_t rot_but_pcnt_unit;

extern SemaphoreHandle_t rotary_mutex;

/**
 * @brief starts the pulse counter unit for the rotary encoder 
 * 
 * @param pcnt_unit_ptr pcnt unit pointer (pass in empty handle)
 */
esp_err_t rotary_pcnt_init(pcnt_unit_handle_t* pcnt_unit_ptr);

/**
 * @brief starts the pulse counter unit for the rotary encoder button
 * 
 * @param pcnt_unit_ptr pcnt unit pointer (pass in empty handle)
 */
esp_err_t rot_but_pcnt_init(pcnt_unit_handle_t* pcnt_unit_ptr);