#include <esp_err.h>
#include <stdbool.h>

esp_err_t prepare_sdspi();
esp_err_t mount_sdspi(bool get_health);
esp_err_t unmount_sdspi();
void toggle_data_logging(void);