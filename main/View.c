#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <u8g2.h>

#include "View.h"
#include "Menu.h"
#include "Display.h"
#include "Rotary.h"
#include "Devices.h"

static const char* TAG = "View";

static uint8_t selected_view = 1;
uint8_t sleep_timer = 10;

static void view_1_airgradient_base(u8g2_t* disp_u8g2);
static void view_1_airgradient_upd(u8g2_t* disp_u8g2, disp_info* info);

static void view_update(u8g2_t* disp_u8g2)
{
    uint8_t items = uxQueueMessagesWaiting(view_queue);
    
    // if the queue is empty, return
    if (items == 0) return;
    
    disp_info temp;

    // for every item in the queue
    for (; items > 0; items--)
    {
        // take one from queue
        if (xQueueReceive(view_queue,&temp,pdMS_TO_TICKS(200)) != pdTRUE)
        {
            ESP_LOGE(TAG, "view_update: couldnt get item from view_queue, timeout!");
            continue;
        }

        //print as the selected view
        switch (selected_view)
        {
        case 1:
            view_1_airgradient_upd(disp_u8g2,&temp); 
            break;
        
        default:
            ESP_LOGE(TAG, "view_update: selected view not available!");
            break;
        }
    }

    if (xSemaphoreTake(sys_time_mutex,pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "view_update: Couldnt take sys time mutex!");
        u8g2_SendBuffer(disp_u8g2);
        return;
    }

    temp.type = HOUR;
    temp.data.hour = sys_time.tm_hour;

    //print time on selected view
    switch (selected_view)
    {
    case 1:
        view_1_airgradient_upd(disp_u8g2,&temp);
        temp.type = MINUTE;
        temp.data.minute = sys_time.tm_min;
        view_1_airgradient_upd(disp_u8g2,&temp);
        break;
    
    default:
        ESP_LOGE(TAG, "view_update: selected view not available!");
        break;
    }

    xSemaphoreGive(sys_time_mutex);

    u8g2_SendBuffer(disp_u8g2);
}

void view_task(void* arg)
{
    bool display_sleep = 0;

    //! set selected view if specified when starting the view task
    if (arg != NULL) selected_view = (uint8_t)arg;

    //! rotary pulse counter variable
    int pcnt_value = 0;

    //! take display and rotary mutexes
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"view_task: Couldnt get u8g2 display mutex");
        vTaskDelete(NULL);
    }

    if (xSemaphoreTake(rotary_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"view_task: Couldnt get rotary encoder mutex");
        xSemaphoreGive(u8g2_mutex);
        vTaskDelete(NULL);
    }

    //! write view base on display
    switch (selected_view)
    {
    case 1:
        view_1_airgradient_base(&u8g2);
        break;
    
    default:
        ESP_LOGW(TAG,"Selected view doesnt exist. Setting default view");
        selected_view = 1;
        view_1_airgradient_base(&u8g2);
        break;
    }

    u8g2_SendBuffer(&u8g2);

    ESP_LOGI(TAG,"view_task STARTED");

    // last button press time
    TickType_t last_act = xTaskGetTickCount();
    const TickType_t sleep_time_in_tick = pdMS_TO_TICKS(sleep_timer*1000);

    while (current_display_mode == VIEW)
    {
        // if display is awake and sleep isnt off (0)
        if (!display_sleep && sleep_timer)
        {
            // and if current tick is more than last tick + sleep time in ticks
            // go to sleep
            if (xTaskGetTickCount() > sleep_time_in_tick+last_act)
            {
                display_sleep = true;
                u8g2_SetPowerSave(&u8g2,1);
            }
        }
        
        // read rotary BUTTON, if screen is on switch to menu, else wake display
        pcnt_unit_get_count(rot_but_pcnt_unit,&pcnt_value);

        if (pcnt_value >= 1)
        {
            // clear pcnt
            pcnt_unit_clear_count(rot_but_pcnt_unit);
            
            if (display_sleep)
            {
                display_sleep = false;
                u8g2_SetPowerSave(&u8g2,0);
                last_act = xTaskGetTickCount();
            }
            else
            {
                // change to menu
                current_display_mode = MENU;
            }
            
        }

        // read rotary, if display is asleep wake display, always update last activation
        pcnt_unit_get_count(rot_pcnt_unit,&pcnt_value);
        if (pcnt_value >= 4 || pcnt_value <= -4)
        {
            // clear pulse counter
            pcnt_unit_clear_count(rot_pcnt_unit);

            if (display_sleep)
            {
                display_sleep = false;
                u8g2_SetPowerSave(&u8g2,0);
            }

            last_act = xTaskGetTickCount();
        }

        // update the view with newest available data
        view_update(&u8g2);
        
        // 10hz
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    //! release mutexes
    xSemaphoreGive(u8g2_mutex);
    xSemaphoreGive(rotary_mutex);
    
    //! start menu task
    esp_err_t err;
    err = xTaskCreatePinnedToCore(menu_task,"menu_task",4096,NULL,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE) ESP_LOGE(TAG,"Error while starting menu_task task");
    
    
    ESP_LOGI(TAG,"view_task DELETED");
    //! delete this task
    vTaskDelete(NULL);
}


// 'water_droplet', 5x10px
static const unsigned char water_droplet[] = {
	0x20, 0x20, 0x20, 0x70, 0x70, 0xf8, 0xf8, 0xf8, 0xf8, 0x70
};

