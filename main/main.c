#include <u8g2_esp32_hal.h>
#include <esp_log.h>

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

    vTaskDelete(NULL);
}

