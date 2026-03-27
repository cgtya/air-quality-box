#pragma once

// i2c for sen54 and ds3231
#define PIN_SDA0 8
#define PIN_SCL0 9

// i2c for oled display ssd1306
#define PIN_SDA1 2
#define PIN_SCL1 1
#define DISPLAY_I2C_ADDR 0x3c

// rotary encoder pins
#define ROT_A_PIN 7
#define ROT_B_PIN 5
#define ROT_BUT_PIN 6
#define PCNT_HIGH_LIMIT 100
#define PCNT_LOW_LIMIT  -100
#define ROTARY_GLITCH_NS 10000

// uart for senseair s8
#define UART1_RX 44
#define UART1_TX 43
#define UART1_PORT 1

#define DATA_QUEUE_SIZE 140
#define MAX_WRITE_ERRORS 5
#define VIEW_QUEUE_SIZE 30

#define SPI2_MISO 13
#define SPI2_SCK  12
#define SPI2_MOSI 11
#define SPI2_CS 10