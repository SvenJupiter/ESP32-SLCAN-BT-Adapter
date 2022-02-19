// Header for flash storage
#include "nvs.h"
#include "nvs_flash.h"

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // vTaskDelete

// Haeder for some Hardware/Project specific constants
#include "HardwareConfig.h"

// Header for BTSPP
#include "btspp.h"

// Header for SLCAN
#include "slcan.h"


// Header for debug messages
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#define TAG "APP"



void app_main(void)
{

    // Init NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // Hello World
    printf("Hello World!\r\n");

    // Init everything nedded for SPP
    btspp_init(HAREWARE_CONFIG_BT_DEVICE_NAME, 10 * BTSPP_MSG_MAX_SIZE);

    // Init everything nedded for SLCAN
    slcan_init();

    // Nothing more to do
    vTaskDelete(NULL);
}
