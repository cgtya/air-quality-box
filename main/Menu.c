#include <string.h>
#include <esp_log.h>

#include "Menu.h"
#include "Display.h"
#include "Rotary.h"
#include "View.h"
#include "System.h"

static const char* TAG = "Menu";

// Forward declarations
static menu_element_t main_menu_items[];
static menu_element_t settings_items[];
static menu_element_t time_date_items[];
static menu_element_t time_adj_items[];
static menu_element_t date_adj_items[];
static menu_element_t display_items[];
static menu_element_t sensors_items[];

// --- Level 4: Leaves (Time/Date Adjustments) ---

static menu_element_t time_adj_items[] = {
    { .name = "Saat",   .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &time_date_items[0], .type = NUM_SEL },
    { .name = "Dakika", .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &time_date_items[0], .type = NUM_SEL },
    { .name = "Saniye", .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &time_date_items[0], .type = NUM_SEL }
};

static menu_element_t date_adj_items[] = {
    { .name = "Gun",   .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &time_date_items[1], .type = NUM_SEL },
    { .name = "Ay", .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &time_date_items[1], .type = NUM_SEL },
    { .name = "Yil",  .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &time_date_items[1], .type = NUM_SEL }
};

// --- Level 3: Sub-Settings ---

static menu_element_t time_date_items[] = {
    { .name = "Saat Ayari", .submenus = time_adj_items, .submenu_count = 3, .action = NULL, .parent = &settings_items[0], .type = MENU },
    { .name = "Tarih Ayari", .submenus = date_adj_items, .submenu_count = 3, .action = NULL, .parent = &settings_items[0], .type = MENU }
};

static menu_element_t display_items[] = {
    { .name = "Ters Renk", .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &settings_items[2], .type = TOGGLE },
    { .name = "Ayr zmn asim",  .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &settings_items[2], .type = NUM_SEL },
    { .name = "Ekrn zmn asim",  .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &settings_items[2], .type = NUM_SEL }
};

static menu_element_t sensors_items[] = {
    { .name = "Sen5x ayar", .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &settings_items[3], .type = MENU },
    { .name = "S8 ayar",    .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &settings_items[3], .type = MENU }
};

// --- Level 2: Settings ---

static menu_element_t settings_items[] = {
    { .name = "Tarih-Saat", .submenus = time_date_items, .submenu_count = 2, .action = NULL, .parent = &main_menu_items[1], .type = MENU },
    { .name = "Veri kaydi", .submenus = NULL, .submenu_count = 0, .action = NULL, .parent = &main_menu_items[1], .type = TOGGLE },
    { .name = "Ekran",   .submenus = display_items,   .submenu_count = 3, .action = NULL, .parent = &main_menu_items[1], .type = MENU },
    { .name = "Sensorler",   .submenus = sensors_items,   .submenu_count = 2, .action = NULL, .parent = &main_menu_items[1], .type = MENU },
    { .name = "FW Update",   .submenus = NULL,   .submenu_count = 0, .action = enter_download_mode, .parent = &main_menu_items[1], .type = BUTTON }
};

// --- Level 1: Main Menu ---
static menu_element_t main_menu = {
    .name = "MainMenu", .submenus= main_menu_items, .submenu_count = 2, .action = NULL, .parent=NULL,   .type=MENU
};

static menu_element_t main_menu_items[] = {
    { .name = "AirGrdnt",    .submenus = NULL,   .submenu_count = 0, .action = (menu_action_t)(uint8_t)1, .parent = &main_menu, .type = VIEW },
    { .name = "Ayarlar", .submenus = settings_items, .submenu_count = 5, .action = NULL, .parent = &main_menu, .type = MENU }
};

static menu_element_t* selected_menu = &main_menu;
static uint8_t selected_menu_element_idx = 0;
menu_type_t current_display_mode = MENU;


