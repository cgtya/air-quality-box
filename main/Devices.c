#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <i2cdev.h>
#include <stdbool.h>
#include <stdatomic.h>

#include <sen5x.h>
#include <ds3231.h>
#include <senseair-s8.h>

#include "Devices.h"
#include "Menu.h"
#include "Config.h"
#include "Storage.h"

static const char* TAG = "Devices";

// data queue handle
QueueHandle_t data_queue = NULL;

// view queue handle
QueueHandle_t view_queue = NULL;

// system time
struct tm sys_time;
SemaphoreHandle_t sys_time_mutex;

// data logging status
_Atomic bool data_logging = false;

// ds3231 rtc variables
static i2c_dev_t ds3231;
bool rtc_batt_dead = true;
bool rtc_available = false;

// sen5x descriptor
i2c_dev_t sen54;
bool sen5x_available = false;

// senseair s8 descriptor
senseair_s8_handle_t s8_sensor = NULL;
bool s8_available = false;

// ---------- DS3231 RTC ----------

// update system time 2 times every second
// send date and time to data queue every second
void rtc_check_task(void* arg)
{
    ESP_LOGI(TAG,"rtc_check_task: RTC task started");
    struct tm old_sys_time;
    rtc_batt_dead = false;

    while (1)
    {
        if (xSemaphoreTake(sys_time_mutex,pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "rtc_check_task: couldnt take sys_time mutex");
            continue;
        }

        old_sys_time = sys_time;
        ds3231_get_time(&ds3231,&sys_time);

        if ((old_sys_time.tm_sec != sys_time.tm_sec) && data_logging)
        {
            BaseType_t err;

            disp_info temp = { .type = YEAR , .data.year = sys_time.tm_year + 1900 };
            err = xQueueSendToBack(data_queue,&temp,pdMS_TO_TICKS(100));
            if (err != pdTRUE) ESP_LOGE(TAG,"Error while pushing year to data queue! (possibly full)");

            temp = (disp_info){ .type = MONTH , .data.month = sys_time.tm_mon + 1 };
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

        xSemaphoreGive(sys_time_mutex);
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG,"RTC task stopped (shouldnt be possible tho)");
    vTaskDelete(NULL);
}

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
        ESP_LOGW(TAG,"RTC Battery is dead!");
    }

    if (xSemaphoreTake(sys_time_mutex,pdMS_TO_TICKS(1000)) != pdTRUE)
        ESP_LOGE(TAG, "set_up_ds3231: couldnt take sys_time mutex (how tf). dont care going ahead");

    ds3231_get_time(&ds3231,&sys_time);

    ESP_LOGI(TAG,"ds3231 initialized successfully");
    
    // tm_mon is the number of months since january,
    // tm_year is the number of years since 1900
    ESP_LOGI(TAG,"Date / time: %d-%d-%d  %d.%d.%d",sys_time.tm_mday,sys_time.tm_mon+1,sys_time.tm_year+1900,
                                                    sys_time.tm_hour,sys_time.tm_min,sys_time.tm_sec);
        
    xSemaphoreGive(sys_time_mutex);

    rtc_available = true;
    return ESP_OK;
}

