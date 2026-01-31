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
    CO2
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
} disp_data;

typedef struct
{
    disp_data data;
    disp_data_type type;
} disp_info;

void view_task(void* arg);