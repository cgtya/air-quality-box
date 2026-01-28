#include <u8g2_esp32_hal.h>
#include <esp_log.h>

#include "Display.h"
#include "Config.h"

u8g2_t u8g2;
SemaphoreHandle_t u8g2_mutex;

static const char* TAG = "Display";

esp_err_t init_display() {
    //! display config
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.sda = PIN_SDA1;
    u8g2_esp32_hal.bus.i2c.scl = PIN_SCL1;
    
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    //! display setup command
    //! noname_f or vcomh0_f is our display
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2,
                                        U8G2_R2,
                                        u8g2_esp32_i2c_byte_cb,
                                        u8g2_esp32_gpio_and_delay_cb);

    u8x8_SetI2CAddress(&u8g2.u8x8,(DISPLAY_I2C_ADDR<<1));

    //! send init sequence to the display, display is in sleep mode after this
    u8g2_InitDisplay(&u8g2);    


    //! Create display u8g2 mutex
    u8g2_mutex = xSemaphoreCreateMutex();
    //! Check if its initialized
    if (u8g2_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex for display obj.");
        return ESP_FAIL;
    }

    u8g2_ClearBuffer(&u8g2);        //! clear display buffer
    u8g2_SendBuffer(&u8g2);
    u8g2_SetDrawColor(&u8g2,1);     //! set color to white
    u8g2_SetPowerSave(&u8g2, 0);    //! wake up display

    return ESP_OK;
}