static void menu_bg_draw(u8g2_t* disp_u8g2)
{
    //! Clear display buffer
    u8g2_ClearBuffer(disp_u8g2);

    //! Draw frame
    u8g2_DrawHLine(disp_u8g2,1,17,125);
    u8g2_DrawHLine(disp_u8g2,1,45,126);
    u8g2_DrawHLine(disp_u8g2,2,46,124);

    u8g2_DrawVLine(disp_u8g2,0,18,27);
    u8g2_DrawVLine(disp_u8g2,126,18,27);
    u8g2_DrawVLine(disp_u8g2,127,19,26);

    //! Draw up arrow
    u8g2_DrawLine(disp_u8g2,13,4,5,12);
    u8g2_DrawLine(disp_u8g2,14,4,22,12);

    u8g2_DrawLine(disp_u8g2,13,5,6,12);
    u8g2_DrawLine(disp_u8g2,14,5,21,12);

    u8g2_DrawLine(disp_u8g2,13,6,7,12);
    u8g2_DrawLine(disp_u8g2,14,6,20,12);

    //! Draw down arrow
    u8g2_DrawLine(disp_u8g2,13,59,5,51);
    u8g2_DrawLine(disp_u8g2,14,59,22,51);

    u8g2_DrawLine(disp_u8g2,13,58,6,51);
    u8g2_DrawLine(disp_u8g2,14,58,21,51);

    u8g2_DrawLine(disp_u8g2,13,57,7,51);
    u8g2_DrawLine(disp_u8g2,14,57,20,51);
}

static void menu_element_update(u8g2_t* disp_u8g2)
{
    u8g2_SetDrawColor(disp_u8g2,0);

    //! Clear text boxes
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
    
    //! ----- Selected item -----
    if (back_buttn_var == 0) {
        //! if back button selected:
        u8g2_SetFont(disp_u8g2,u8g2_font_luRS12_tr);
        u8g2_DrawStr(disp_u8g2,33,38,"Geri don");
    } else {
        //! displays differ for each menu element type
        switch (selected_menu->submenus[selected_menu_element_idx].type)
        {
            case TOGGLE:
                u8g2_SetFont(disp_u8g2,u8g2_font_luRS08_tr);
                u8g2_DrawStr(disp_u8g2,33,36,selected_menu->submenus[selected_menu_element_idx].name);
                // TODO add toggle switch render function
                break;

            case NUM_SEL:
                u8g2_SetFont(disp_u8g2,u8g2_font_luRS08_tr);
                u8g2_DrawStr(disp_u8g2,33,36,selected_menu->submenus[selected_menu_element_idx].name);
                // TODO add num selector render function
                break;
            
            default:
                u8g2_SetFont(disp_u8g2,u8g2_font_luRS12_tr);
                u8g2_DrawStr(disp_u8g2,33,38,selected_menu->submenus[selected_menu_element_idx].name);
                break;
        }
    }

    //! set font for next and previous items
    u8g2_SetFont(disp_u8g2,u8g2_font_luRS08_tr);

    //! ----- bottom (next) item -----
    if (back_buttn_var == 1) {
        //! if back button is at bottom:
        u8g2_DrawStr(disp_u8g2,27,60,"Geri don");
    } else if (back_buttn_var == 0) {
        //! if back button is selected (wrap around, bottom element is menus first element):
        u8g2_DrawStr(disp_u8g2,27,60,selected_menu->submenus[0].name);
    } else {
        //! write next element by default
        u8g2_DrawStr(disp_u8g2,27,60,selected_menu->submenus[selected_menu_element_idx+1].name);
    }

    //! ----- top (previous) item -----
    if (back_buttn_var == selected_menu->submenu_count) {
        //! if back button is on top:
        u8g2_DrawStr(disp_u8g2,28,12,"Geri don");
    } else {
        //! write previous element by default
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
    //! reset menu variables
    selected_menu = &main_menu;
    selected_menu_element_idx = 0;

    //! set display mode to change tasks
    current_display_mode = VIEW;

    //! clear pulse counters for rotary
    pcnt_unit_clear_count(rot_but_pcnt_unit);
    pcnt_unit_clear_count(rot_pcnt_unit);

    //! start view task
    esp_err_t err;
    err = xTaskCreatePinnedToCore(view_task,"view_task",4096,(void*)viewnum,3,NULL,tskNO_AFFINITY);
    if (err != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt start view task");
        return;
    }

    ESP_LOGI(TAG,"started view_task with the view id %d",viewnum);

    // after this function, select_menu_element releases the u8g2 display mutex
    // or timeout occurs TODO
    // menu_task while loop stops and rotary mutex is released. then menu_task gets deleted
    return;
}

static void next_menu_element()
{
    //! take display mutex
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt take display u8g2 mutex, timeout!");
        return;
    }

    //! to check if next element rolls over to first
    if (selected_menu_element_idx == selected_menu->submenu_count) {
        selected_menu_element_idx = 0;
    } else {
        selected_menu_element_idx++;
    }

    ESP_LOGI(TAG,"next menu element: %d",selected_menu_element_idx);
    
    //! update display
    menu_element_update(&u8g2);
    u8g2_SendBuffer(&u8g2);

    //! give display mutex
    xSemaphoreGive(u8g2_mutex);

    return;
}

