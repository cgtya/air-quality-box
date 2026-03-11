#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "driver/spi_common.h"
#include "driver/gpio.h"


#include "Config.h"
#include "Devices.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"

static const char* TAG = "Storage";

static sdmmc_host_t sd_host_conf = SDSPI_HOST_DEFAULT();
static sdmmc_card_t* sd_card_ptr;
static sdspi_device_config_t sdspi_conf = SDSPI_DEVICE_CONFIG_DEFAULT();
static const char mount_point[] = "/sdcard";
SemaphoreHandle_t sdcard_mutex = NULL;

static uint8_t sd_write_errors = 0;

bool get_sd_health = false;

esp_err_t prepare_sdspi()
{
    // spi bus config
    spi_bus_config_t spi_bus_conf = { .miso_io_num = SPI2_MISO,
                                      .mosi_io_num = SPI2_MOSI,
                                      .sclk_io_num = SPI2_SCK,
                                      .quadhd_io_num = -1,
                                      .quadwp_io_num = -1 ,
                                      .max_transfer_sz = 0 };

    // initialize spi bus
    esp_err_t err;
    err = spi_bus_initialize(SPI2_HOST, &spi_bus_conf, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "prepare_sdspi: spi2 bus couldnt be initialized!");
        return ESP_FAIL;
    }

    // modify sdspi defaults
    sdspi_conf.gpio_cs = SPI2_CS;
    sdspi_conf.host_id = sd_host_conf.slot;

    // create mutex for sdcard
    sdcard_mutex = xSemaphoreCreateMutex();
    if (sdcard_mutex == NULL)
    {
        ESP_LOGE(TAG, "prepare_sdspi: couldnt create sdcard mutex!");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mount_sdspi(bool get_health)
{
    esp_vfs_fat_mount_config_t vfs_fat_mnt_conf = { .format_if_mount_failed = false,
                                                    .max_files = 2,
                                                    .allocation_unit_size = 0,
                                                    .use_one_fat = false,
                                                    .disk_status_check_enable = get_health };

    esp_err_t err;
    err = esp_vfs_fat_sdspi_mount(mount_point, &sd_host_conf, &sdspi_conf, &vfs_fat_mnt_conf, &sd_card_ptr);
    if (err != ESP_OK)
    {
        if (err == ESP_FAIL) 
        {
            ESP_LOGE(TAG, "mount_sdspi: Failed to mount file system. try formatting the card!");
        }
        else
        {
            ESP_LOGE(TAG, "mount_sdspi: Failed to initialize the sd card!");
        }
        return err;
    }

    return ESP_OK;
}

esp_err_t unmount_sdspi()
{
    esp_err_t err;
    err = esp_vfs_fat_sdcard_unmount(mount_point,sd_card_ptr);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "unmount_sdspi: sd card unmount failed!");
    }
    return err;
}

void data_logging_task(void* arg);

void toggle_data_logging(void)
{
    // if already data logging, stop logging
    if (data_logging)
    {
        // this will automatically stop data logging task safely
        data_logging = false;
    }
    else    // start logging if not
    {
        // check if sd card is available
        if (mount_sdspi(get_sd_health) != ESP_OK)
        {
            ESP_LOGE(TAG, "toggle_data_logging: sd card not available");
            data_logging = false;
            return;
        }

        if (unmount_sdspi() != ESP_OK)
        {
            ESP_LOGE(TAG, "toggle_data_logging: sd card couldnt be unmounted");
            data_logging = false;
            return;
        }

        if (rtc_batt_dead)
        {
            ESP_LOGE(TAG, "toggle_data_logging: rtc battery died! set time/date");
            data_logging = false;
            return;
        }
        
        if (data_queue == NULL)
        {
            ESP_LOGE(TAG, "toggle_data_logging: data queue not initialized properly! reboot system");
            data_logging = false;
            return;
        }

        // reset write error counter
        sd_write_errors = 0;

        // start logging task
        data_logging = true;
        BaseType_t err;
        err = xTaskCreatePinnedToCore(data_logging_task,"data_loggging_task",4096,NULL,3,NULL,tskNO_AFFINITY);
        if (err != pdTRUE)
        {
            ESP_LOGE(TAG, "toggle_data_logging: couldnt start data_logging_task!");
            data_logging = false;
            return;
        }
        return;
    }
}


