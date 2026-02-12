idf_component_register(SRCS "Devices.c" "main.c"
                            "View.c"
                            "Rotary.c"
                            "Display.c"
                            "Menu.c"
                            "System.c"
                    REQUIRES freertos driver u8g2 sen5x u8g2-hal-esp-idf)