static void prev_menu_element()
{
    //! take display mutex
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt take display u8g2 mutex, timeout!");
        return;
    }

    //! to check if previous element rolls over to last
    if (selected_menu_element_idx <= 0) {
        selected_menu_element_idx = selected_menu->submenu_count;
    } else {
        selected_menu_element_idx--;
    }

    ESP_LOGI(TAG,"prev menu element: %d",selected_menu_element_idx);

    //! update display
    menu_element_update(&u8g2);
    u8g2_SendBuffer(&u8g2);

    //! give display mutex
    xSemaphoreGive(u8g2_mutex);

    return;
}

static void select_menu_element()
{
    menu_element_t* selected_element = ((selected_menu->submenus)+selected_menu_element_idx);
    //! take display mutex
    if (xSemaphoreTake(u8g2_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt take display u8g2 mutex, timeout!");
        return;
    }

    ESP_LOGI(TAG,"select menu element");
    
    //! check to see if go back button was selected
    //! if so return to parent menu
    if (selected_menu_element_idx >= selected_menu->submenu_count)
    {
        //! NULL check
        if (selected_menu->parent == NULL) {
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

    //! if selected element was a menu, enter the menu
    //! if selected element was anything else, do action (or something else ig)
    switch (selected_element->type)
    {
        case MENU:
            //! NULL check
            if (selected_element->submenus == NULL) {
                xSemaphoreGive(u8g2_mutex);
                return;
            }

            //! set selected menu
            selected_menu = selected_element;

            //! reset selected element
            selected_menu_element_idx = 0;

            //! update display
            menu_element_update(&u8g2);
            u8g2_SendBuffer(&u8g2);
            break;
        
        case VIEW:
            switch_to_view((uint8_t)(selected_element->action));
            break;

        case BUTTON:
            selected_element->action();
            break;
            
        default:
            break;
        }
    //! give display mutex
    xSemaphoreGive(u8g2_mutex);
}

void menu_task(void* arg)
{
    int pcnt_value = 0;

    menu_bg_draw(&u8g2);
    menu_element_update(&u8g2);
    u8g2_SendBuffer(&u8g2);

    if (xSemaphoreTake(rotary_mutex,pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG,"Couldnt take rotary mutex, timeout!");
        // TODO maybe start view here??
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG,"menu_task STARTED");

    while (current_display_mode == MENU)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        pcnt_unit_get_count(rot_pcnt_unit,&pcnt_value);

        if (pcnt_value >=4){
            pcnt_unit_clear_count(rot_pcnt_unit);
            next_menu_element();
        } else if (pcnt_value <= -4){
            pcnt_unit_clear_count(rot_pcnt_unit);
            prev_menu_element();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        pcnt_unit_get_count(rot_but_pcnt_unit,&pcnt_value);

        if (pcnt_value >=1)
        {
            pcnt_unit_clear_count(rot_but_pcnt_unit);
            select_menu_element();
        }
    }

    //! release mutex
    xSemaphoreGive(rotary_mutex);

    ESP_LOGI(TAG,"menu_task DELETED");
    //! delete task
    vTaskDelete(NULL);
}
