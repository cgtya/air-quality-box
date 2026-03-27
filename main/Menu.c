#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <u8g2_esp32_hal.h>
#include <string.h>
#include <esp_log.h>
#include <stdatomic.h>

#include "Menu.h"
#include "Display.h"
#include "Rotary.h"
#include "View.h"
#include "System.h"
#include "Devices.h"
#include "Storage.h"
#include "ErrorLog.h"

static void error_log_viewer_start(void);

// number selecting type setting options
// used to distinguish which number type variable to change
typedef enum 
{
    DATE_VAR,
    TIME_VAR,
    MENU_TIMEOUT_VAR,
    DISP_TIMEOUT_VAR
} num_vars;

// struct for toggleable bool variables,
// if needed, mutexes can be given if not in should be given NULL.
// if needed, switching functions can be given if not in should be given NULL.
typedef struct toggle_info
{
    _Atomic bool* var_ptr;
    menu_action_t switch_func;
} toggle_info;

static toggle_info inverse_color_info = { .var_ptr = &inverse_color, .switch_func = switch_inverse_color };
static toggle_info data_logging_info = { .var_ptr = &data_logging, .switch_func = toggle_data_logging };

static const char* TAG = "Menu";

// Forward declarations
static menu_element_t main_menu_items[];
static menu_element_t settings_items[];
static menu_element_t time_date_items[];
static menu_element_t display_items[];
static menu_element_t sensors_items[];

// --- Level 4 ---

static menu_element_t sen5x_items[] = {
    { .name = "Fan temizleme", .submenus = NULL, .submenu_count = 0, .action = sen5x_fan_clean, .parent = &sensors_items[0], .type = BUTTON }
};

static menu_element_t s8_items[] = {
    { .name = "Kalibrasyon", .submenus = NULL, .submenu_count = 0, .action = s8_start_calibration, .parent = &sensors_items[0], .type = BUTTON }
};

// --- Level 3: Sub-Settings ---

static menu_element_t time_date_items[] = {
    { .name = "Saat Ayari", .submenus = NULL, .submenu_count = 0, .action = (menu_action_t)(num_vars)TIME_VAR, .parent = &settings_items[0], .type = NUM_SEL },
    { .name = "Tarih Ayari", .submenus = NULL, .submenu_count = 0, .action = (menu_action_t)(num_vars)DATE_VAR, .parent = &settings_items[0], .type = NUM_SEL }
};

static menu_element_t display_items[] = {
    { .name = "Ters Renk", .submenus = NULL, .submenu_count = 0, .action = (menu_action_t)(&inverse_color_info), .parent = &settings_items[2], .type = TOGGLE },
    { .name = "Ayar zmn asimi",  .submenus = NULL, .submenu_count = 0, .action = (menu_action_t)(num_vars)MENU_TIMEOUT_VAR, .parent = &settings_items[2], .type = NUM_SEL },
    { .name = "Ekran zmn asimi",  .submenus = NULL, .submenu_count = 0, .action = (menu_action_t)(num_vars)DISP_TIMEOUT_VAR, .parent = &settings_items[2], .type = NUM_SEL }
};

static menu_element_t sensors_items[] = {
    { .name = "Sen5x ayar", .submenus = sen5x_items, .submenu_count = 1, .action = NULL, .parent = &settings_items[3], .type = MENU },
    { .name = "S8 ayar",    .submenus = s8_items, .submenu_count = 1, .action = NULL, .parent = &settings_items[3], .type = MENU }
};

static menu_element_t advanced_items[] = {
    { .name = "Hata kaydi", .submenus = NULL, .submenu_count = 0, .action = error_log_viewer_start, .parent = &settings_items[4], .type = BUTTON },
    { .name = "Hata kaydi temizle", .submenus = NULL, .submenu_count = 0, .action = clear_error_buffer, .parent = &settings_items[4], .type = BUTTON },
    { .name = "FW Update",   .submenus = NULL,   .submenu_count = 0, .action = enter_download_mode, .parent = &settings_items[4], .type = BUTTON }
};
    

// --- Level 2: Settings ---

