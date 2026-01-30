#pragma once

#include <stdint.h>


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

    uint8_t year;
    uint8_t month;
    uint8_t day;

    uint8_t hour;
    uint8_t minute;

} disp_data;

typedef struct
{
    disp_data data;
    disp_data_type type;
} disp_info;