// checks the given date and sends to rtc module
bool rtc_check_and_save_date(uint8_t* nums)
{
    // cant change date when data logging is on
    if (data_logging) return false;

    // nums array   0: day (1-31)   1: month (1-12)     2: year (00-99) (20xx)

    // year control 2000-2099
    // should be redundant
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
    
    // take sys_time mutex
    if (xSemaphoreTake(sys_time_mutex,pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "rtc_check_and_save_date: couldnt take sys_time mutex.");
        return false;
    }

    // create temp time struct and fill with new date values
    struct tm temp_time;
    temp_time.tm_hour = sys_time.tm_hour;
    temp_time.tm_min = sys_time.tm_min;
    temp_time.tm_sec = sys_time.tm_sec;

    // give sys_time mutex
    xSemaphoreGive(sys_time_mutex);

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
    // cant change time when data logging is on
    if (data_logging) return false;


    // take sys_time mutex
    if (xSemaphoreTake(sys_time_mutex,pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "rtc_check_and_save_time: couldnt take sys_time mutex1.");
        return false;
    }

    // if rtc battery was dead, sys_time wont get updated automatically
    // so if any changes were made to date, they were directly saved
    // to the rtcs flash and not sys_time
    // we update the sys_time here manually
    if (rtc_batt_dead) ds3231_get_time(&ds3231, &sys_time);

    xSemaphoreGive(sys_time_mutex);

    // nums array   0: hour  1: minute  2: second
    // hour control 00-23
    if ((nums[0] > 23)) return false;

    // minute control 00-59
    if ((nums[1] > 59)) return false;

    // second control 00-59
    if ((nums[2] > 59)) return false;


    // take sys_time mutex
    if (xSemaphoreTake(sys_time_mutex,pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "rtc_check_and_save_time: couldnt take sys_time mutex2.");
        return false;
    }

    // create temp time struct and fill with new time values
    struct tm temp_time;
    temp_time.tm_hour = nums[0];
    temp_time.tm_min = nums[1];
    temp_time.tm_sec = nums[2];

    temp_time.tm_year = sys_time.tm_year;
    temp_time.tm_mon = sys_time.tm_mon;
    temp_time.tm_mday = sys_time.tm_mday;
    
    xSemaphoreGive(sys_time_mutex);

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

    // start rtc task to update system time
    // if it wasnt started at boot because of a dead battery
    if (rtc_batt_dead)
    {
        BaseType_t errX;
        errX = xTaskCreatePinnedToCore(rtc_check_task,"rtc_check_task",4096,NULL,3,NULL,tskNO_AFFINITY);
        if (errX != pdTRUE) 
        {
            ESP_LOGE(TAG,"Error while starting rtc_check_task!");
        }
    }

    ESP_LOGI(TAG, "Time set successfully! %02u.%02u.%02u",nums[0],nums[1],nums[2]);
    return true;
}

// ---------- SEN54 PM ----------

// sen5x_fan_clean function sets this to true and the task starts the fan cleaning
static bool fan_clean_request = false;

void sen5x_fan_clean(void)
{
    fan_clean_request = true;
}

