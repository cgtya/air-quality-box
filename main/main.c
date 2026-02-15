#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include <u8g2_esp32_hal.h>

#include "Menu.h"
#include "Display.h"
#include "Rotary.h"
#include "Devices.h"
#include "System.h"
#include "View.h"

static const char* TAG = "main";

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(3000));

    init_display();

    set_up_devices();

    rotary_pcnt_init(&rot_pcnt_unit,&rot_pcnt_chan_a,&rot_pcnt_chan_b);
    rot_but_pcnt_init(&rot_but_pcnt_unit,&rot_pcnt_chan_but);

    BaseType_t err;

    /*
    err = xTaskCreatePinnedToCore(diag_task,"diag_task",2048,NULL,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE)
    {
        ESP_LOGE(TAG,"Error while starting diag_task task");
    }
    */

    current_display_mode = VIEW;
    err = xTaskCreatePinnedToCore(view_task,"view_task",4096,NULL,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE)
    {
        ESP_LOGE(TAG,"Error while starting view_task");
    }
    
    vTaskDelete(NULL);
}

