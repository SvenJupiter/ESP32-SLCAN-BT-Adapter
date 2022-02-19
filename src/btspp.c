#include "btspp.h"

#include "HardwareConfig.h"

// Some standard header
#include <string.h> // memcpy


// Bluetooth header
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"



// FreeRTOS
// Ringbuffers for incomming and outgoing data
// and EventGroups for BT events
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"
#include "freertos/task.h" // vTaskDelay()



// Header for debug messages
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#define BT_TAG "BT"
#define GAP_TAG "BT-GAP"
#define SPP_TAG "BT-SPP"


// OTA
#include "esp_ota_ops.h"
#include "esp_partition.h"


// Do a OTA uptade via Bluetooth SPP
#define OTA_TAG "BT-OTA"
#define BUFFSIZE (1024u)
#define MAX_CHUNK_SIZE (950u)
static uint8_t ota_write_data[BUFFSIZE + 1] = { 0 };



// Do a OTA updade via Bluetooth SPP
// You need my custom python script for that
// Based on the expressif OTA example
bool btspp_do_ota_update() {

    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(OTA_TAG, "Starting BT-OTA");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(OTA_TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address
        );
        ESP_LOGW(OTA_TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }

    ESP_LOGI(OTA_TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address
    );

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(OTA_TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address
    );


    /*deal with all receive packet*/
    uint64_t total_bytes_read = 0;
    uint64_t binary_file_length = 0;
    uint64_t progress_percent = 0;
    uint64_t progress_percent_decimals = 0;
    bool data_send = false;
    int data_read = 0;
    bool image_header_was_checked = false;

    // Get ready
    data_send = btspp_send_msg("DO FIRMWARE UPLOAD?\r\n", 2000);
    if (!data_send) { return false; }
    data_read = btspp_recv_msg((char*) ota_write_data, BUFFSIZE, "\r\n",  1000,  100);
    if (data_read <= 0) { 
        btspp_send_msg("ABORT!\r\n", 2000);
        return false;
    }
    else {
        if (data_read != 5) {
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        }
        else if (strncmp((char*) ota_write_data, "YES\r\n", 5) != 0) {
            ESP_LOGE(OTA_TAG, "Error: SPP answer error");
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        }
    }

    // Get firmware filesize
    data_send = btspp_send_msg("FIRMWARE FILESIZE?\r\n", 2000);
    if (!data_send) { return false; }
    data_read = btspp_recv_msg((char*) ota_write_data, BUFFSIZE, "\r\n",  1000,  100);
    if (data_read <= 0) { 
        btspp_send_msg("ABORT!\r\n", 2000);
        return false;
    }
    else {
        // Get firmware filesize
        int result = sscanf((char*) ota_write_data, "%llu\r\n", &binary_file_length);
        if (result != 1) {
            ESP_LOGE(OTA_TAG, "Error: SPP answer error");
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        }
        else {
            ESP_LOGI(OTA_TAG, "Firmware filesize = %llu", binary_file_length);
        }
    }

    // Set chunk size
    ESP_LOGI(OTA_TAG, "Max chunk size = %u", MAX_CHUNK_SIZE);
    snprintf((char*) ota_write_data, BUFFSIZE, "MAX CHUNK SIZE = %u\r\n", MAX_CHUNK_SIZE);
    data_send = btspp_send_msg((char*) ota_write_data, 2000);
    if (!data_send) { return false; }
    data_read = btspp_recv_msg((char*) ota_write_data, BUFFSIZE, "\r\n",  1000,  100);
    if (data_read <= 0) { 
        btspp_send_msg("ABORT!\r\n", 2000);
        return false;
    }
    else {
        if (data_read != 4) {
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        }
        else if (strncmp((char*) ota_write_data, "OK\r\n", 4) != 0) {
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        }
    }


    ESP_LOGI(OTA_TAG, "Starting upload...");
    data_send = btspp_send_msg("START UPLOAD!\r\n", 2000);
    if (!data_send) { return false; }
    do {

        // read chunk of firmware
        data_read = btspp_recv_data(ota_write_data, BUFFSIZE, 2000, 10);

        if (data_read == -1) {
            ESP_LOGE(OTA_TAG, "Error: SPP data read error");
            esp_ota_abort(update_handle);
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        } 
        else if (data_read == -2) {
            ESP_LOGE(OTA_TAG, "Timeout: SPP data read timeout");
            esp_ota_abort(update_handle);
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        } 
        else if (data_read == 0) {
            ESP_LOGE(OTA_TAG, "Error: SPP error");
            esp_ota_abort(update_handle);
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        }
        else /* if (data_read > 0) */ {
            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    
                    // check current version with downloading
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(OTA_TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGI(OTA_TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                        ESP_LOGI(OTA_TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }

                    // check current version with last invalid partition
                    if (last_invalid_app != NULL) {
                        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                            ESP_LOGW(OTA_TAG, "New version is the same as invalid version.");
                            ESP_LOGW(OTA_TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                            ESP_LOGW(OTA_TAG, "The firmware has been rolled back to the previous version.");
                            btspp_send_msg("ABORT!\r\n", 2000);
                            return false;
                        }
                    }

                    // Version check
                    if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
                        ESP_LOGW(OTA_TAG, "Current running version is the same as a new. We will continue the update anyway.");
                    }
                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(OTA_TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        esp_ota_abort(update_handle);
                        btspp_send_msg("ABORT!\r\n", 2000);
                        return false;
                    }
                    ESP_LOGI(OTA_TAG, "esp_ota_begin succeeded");
                }
                else {
                    ESP_LOGE(OTA_TAG, "received package is not fit len");
                    esp_ota_abort(update_handle);
                    btspp_send_msg("ABORT!\r\n", 2000);
                    return false;
                }
            }

            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                esp_ota_abort(update_handle);
                btspp_send_msg("ABORT!\r\n", 2000);
                return false;
            }
            total_bytes_read += data_read;
            ESP_LOGD(OTA_TAG, "Written image length %llu", total_bytes_read);

            // show progress
            progress_percent = (total_bytes_read * 100ull) / binary_file_length;
            progress_percent_decimals = (total_bytes_read * 100ull) % binary_file_length;
            ESP_LOGI(OTA_TAG, "Progress: %llu/%llu (%llu.%llu%%)", 
                total_bytes_read, binary_file_length, progress_percent, progress_percent_decimals
            );

            if (total_bytes_read < binary_file_length) {
                ESP_LOGD(OTA_TAG, "Next chunk...");
                data_send = btspp_send_msg("NEXT CHUNK!\r\n", 2000);
                if (!data_send) {
                    ESP_LOGE(OTA_TAG, "Error: SPP data write error");
                    esp_ota_abort(update_handle);
                    btspp_send_msg("ABORT!\r\n", 2000);
                    return false;
                };
            }
            // else {
            //     // All bytes have been uploaded
            //     break;
            // }
        } 
    } 
    while (total_bytes_read < binary_file_length);

    // check that the upload is complete
    data_send = btspp_send_msg("UPLOAD COMPLETE?\r\n", 2000);
    if (!data_send) { return false; }
    data_read = btspp_recv_msg((char*) ota_write_data, BUFFSIZE, "\r\n",  1000,  100);
    if (data_read <= 0) {
        ESP_LOGE(OTA_TAG, "Error: SPP data read error");
        esp_ota_abort(update_handle);
        btspp_send_msg("ABORT!\r\n", 2000);
        return false;
    }
    else {
        if (data_read != 5) {
            ESP_LOGE(OTA_TAG, "Error: SPP data read error");
            esp_ota_abort(update_handle);
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        }
        else if (strncmp((char*) ota_write_data, "YES\r\n", 5) != 0) {
            ESP_LOGE(OTA_TAG, "Error: SPP answer error");
            esp_ota_abort(update_handle);
            btspp_send_msg("ABORT!\r\n", 2000);
            return false;
        }
    }

    ESP_LOGI(OTA_TAG, "Total Write binary data length: %llu", total_bytes_read);
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(OTA_TAG, "Image validation failed, image is corrupted");
            data_send = btspp_send_msg("VALIDATION FAILED, IMAGE IS CORRUPTED!\r\n", 2000);
            
        } else {
            ESP_LOGE(OTA_TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
            data_send = btspp_send_msg("OTA ERROR!\r\n", 2000);
        }
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        data_send = btspp_send_msg("OTA ERROR!\r\n", 2000);
        return false;
    }

    data_send = btspp_send_msg("OK!\r\n", 2000);
    ESP_LOGI(OTA_TAG, "Prepare to restart system!");
    for (uint32_t i = 5; i > 0; --i) {
        ESP_LOGI(OTA_TAG, "Restarting in %u...", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(OTA_TAG, "Restarting system...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return true;

}








// Bluetooth parameter
#define SPP_SERVER_NAME HARDWARE_CONFIG_SPP_SERVICE_NAME
#define SPP_CHANNEL HARDWARE_CONFIG_SPP_CHANNEL
#define BT_DEVICE_NAME_MAX_LEN 32
static char BT_DEVICE_NAME[BT_DEVICE_NAME_MAX_LEN+1] = HAREWARE_CONFIG_BT_DEVICE_NAME;
static const esp_spp_mode_t esp_spp_mode = ESP_SPP_MODE_CB; // When data is coming, a callback will come with data
static const esp_spp_sec_t sec_mask = ESP_SPP_SEC_AUTHENTICATE;
static const esp_spp_role_t role_slave = ESP_SPP_ROLE_SLAVE;
static uint32_t spp_connection_handle = 0;

// Ringbuffer and EventGroup for Sending and Receiving Data
#define SPP_CTS_STATUS_EVENTBIT ((EventBits_t) 0x01)
#define SPP_WRITE_COMPLETE_STATUS_EVENTBIT ((EventBits_t) 0x02)
#define SPP_DATA_AVAILABLE_STATUS_EVENTBIT ((EventBits_t) 0x04)
static RingbufHandle_t xSppBuffer = NULL;
static EventGroupHandle_t xSppEventGroup = NULL;
static btspp_da_cb_t* da_callback = NULL;
static void* da_ctx = NULL;




// Callback function for GAP (Generic Access Profile) events
// Based on the expressif SPP acceptor example
static void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT: { // Authentication complete event
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(GAP_TAG, "authentication success: %s", param->auth_cmpl.device_name);
                esp_log_buffer_hex(GAP_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
            } 
            else {
                ESP_LOGE(GAP_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
            }
            break;
        }

        case ESP_BT_GAP_PIN_REQ_EVT:{
            ESP_LOGI(GAP_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
            if (param->pin_req.min_16_digit) {
                ESP_LOGI(GAP_TAG, "Input pin code: 0000 0000 0000 0000");
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            } else {
                ESP_LOGI(GAP_TAG, "Input pin code: 1234");
                esp_bt_pin_code_t pin_code;
                pin_code[0] = '1';
                pin_code[1] = '2';
                pin_code[2] = '3';
                pin_code[3] = '4';
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        }

        #if (CONFIG_BT_SSP_ENABLED == true)
        case ESP_BT_GAP_CFM_REQ_EVT:
            ESP_LOGI(GAP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        case ESP_BT_GAP_KEY_NOTIF_EVT:
            ESP_LOGI(GAP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
            break;
        case ESP_BT_GAP_KEY_REQ_EVT:
            ESP_LOGI(GAP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
            break;
        #endif

        case ESP_BT_GAP_MODE_CHG_EVT:
            ESP_LOGI(GAP_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d", param->mode_chg.mode);
            break;

        default: {
            ESP_LOGI(GAP_TAG, "event: %d", event);
            break;
        }
    }
    return;
}


// Callback function for SSP (Serial Port Profile) events
// Based on the expressif SPP acceptor example
static void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
        case ESP_SPP_INIT_EVT: // When SPP is inited, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_INIT_EVT");

            // Start SPP Server
            esp_spp_start_srv(sec_mask, role_slave, SPP_CHANNEL, SPP_SERVER_NAME);

            break;

        case ESP_SPP_DISCOVERY_COMP_EVT: // When SDP discovery complete, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_DISCOVERY_COMP_EVT");
            break;

        case ESP_SPP_OPEN_EVT: // When SPP Client connection open, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_OPEN_EVT handle=%d", param->open.handle); // param->open.rem_bda
            break;

        case ESP_SPP_CLOSE_EVT: // When SPP connection closed, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_CLOSE_EVT");

            // reset handle used in btspp_send()
            spp_connection_handle = 0;
            break;

            break;

        case ESP_SPP_START_EVT: // When SPP server started, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_START_EVT");

            // Set Bluetooth Device Name
            esp_bt_dev_set_device_name(BT_DEVICE_NAME);

            // Make Bluetooth device discoverable and connectable
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

            break;

        case ESP_SPP_CL_INIT_EVT: // When SPP client initiated a connection, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_CL_INIT_EVT");
            break;

        case ESP_SPP_DATA_IND_EVT: // When SPP connection received data, the event comes, only for ESP_SPP_MODE_CB
            ESP_LOGD(SPP_TAG, "ESP_SPP_DATA_IND_EVT len=%d handle=%d", param->data_ind.len, param->data_ind.handle);
            // esp_log_buffer_hex("",param->data_ind.data,param->data_ind.len);

            // if (!ota_in_progress) {
            //     if (param->data_ind.len == BT_OTA_START_CMD_LENGTH) {
            //         if (strncmp((const char*) aram->data_ind.data, BT_OTA_START_CMD, BT_OTA_START_CMD_LENGTH) == 0) {
            //             ota_in_progress = true;
            //             break;
            //         }
            //     }
            // }

            // excecute the callback function
            if (da_callback != NULL) { da_callback(da_ctx, param->data_ind.data, param->data_ind.len); }
            
            // put the received data in the ring buffer
            xRingbufferSend(xSppBuffer, param->data_ind.data, param->data_ind.len, 0);

            // Set status bit for data available
            xEventGroupSetBits(xSppEventGroup, SPP_DATA_AVAILABLE_STATUS_EVENTBIT);

            break;

        case ESP_SPP_CONG_EVT: // When SPP connection congestion status changed, the event comes, only for ESP_SPP_MODE_CB
            ESP_LOGD(SPP_TAG, "ESP_SPP_CONG_EVT: %s", (param->cong.cong ? "congested" : "uncongested"));
            if (param->cong.cong == true) { // check congestion status
                xEventGroupClearBits(xSppEventGroup, SPP_CTS_STATUS_EVENTBIT); 
            }
            else {
                xEventGroupSetBits(xSppEventGroup, SPP_CTS_STATUS_EVENTBIT);
            }
            break;

        case ESP_SPP_WRITE_EVT: // When SPP write operation completes, the event comes, only for ESP_SPP_MODE_CB
            ESP_LOGD(SPP_TAG, "ESP_SPP_WRITE_EVT handle=%d: %s", param->write.handle, (param->write.cong ? "congested" : "uncongested"));
            xEventGroupSetBits(xSppEventGroup, SPP_WRITE_COMPLETE_STATUS_EVENTBIT);
            if (param->write.cong == true) { // check congestion status
                xEventGroupClearBits(xSppEventGroup, SPP_CTS_STATUS_EVENTBIT); 
            }
            else {
                xEventGroupSetBits(xSppEventGroup, SPP_CTS_STATUS_EVENTBIT);
            }
            break;

        case ESP_SPP_SRV_OPEN_EVT: // When SPP Server connection open, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_OPEN_EVT handle=%d, new_listen_handle=%d", param->srv_open.handle, param->srv_open.new_listen_handle);

            // Set handle used in btspp_send()
            spp_connection_handle = param->srv_open.handle;
            break;

        case ESP_SPP_SRV_STOP_EVT: // When SPP server stopped, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_STOP_EVT");
            break;

        case ESP_SPP_UNINIT_EVT: // When SPP is uninited, the event comes
            ESP_LOGI(SPP_TAG, "ESP_SPP_UNINIT_EVT");
            break;

        default:
            break;
    }
}





// Init everything nedded for SPP
// Based on the expressif SPP acceptor example
void btspp_init(const char* device_name, const uint32_t ringbuf_size) {

    // Copy bluetooth device name
    strncpy(BT_DEVICE_NAME, device_name, BT_DEVICE_NAME_MAX_LEN);
    BT_DEVICE_NAME[BT_DEVICE_NAME_MAX_LEN] = '\0'; // make sure that we have a c-string

    // Create event group
    xSppEventGroup = xEventGroupCreate();
    xEventGroupSetBits(xSppEventGroup, SPP_CTS_STATUS_EVENTBIT | SPP_WRITE_COMPLETE_STATUS_EVENTBIT);
    assert(xSppEventGroup != NULL);

    // Create ring buffer
    xSppBuffer = xRingbufferCreate(ringbuf_size, RINGBUF_TYPE_BYTEBUF);
    assert(xSppBuffer != NULL);



    // We don't use BLE mode
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    // save return value for error checking
    esp_err_t ret;

    // Init bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(BT_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    // Enable bluetooth controller
    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(BT_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    // Init the 'bluedroid' bluetooth stack
    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    // Enable the 'bluedroid' bluetooth stack
    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    // Register the callback function for GAP events
    if ((ret = esp_bt_gap_register_callback(esp_bt_gap_cb)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s gap register callback failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    // Register the callback function for SPP events
    if ((ret = esp_spp_register_callback(esp_spp_cb)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s spp register callback failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    // Init the Serial Port Profile
    if ((ret = esp_spp_init(esp_spp_mode)) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s spp init failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }


    #if (CONFIG_BT_SSP_ENABLED == true)
    /* Set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
    #endif

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

}





// Wait for CTS event and send data via SPP
bool btspp_send(const uint8_t* const data, const uint32_t len, const uint32_t timeout_ms) {

    // make sure that a client is connected
    if (spp_connection_handle == 0) { return -1; }

    // Wait for CTS status
    EventBits_t status = xEventGroupWaitBits(
        xSppEventGroup, SPP_CTS_STATUS_EVENTBIT,
        0, pdTRUE, (timeout_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms))
    );

    // check if the right bit ist set
    const bool cts_status = (status & SPP_CTS_STATUS_EVENTBIT);

    //  return a timeout if no cts
    if (!cts_status) { return -2; }


    // send data vis spp
    xEventGroupClearBits(xSppEventGroup, SPP_WRITE_COMPLETE_STATUS_EVENTBIT);
    esp_spp_write(spp_connection_handle, len, (uint8_t*) data);
    // esp_spp_write(spp_connection_handle, len, data);

    // Wait for write complete event
    status = xEventGroupWaitBits(
        xSppEventGroup, SPP_WRITE_COMPLETE_STATUS_EVENTBIT,
        0, pdTRUE, portMAX_DELAY
    );

    return true;
}


// Wait for and read data via SPP
int btspp_recv(uint8_t* data, const uint32_t bufsize, const uint32_t timeout_ms) {

    // Its better to check 
    if (data == NULL) { return -1; }

    // Receive Data from the Ringbuffer
    size_t xItemSize = 0;
    uint8_t* item = xRingbufferReceiveUpTo(
        xSppBuffer, &xItemSize, 
        (timeout_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)), 
        bufsize
    ); 

    // Timeout
    if (item == NULL) { return -2; }
    else {
        // copy data into new buffer
        memcpy(data, item, xItemSize);

        // return item pointer to the ringbuffer
        vRingbufferReturnItem(xSppBuffer, item);

        return xItemSize;
    }
}





// Wait for CTS event and send data via SPP
bool btspp_send_data(const uint8_t* const data, const uint32_t len, const uint32_t timeout_ms) {
    return btspp_send(data, len, timeout_ms);
}


// Wait for CTS event and send message via SPP
bool btspp_send_msg(const char* const msg, const uint32_t timeout_ms) {
    const uint32_t len = strlen(msg);
    return btspp_send((const uint8_t*) msg, len, timeout_ms);
}





// Wait for and read data via SPP
// Try to read as much data from the ringbuffer as possible
int btspp_recv_data(uint8_t* data, const uint32_t bufsize, const uint32_t timeout_ms, const uint32_t delay_ms) {

    // Its better to check 
    if (data == NULL) { return -1; }

    // Keep track of how much data has already been received
    int total_bytes_read = 0;
    int free_buffer_space = bufsize;
    uint8_t* buffer = data;
    uint32_t time_to_wait_ms = timeout_ms; // Wait longer for the first chunk of bytes

    // Receive Data from the Ringbuffer
    size_t xItemSize = 0;
    uint8_t* item = xRingbufferReceiveUpTo(
        xSppBuffer, &xItemSize, 
        (time_to_wait_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(time_to_wait_ms)),
        free_buffer_space
    ); 

    // Timeout
    if (item == NULL) { return -2; }
    else {
        // copy data into new buffer
        memcpy(buffer, item, xItemSize);

        // return item pointer to the ringbuffer
        vRingbufferReturnItem(xSppBuffer, item);

        // Keep track of how much data has already been received
        total_bytes_read += xItemSize;
        free_buffer_space -= xItemSize;

        // Set Pointer to the next free buffer position
        buffer += xItemSize;
    }

    // Wait longer for the first chunk of bytes
    // Wait shorter for the next chunk of bytes
    time_to_wait_ms = delay_ms;

    // Try to read as much data from the ringbuffer as possible
    // Two calls to RingbufferReceiveUpTo() are required 
    // if the bytes wrap around the end of the ring buffer.
    while (free_buffer_space > 0) {

        // Receive more Data from the Ringbuffer
        xItemSize = 0;
        item = xRingbufferReceiveUpTo(
            xSppBuffer, &xItemSize, 
            (time_to_wait_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(time_to_wait_ms)),
            free_buffer_space
        ); 

        // Ringbuffer is empty
        if (item == NULL) { break; }
        else {
            // copy data into new buffer
            memcpy(buffer, item, xItemSize);

            // return item pointer to the ringbuffer
            vRingbufferReturnItem(xSppBuffer, item);

            // Keep track of how much data has already been received
            total_bytes_read += xItemSize;
            free_buffer_space -= xItemSize;

            // Set Pointer to the next free buffer position
            buffer += xItemSize;
        }

    }

    return total_bytes_read;
}


// Wait for and read data via SPP
int btspp_recv_msg(char* msg, const uint32_t bufsize, const char* delimiter, const uint32_t timeout_ms, const uint32_t delay_ms) {

    // Its better to check 
    if (msg == NULL) { return -1; }
    if (delimiter == NULL) { return -1; }
    
    // Some variables
    int total_bytes_read = 0; // Keep track of how many bytes have been read
    int free_buffer_space = bufsize - 1; // Save one space for the '\0'
    int delimiter_len = strlen(delimiter); // How long is the delimiter string
    size_t xItemSize = 0; // Needed for 'xRingbufferReceiveUpTo()'
    char* item = NULL; // Needed for 'xRingbufferReceiveUpTo()'
    char* msg_tail; // Points to the last bytes of the message.
    char* buffer = msg; // Points to the next free position of the msg-buffer
    uint32_t time_to_wait_ms = timeout_ms; // Wait longer for the first character

     // Receive characters from the Ringbuffer
    while (free_buffer_space > 0) {

        // Wait longer for the first character
        // Wait shorter for the next characters
        if (total_bytes_read > 0) { time_to_wait_ms = delay_ms; }

        // Receive next character from the Ringbuffer
        xItemSize = 0;
        item = xRingbufferReceiveUpTo(xSppBuffer, &xItemSize, 
            (time_to_wait_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)), 1
        ); 

        // Ringbuffer is empty
        if (item == NULL) { break; }
        else {
            // copy data into new buffer
            memcpy(buffer, item, xItemSize); // xItemsize should be equal to 1

            // return item pointer to the ringbuffer
            vRingbufferReturnItem(xSppBuffer, item);

            // Keep track of how much data has already been received
            total_bytes_read += xItemSize;
            free_buffer_space -= xItemSize;

            // Set Pointer to the next free buffer position
            buffer += xItemSize; // xItemsize should be equal to 1

            // check if the end of the message contains the delimiter
            if (total_bytes_read >= delimiter_len) {
                
                msg_tail = buffer - delimiter_len; // Points to the last 'n' bytes of the message.
                if (strncmp(msg_tail, delimiter, delimiter_len) == 0) {
                
                    // append the message with a null byte
                    *buffer = '\0';
                    return total_bytes_read;
                }
            }

        }
    }

    // We get here if the buffer is full or a timeout occured
    if (free_buffer_space == 0) { //
        // append the message with a null byte
        *buffer = '\0';
        return -4; // buffer full
    }
    else if (total_bytes_read == 0) {
        // append the message with a null byte
        *buffer = '\0';
        return -2; // timeout on first character
    }
    else {
        // append the message with a null byte
        *buffer = '\0';
        return -3; // timeout on intermediate character
    }
}



// Register a callback that gets called when new data arrives
void btspp_register_data_available_callback(btspp_da_cb_t* const callback, void* const ctx) {
    da_callback = callback;
    da_ctx = ctx;
}