static menu_element_t settings_items[] = {
    { .name = "Tarih-Saat", .submenus = time_date_items, .submenu_count = 2, .action = NULL, .parent = &main_menu_items[1], .type = MENU },
    { .name = "Veri kaydi", .submenus = NULL, .submenu_count = 0, .action = (menu_action_t)(&data_logging_info), .parent = &main_menu_items[1], .type = TOGGLE },
    { .name = "Ekran",   .submenus = display_items,   .submenu_count = 3, .action = NULL, .parent = &main_menu_items[1], .type = MENU },
    { .name = "Sensorler",   .submenus = sensors_items,   .submenu_count = 2, .action = NULL, .parent = &main_menu_items[1], .type = MENU },
    { .name = "Gelismis", .submenus = advanced_items, .submenu_count = 3, .action = NULL, .parent = &main_menu_items[1], .type = MENU }
};

// --- Level 1: Main Menu ---
static menu_element_t main_menu = {
    .name = "MainMenu", .submenus= main_menu_items, .submenu_count = 2, .action = NULL, .parent=NULL,   .type=MENU
};

static menu_element_t main_menu_items[] = {
    { .name = "AirGrdnt",    .submenus = NULL,   .submenu_count = 0, .action = (menu_action_t)(uint8_t)1, .parent = &main_menu, .type = VIEW },
    { .name = "Ayarlar", .submenus = settings_items, .submenu_count = 5, .action = NULL, .parent = &main_menu, .type = MENU }
};

static uint8_t menu_timeout = 10;
static menu_element_t* selected_menu = &main_menu;
static uint8_t selected_menu_element_idx = 0;

// atomic because expecting access from multiple tasks
_Atomic menu_type_t current_display_mode = MENU;

// ---------- TOGGLE SWITCH ----------

// this function draws the toggle switch on the menu based on the status parameter
static void draw_toggle(u8g2_t* disp_u8g2, toggle_info* toggle)
{
    // clear the toggle space first
    u8g2_SetDrawColor(disp_u8g2,0);
    u8g2_DrawBox(disp_u8g2,107,27,16,10);

    u8g2_SetDrawColor(disp_u8g2,1);

    // draw frame
    u8g2_DrawHLine(disp_u8g2,108,27,14);
    u8g2_DrawHLine(disp_u8g2,108,36,14);

    u8g2_DrawVLine(disp_u8g2,107,28,8);
    u8g2_DrawVLine(disp_u8g2,122,28,8);
    
    // draw switch based on status
    if (*(toggle->var_ptr))
    {
        u8g2_DrawHLine(disp_u8g2,114,29,7);
        u8g2_DrawHLine(disp_u8g2,114,34,7);
        u8g2_DrawBox(disp_u8g2,114,30,3,4);
        u8g2_DrawBox(disp_u8g2,118,30,3,4);
    }
    else
    {
        u8g2_DrawFrame(disp_u8g2,109,29,7,6);
    }
}

// this function clears the display and
// draws the menu background (selection frame, up and down arrow)
static void menu_bg_draw(u8g2_t* disp_u8g2)
{
    // Clear display buffer
    u8g2_ClearBuffer(disp_u8g2);

    // Draw frame
    u8g2_DrawHLine(disp_u8g2,1,17,125);
    u8g2_DrawHLine(disp_u8g2,1,45,126);
    u8g2_DrawHLine(disp_u8g2,2,46,124);

    u8g2_DrawVLine(disp_u8g2,0,18,27);
    u8g2_DrawVLine(disp_u8g2,126,18,27);
    u8g2_DrawVLine(disp_u8g2,127,19,26);

    // Draw up arrow
    u8g2_DrawLine(disp_u8g2,13,4,5,12);
    u8g2_DrawLine(disp_u8g2,14,4,22,12);

    u8g2_DrawLine(disp_u8g2,13,5,6,12);
    u8g2_DrawLine(disp_u8g2,14,5,21,12);

    u8g2_DrawLine(disp_u8g2,13,6,7,12);
    u8g2_DrawLine(disp_u8g2,14,6,20,12);

    // Draw down arrow
    u8g2_DrawLine(disp_u8g2,13,59,5,51);
    u8g2_DrawLine(disp_u8g2,14,59,22,51);

    u8g2_DrawLine(disp_u8g2,13,58,6,51);
    u8g2_DrawLine(disp_u8g2,14,58,21,51);

    u8g2_DrawLine(disp_u8g2,13,57,7,51);
    u8g2_DrawLine(disp_u8g2,14,57,20,51);
}

