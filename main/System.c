#include <esp_log.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/rtc.h>

#include "Menu.h"

static const char* TAG = "System";

void enter_download_mode(void)
{
    ESP_LOGI(TAG,"DOWNLOAD MODE RESET");
    
    vTaskDelay(pdMS_TO_TICKS(100)); 

    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
    return;
}