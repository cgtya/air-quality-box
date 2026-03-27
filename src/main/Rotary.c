#include "driver/pulse_cnt.h"
#include "esp_log.h"

#include "Rotary.h"

static const char *TAG = "Rotary";

pcnt_unit_handle_t rot_pcnt_unit = NULL;
static pcnt_channel_handle_t rot_pcnt_chan_a = NULL;
static pcnt_channel_handle_t rot_pcnt_chan_b = NULL;

pcnt_unit_handle_t rot_but_pcnt_unit = NULL;
static pcnt_channel_handle_t rot_pcnt_chan_but = NULL;

SemaphoreHandle_t rotary_mutex;

esp_err_t rotary_pcnt_init(pcnt_unit_handle_t* pcnt_unit_ptr)
{
    esp_err_t err;

    //! create pcnt unit
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };

    err = pcnt_new_unit(&unit_config, pcnt_unit_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while creating new pcnt unit");
        return err;
    }
    
    //! set glitch filter
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = ROTARY_GLITCH_NS,
    };
    err = pcnt_unit_set_glitch_filter(*pcnt_unit_ptr, &filter_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while setting glitch filter");
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    //! install pcnt channels
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ROT_A_PIN,
        .level_gpio_num = ROT_B_PIN,
    };
    err = pcnt_new_channel(*pcnt_unit_ptr, &chan_a_config, &rot_pcnt_chan_a);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while setting channel A");
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ROT_B_PIN,
        .level_gpio_num = ROT_A_PIN,
    };
    err = pcnt_new_channel(*pcnt_unit_ptr, &chan_b_config, &rot_pcnt_chan_b);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while setting channel B");
        pcnt_del_channel(rot_pcnt_chan_a);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    //! set edge and level actions for pcnt channels
    err = pcnt_channel_set_edge_action(rot_pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    err = err | pcnt_channel_set_level_action(rot_pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    err = err | pcnt_channel_set_edge_action(rot_pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    err = err | pcnt_channel_set_level_action(rot_pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while setting edge and level actions");
        pcnt_del_channel(rot_pcnt_chan_a);
        pcnt_del_channel(rot_pcnt_chan_b);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    //! enable, clear and start the pcnt unit
    err = pcnt_unit_enable(*pcnt_unit_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while enabling rotary pcnt");
        pcnt_del_channel(rot_pcnt_chan_a);
        pcnt_del_channel(rot_pcnt_chan_b);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }
    err = pcnt_unit_clear_count(*pcnt_unit_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while clearing rotary pcnt");
        pcnt_del_channel(rot_pcnt_chan_a);
        pcnt_del_channel(rot_pcnt_chan_b);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }
    err = pcnt_unit_start(*pcnt_unit_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while starting rotary pcnt");
        pcnt_del_channel(rot_pcnt_chan_a);
        pcnt_del_channel(rot_pcnt_chan_b);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    //! Create rotary mutex
    rotary_mutex = xSemaphoreCreateMutex();
    //! Check if its initialized
    if (rotary_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex for rotary obj.");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t rot_but_pcnt_init(pcnt_unit_handle_t* pcnt_unit_ptr)
{
    esp_err_t err;

    //! create pcnt unit
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };

    err = pcnt_new_unit(&unit_config, pcnt_unit_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while creating new pcnt unit");
        return err;
    }
    
    //! set glitch filter
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = ROTARY_GLITCH_NS,
    };
    err = pcnt_unit_set_glitch_filter(*pcnt_unit_ptr, &filter_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while setting glitch filter");
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    //! install pcnt channels
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ROT_BUT_PIN
    };
    err = pcnt_new_channel(*pcnt_unit_ptr, &chan_a_config, &rot_pcnt_chan_but);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while setting button channel");
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    //! set edge and level actions for pcnt channels
    err = pcnt_channel_set_edge_action(rot_pcnt_chan_but, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while setting button edge action");
        pcnt_del_channel(rot_pcnt_chan_but);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    //! enable, clear and start the pcnt unit
    err = pcnt_unit_enable(*pcnt_unit_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while enabling button pcnt");
        pcnt_del_channel(rot_pcnt_chan_but);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }
    err = pcnt_unit_clear_count(*pcnt_unit_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while clearing button pcnt");
        pcnt_del_channel(rot_pcnt_chan_but);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }
    err = pcnt_unit_start(*pcnt_unit_ptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Error while starting button pcnt");
        pcnt_del_channel(rot_pcnt_chan_but);
        pcnt_unit_disable(*pcnt_unit_ptr);
        pcnt_del_unit(*pcnt_unit_ptr);
        return err;
    }

    return ESP_OK;
}
