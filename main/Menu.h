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

/**
 *  menu task that reads encoder input and 
 *  draws the menu on screen
 * 
 *  @param arg argument to be passed in task creation (does nothing)
 */
void menu_task(void* arg);