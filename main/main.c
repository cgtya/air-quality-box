#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include <u8g2_esp32_hal.h>

#include "Menu.h"
#include "Display.h"
#include "Rotary.h"

static const char* TAG = "main";

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(3000));

    init_display();
    rotary_pcnt_init(&rot_pcnt_unit,&rot_pcnt_chan_a,&rot_pcnt_chan_b);
    rot_but_pcnt_init(&rot_but_pcnt_unit,&rot_pcnt_chan_but);

    BaseType_t err;
    err = xTaskCreatePinnedToCore(menu_task,"menu_task",4096,NULL,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE)
    {
        ESP_LOGE(TAG,"Error while starting menu_task task");
    }
    
    vTaskDelete(NULL);
}

