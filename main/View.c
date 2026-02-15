#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>

#include "View.h"
#include "Menu.h"
#include "Display.h"
#include "Rotary.h"
#include "Devices.h"

static const char* TAG = "View";

static uint8_t selected_view = 1;
static uint8_t display_sleep = 0;

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
    //! set selected view if specified when starting the view task
    if (arg != NULL) selected_view = (uint8_t)arg;

    //! rotary pulse counter variable
    int pcnt_value = 0;

    //! take display and rotary mutexes
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt get u8g2 display mutex");
        vTaskDelete(NULL);
    }

    if (xSemaphoreTake(rotary_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt get rotary encoder mutex");
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

    //! TODO just to see if view works. probably should delete this
    u8g2_SendBuffer(&u8g2);

    ESP_LOGI(TAG,"view_task STARTED");

    while (current_display_mode == VIEW)
    {
        //! read rotary BUTTON, if screen is on switch to menu, else wake display
        pcnt_unit_get_count(rot_but_pcnt_unit,&pcnt_value);

        if (pcnt_value >= 1) {
            //! clear pcnt
            pcnt_unit_clear_count(rot_but_pcnt_unit);

            switch (display_sleep)
            {
            case 0:     //! if display is not asleep
                //! set display mode
                current_display_mode = MENU;
                continue;

            default:    //! if display is asleep
                //  TODO wake from sleep
                continue;
            }
        }

        //if display is asleep read rotary, if rotary was activated, wake display
        if (display_sleep != 0) {
            pcnt_unit_get_count(rot_pcnt_unit,&pcnt_value);
            if (pcnt_value >= 4 || pcnt_value <= -4) {
                display_sleep = 0;
                // TODO wake display
            }
        }

        // TODO sleep display

        view_update(&u8g2);
        
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
    u8g2_SetFont(disp_u8g2, u8g2_font_mercutio_basic_nbp_tf);
    u8g2_DrawStr(disp_u8g2,0,27,"PM2.5");
    u8g2_DrawStr(disp_u8g2,0,61,"ug/m3");
    
    //! co2 top and bottom text
    u8g2_DrawStr(disp_u8g2,39,27,"CO2");
    u8g2_DrawStr(disp_u8g2,39,61,"ppm");

    //! humidity icon
    u8g2_DrawBitmap(disp_u8g2,89,0,1,10,water_droplet);

    /* TODO bunu alttaki fonksiyona yay
    //hour and minute
    currU8g2.setCursor(96,27);
    currU8g2.printf("%u:%02u",buf_ptr->hour,buf_ptr->minute);
    
    currU8g2.setCursor(93,44);
    currU8g2.print("tVOC:");
    currU8g2.setCursor(93,58);
    currU8g2.printf(" %0.f",buf_ptr->vocIndex);



    currU8g2.setCursor(0,47);
    currU8g2.setFont(u8g2_font_lubB12_tn);
    //pm2.5
    currU8g2.printf("%0.f",buf_ptr->massConcentrationPm2p5);

    currU8g2.setCursor(39,47);
    //CO2
    currU8g2.printf("%d",buf_ptr->co2);


    currU8g2.setFont(u8g2_font_mercutio_basic_nbp_tf);
    currU8g2.setCursor(0,10);
    //temperature
    currU8g2.printf("%.1f °C",buf_ptr->ambientTemperature);
    */
}

static void view_1_airgradient_upd(u8g2_t* disp_u8g2, disp_info* info)
{
    char buf[10];
    //! differ placement and font for each different type of data
    switch (info->type)
    {
    case VOC:
    
        break;

    case PM2p5:
    
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
    
        break;

    case HUMIDITY:
    
        break;
    
    case HOUR:
        u8g2_SetDrawColor(disp_u8g2,0);
        u8g2_DrawBox(disp_u8g2,88,14,40,16);

        u8g2_SetDrawColor(disp_u8g2,1);
        u8g2_SetFont(disp_u8g2,u8g2_font_luRS08_tr);
        sprintf(buf,"%02u",info->data.hour);
        u8g2_DrawStr(disp_u8g2,91,27,buf);

        u8g2_DrawStr(disp_u8g2,107,26,":");
        break;
    
    case MINUTE:
        u8g2_SetFont(disp_u8g2,u8g2_font_luRS08_tr);
        sprintf(buf,"%02u",info->data.minute);
        u8g2_DrawStr(disp_u8g2,113,27,buf);
        break;

    default:
        break;
    }

}