// this function clears menu text boxes and
// fills the text boxes based on selected menu option
// also draws toggle switch or other elements if needed
static void menu_element_update(u8g2_t* disp_u8g2)
{
    u8g2_SetDrawColor(disp_u8g2,0);

    // Clear text boxes
    u8g2_DrawBox(disp_u8g2,23,0,105,16);
    u8g2_DrawBox(disp_u8g2,1,18,125,27);
    u8g2_DrawBox(disp_u8g2,23,47,105,16);

    u8g2_SetDrawColor(disp_u8g2,1);
    
    uint8_t back_buttn_var = selected_menu->submenu_count - selected_menu_element_idx;
    
    /**
     * back button in menus arent held in memory but
     * added automatically for each menu as the last element.
     * 
     * submenu_count - selected_menu_element_idx:
     *      0 = back selected
     *      1 = back button at bottom
     *      submenu_count = back button on top
     */
    
    // ----- selected item -----
    if (back_buttn_var == 0) {
        // if back button selected:
        u8g2_SetFont(disp_u8g2,u8g2_font_luRS12_tr);
        u8g2_DrawStr(disp_u8g2,15,38,"Geri don");
    } else {
        // displays differ for each menu element type
        switch (selected_menu->submenus[selected_menu_element_idx].type)
        {
            case TOGGLE:
                draw_toggle(disp_u8g2,(toggle_info*)(selected_menu->submenus[selected_menu_element_idx].action));
                break;
            
            default:
                break;
        }

        // smaller text if menu header exceeds 11 chars
        if (strlen(selected_menu->submenus[selected_menu_element_idx].name) > 11)
        {
            u8g2_SetFont(disp_u8g2,u8g2_font_Wizzard_tr);
            u8g2_DrawStr(disp_u8g2,15,36,selected_menu->submenus[selected_menu_element_idx].name);
        }
        else
        {
            u8g2_SetFont(disp_u8g2,u8g2_font_luRS12_tr);
            u8g2_DrawStr(disp_u8g2,15,38,selected_menu->submenus[selected_menu_element_idx].name);
        }
    }

    // set font for next and previous items
    u8g2_SetFont(disp_u8g2,u8g2_font_Wizzard_tr);

    // ----- bottom (next) item -----
    if (back_buttn_var == 1) {
        // if back button is at bottom:
        u8g2_DrawStr(disp_u8g2,27,60,"Geri don");
    } else if (back_buttn_var == 0) {
        // if back button is selected (wrap around, bottom element is menus first element):
        u8g2_DrawStr(disp_u8g2,27,60,selected_menu->submenus[0].name);
    } else {
        // write next element by default
        u8g2_DrawStr(disp_u8g2,27,60,selected_menu->submenus[selected_menu_element_idx+1].name);
    }

    // ----- top (previous) item -----
    if (back_buttn_var == selected_menu->submenu_count) {
        // if back button is on top:
        u8g2_DrawStr(disp_u8g2,28,12,"Geri don");
    } else {
        // write previous element by default
        u8g2_DrawStr(disp_u8g2,28,12,selected_menu->submenus[selected_menu_element_idx-1].name);
    }
}

/**
 * @brief switches to the view thats id is given as an arg. exits menu task
 * 
 * @param viewnum id of the selected view
 * 
 */
static void switch_to_view(uint8_t viewnum)
{
    // reset menu variables
    selected_menu = &main_menu;
    selected_menu_element_idx = 0;

    // set display mode to change tasks
    current_display_mode = VIEW;

    // clear pulse counters for rotary
    pcnt_unit_clear_count(rot_but_pcnt_unit);
    pcnt_unit_clear_count(rot_pcnt_unit);

    // start view task
    esp_err_t err;
    err = xTaskCreatePinnedToCore(view_task,"view_task",4096,(void*)viewnum,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt start view task");
        return;
    }

    ESP_LOGI(TAG,"started view_task with the view id %d",viewnum);

    return;
}

// switches the selected menu element to the next one
static void next_menu_element()
{
    // take display mutex
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt take display u8g2 mutex, timeout!");
        return;
    }

    // to check if next element rolls over to first
    if (selected_menu_element_idx == selected_menu->submenu_count) {
        selected_menu_element_idx = 0;
    } else {
        selected_menu_element_idx++;
    }

    ESP_LOGD(TAG,"next menu element: %d",selected_menu_element_idx);
    
    // update display
    menu_element_update(&u8g2);
    u8g2_SendBuffer(&u8g2);

    // give display mutex
    xSemaphoreGive(u8g2_mutex);

    return;
}

