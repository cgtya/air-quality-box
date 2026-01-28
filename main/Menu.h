#pragma once

#include <stdint.h>
#include <u8g2_esp32_hal.h>

typedef struct menu_element_t menu_element_t;

typedef void (*menu_action_t)(void);

/**
 * @brief Types of menu elements.
 */
typedef enum {
    MENU,
    TOGGLE,
    BUTTON,
    NUM_SEL,
    VIEW
} menu_type_t;

/**
 * @brief Each element to be listed in a menu.
 */
struct menu_element_t
{
    const char* name;
    menu_element_t* submenus;
    uint8_t submenu_count;
    menu_action_t action;
    menu_element_t* parent;
    menu_type_t type;
};


void menu_bg_draw(u8g2_t* disp_u8g2);
void menu_element_update(u8g2_t* disp_u8g2);