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

// data logging status
// TODO might be read and written at the same time from different tasks
bool data_logging = false;

// ds3231 rtc variables
static i2c_dev_t ds3231;
bool rtc_batt_dead = true;
bool rtc_available = false;

// sen5x descriptor
i2c_dev_t sen54;
bool sen5x_available = false;

void rtc_check_task(void* arg);

// set rtc module and rtc task up
static esp_err_t set_up_ds3231()
{
    esp_err_t err;

    // flush i2c dev obj
    memset(&ds3231,0,sizeof(i2c_dev_t));

    // ds3231 rtc setup
    err = ds3231_init_desc(&ds3231,I2C_NUM_0,PIN_SDA0,PIN_SCL0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Error while setting up ds3231 rtc!");
        rtc_available = false;
        return ESP_FAIL;
    }

    // update rtc battery status (to check if the timekeeping stopped)
    // TODO seems to always return true but need to check after setting time
    err = ds3231_get_oscillator_stop_flag(&ds3231,&rtc_batt_dead);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Communication error with ds3231 rtc! (while reading stop flag)");
        ds3231_free_desc(&ds3231);
        rtc_available = false;
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

    ds3231_get_time(&ds3231,&sys_time);
    ESP_LOGI(TAG,"ds3231 initialized successfully");

    // tm_mon is the number of months since january,
    // tm_year is the number of years since 1900
    ESP_LOGI(TAG,"Date / time: %d-%d-%d  %d.%d.%d",sys_time.tm_mday,sys_time.tm_mon+1,sys_time.tm_year+1900,
                                        sys_time.tm_hour,sys_time.tm_min,sys_time.tm_sec);

    rtc_available = true;
    return ESP_OK;
}

// checks the given date and sends to rtc module
bool rtc_check_and_save_date(uint8_t* nums)
{
    // nums array   0: day (1-31)   1: month (1-12)     2: year (00-99) (20xx)

    // year control 2000-2099
    // TODO should be redundant
    if ((nums[2] > 99)) return false;

    // month control 1-12
    if ((nums[1] > 12) || (nums[1] == 0)) return false;

    // day control 1-31
    if ((nums[0] > 31) || (nums[0] == 0)) return false;

    // 30 day months
    if ((nums[1] == 4) || (nums[1] == 6) || (nums[1] == 9) || (nums[1] == 11))
        if (nums[0] > 30) return false;

    // february
    if ((nums[1] == 2) && (nums[0] > 28)) return false;
    
    // create temp time struct and fill with new date values
    struct tm temp_time;
    temp_time.tm_hour = sys_time.tm_hour;
    temp_time.tm_min = sys_time.tm_min;
    temp_time.tm_sec = sys_time.tm_sec;

    temp_time.tm_year = nums[2]+100;
    temp_time.tm_mon = nums[1]-1;
    temp_time.tm_mday = nums[0];
    
    // send to rtc
    esp_err_t err;
    err = ds3231_set_time(&ds3231,&temp_time);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "rtc_check_and_save_date: Error while trying to set date!");
        return false;
    }

    ESP_LOGI(TAG, "Date set successfully! %02u.%02u.20%02u",nums[0],nums[1],nums[2]);
    return true;
}

// checks the given time and sends to rtc module
bool rtc_check_and_save_time(uint8_t* nums)
{
    if (rtc_batt_dead) ds3231_get_time(&ds3231, &sys_time);
    
    // nums array   0: hour  1: minute  2: second

    // hour control 00-23
    if ((nums[0] > 23)) return false;

    // minute control 00-59
    if ((nums[1] > 59)) return false;

    // second control 00-59
    if ((nums[2] > 59)) return false;

    // create temp time struct and fill with new time values
    struct tm temp_time;
    temp_time.tm_hour = nums[0];
    temp_time.tm_min = nums[1];
    temp_time.tm_sec = nums[2];

    temp_time.tm_year = sys_time.tm_year;
    temp_time.tm_mon = sys_time.tm_mon;
    temp_time.tm_mday = sys_time.tm_mday;
    
    // send to rtc
    esp_err_t err;
    err = ds3231_set_time(&ds3231,&temp_time);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "rtc_check_and_save_time: Error while trying to set time!");
        return false;
    }

    // clear stop flag on rtc (battery dead flag)
    err = ds3231_clear_oscillator_stop_flag(&ds3231);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "rtc_check_and_save_time: Error while trying to clear stop flag!");
        return false;
    }

    rtc_batt_dead = false;

    // start rtc task to update system time
    BaseType_t errX;
    errX = xTaskCreatePinnedToCore(rtc_check_task,"rtc_check_task",4096,NULL,3,NULL,tskNO_AFFINITY);
    if (errX != pdTRUE) 
    {
        ESP_LOGE(TAG,"Error while starting rtc_check_task!");
    }

    ESP_LOGI(TAG, "Time set successfully! %02u.%02u.%02u",nums[0],nums[1],nums[2]);
    return true;
}

// set sen5x module up
static esp_err_t set_up_sen5x()
{
    esp_err_t err;

    // flush i2c dev obj
    memset(&sen54,0,sizeof(i2c_dev_t));
    
    // sen54 sensor setup
    err = sen5x_init_descriptor(&sen54,I2C_NUM_0,PIN_SDA0,PIN_SCL0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Error while setting up sen5x sensor!");
        sen5x_available = false;
        return ESP_FAIL;
    }
    
    //start pm measurement
    err = sen5x_start_measurement(&sen54);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Communication error with sen54 sensor! (while starting measurements)");
        sen5x_available = false;
        sen5x_delete_descriptor(&sen54);
        return ESP_FAIL;
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));

    // TODO reactivate this, currently commented out for development
    //does a fan cleaning cycle on every boot
    //err = sen5x_start_fan_cleaning(&sen54);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"Communication error with sen54 sensor! (while starting fan cleaning)");
        sen5x_available = false;
        sen5x_delete_descriptor(&sen54);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,"SEN5x initialized successfully");

    sen5x_available = true;
    return ESP_OK;
}


esp_err_t set_up_devices()
{
    // create the data queue
    data_queue = xQueueCreate(70,sizeof(disp_info));
    if (data_queue == NULL)
    {
        ESP_LOGE(TAG,"Couldnt create data_queue most likely out of memory!");
        return ESP_FAIL;
    }

    i2cdev_init();

    set_up_ds3231();
    set_up_sen5x();

    ESP_LOGI(TAG,"Queue created successfully");
    return ESP_OK;
}


void rtc_check_task(void* arg)
{
    ESP_LOGI(TAG,"RTC task started");
    struct tm old_sys_time;

    // update system time 2 times every second
    // send date and time to data queue every second
    while (1)
    {
        old_sys_time = sys_time;
        ds3231_get_time(&ds3231,&sys_time);

        if ((old_sys_time.tm_sec != sys_time.tm_sec) && data_logging)
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