// used to check if the day changed during
// the queue write operation
// to stop opening and closing the file and mounting sd repeatedly
// 0 == card not mounted
// 4 == card mounted and yyyy-mm-04 file is open
static uint8_t last_w_day = 0;

/**
 * data_queue items:
 * 
 * YEAR: int
 * MONTH: int
 * DAY: int
 * HOUR: int
 * MINUTE: int
 * SECOND: int
 * 
 * CO2: uint16_t
 * 
 * HUMIDITY: float
 * TEMP: float
 * PM1p0: float
 * PM2p5: float
 * PM4p0: float
 * PM10p0: float
 * VOC: float
 * 
 * total 14
 */

static int buf_year = -1;
static int buf_month = -1;
static int buf_day = -1;
static int buf_hour = -1;
static int buf_minute = -1;
static int buf_second = -1;

static uint16_t buf_co2 = 0;

static float buf_humidity = -1;
static float buf_temp = -1;
static float buf_pm1p0 = -1;
static float buf_pm2p5 = -1;
static float buf_pm4p0 = -1;
static float buf_pm10p0 = -1;
static float buf_voc = -1;

static FILE* f = NULL;

static char file_path[30];

// mounts the sd if not mounted, creates the file if not created,
static esp_err_t send_buffer_to_sd()
{
    // if card isnt mounted, mount the card
    if (last_w_day == 0) mount_sdspi(get_sd_health);

    // if day changed, open new file
    if (last_w_day != buf_day)
    {
        // if sd was already mounted but day changed, close the file
        if (last_w_day != 0) 
        {
            fclose(f);
            f = NULL;
        }

        // check if year folder exists. if not, create
        struct stat st;
        
        sprintf(file_path, "/sdcard/%04d",buf_year);
        if (stat(file_path, &st) == -1) 
        {
            if (mkdir(file_path, 0700) != 0)
            {
                ESP_LOGE(TAG, "send_buffer_to_sd: Error while trying to create the folder %s", file_path);
                return ESP_FAIL;
            }
        }

        bool file_created = false;

        // build the file path
        sprintf(file_path, "/sdcard/%04d/%02d-%02d.csv",buf_year,buf_month,buf_day);

        // check if it exists first
        if (stat(file_path, &st) == -1) file_created = true;

        // open the day file
        f = fopen(file_path,"a");
        
        // null check
        if (f == NULL)
        {
            ESP_LOGE(TAG, "send_buffer_to_sd: error while trying to open the file %s",file_path);
            return ESP_FAIL;
        }

        // update the latest open file indicator
        last_w_day = buf_day;
        
        // if the file was just created, print csv header
        if (file_created)
        {
            int err = 0;
            err = fprintf(f,"Time,RelHumidity,Temperature,PM10,PM4,PM2.5,PM1,VOCIndex,CO2\n");
            if (err < 0)
            {
                ESP_LOGE(TAG, "send_buffer_to_sd: error while printing csv file header");
                return ESP_FAIL;
            }

            ESP_LOGI(TAG, "send_buffer_to_sd: file does not exist, file created and csv header printed");
        }

    } // if day changed, open new file

    // go to the end of the file
    fseek(f,0,SEEK_END);

    // write time
    fprintf(f, "%02u.%02u.%02u,", buf_hour, buf_minute, buf_second);

    // write sensor data, leave field empty if no new data available
    if (buf_humidity != -1) { fprintf(f, "%.2f", buf_humidity); buf_humidity = -1; }
    fprintf(f, ",");

    if (buf_temp != -1) { fprintf(f, "%.2f", buf_temp); buf_temp = -1; }
    fprintf(f, ",");

    if (buf_pm10p0 != -1) { fprintf(f, "%.1f", buf_pm10p0); buf_pm10p0 = -1; }
    fprintf(f, ",");

    if (buf_pm4p0 != -1) { fprintf(f, "%.1f", buf_pm4p0); buf_pm4p0 = -1 ; }
    fprintf(f, ",");

    if (buf_pm2p5 != -1) { fprintf(f, "%.1f", buf_pm2p5); buf_pm2p5 = -1 ; }
    fprintf(f, ",");

    if (buf_pm1p0 != -1) { fprintf(f, "%.1f", buf_pm1p0); buf_pm1p0 = -1 ; }
    fprintf(f, ",");

    if (buf_voc != -1) { fprintf(f, "%.0f", buf_voc); buf_voc = -1; }
    fprintf(f, ",");

    if (buf_co2 != 0) { fprintf(f, "%d", buf_co2); buf_co2 = 0; }

    if (fprintf(f, "\n") < 0)
    {
        ESP_LOGE(TAG, "send_buffer_to_sd: error while trying to write data to sd");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static int16_t save_queue_to_sd(UBaseType_t* item_count)
{
    disp_info temp_info;
    
    int64_t temp_time = esp_timer_get_time();

    bool new_data_available = false;
    
    for (; (*item_count) > 0; (*item_count)--)
    {
        // take one item from the queue
        if (xQueueReceive(data_queue,&temp_info,pdMS_TO_TICKS(200)) != pdTRUE)
        {
            ESP_LOGE(TAG, "save_queue_to_sd: couldnt pop item from data_queue!");
            continue;
        }

        // fill the buffers with the info in the queue
        switch (temp_info.type)
        {
        case MONTH:
            buf_month = temp_info.data.month;
            break;

        case DAY:
            buf_day = temp_info.data.day;
            break;

        case HOUR:
            buf_hour = temp_info.data.hour;
            break;

        case MINUTE:
            buf_minute = temp_info.data.minute;
            break;

        case SECOND:
            buf_second = temp_info.data.second;
            break;


        case CO2:
            buf_co2 = temp_info.data.co2;
            new_data_available = true;
            break;


        case HUMIDITY:
            buf_humidity = temp_info.data.rel_humidity;
            new_data_available = true;
            break;

        case TEMP:
            buf_temp = temp_info.data.amb_temperature;
            new_data_available = true;
            break;

        case PM1p0:
            buf_pm1p0 = temp_info.data.PM1p0;
            new_data_available = true;
            break;

        case PM2p5:
            buf_pm2p5 = temp_info.data.PM2p5;
            new_data_available = true;
            break;

        case PM4p0:
            buf_pm4p0 = temp_info.data.PM4p0;
            new_data_available = true;
            break;
        
        case PM10p0:
            buf_pm10p0 = temp_info.data.PM10p0;
            new_data_available = true;
            break;

        case VOC:
            buf_voc = temp_info.data.voc_index;
            new_data_available = true;
            break;

        // the year is the first sent item every second
        // so we check the year for starting sd write operations
        case YEAR:
            // if the date buffers are empty (year is empty),
            // skip write operation for this second
            if (buf_year != -1)
            {
                if (new_data_available)
                    if (send_buffer_to_sd() != ESP_OK) sd_write_errors++;

                new_data_available = false;
            }

            buf_year = temp_info.data.year;
            break;
        } // switch
    } // for

    // close file and unmount sd after write operation is done
    fclose(f);
    f = NULL;
    if (unmount_sdspi() != ESP_OK)
    {
        ESP_LOGE(TAG, "save_queue_to_sd: couldnt unmount sd");
        return -1;
    }

    last_w_day = 0;

    temp_time = (esp_timer_get_time() - temp_time)/1000;

    ESP_LOGI(TAG, "save_queue_to_sd: time took to write to sd: %"PRId64"ms", temp_time);

    return temp_time;
} // save_queue_to_sd

void data_logging_task(void* arg)
{   
    if (rtc_batt_dead)
    {
        ESP_LOGE(TAG, "data_logging_task: rtc battery died! set time/date");
        data_logging = false;
        vTaskDelete(NULL);
    }

    UBaseType_t queue_items_count;
    while (data_logging)
    {
        // if write error threshold is exceeded, disable data logging
        if (sd_write_errors > MAX_WRITE_ERRORS) data_logging = false;

        // TODO implement data logging
        vTaskDelay(pdMS_TO_TICKS(10));

        queue_items_count = uxQueueMessagesWaiting(data_queue);

        // about 13-14 messages per second
        // should run every data_queue_size/28 seconds
        if (queue_items_count > DATA_QUEUE_SIZE/2) save_queue_to_sd(&queue_items_count);
        
    }
    
    // empty the queue
    disp_info temp;
    while (xQueueReceive(data_queue,&temp,pdMS_TO_TICKS(100)) != pdTRUE);

    // will skip the first second when data logging is restarted
    buf_year = -1;

    vTaskDelete(NULL);
}