/* UNUSED
// 'floppy_icon', 10x10px
static const unsigned char floppy_icon[] = {
	0xff, 0x00, 0xa2, 0x80, 0xbe, 0x40, 0x80, 0x40, 0x80, 0x40, 0xbe, 0x40, 0xa2, 0x40, 0xa2, 0x40, 
	0xa2, 0x40, 0xff, 0xc0
};
*/
static void view_1_airgradient_base(u8g2_t* disp_u8g2)
{
    //! clear display
    u8g2_ClearBuffer(disp_u8g2);

    //! set color
    u8g2_SetDrawColor(disp_u8g2,1);

    //! draw the borders
    u8g2_DrawHLine(disp_u8g2,0,13,128);
    u8g2_DrawVLine(disp_u8g2,36,13,51);
    u8g2_DrawVLine(disp_u8g2,87,13,51);
    u8g2_DrawHLine(disp_u8g2,87,30,41);

    
    //! pm2.5 top and bottom text
    u8g2_SetFont(disp_u8g2,u8g2_font_Wizzard_tr);
    u8g2_DrawStr(disp_u8g2,0,27,"PM2.5");
    u8g2_DrawStr(disp_u8g2,0,61,"ug/m3");
    
    //! co2 top and bottom text
    u8g2_DrawStr(disp_u8g2,39,27,"CO2");
    u8g2_DrawStr(disp_u8g2,39,61,"ppm");

    // VOC idx
    u8g2_DrawStr(disp_u8g2,93,44,"VOCs");

    // temp degree and C
    u8g2_DrawHLine(disp_u8g2,24,0,2);
    u8g2_DrawHLine(disp_u8g2,24,3,2);
    u8g2_DrawVLine(disp_u8g2,23,1,2);
    u8g2_DrawVLine(disp_u8g2,26,1,2);
    u8g2_DrawStr(disp_u8g2,28,9,"C");


    // humidity icon
    u8g2_DrawBitmap(disp_u8g2,84,0,1,10,water_droplet);
}

static void view_1_airgradient_upd(u8g2_t* disp_u8g2, disp_info* info)
{
    char buf[10];
    // differ placement and font for each different type of data
    switch (info->type)
    {
    case VOC:
        u8g2_SetDrawColor(disp_u8g2,0);
        u8g2_DrawBox(disp_u8g2,88,45,40,33);

        u8g2_SetDrawColor(disp_u8g2,1);
        u8g2_SetFont(disp_u8g2,u8g2_font_Wizzard_tr);
        sprintf(buf,"%.0f",info->data.voc_index);
        u8g2_DrawStr(disp_u8g2,93,58,buf);
        break;

    case PM2p5:
        u8g2_SetDrawColor(disp_u8g2,0);
        u8g2_DrawBox(disp_u8g2,0,28,36,20);

        u8g2_SetDrawColor(disp_u8g2,1);
        u8g2_SetFont(disp_u8g2,u8g2_font_luRS12_tr);
        sprintf(buf,"%.0f",info->data.PM2p5);
        u8g2_DrawStr(disp_u8g2,0,47,buf);
        break;

    case CO2:
        u8g2_SetDrawColor(disp_u8g2,0);
        u8g2_DrawBox(disp_u8g2,37,28,50,20);

        u8g2_SetDrawColor(disp_u8g2,1);
        u8g2_SetFont(disp_u8g2,u8g2_font_luRS12_tr);
        sprintf(buf,"%u",info->data.co2);
        u8g2_DrawStr(disp_u8g2,39,47,buf);
        break;

    case TEMP:
        u8g2_SetDrawColor(disp_u8g2,0);
        u8g2_DrawBox(disp_u8g2,0,0,23,13);

        u8g2_SetDrawColor(disp_u8g2,1);
        u8g2_SetFont(disp_u8g2,u8g2_font_Wizzard_tr);
        sprintf(buf,"%.1f",info->data.amb_temperature);
        u8g2_DrawStr(disp_u8g2,0,9,buf);
        break;

    case HUMIDITY:
        u8g2_SetDrawColor(disp_u8g2,0);
        u8g2_DrawBox(disp_u8g2,89,0,39,13);

        u8g2_SetDrawColor(disp_u8g2,1);
        u8g2_SetFont(disp_u8g2,u8g2_font_Wizzard_tr);
        sprintf(buf,"%.1f",info->data.rel_humidity);
        u8g2_DrawStr(disp_u8g2,101,9,buf);
        u8g2_DrawStr(disp_u8g2,91,9,"%");
        break;
    
    case HOUR:
        u8g2_SetDrawColor(disp_u8g2,0);
        u8g2_DrawBox(disp_u8g2,88,14,40,16);

        u8g2_SetDrawColor(disp_u8g2,1);
        u8g2_SetFont(disp_u8g2,u8g2_font_Wizzard_tr);
        sprintf(buf,"%02u",info->data.hour);
        u8g2_DrawStr(disp_u8g2,91,27,buf);

        u8g2_DrawStr(disp_u8g2,106,26,":");
        break;
    
    case MINUTE:
        u8g2_SetFont(disp_u8g2,u8g2_font_Wizzard_tr);
        sprintf(buf,"%02u",info->data.minute);
        u8g2_DrawStr(disp_u8g2,113,27,buf);
        break;

    default:
        break;
    }

}