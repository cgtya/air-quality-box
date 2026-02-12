#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <i2cdev.h>
#include <sen5x.h>
#include <ds3231.h>

#include "Devices.h"
#include "Config.h"

static const char* TAG = "Devices";

// data queue handle
QueueHandle_t data_queue = NULL;

// system time
struct tm sys_time; 

// ds3231 rtc variables
i2c_dev_t ds3231;
bool rtc_batt_dead = true;

// sen5x descriptor
i2c_dev_t sen54;

void rtc_check_task(void* arg);

esp_err_t set_up_devices()
{
    i2cdev_init();

    // flush i2c dev obj
    memset(&ds3231,0,sizeof(i2c_dev_t));
    memset(&sen54,0,sizeof(i2c_dev_t));

    esp_err_t err;

    // ds3231 rtc setup
    err = ds3231_init_desc(&ds3231,I2C_NUM_0,PIN_SDA0,PIN_SCL0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Error while setting up ds3231 rtc!");
        return ESP_FAIL;
    }

    // update rtc battery status (to check if the timekeeping stopped)
    err = ds3231_get_oscillator_stop_flag(&ds3231,&rtc_batt_dead);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Communication error with ds3231 rtc! (while reading stop flag)");
        return ESP_FAIL;
    }

    // if rtc battery didnt die, start rtc task right away
    if (!rtc_batt_dead) 
    {
        BaseType_t err;
        err = xTaskCreatePinnedToCore(rtc_check_task,"rtc_check_task",4096,NULL,3,NULL,tskNO_AFFINITY);
        if (err != pdTRUE) 
        {
            ESP_LOGE(TAG,"Error while starting rtc_check_task!");
        }
    } 
    else
    {
        ESP_LOGI(TAG,"RTC Battery is dead!");
    }
    
    //sen54 sensor setup
    err = sen5x_init_descriptor(&sen54,I2C_NUM_0,PIN_SDA0,PIN_SCL0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Error while setting up sen5x sensor!");
        return ESP_FAIL;
    }
    
    //start pm measurement
    err = sen5x_start_measurement(&sen54);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Communication error with sen54 sensor! (while starting measurements)");
        return ESP_FAIL;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));

    //does a fan cleaning cycle on every boot
    err = sen5x_start_fan_cleaning(&sen54);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Communication error with sen54 sensor! (while starting fan cleaning)");
        return ESP_FAIL;
    }

    // create the data queue
    data_queue = xQueueCreate(70,sizeof(disp_info));
    if (data_queue == NULL)
    {
        ESP_LOGE(TAG,"Couldnt create data_queue most likely out of memory!");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG,"ds3231 and SEN5x initialized and queue created successfully");
    return ESP_OK;
}


void rtc_check_task(void* arg)
{
    ESP_LOGI(TAG,"RTC task started");
    struct tm old_sys_time;

    // update system time 2 times every second
    // send date and time to data queue every second
    while (!rtc_batt_dead)
    {
        old_sys_time = sys_time;
        ds3231_get_time(&ds3231,&sys_time);

        if (old_sys_time.tm_sec != sys_time.tm_sec)
        {
            BaseType_t err;

            disp_info temp = { .type = YEAR , .data.year = sys_time.tm_year };
            err = xQueueSendToBack(data_queue,&temp,pdMS_TO_TICKS(100));
            if (err != pdTRUE) ESP_LOGE(TAG,"Error while pushing year to data queue! (possibly full)");

            temp = (disp_info){ .type = MONTH , .data.month = sys_time.tm_mon };
            err = xQueueSendToBack(data_queue,&temp,pdMS_TO_TICKS(100));
            if (err != pdTRUE) ESP_LOGE(TAG,"Error while pushing month to data queue! (possibly full)");

            temp = (disp_info){ .type = DAY , .data.day = sys_time.tm_mday };
            err = xQueueSendToBack(data_queue,&temp,pdMS_TO_TICKS(100));
            if (err != pdTRUE) ESP_LOGE(TAG,"Error while pushing day to data queue! (possibly full)");

            temp = (disp_info){ .type = HOUR , .data.hour = sys_time.tm_hour };
            err = xQueueSendToBack(data_queue,&temp,pdMS_TO_TICKS(100));
            if (err != pdTRUE) ESP_LOGE(TAG,"Error while pushing hour to data queue! (possibly full)");

            temp = (disp_info){ .type = MINUTE , .data.minute = sys_time.tm_min };
            err = xQueueSendToBack(data_queue,&temp,pdMS_TO_TICKS(100));
            if (err != pdTRUE) ESP_LOGE(TAG,"Error while pushing minute to data queue! (possibly full)");

            temp = (disp_info){ .type = SECOND , .data.second = sys_time.tm_sec };
            err = xQueueSendToBack(data_queue,&temp,pdMS_TO_TICKS(100));
            if (err != pdTRUE) ESP_LOGE(TAG,"Error while pushing second to data queue! (possibly full)");
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG,"RTC task stoped (shouldnt be possible tho)");
    vTaskDelete(NULL);
}