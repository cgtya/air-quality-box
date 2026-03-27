#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/rtc.h>

#include "System.h"

static const char* TAG = "System";

void enter_download_mode(void)
{
    ESP_LOGI(TAG,"DOWNLOAD MODE RESET");
    vTaskDelay(pdMS_TO_TICKS(100));
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
    return;
}

QueueHandle_t mem_free_s;
QueueHandle_t mem_free_m;

void diag_task(void* arg)
{
    uint8_t count = 0;

    uint32_t sum = 0;

    mem_free_s = xQueueCreate(10,sizeof(size_t));
    mem_free_m = xQueueCreate(10,sizeof(size_t));

    while (1)
    {
        size_t buf;

        vTaskDelay(pdMS_TO_TICKS(1000));

        // check if the queue is full
        if (uxQueueSpacesAvailable(mem_free_s) > 1)
        {
            // take the oldest value
            xQueueReceive(mem_free_s,&buf,pdMS_TO_TICKS(100));
        }

        // get the latest free heap size
        buf = heap_caps_get_free_size(MALLOC_CAP_8BIT);

        // send to seconds queue
        xQueueSendToBack(mem_free_s, &buf, pdMS_TO_TICKS(100));


        if (count % 10 == 0)
        {
            ESP_LOGI(TAG, "diag_task free heap size last sec:%u", buf);
        }

        // add free size to sum each second for minutes average;
        if (count < 60)
        {
            sum+=buf;
            count++;
        }
        else
        {
            // check if the queue is full
            if (uxQueueSpacesAvailable(mem_free_m) > 1)
            {
                // take the oldest value
                xQueueReceive(mem_free_m,&buf,pdMS_TO_TICKS(100));
            }
            sum = sum / count;
            xQueueSendToBack(mem_free_m, &sum, pdMS_TO_TICKS(100));

            // TODO this returns garbage!
            ESP_LOGI(TAG, "diag_task free heap size min:%u", sum);

            sum = 0;
            count = 0;
        }
    }
    
    vTaskDelete(NULL);
}