// initializes the sen54 sensor and periodically reads it
void sen54_task(void* arg)
{
    esp_err_t err;

    // flush i2c dev obj
    memset(&sen54,0,sizeof(i2c_dev_t));
    
    // sen54 sensor setup
    err = sen5x_init_descriptor(&sen54,I2C_NUM_0,PIN_SDA0,PIN_SCL0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"sen54_task: Error while setting up sen5x sensor! Stopping task!");
        sen5x_available = false;
        memset(&sen54,0,sizeof(i2c_dev_t));
        vTaskDelete(NULL);
    }
    
    vTaskDelay(pdMS_TO_TICKS(200));

    //start pm measurement
    err = sen5x_start_measurement(&sen54);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"sen54_task: Communication error with sen54 sensor! (while starting measurements) Stopping task!");
        sen5x_available = false;
        sen5x_delete_descriptor(&sen54);
        memset(&sen54,0,sizeof(i2c_dev_t));
        vTaskDelete(NULL);
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));

    // TODO reactivate this, currently commented out for development
    //does a fan cleaning cycle on every boot
    //err = sen5x_start_fan_cleaning(&sen54);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,"sen54_task: Communication error with sen54 sensor! (while starting fan cleaning) Stopping task!");
        sen5x_available = false;
        sen5x_delete_descriptor(&sen54);
        memset(&sen54,0,sizeof(i2c_dev_t));
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG,"sen54_task: SEN5x initialized successfully, task STARTED");

    sen5x_available = true;

    bool data_ready_flag = false;
    sen5x_data_t sen5x_buf;
    uint32_t sensor_status;
    disp_info temp_info;

    disp_data_type sen54_data_types[] = { HUMIDITY, TEMP, PM1p0, PM2p5, PM4p0, PM10p0, VOC};

    float* sen5x_buf_ptrs[] = { &(sen5x_buf.ambient_humidity), &(sen5x_buf.ambient_temperature),
                               &(sen5x_buf.mass_concentration_pm1_0), &(sen5x_buf.mass_concentration_pm2_5),
                               &(sen5x_buf.mass_concentration_pm4_0), &(sen5x_buf.mass_concentration_pm10_0),
                               &(sen5x_buf.voc_index) };

    // twice a second, read data ready flag,
    // if available, read data and push to queues
    // if not, read sensor status
    while (sen5x_available)
    {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (fan_clean_request)
        {
            sen5x_start_fan_cleaning(&sen54);
            fan_clean_request = false;
        }
        // read data ready flag
        err = sen5x_read_data_ready(&sen54, &data_ready_flag);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG,"sen54_task: Communication error with sen54 sensor! (while reading data ready flag)");
            continue;
        }

        // while new data is not ready, check sensor status
        if (!data_ready_flag)
        {
            err = sen5x_read_device_status(&sen54,&sensor_status);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG,"sen54_task: Communication error with sen54 sensor! (while reading status)");
                continue;
            }

            err = sen5x_clear_device_status(&sen54);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG,"sen54_task: Communication error with sen54 sensor! (while clearing status)");
            }
            
            if (sensor_status & SEN5X_STATUS_FAN_ERROR)
            {
                ESP_LOGE(TAG, "sen54_task: status: Fan Failure! (refer to manual bit 4)");
            }

            if (sensor_status & SEN5X_STATUS_LASER_ERROR)
            {
                ESP_LOGE(TAG, "sen54_task: status: Laser Failure! (refer to manual bit 5)");
            }

            if (sensor_status & SEN5X_STATUS_RHT_ERROR)
            {
                ESP_LOGE(TAG, "sen54_task: status: RHT communication error! (refer to manual bit 6)");
            }

            if (sensor_status & SEN5X_STATUS_GAS_ERROR)
            {
                ESP_LOGE(TAG, "sen54_task: status: Gas sensor error! (refer to manual bit 7)");
            }

            if (sensor_status & SEN5X_STATUS_FAN_CLEANING)
            {
                ESP_LOGI(TAG, "sen54_task: status: Fan cleaning active (refer to manual bit 19)");
            }

            if (sensor_status & SEN5X_STATUS_SPEED_WARNING)
            {
                ESP_LOGW(TAG, "sen54_task: status: Fan speed out of range! (refer to manual bit 21)");
            }

            continue;
        }

        err = sen5x_read_measurement(&sen54,&sen5x_buf);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG,"sen54_task: Communication error with sen54 sensor! (while reading measurements)");
            continue;
        }

        /**
         * sen5x readings:
         * [0] - ambient humidity
         * [1] - ambient temperature
         * [2] - pm1.0
         * [3] - pm2.5
         * [4] - pm4.0
         * [5] - pm10.0
         * [6] - voc index
         */

        // push data to data logging queue if enabled
        if (data_logging)
        {
            for (uint8_t i = 0; i < 7; i++)
            {
                memcpy(&(temp_info.data),sen5x_buf_ptrs[i],sizeof(float));
                temp_info.type = sen54_data_types[i];
                if (xQueueSendToBack(data_queue,&temp_info,pdMS_TO_TICKS(200)) != pdTRUE)
                    ESP_LOGE(TAG, "sen54_task: couldnt push to data queue, timeout. probably full queue");
            }
        }

        // push data to display queue if currently in view
        if (current_display_mode == VIEW)
        {
            for (uint8_t i = 0; i < 7; i++)
            {
                memcpy(&(temp_info.data),sen5x_buf_ptrs[i],sizeof(float));
                temp_info.type = sen54_data_types[i];
                if (xQueueSendToBack(view_queue,&temp_info,pdMS_TO_TICKS(200)) != pdTRUE)
                    ESP_LOGE(TAG, "sen54_task: couldnt push to view queue, timeout. probably full queue");
            }
        }

    }

    sen5x_reset(&sen54);

    sen5x_delete_descriptor(&sen54);
    memset(&sen54,0,sizeof(i2c_dev_t));

    ESP_LOGI(TAG,"sen54_task STOPPED");
    vTaskDelete(NULL);
}

// ---------- SenseAir S8 ----------

// if true, the s8 task starts a background calibration cycle on the sensor
static bool s8_calibration_request = false;

void s8_start_calibration(void)
{
    s8_calibration_request = true;
}