// switches the selected menu element to the previous one
static void prev_menu_element()
{
    // take display mutex
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt take display u8g2 mutex, timeout!");
        return;
    }

    // to check if previous element rolls over to last
    if (selected_menu_element_idx <= 0) {
        selected_menu_element_idx = selected_menu->submenu_count;
    } else {
        selected_menu_element_idx--;
    }

    ESP_LOGD(TAG,"prev menu element: %d",selected_menu_element_idx);

    // update display
    menu_element_update(&u8g2);
    u8g2_SendBuffer(&u8g2);

    // give display mutex
    xSemaphoreGive(u8g2_mutex);

    return;
}

// forward declaration
void num_select_task(void* arg);

static void select_menu_element()
{
    menu_element_t* selected_element = ((selected_menu->submenus)+selected_menu_element_idx);
    // take display mutex
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"select_menu_element: Couldnt take display u8g2 mutex, timeout!");
        return;
    }

    ESP_LOGD(TAG,"select menu element");
    
    // check to see if go back button was selected
    // if so return to parent menu
    if (selected_menu_element_idx >= selected_menu->submenu_count)
    {
        // NULL check
        if (selected_menu->parent == NULL) {
            switch_to_view(0);
            xSemaphoreGive(u8g2_mutex);
            return;
        }

        selected_menu = selected_menu->parent;
        selected_menu_element_idx = 0;
        menu_element_update(&u8g2);
        u8g2_SendBuffer(&u8g2);
        xSemaphoreGive(u8g2_mutex);
        return;
    }

    // if selected element was a menu, enter the menu
    // if selected element was anything else, do action (or something else ig)
    switch (selected_element->type)
    {
        case MENU:
            // NULL check
            if (selected_element->submenus == NULL) {
                xSemaphoreGive(u8g2_mutex);
                return;
            }

            // set selected menu
            selected_menu = selected_element;

            // reset selected element
            selected_menu_element_idx = 0;

            // update display
            menu_element_update(&u8g2);
            u8g2_SendBuffer(&u8g2);
            break;
        
        case VIEW:
            switch_to_view((uint8_t)(selected_element->action));
            break;

        case BUTTON:
            selected_element->action();
            break;

        case NUM_SEL:
            // change display mode to exit the menu task
            current_display_mode = NUM_SEL;

            // start number selector task
            esp_err_t err;
            err = xTaskCreatePinnedToCore(num_select_task,"num_select_task",4096,(void*)selected_element->action,3,NULL,tskNO_AFFINITY);
            if (err != pdTRUE) ESP_LOGE(TAG,"select_menu_element: Error while starting num_select_task");

            break;

        case TOGGLE:
            // give mutex as some toggles might need the display mutex
            xSemaphoreGive(u8g2_mutex);

            // check if variable toggle switching function exists
            if (((toggle_info*)(selected_element->action))->switch_func != NULL)
            {
                ((toggle_info*)(selected_element->action))->switch_func();
            }

            // retake display mutex and refresh the toggle status
            if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
                ESP_LOGE(TAG,"select_menu_element (TOGGLE): Couldnt take display u8g2 mutex, timeout!");
                return;
            }

            draw_toggle(&u8g2,((toggle_info*)(selected_element->action)));
            u8g2_SendBuffer(&u8g2);

            break;
            
        default:
            break;
        }
    // give display mutex
    xSemaphoreGive(u8g2_mutex);
}

// ---------- ERROR LOG VIEWER ----------

