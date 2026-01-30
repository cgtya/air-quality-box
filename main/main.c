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
    menu_bg_draw(&u8g2);
    menu_element_update(&u8g2);
    u8g2_SendBuffer(&u8g2);
    rotary_pcnt_init(&rot_pcnt_unit,&rot_pcnt_chan_a,&rot_pcnt_chan_b);
    rot_but_pcnt_init(&rot_but_pcnt_unit,&rot_pcnt_chan_but);

    uint8_t test0 = 0;
    BaseType_t err;
    err = xTaskCreatePinnedToCore(menu_input_handler_task,"menu_inp_handler",4096,(void*)test0,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE)
    {
        ESP_LOGE(TAG,"Error while starting menu_inp_handler task");
    }
    


    vTaskDelete(NULL);
}

