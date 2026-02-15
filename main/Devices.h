#pragma once

#include <freertos/semphr.h>
#include <stdint.h>
#include <esp_log.h>
#include <i2cdev.h>
#include <time.h>

typedef enum
{
    PM1p0 = 0,
    PM2p5,
    PM4p0,
    PM10p0,
    HUMIDITY,
    TEMP,
    VOC,
    CO2,
    YEAR,
    MONTH,
    DAY,
    HOUR,
    MINUTE,
    SECOND
} disp_data_type;

typedef union
{
    float PM1p0;
    float PM2p5;
    float PM4p0;
    float PM10p0;
    float rel_humidity;
    float amb_temperature;
    float voc_index;

    uint16_t co2;

    int year;  // doesnt really matter that i hold the whole number, union is large anyways
    int month;
    int day;

    int hour;
    int minute;
    int second;

} disp_data;

typedef struct
{
    disp_data data;
    disp_data_type type;
} disp_info;

esp_err_t set_up_devices();

extern bool rtc_batt_dead;

extern i2c_dev_t sen54;

extern QueueHandle_t data_queue;
extern QueueHandle_t view_queue;


extern struct tm sys_time;
extern SemaphoreHandle_t sys_time_mutex;


bool rtc_check_and_save_date(uint8_t* nums);
bool rtc_check_and_save_time(uint8_t* nums);