void s8_task(void* arg)
{
    esp_err_t err;

    // configure the sensor
    senseair_s8_config_t config = {
        .uart_port = UART1_PORT,
        .tx_pin = UART1_TX,
        .rx_pin = UART1_RX,
        .slave_addr = SENSEAIR_S8_DEFAULT_ADDR
    };

    // initialize the sensor
    err = senseair_s8_init(&config, &s8_sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "s8_task: Failed to initialize Senseair S8");
        vTaskDelete(NULL);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    // Get ABC period
    uint16_t abc_period;
    if (senseair_s8_get_abc_period(s8_sensor, &abc_period) == ESP_OK)
    {
        ESP_LOGI(TAG, "ABC period: %u hours", abc_period);
    }
    else
    {
        ESP_LOGE(TAG, "s8_task: Failed to get ABC period. Stopping Task!");
        senseair_s8_free(s8_sensor);
        vTaskDelete(NULL);
    }
    
    s8_available = true;
    
    bool last_sent = false;
    uint16_t co2;
    uint16_t co2_old = 0;
    uint16_t status;
    
    disp_info temp;

    temp.type = CO2;

    while (s8_available)
    {
        if (s8_calibration_request)
        {
            err = senseair_s8_background_calibration(s8_sensor);
            if (err != ESP_OK) ESP_LOGE(TAG, "s8_task: Failed to start background calibration!");

            s8_calibration_request = false;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
        
        err = senseair_s8_read_all(s8_sensor,&status,&co2);
        if (err != ESP_OK) 
        {
            ESP_LOGE(TAG, "s8_task: Error while trying to read sensor!");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // if any error codes available, print
        if (status != SENSEAIR_S8_STATUS_OK)
        {
            ESP_LOGW(TAG, "s8_task: Sensor status warning (refer to datasheet): 0x%04X", status);

            if (status & SENSEAIR_S8_STATUS_FATAL_ERROR)
                ESP_LOGE(TAG, "  - Fatal error");

            if (status & SENSEAIR_S8_STATUS_OFFSET_ERROR)
                ESP_LOGE(TAG, "  - Offset error");

            if (status & SENSEAIR_S8_STATUS_ALGO_ERROR)
                ESP_LOGE(TAG, "  - Algorithm error");

            if (status & SENSEAIR_S8_STATUS_OUTPUT_ERROR)
                ESP_LOGE(TAG, "  - Output error");

            if (status & SENSEAIR_S8_STATUS_DIAG_ERROR)
                ESP_LOGE(TAG, "  - Self-diagnostic error");

            if (status & SENSEAIR_S8_STATUS_OUT_OF_RANGE)
                ESP_LOGW(TAG, "  - Out of range");

            if (status & SENSEAIR_S8_STATUS_MEMORY_ERROR)
                ESP_LOGE(TAG, "  - Memory error");
        }

        // as the sensor gets new data every 4 secs and
        // we refresh every 2 secs, we check if we have new data
        // or if we have saved the previous data
        // (in case the co2 val didnt change)
        if (!last_sent || (co2_old != co2))
        {
            last_sent = true;
            temp.data.co2 = co2;

            // crazy statement made up by the utterly deranged
            if (data_logging)
                if (xQueueSendToBack(data_queue,&temp,pdMS_TO_TICKS(1000)) != pdTRUE)
                    ESP_LOGE(TAG,"s8_task: Couldnt push to data_queue, probably full!");
            
            if (current_display_mode == VIEW)
                if (xQueueSendToBack(view_queue,&temp,pdMS_TO_TICKS(1000)) != pdTRUE)
                    ESP_LOGE(TAG,"s8_task: Couldnt push to view_queue, probably full!");
        }
        else
        {
            last_sent = false;
        }

        co2_old = co2;
    }

    senseair_s8_free(s8_sensor);

    ESP_LOGI(TAG, "s8_task STOPPED");
    vTaskDelete(NULL);
}

esp_err_t set_up_devices()
{
    // create the data queue
    data_queue = xQueueCreate(DATA_QUEUE_SIZE,sizeof(disp_info));
    if (data_queue == NULL)
    {
        ESP_LOGE(TAG,"Couldnt create data_queue most likely out of memory!");
        return ESP_FAIL;
    }

    // create the view queue
    view_queue = xQueueCreate(VIEW_QUEUE_SIZE,sizeof(disp_info));
    if (view_queue == NULL)
    {
        ESP_LOGE(TAG,"Couldnt create view_queue most likely out of memory!");
        return ESP_FAIL;
    }

    i2cdev_init();

    // create mutex for system time variable expecting multiple task access
    sys_time_mutex = xSemaphoreCreateMutex();
    if (sys_time_mutex == NULL)
    {
        ESP_LOGE(TAG, "set_up_devices: failed to create mutex for sys_time.");
        return ESP_FAIL;
    }
    
    set_up_ds3231();

    prepare_sdspi();
    
    // start sen54 task
    BaseType_t err;
    err = xTaskCreatePinnedToCore(sen54_task,"sen54_task",4096,NULL,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE) ESP_LOGE(TAG, "set_up_devices: Error while starting sen54_task!");
    
    // start s8 task
    err = xTaskCreatePinnedToCore(s8_task,"s8_task",4096,NULL,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE) ESP_LOGE(TAG, "set_up_devices: Error while starting s8_task!");

    ESP_LOGI(TAG,"data and view queues created successfully");
    return ESP_OK;
}