static void error_log_viewer_task(void* arg)
{
    int pcnt_value = 0;
    uint8_t scroll_pos = 0;

    // take display mutex
    if (xSemaphoreTake(u8g2_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "error_log_viewer_task: Couldnt take u8g2 mutex");
        current_display_mode = MENU;
        xTaskCreatePinnedToCore(menu_task,"menu_task",4096,NULL,3,NULL,tskNO_AFFINITY);
        vTaskDelete(NULL);
    }

    // take rotary mutex
    if (xSemaphoreTake(rotary_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "error_log_viewer_task: Couldnt take rotary mutex");
        xSemaphoreGive(u8g2_mutex);
        current_display_mode = MENU;
        xTaskCreatePinnedToCore(menu_task,"menu_task",4096,NULL,3,NULL,tskNO_AFFINITY);
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "error_log_viewer_task STARTED");

    // draw initial screen
    while (current_display_mode == ERROR_LOG)
    {
        uint8_t count = error_log_get_count();

        u8g2_ClearBuffer(&u8g2);
        u8g2_SetDrawColor(&u8g2, 1);
        u8g2_SetFont(&u8g2, u8g2_font_Wizzard_tr);

        u8g2_DrawHLine(&u8g2, 0, 11, 128);

        if (count == 0)
        {
            u8g2_SetFont(&u8g2, u8g2_font_Wizzard_tr);
            u8g2_DrawStr(&u8g2, 0, 9, "Hata/Uyari Kaydi");
            u8g2_DrawHLine(&u8g2, 0, 11, 128);

            u8g2_SetFont(&u8g2, u8g2_font_luRS12_tr);
            u8g2_DrawStr(&u8g2, 20, 42, "Kayit yok");
        }
        else
        {
            const error_log_entry_t* entry = error_log_get_entry(scroll_pos);
            if (entry != NULL && entry->valid)
            {
                u8g2_SetFont(&u8g2, u8g2_font_Wizzard_tr);

                // header: level + timestamp + position counter
                char buf[24];
                sprintf(buf, "%c %02u:%02u:%02u",
                        entry->level, entry->hour, entry->minute, entry->second);
                u8g2_DrawStr(&u8g2, 0, 9, buf);

                sprintf(buf, "%u/%u", scroll_pos + 1, count);
                u8g2_DrawStr(&u8g2, 100, 9, buf);

                u8g2_DrawHLine(&u8g2, 0, 11, 128);

                // message text: 4 lines, ~21 chars each (Wizzard font)
                size_t msg_len = strlen(entry->message);
                char line[22];

                for (uint8_t i = 0; i < 4 && i * 21 < msg_len; i++)
                {
                    size_t remaining = msg_len - (i * 21);
                    size_t copy_len = remaining < 21 ? remaining : 21;
                    memcpy(line, entry->message + (i * 21), copy_len);
                    line[copy_len] = '\0';
                    u8g2_DrawStr(&u8g2, 0, 23 + (i * 13), line);
                }
            }
        }

        u8g2_SendBuffer(&u8g2);

        // wait for input
        while (current_display_mode == ERROR_LOG)
        {
            vTaskDelay(pdMS_TO_TICKS(20));

            // rotary scroll
            pcnt_unit_get_count(rot_pcnt_unit, &pcnt_value);
            if (pcnt_value >= 4)
            {
                pcnt_unit_clear_count(rot_pcnt_unit);
                if (count > 0 && scroll_pos < count - 1) scroll_pos++;
                break; // redraw
            }
            else if (pcnt_value <= -4)
            {
                pcnt_unit_clear_count(rot_pcnt_unit);
                if (scroll_pos > 0) scroll_pos--;
                break; // redraw
            }

            // button press = exit
            pcnt_unit_get_count(rot_but_pcnt_unit, &pcnt_value);
            if (pcnt_value >= 1)
            {
                pcnt_unit_clear_count(rot_but_pcnt_unit);
                current_display_mode = MENU;
            }
        }
    }

    // release mutexes
    xSemaphoreGive(u8g2_mutex);
    xSemaphoreGive(rotary_mutex);

    // return to menu
    xTaskCreatePinnedToCore(menu_task, "menu_task", 4096, NULL, 3, NULL, tskNO_AFFINITY);

    ESP_LOGI(TAG, "error_log_viewer_task DELETED");
    vTaskDelete(NULL);
}

static void error_log_viewer_start(void)
{
    current_display_mode = ERROR_LOG;

    esp_err_t err;
    err = xTaskCreatePinnedToCore(error_log_viewer_task, "errlog_viewer", 4096, NULL, 3, NULL, tskNO_AFFINITY);
    if (err != pdTRUE) ESP_LOGE(TAG, "error_log_viewer_start: Error starting viewer task");
}

