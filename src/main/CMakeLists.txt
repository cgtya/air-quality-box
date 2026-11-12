idf_component_register(SRCS "main.c"
                            "Devices.c"
                            "View.c"
                            "Rotary.c"
                            "Display.c"
                            "Menu.c"
                            "System.c"
                            "Storage.c"
                            "ErrorLog.c"
                        REQUIRES freertos 
                            driver 
                            esp_driver_gpio
                            esp_driver_pcnt
                            esp_driver_spi
                            esp_driver_sdspi
                            sdmmc
                            esp_timer
                            u8g2 
                            sen5x 
                            senseair-s8 
                            u8g2-hal-esp-idf
                            vfs
                            fatfs
                        INCLUDE_DIRS "include")
