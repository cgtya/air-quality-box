#include <stdint.h>

#include "View.h"
#include "Menu.h"
#include "Display.h"

static uint8_t selected_view = 0;

static void view_1_airgradient();

void view_task(void* arg)
{

}

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
    u8g2_SetFont(u8g2_font_mercutio_basic_nbp_tf);
    u8g2_DrawStr(disp_u8g2,0,27,"PM2.5");
    u8g2_DrawStr(disp_u8g2,0,61,"ug/m3");
    
    //! co2 top and bottom text
    u8g2_DrawStr(disp_u8g2,39,27,"CO2");
    u8g2_DrawStr(disp_u8g2,39,61,"ppm");
    
    //! clock colon
    u8g2_DrawStr(disp_u8g2,106,27,":");

    //! humidity icon
    u8g2_DrawBitmap(disp_u8g2,89,0,5,10,water_droplet)

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
    //! differ placement and font for each different type of data
    switch (info->type)
    {
    case MINUTE:
        
        break;

    case HOUR:
    
        break;

    case VOC:
    
        break;

    case PM2p5:
    
        break;

    case CO2:
    
        break;

    case TEMP:
    
        break;

    case HUMIDITY:
    
        break;
    
    default:
        break;
    }

}


// 'water_droplet', 5x10px
static const unsigned char water_droplet[] = {
	0x20, 0x20, 0x20, 0x70, 0x70, 0xf8, 0xf8, 0xf8, 0xf8, 0x70
};

// 'floppy_icon', 10x10px
static const unsigned char floppy_icon[] = {
	0xff, 0x00, 0xa2, 0x80, 0xbe, 0x40, 0x80, 0x40, 0x80, 0x40, 0xbe, 0x40, 0xa2, 0x40, 0xa2, 0x40, 
	0xa2, 0x40, 0xff, 0xc0
};