void menu_task(void* arg)
{
    int pcnt_value = 0;

    // draw the menu background and foreground at start
    if (xSemaphoreTake(u8g2_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    {
        ESP_LOGE(TAG, "menu_task: couldnt take display mutex");
    }
    else
    {
        menu_bg_draw(&u8g2);
        menu_element_update(&u8g2);

        // default text based on the given task start argument
        // currently used when indicating if num selecting was successful or not
        switch ((uint8_t)arg)
        {
        case 1:
            u8g2_SetDrawColor(&u8g2,0);
            u8g2_DrawBox(&u8g2,1,18,125,27);

            u8g2_SetDrawColor(&u8g2,1);
            u8g2_SetFont(&u8g2,u8g2_font_luRS12_tr);
            u8g2_DrawStr(&u8g2,15,38,"Basarili");
            break;

        case 2:
            u8g2_SetDrawColor(&u8g2,0);
            u8g2_DrawBox(&u8g2,1,18,125,27);

            u8g2_SetDrawColor(&u8g2,1);
            u8g2_SetFont(&u8g2,u8g2_font_luRS12_tr);
            u8g2_DrawStr(&u8g2,15,38,"Basarisiz");
            break;
        
        default:
            break;
        }

        u8g2_SendBuffer(&u8g2);
        xSemaphoreGive(u8g2_mutex);
    }



    // take encoder mutex
    if (xSemaphoreTake(rotary_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt take rotary mutex, timeout!");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG,"menu_task STARTED");

    // last button press time
    TickType_t last_act = xTaskGetTickCount();
    const TickType_t timeout_time_in_tick = pdMS_TO_TICKS(menu_timeout*1000);

    // read encoder pcnt periodically
    while (current_display_mode == MENU)
    {
        // if menu timeout isnt disabled (0)
        if (menu_timeout)
        {
            // if current tick is more than last act tick + timeout time in ticks
            // exit menu
            if (xTaskGetTickCount() > timeout_time_in_tick+last_act)
            {
                current_display_mode = VIEW;
                switch_to_view(0);
                continue;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        pcnt_unit_get_count(rot_pcnt_unit,&pcnt_value);

        if (pcnt_value >=4){
            pcnt_unit_clear_count(rot_pcnt_unit);
            last_act = xTaskGetTickCount();
            next_menu_element();
        } else if (pcnt_value <= -4){
            pcnt_unit_clear_count(rot_pcnt_unit);
            last_act = xTaskGetTickCount();
            prev_menu_element();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        pcnt_unit_get_count(rot_but_pcnt_unit,&pcnt_value);

        if (pcnt_value >=1)
        {
            pcnt_unit_clear_count(rot_but_pcnt_unit);
            last_act = xTaskGetTickCount();
            select_menu_element();
        }
    }

    // release mutex
    xSemaphoreGive(rotary_mutex);

    ESP_LOGI(TAG,"menu_task DELETED");
    // delete task
    vTaskDelete(NULL);
}

// ---------- NUMBER SELECTING ----------

static void num_select_bg_draw(u8g2_t *disp_u8g2, const num_vars var_to_chg)
{
    // take display mutex
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"num select bg draw: Couldnt take u8g2 display mutex, timeout!");
        return;
    }

    u8g2_SetFont(disp_u8g2, u8g2_font_Wizzard_tr);
    u8g2_ClearBuffer(disp_u8g2);
    switch (var_to_chg)
    {
    case TIME_VAR:
        u8g2_DrawStr(disp_u8g2,10,12,"Saat Ayari");
        break;

    case DATE_VAR:
        u8g2_DrawStr(disp_u8g2,10,12,"Tarih Ayari");
        break;

    case DISP_TIMEOUT_VAR:
        u8g2_DrawStr(disp_u8g2,5,12,"Ekrn zmn asimi");

        u8g2_DrawStr(disp_u8g2,0,60,"en az: 5 - hep acik: 0");
        break;

    case MENU_TIMEOUT_VAR:
        u8g2_DrawStr(disp_u8g2,5,12,"Menu zmn asimi");

        u8g2_DrawStr(disp_u8g2,0,60,"en az: 5 - hep acik: 0");
        break;

    default:
        u8g2_DrawStr(disp_u8g2,0,12,"sanirim bozuk");
        break;
    }

    xSemaphoreGive(u8g2_mutex);
}

// used to update the buffer in the num selector task and
// refresh the screen with new numbers
static void num_select_upd(u8g2_t *disp_u8g2, const int8_t change_value,
                        const num_vars var_to_chg, uint8_t* nums, const uint8_t selected_num)
{
    // changes the number buffers based on given parameters
    switch (selected_num)
    {
    case 1:
        // if within limits, change by given value
        if (((nums[0] == 0) && (change_value<0)) || ((nums[0] >= 99) && (change_value>0))) break;
        nums[0] += change_value;
        break;

    case 2:
        // 2-3 num selector control
        if (nums[1] == 255) 
        {
            ESP_LOGW(TAG,"Number selector: Num2 selected on non 2 or 3 numselector! (ignore if end of num selecting)");
            return;
        }

        // if within limits, change by given value
        if (((nums[1] == 0) && (change_value<0)) || ((nums[1] >= 99) && (change_value>0))) break;
        nums[1] += change_value;
        break;

    case 3:
        // 3 num selector control
        if (nums[2] == 255) 
        {
            ESP_LOGE(TAG,"Number selector: Num3 selected on non 3 numselector!");
            return;
        }

        // if within limits, change by given value
        if (((nums[2] == 0) && (change_value<0)) || ((nums[2] >= 99) && (change_value>0))) break;
        nums[2] += change_value;
        break;
    
    case 4:
        ESP_LOGW(TAG, "Number selector: Invalid num4 selected (if end of num selecting, ignore this)");
        break;
    
    default:
        ESP_LOGE(TAG,"Number selector: Invalid num selected!: %u",selected_num);
        return;
    }

    // take display mutex
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"num select upd: Couldnt take u8g2 display mutex, timeout! ");
        return;
    }

    // clear previous numbers
    u8g2_SetDrawColor(disp_u8g2,0);
    u8g2_DrawBox(disp_u8g2,0,16,128,28);
    u8g2_SetDrawColor(disp_u8g2,1);

    u8g2_SetFont(disp_u8g2, u8g2_font_luRS12_tr);

    char buf[15];

    switch (var_to_chg)
    {
    case TIME_VAR:
        sprintf(buf,"%02u.",nums[0]);
        u8g2_DrawStr(disp_u8g2,0,35,buf);
        sprintf(buf,"%02u.",nums[1]);
        u8g2_DrawStr(disp_u8g2,30,35,buf);
        sprintf(buf,"%02u",nums[2]);
        u8g2_DrawStr(disp_u8g2,60,35,buf);
        break;

    case DATE_VAR:
        sprintf(buf,"%02u.",nums[0]);
        u8g2_DrawStr(disp_u8g2,0,35,buf);
        sprintf(buf,"%02u.",nums[1]);
        u8g2_DrawStr(disp_u8g2,30,35,buf);
        sprintf(buf,"20%02u",nums[2]);
        u8g2_DrawStr(disp_u8g2,60,35,buf);
        break;
        
    case DISP_TIMEOUT_VAR:
        sprintf(buf,"%02u saniye",nums[0]);
        u8g2_DrawStr(disp_u8g2,0,35,buf);
        break;

    case MENU_TIMEOUT_VAR:
        sprintf(buf,"%02u saniye",nums[0]);
        u8g2_DrawStr(disp_u8g2,0,35,buf);
        break;

    default:
        u8g2_DrawStr(disp_u8g2,0,35,"sanirim bozuk");
        break;
    }

    switch (selected_num)
    {
    case 1:
        u8g2_DrawHLine(disp_u8g2,2,38,18);
        u8g2_DrawHLine(disp_u8g2,1,39,20);
        u8g2_DrawHLine(disp_u8g2,1,40,20);
        u8g2_DrawHLine(disp_u8g2,2,41,18);
        break;
    case 2:
        u8g2_DrawHLine(disp_u8g2,32,38,18);
        u8g2_DrawHLine(disp_u8g2,31,39,20);
        u8g2_DrawHLine(disp_u8g2,31,40,20);
        u8g2_DrawHLine(disp_u8g2,32,41,18);
        break;
    case 3:
        u8g2_DrawHLine(disp_u8g2,62,38,18);
        u8g2_DrawHLine(disp_u8g2,61,39,20);
        u8g2_DrawHLine(disp_u8g2,61,40,20);
        u8g2_DrawHLine(disp_u8g2,62,41,18);

        // make the year indicator longer
        if (var_to_chg == DATE_VAR)
        {
            u8g2_DrawHLine(disp_u8g2,80,38,20);
            u8g2_DrawHLine(disp_u8g2,81,39,20);
            u8g2_DrawHLine(disp_u8g2,81,40,20);
            u8g2_DrawHLine(disp_u8g2,80,41,20);
        }
        break;

    default:
        break;
    }

    u8g2_SendBuffer(disp_u8g2);
    xSemaphoreGive(u8g2_mutex);
}

void num_select_task(void* arg)
{
    int pcnt_value = 0;
    
    const num_vars var_to_chg = (num_vars)arg;
    uint8_t selected_num = 1;
    uint8_t num_selector_cnt = 0;

    uint8_t nums[3];

    nums[0] = 255;
    nums[1] = 255;
    nums[2] = 255;


    // take sys time mutex, would be better if it was only
    // taken when time or date was getting modified but whatever
    if (xSemaphoreTake(sys_time_mutex,pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG,"num_select_task: Couldnt take sys time mutex!");

        //back to menu
        current_display_mode = MENU;
        esp_err_t err;
        err = xTaskCreatePinnedToCore(menu_task,"menu_task",4096,NULL,3,NULL,tskNO_AFFINITY);
        if (err != pdTRUE) ESP_LOGE(TAG,"num_select_task:  Error while starting menu_task task");

        vTaskDelete(NULL);
    }
    
    // fill the buffers with current values
    switch (var_to_chg)
    {
    case TIME_VAR:
        nums[0] = (uint8_t)sys_time.tm_hour;
        nums[1] = (uint8_t)sys_time.tm_min;
        nums[2] = (uint8_t)sys_time.tm_sec;
        num_selector_cnt = 3;
        break;

    case DATE_VAR:
        nums[0] = (uint8_t)sys_time.tm_mday;
        nums[1] = (uint8_t)(sys_time.tm_mon+1);  //months since january
        nums[2] = (uint8_t)(sys_time.tm_year-100);   //years since 1900
        num_selector_cnt = 3;
        break;

    case DISP_TIMEOUT_VAR:
        nums[0] = sleep_timer;
        num_selector_cnt = 1;
        break;

    case MENU_TIMEOUT_VAR:
        nums[0] = menu_timeout;
        num_selector_cnt = 1;
        break;
    
    default:
        ESP_LOGE(TAG,"num_select_task: Invalid arg var_to_chg!");
        
        //back to menu
        current_display_mode = MENU;
        esp_err_t err;
        err = xTaskCreatePinnedToCore(menu_task,"menu_task",4096,NULL,3,NULL,tskNO_AFFINITY);
        if (err != pdTRUE) ESP_LOGE(TAG,"num_select_task:  Error while starting menu_task task");

        vTaskDelete(NULL);
        break;
    }

    xSemaphoreGive(sys_time_mutex);

    // take encoder mutex
    if (xSemaphoreTake(rotary_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"num_select_task: Couldnt take rotary mutex, timeout!");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG,"num_select_task STARTED");

    // draw the num selector background and numbers, send buffer
    num_select_bg_draw(&u8g2,var_to_chg);
    num_select_upd(&u8g2,0,var_to_chg,nums,selected_num);

    while (selected_num<=num_selector_cnt)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        pcnt_unit_get_count(rot_pcnt_unit,&pcnt_value);

        if (pcnt_value >=4){
            pcnt_unit_clear_count(rot_pcnt_unit);
            num_select_upd(&u8g2,1,var_to_chg,nums,selected_num);
        } else if (pcnt_value <= -4){
            pcnt_unit_clear_count(rot_pcnt_unit);
            num_select_upd(&u8g2,-1,var_to_chg,nums,selected_num);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        pcnt_unit_get_count(rot_but_pcnt_unit,&pcnt_value);

        if (pcnt_value >=1)
        {
            pcnt_unit_clear_count(rot_but_pcnt_unit);
            selected_num++;
            num_select_upd(&u8g2,0,var_to_chg,nums,selected_num);
        }
    }

    uint8_t status = 0;
    // check and save the values
    switch (var_to_chg)
    {
    case DATE_VAR:
        status = rtc_check_and_save_date(nums) ? 1 : 2;
        break;

    case TIME_VAR:
        status = rtc_check_and_save_time(nums) ? 1 : 2;
        break;

    case DISP_TIMEOUT_VAR:
        if ((nums[0] > 4) || (nums[0] == 0))
        {
            sleep_timer = nums[0];
            status = 1;
        } else status = 2;
        break;

    case MENU_TIMEOUT_VAR:
        if ((nums[0] > 4) || (nums[0] == 0))
        {
            menu_timeout = nums[0];
            status = 1;
        } else status = 2;
        break;
        
    default:
        break;
    }

    // release mutex
    xSemaphoreGive(rotary_mutex);

    // start menu task
    current_display_mode = MENU;
    esp_err_t err;
    err = xTaskCreatePinnedToCore(menu_task,"menu_task",4096,(void*)status,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE) ESP_LOGE(TAG,"num_select_task:  Error while starting menu_task task");

    ESP_LOGI(TAG,"num_select_task DELETED");

    vTaskDelete(NULL);
}