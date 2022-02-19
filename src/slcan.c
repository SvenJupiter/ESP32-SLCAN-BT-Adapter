#include "slcan.h"

#include "HardwareConfig.h"

// Some more standard header
#include <stdint.h> // uint<X>_t
#include <stdio.h> // sscanf, snprintf
#include <string.h> // strlen, strcmp, strcpy


// Bluetooth SPP
#include "btspp.h"
#include "buffer_access.h"
#include "file_access.h"


// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// CAN API
#include "driver/twai.h" // #warning driver/can.h is deprecated, please use driver/twai.h instead

// Timer API
#include "esp_timer.h" // esp_timer_get_time




// Header for debug messages
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#define CAN_TAG "CAN"
#define SLCAN_TAG "SLCAN"

// Filenames for EEPROM data
#define TIMING_FILENAME "timing_config.bin"
#define FILTER_FILENAME "filter_config.bin"
#define SLCAN_FILENAME "slcan_config.bin"


// Constants for CAN-Driver (TWAI-Driver)
#define CAN_TX_PIN HAREWARE_CONFIG_CAN_TX_PIN // Hardware dependend
#define CAN_RX_PIN HAREWARE_CONFIG_CAN_RX_PIN // Hardware dependend
#define CAN_TX_QUEUE_SIZE 10
#define CAN_RX_QUEUE_SIZE 1024

// Configs used for twai_driver_install()
static twai_timing_config_t timing_config = TWAI_TIMING_CONFIG_500KBITS(); // Saved in EEPROM
static twai_filter_config_t filter_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); // Saved in EEPROM
static twai_general_config_t can_config =  {                             
    .mode = TWAI_MODE_NORMAL, .tx_io = CAN_TX_PIN, .rx_io = CAN_RX_PIN,  
    .clkout_io = TWAI_IO_UNUSED, .bus_off_io = TWAI_IO_UNUSED,           
    .tx_queue_len = CAN_TX_QUEUE_SIZE, .rx_queue_len = CAN_RX_QUEUE_SIZE,
    .alerts_enabled = TWAI_ALERT_ALL, .clkout_divider = 0,              
    .intr_flags = ESP_INTR_FLAG_LEVEL1                                   
}; // Fixed (Hardware dependend)




// struct to hold the slcan configuration (Saved in EEPROM)
typedef struct {

    bool auto_poll_enabled;
    bool timestamps_enabled;
    bool auto_startup_enabled;
    bool startup_in_listen_mode;

} slcan_config_t;

static slcan_config_t slcan_config = {
    .auto_poll_enabled = true,
    .timestamps_enabled = false,
    .auto_startup_enabled = false,
    .startup_in_listen_mode = false
};

// Flag thats indicats the status of the can driver
static bool can_channel_initiated = false; // A baudrate has been set via the 'S' or 's' command
static bool can_channel_open = false;
static bool listen_mode_only = false;

// Constants for SLCAN messages
#define CR '\r'
#define BELL '\b'
#define OK "\r"
#define zOK "z\r"
#define ERROR "\b"


// Store Timing confiuration in EEPROM
static void save_timing_config_to_eeprom() {
    write_data_to_storage(TIMING_FILENAME, (const uint8_t*) &timing_config, sizeof(timing_config));
}

// Store Filter confiuration in EEPROM
static void save_filter_config_to_eeprom() {
    write_data_to_storage(FILTER_FILENAME, (const uint8_t*) &filter_config, sizeof(filter_config));
}

// Store SLCAN confiuration in EEPROM
static void save_slcan_config_to_eeprom() {
    write_data_to_storage(SLCAN_FILENAME, (const uint8_t*) &slcan_config, sizeof(slcan_config));
}


// Restore Timing confiuration from EEPROM
static void restore_timing_config_from_eeprom() {
    read_data_from_storage(TIMING_FILENAME, (uint8_t*) &timing_config, sizeof(timing_config));
}

// Restore Filter confiuration from EEPROM
static void restore_filter_config_from_eeprom() {
    read_data_from_storage(FILTER_FILENAME, (uint8_t*) &filter_config, sizeof(filter_config));
}

// Restore SLCAN confiuration from EEPROM
static void restore_slcan_config_from_eeprom() {
    read_data_from_storage(SLCAN_FILENAME, (uint8_t*) &slcan_config, sizeof(slcan_config));
}

/**
 * @brief // Convert a CAN frame to a SLCAN message
 * 
 * @param [in] message The CAN-Bus frame
 * @param [in] auto_poll_enabled Is auto-poll for SLCAN enabled? (Changes the Termination from 'OK' to 'zOK')
 * @param [in] timestamp_enabled Are Timestambs enabled? Append a tinestamp at the end of the SLCAN message
 * @param [in] timestamp_ms CAN frame tinestamp (only used if 'timestamp_enabled' is true)
 * @param [out] buffer The SLCAN message will be written to the this location
 * @param [in] bufsize Size of the output buffer (should be at least 35 bytes)
 * @return int SLCAN message length 
 */
static int can2sl(const twai_message_t* message, const bool auto_poll_enabled, const bool timestamp_enabled, const uint32_t timestamp_ms, char* const buffer, const uint32_t bufsize) {

    // check (just in case)
    if (message == NULL) { return -1; } // invalid pointer
    if (buffer == NULL) { return -1; } // invalid pointer
    if (bufsize < 35) { return -1; } // buffer to small (1 + 8 + 1 + 16 + 4 + 2 + 1 == 33)

    // message pointer
    char* msg = buffer;
    int len = 0;
    int msg_len = 0;


    // 1. cmd char
    // use math (ASCII values) instead of complicated nested if-structure
    msg[0] = 't' - message->rtr * 2 - message->extd * 32; // 't', 'r', 'T', 'R'
    msg += 1; // update message pointer
    msg_len += 1;


    // 2. identifier
    /** '*' (width) -> The width is not specified in the format string, 
     * but as an additional integer value argument 
     * preceding the argument that has to be formatted.
     */
    len = sprintf(msg, "%0*X", (message->extd ? 8 : 3), message->identifier);
    msg += len; // update message pointer
    msg_len += len;


    // 3. dlc
    len = sprintf(msg, "%01X", message->data_length_code);
    msg += len; // update message pointer
    msg_len += len;


    // 4. data
    if (message->rtr == 0) {
        for (uint32_t i = 0; i < message->data_length_code; ++i) {
            len = sprintf(msg, "%02X", message->data[i]);
            msg += len; // update message pointer
        msg_len += len;
        }
    }


    // 5. timestamp
    if (timestamp_enabled) {
        len = sprintf(msg, "%04X", timestamp_ms);
        msg += len; // update message pointer
        msg_len += len;
    }


    // 6. zOK / OK
    len = sprintf(msg, "%s", (auto_poll_enabled ? zOK : OK));
    msg += len; // update message pointer
    msg_len += len;

    return msg_len;
}

// The background task for SLCANs auto-poll feature
static void auto_poll_task(void* args) {

    ESP_LOGI(SLCAN_TAG, "Starting Auto-Poll Task");

    // Response buffer for slcan messages
    char response_buffer[64];
    twai_message_t message = {};
    esp_err_t err = ESP_OK;

    // Run while the CAN channel is open and the auto-poll feature is enabled
    while (can_channel_open && slcan_config.auto_poll_enabled) { 

        // Receive a single CAN frame from the queue
        err = twai_receive(&message, pdMS_TO_TICKS(1000));
        
        if (err == ESP_ERR_TIMEOUT) {
            // If there are no pending frames just continue
            ESP_LOGV(SLCAN_TAG, "Auto-Poll: No pending frames");
            continue;
        }
        else if (err != ESP_OK) {
            // Stop the task if 'twai_receive' returns an error
            ESP_LOGW(SLCAN_TAG, "Auto-Poll: twai_receive ERROR %d", err);
            btspp_send_msg(ERROR, 1000);
            break;
        }
        else {
            ESP_LOGV(SLCAN_TAG, "Auto-Poll: New frame received");

            // converting CAN frame to SLCAN message
            const int result = can2sl(
                &message, true, 
                slcan_config.timestamps_enabled, esp_timer_get_time() % 60000LL, 
                response_buffer, sizeof(response_buffer)
            );

            // Sending response
            btspp_send_msg(response_buffer, 1000);
            ESP_LOGI(SLCAN_TAG, "Auto-Poll: Responding: (len = %d): %s", result, response_buffer);
            continue;
        }


    }

    // Terminate task
    ESP_LOGI(SLCAN_TAG, "Stopping Auto-Poll Task");
    vTaskDelete(NULL);
}

// Start the background task for SLCANs auto-poll feature
// Restict it to the APP-CPU-Core so it doesn't interfere with the bluetooth task on the Pro-CPU-Core
static void start_auto_poll_task() {
    xTaskCreatePinnedToCore(auto_poll_task, "SLCAN-AUTO-POLL", 8 * 1024, NULL, 16, NULL, 1);
}

// Open the CAN channel
static bool open_can_channel() {

    // install driver
    esp_err_t err = twai_driver_install(&can_config, &timing_config, &filter_config);
    if (err != ESP_OK) { 
        return false; 
    }

    // start driver
    err = twai_start();
    if (err != ESP_OK) { 
        twai_driver_uninstall(); 
        return false; 
    }
    listen_mode_only = (can_config.mode == TWAI_MODE_LISTEN_ONLY ? true : false);
    can_channel_open = true;

    // start auto poll task
    if (slcan_config.auto_poll_enabled) {
        start_auto_poll_task();
    }

    return true;
}

// Close the CAN channel
static bool close_can_channel() {

    // signal to the auto-poll task
    can_channel_open = false;

    if (slcan_config.auto_poll_enabled) {
        // Wait untill auto poll task is terminated
        vTaskDelay(pdMS_TO_TICKS(1100));
    }

    // stop the driver
    twai_stop();

    // uninstall the driver
    twai_driver_uninstall();

    return true;
}

/**
 * @brief Process a received SLCAN message
 * 
 * @param cmd SLCAN message
 * @return true Success
 * @return false Error
 */
static bool slcan_process_cmd(const char* cmd) {

    // Response buffer for slcan messages
    char response_buffer[64];

    // Its better to check
    if (cmd == NULL) { return false; }
    uint32_t cmd_len = strlen(cmd);
    ESP_LOGI(SLCAN_TAG, "Processing: %s", cmd);

    // The first char is the command
    const char command = cmd[0];
    switch (command) {

        /** Sn[CR]
         * Setup with standard CAN bit-rates where n is 0-8.
         * This command is only active if the CAN channel is closed.
         * 
         * S0 - Setup 10Kbit
         * S1 - Setup 20Kbit
         * S2 - Setup 50Kbit
         * S3 - Setup 100Kbit
         * S4 - Setup 125Kbit
         * S5 - Setup 250Kbit
         * S6 - Setup 500Kbit
         * S7 - Setup 800Kbit
         * S8 - Setup 1Mbit
         * 
         * Example: S4[CR] - Setup CAN to 125Kbit.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'S': {
            if (cmd_len != 3 || cmd[2] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (can_channel_open) {
                // This command is only active if the CAN channel is closed.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                const char value = cmd[1];
                switch (value) {
                    
                    case '0':
                        // maybe not supported by hardware?
                        // timing_config = TWAI_TIMING_CONFIG_10KBITS();
                        timing_config.brp = 400;
                        timing_config.tseg_1 = 15;
                        timing_config.tseg_2 = 4;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;
                    
                    case '1':
                        // maybe not supported by hardware?
                        // timing_config = TWAI_TIMING_CONFIG_20KBITS();
                        timing_config.brp = 200;
                        timing_config.tseg_1 = 15;
                        timing_config.tseg_2 = 4;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;
                    
                    case '2':
                        // timing_config = TWAI_TIMING_CONFIG_50KBITS();
                        timing_config.brp = 80;
                        timing_config.tseg_1 = 15;
                        timing_config.tseg_2 = 4;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;
                    
                    case '3':
                        // timing_config = TWAI_TIMING_CONFIG_100KBITS();
                        timing_config.brp = 40;
                        timing_config.tseg_1 = 15;
                        timing_config.tseg_2 = 4;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;
                    
                    case '4':
                        // timing_config = TWAI_TIMING_CONFIG_125KBITS();
                        timing_config.brp = 32;
                        timing_config.tseg_1 = 15;
                        timing_config.tseg_2 = 4;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;
                    
                    case '5':
                        // timing_config = TWAI_TIMING_CONFIG_250KBITS();
                        timing_config.brp = 16;
                        timing_config.tseg_1 = 15;
                        timing_config.tseg_2 = 4;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;
                    
                    case '6':
                        // timing_config = TWAI_TIMING_CONFIG_500KBITS();
                        timing_config.brp = 8;
                        timing_config.tseg_1 = 15;
                        timing_config.tseg_2 = 4;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;
                    
                    case '7':
                        // timing_config = TWAI_TIMING_CONFIG_800KBITS();
                        timing_config.brp = 4;
                        timing_config.tseg_1 = 16;
                        timing_config.tseg_2 = 8;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;

                    case '8':
                        // timing_config = TWAI_TIMING_CONFIG_1MBITS();
                        timing_config.brp = 4;
                        timing_config.tseg_1 = 15;
                        timing_config.tseg_2 = 4;
                        timing_config.sjw = 3;
                        timing_config.triple_sampling = false;
                        break;

                    default:
                        // can_channel_initiated = false;
                        btspp_send_msg(ERROR, 1000);
                        return false;
                }

                can_channel_initiated = true;
                save_timing_config_to_eeprom();
                btspp_send_msg(OK, 1000);
                return true;
            }

        }
        break;

        /** sxxyy[CR]
         * Setup with BTR0/BTR1 CAN bit-rates where xx and yy is a hex value. 
         * This command is only active if the CAN channel is closed.
         * 
         * xx - BTR0 value in hex
         * yy - BTR1 value in hex
         * 
         * Example: s031C[CR] - Setup CAN with BTR0=0x03 & BTR1=0x1C which equals to 125Kbit.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 's': {
            // Not implemented
            btspp_send_msg(ERROR, 1000);
            return false;
        }
        break;

        /** O[CR]
         * Open the CAN channel in normal mode (sending & receiving).
         * This command is only active if the CAN channel is closed and
         * has been set up prior with either the S or s command (i.e. initiated).
         * 
         * Example: O[CR] - Open the channel, yellow LED is turned ON.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'O': {
            if (cmd_len != 2 || cmd[1] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_initiated || can_channel_open) {
                // This command is only active if the CAN channel is closed and
                // has been set up prior with either the S or s command (i.e. initiated).
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                // Open in normal mode
                can_config.mode = TWAI_MODE_NORMAL;

                if (open_can_channel()) { btspp_send_msg(OK, 1000); }
                else { btspp_send_msg(ERROR, 1000); }
                return true;
            }
        }
        break;

        /** L[CR]
         * Open the CAN channel in listen only mode (receiving).
         * This command is only active if the CAN channel is closed and
         * has been set up prior with either the S or s command (i.e. initiated).
         * 
         * Note: It’s not possible to send CAN frames (t, T, r & R)
         * 
         * Example: L[CR] -  Open the channel, yellow LED is blinking.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'L': {
            if (cmd_len != 2 || cmd[1] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_initiated || can_channel_open) {
                // This command is only active if the CAN channel is closed and
                // has been set up prior with either the S or s command (i.e. initiated).
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                // Open in listen only mode
                can_config.mode = TWAI_MODE_LISTEN_ONLY;

                if (open_can_channel()) { btspp_send_msg(OK, 1000); }
                else { btspp_send_msg(ERROR, 1000); }
                return true;
            }
        }
        break;

        /** C[CR]
         * Close the CAN channel.
         * This command is only active if the CAN channel is open.
         * 
         * Example: C[CR] - Close the channel, yellow LED is turned OFF.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'C': {
            if (cmd_len != 2 || cmd[1] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open) {
                // This command is only active if the CAN channel is open.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                if (close_can_channel()) { btspp_send_msg(OK, 1000); }
                else { btspp_send_msg(ERROR, 1000); }
                return true;
            }
        }
        break;

        /** tiiildd...[CR]
         * Transmit a standard (11bit) CAN frame.
         * This command is only active if the CAN232 is open in normal mode.
         * 
         * iii  - Identifier in hex (000-7FF)
         * l    - Data length (0-8)
         * dd   - Byte value in hex (00-FF). 
         *        Numbers of dd pairs must match the data length, 
         *        otherwise an error occur.
         * 
         * Example 1: t10021133[CR]
         * Sends an 11bit CAN frame with ID=0x100, 2 bytes
         * with the value 0x11 and 0x33.
         * 
         * Example 2: t0200[CR]
         * Sends an 11bit CAN frame with ID=0x20 & 0 bytes.
         * 
         * Returns: If Auto Poll is disabled (default) the CAN232 replies
         * CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         * If Auto Poll is enabled (see X command) the CAN232
         * replies z[CR] for OK or BELL (Ascii 7) for ERROR.
         */
        case 't': {
            if (cmd_len < 5) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open || listen_mode_only) {
                // This command is only active if the CAN232 is open in normal mode.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {

                // Parse identifier from cmd
                char identifier_string[4] = "xxx";
                uint32_t identifier = 0;
                strncpy(identifier_string, cmd+1, 3);
                int result = sscanf(identifier_string, "%x", &identifier);

                // Parse data length from cmd
                const uint8_t dlc = (uint8_t) (cmd[4] - '0');

                // check identifier
                if (result != 1 || identifier > 0x7FF) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }

                // check dlc 
                if (dlc > 8) {
                    btspp_send_msg(ERROR, 1000);
                    return false;   
                }

                // check cmd length
                if (cmd_len != 6 + 2*dlc || cmd[6 + 2*dlc -1] != CR) { 
                    btspp_send_msg(ERROR, 1000);
                    return false; 
                }

                // init can frame
                twai_message_t message = {
                    .identifier = identifier,
                    .data_length_code = dlc
                };

                // Parse data values
                char data_value_string[3] = "xx";
                uint32_t data_value = 0;
                for (uint8_t k = 0; k < dlc; ++k) {

                    // Parse data values from cmd
                    strncpy(data_value_string, cmd+5 + 2*k, 2);
                    result = sscanf(data_value_string, "%x", &data_value);

                    // check
                    if (result != 1) {
                        btspp_send_msg(ERROR, 1000);
                        return false;
                    }
                    else {
                        message.data[k] = data_value;
                    }
                }

                // Send can frame
                esp_err_t err = twai_transmit(&message, 10);
                if (err != ESP_OK) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }
                else {
                    if (slcan_config.auto_poll_enabled) {
                        btspp_send_msg(zOK, 1000);
                        return false;
                    }
                    else {
                        btspp_send_msg(OK, 1000);
                        return false;
                    }
                }

            }
        }
        break;

        /** Tiiiiiiiildd...[CR]
         * Transmit an extended (29bit) CAN frame.
         * This command is only active if the CAN channel is open.
         * 
         * iiiiiiii - Identifier in hex (00000000-1FFFFFFF)
         * l        - Data length (0-8)
         * dd       - Byte value in hex (00-FF). 
         *            Numbers of dd pairs must match the data length, 
         *            otherwise an error occur.
         * 
         * Example 1: T0000010021133[CR]
         * Sends a 29bit CAN frame with ID=0x100, 2 bytes
         * with the value 0x11 and 0x33.
         * 
         * Returns: If Auto Poll is disabled (default) the CAN232 replies
         * CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         * If Auto Poll is enabled (see X command) the CAN232
         * replies z[CR] for OK or BELL (Ascii 7) for ERROR.
         */
        case 'T': {
            if (cmd_len < 10) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open || listen_mode_only) {
                // This command is only active if the CAN232 is open in normal mode.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {

                // Parse identifier from cmd
                char identifier_string[9] = "xxxxxxxx";
                uint32_t identifier = 0;
                strncpy(identifier_string, cmd+1, 8);
                int result = sscanf(identifier_string, "%x", &identifier);

                // Parse data length from cmd
                const uint8_t dlc = (uint8_t) (cmd[9] - '0');

                // check identifier
                if (result != 1 || identifier > 0x1FFFFFFF) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }

                // check dlc 
                if (dlc > 8) {
                    btspp_send_msg(ERROR, 1000);
                    return false;   
                }

                // check cmd length
                if (cmd_len != 11 + 2*dlc || cmd[10 + 2*dlc] != CR) { 
                    btspp_send_msg(ERROR, 1000);
                    return false; 
                }

                // init can frame
                twai_message_t message = {
                    .extd = 1,
                    .identifier = identifier,
                    .data_length_code = dlc
                };

                // Parse data values
                char data_value_string[3] = "xx";
                uint32_t data_value = 0;
                for (uint8_t k = 0; k < dlc; ++k) {

                    // Parse data values from cmd
                    strncpy(data_value_string, cmd+10 + 2*k, 2);
                    result = sscanf(data_value_string, "%x", &data_value);

                    // check
                    if (result != 1) {
                        btspp_send_msg(ERROR, 1000);
                        return false;
                    }
                    else {
                        message.data[k] = data_value;
                    }
                }

                // Send can frame
                esp_err_t err = twai_transmit(&message, 10);
                if (err != ESP_OK) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }
                else {
                    if (slcan_config.auto_poll_enabled) {
                        btspp_send_msg(zOK, 1000);
                        return false;
                    }
                    else {
                        btspp_send_msg(OK, 1000);
                        return false;
                    }
                }

            }
        }
        break;

        /** riiil[CR]
         * Transmit an standard RTR (11bit) CAN frame.
         * This command is only active if the CAN232 is open in normal mode.
         * 
         * iii  - Identifier in hex (000-7FF)
         * l    - Data length (0-8)
         * 
         * Example 1: r1002[CR]
         * Sends an 11bit RTR CAN frame with ID=0x100
         * and DLC set to two (2 bytes).
         * 
         * Returns: If Auto Poll is disabled (default) the CAN232 replies
         * CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         * If Auto Poll is enabled (see X command) the CAN232
         * replies z[CR] for OK or BELL (Ascii 7) for ERROR.
         */
        case 'r': {
            if (cmd_len != 6 || cmd[5] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open || listen_mode_only) {
                // This command is only active if the CAN232 is open in normal mode.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {

                // Parse identifier from cmd
                char identifier_string[4] = "xxx";
                uint32_t identifier = 0;
                strncpy(identifier_string, cmd+1, 3);
                const int result = sscanf(identifier_string, "%x", &identifier);

                // Parse data length from cmd
                const uint8_t dlc = (uint8_t) (cmd[4] - '0');

                // check identifier
                if (result != 1 || identifier > 0x7FF) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }

                // check dlc 
                if (dlc > 8) {
                    btspp_send_msg(ERROR, 1000);
                    return false;   
                }

                // init can frame
                twai_message_t message = {
                    .rtr = 1,
                    .identifier = identifier,
                    .data_length_code = dlc
                };


                // Send can frame
                esp_err_t err = twai_transmit(&message, 10);
                if (err != ESP_OK) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }
                else {
                    if (slcan_config.auto_poll_enabled) {
                        btspp_send_msg(zOK, 1000);
                        return false;
                    }
                    else {
                        btspp_send_msg(OK, 1000);
                        return false;
                    }
                }

            }
        }
        break;

        /** Riiiiiiiil[CR]
         * Transmit an extended RTR (29bit) CAN frame.
         * This command is only active if the CAN232 is open in normal mode.
         * 
         * iiiiiiii - Identifier in hex (00000000-1FFFFFFF)
         * l        - Data length (0-8)
         * 
         * Example 1: R000001002[CR]
         * Sends an 11bit RTR CAN frame with ID=0x100
         * and DLC set to two (2 bytes).
         * 
         * Returns: If Auto Poll is disabled (default) the CAN232 replies
         * CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         * If Auto Poll is enabled (see X command) the CAN232
         * replies z[CR] for OK or BELL (Ascii 7) for ERROR.
         */
        case 'R': {
            if (cmd_len != 11 || cmd[10] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open || listen_mode_only) {
                // This command is only active if the CAN232 is open in normal mode.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {

                // Parse identifier from cmd
                char identifier_string[9] = "xxxxxxxx";
                uint32_t identifier = 0;
                strncpy(identifier_string, cmd+1, 8);
                const int result = sscanf(identifier_string, "%x", &identifier);

                // Parse data length from cmd
                const uint8_t dlc = (uint8_t) (cmd[9] - '0');

                // check identifier
                if (result != 1 || identifier > 0x1FFFFFFF) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }

                // check dlc 
                if (dlc > 8) {
                    btspp_send_msg(ERROR, 1000);
                    return false;   
                }

                // init can frame
                twai_message_t message = {
                    .extd = 1,
                    .rtr = 1,
                    .identifier = identifier,
                    .data_length_code = dlc
                };

                // Send can frame
                esp_err_t err = twai_transmit(&message, 10);
                if (err != ESP_OK) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }
                else {
                    if (slcan_config.auto_poll_enabled) {
                        btspp_send_msg(zOK, 1000);
                        return false;
                    }
                    else {
                        btspp_send_msg(OK, 1000);
                        return false;
                    }
                }

            }
        }
        break;

        /** P[CR]
         * Poll incomming FIFO for CAN frames (single poll)
         * This command is only active if the CAN channel is open.
         * NOTE: This command is disabled in the new AUTO POLL/SEND
         * feature from version V1220. It will then reply BELL if used.
         * 
         * Example 1: P[CR] - Poll one CAN frame from the FIFO queue.
         * 
         * Returns: A CAN frame with same formatting as when sending
         * frames and ends with a CR (Ascii 13) for OK. 
         * If there are no pendant frames it returns only CR. 
         * If CAN channel isn’t open it returns BELL (Ascii 7). 
         * If the TIME STAMP is enabled, it will reply back the time
         * in milliseconds as well after the last data byte (before the CR). 
         * For more information, see the Z command.
         */
        case 'P': {
            if (cmd_len != 2 || cmd[1] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open || slcan_config.auto_poll_enabled) {
                // This command is only active if the CAN channel is open.
                // NOTE: This command is disabled in the new AUTO POLL/SEND
                // feature from version V1220. It will then reply BELL if used.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {

                // Receive a single CAN frame from the queue
                twai_message_t message = {};
                const esp_err_t err = twai_receive(&message, 0);

                
                if (err == ESP_ERR_TIMEOUT) {
                    // If there are no pending frames it returns only CR
                    btspp_send_msg(OK, 1000);
                    return true;
                }
                else if (err != ESP_OK) {
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }
                else {
                    // converting CAN frame to SLCAN message
                    const int result = can2sl(
                        &message, false, 
                        slcan_config.timestamps_enabled, esp_timer_get_time() % 60000LL, 
                        response_buffer, sizeof(response_buffer)
                    );

                    // Sending response
                    btspp_send_msg(response_buffer, 1000);
                    ESP_LOGI(SLCAN_TAG, "Responding: (len = %d): %s", result, response_buffer);
                    return true;
                }
            }    
        }
        break;

        /** A[CR]
         * Polls incomming FIFO for CAN frames (all pending frames)
         * This command is only active if the CAN channel is open.
         * NOTE: This command is disabled in the new AUTO POLL/SEND
         * feature from version V1220. It will then reply BELL if used.
         * 
         * Example 1: A[CR] - Polls all CAN frame from the FIFO queue.
         * 
         * Returns: CAN frames with same formatting as when sending
         * frames seperated with a CR (Ascii 13). 
         * When all frames are polled it ends with an A and a CR (Ascii 13) for OK. 
         * If there are no pending frames it returns only an A and CR. 
         * If CAN channel isn’t open it returns BELL (Ascii 7). 
         * If the TIME STAMP is enabled, it will reply back the time
         * in milliseconds as well after the last data byte (before the CR). 
         * For more information, see the Z command.
         */
        case 'A': {
            if (cmd_len != 2 || cmd[1] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open || slcan_config.auto_poll_enabled) {
                // This command is only active if the CAN channel is open.
                // NOTE: This command is disabled in the new AUTO POLL/SEND
                // feature from version V1220. It will then reply BELL if used.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {

                // Receive a single CAN frame from the queue
                twai_message_t message = {};
                esp_err_t err = ESP_OK;
                int result = 0;

                do {

                    // Receive a single CAN frame from the queue
                    err = twai_receive(&message, 0);
                    
                    if (err == ESP_ERR_TIMEOUT) {
                        // Stop if there are no more pending frames 
                        break;
                    }
                    else if (err != ESP_OK) {
                        btspp_send_msg(ERROR, 1000);
                        return false;
                    }
                    else {
                        // converting CAN frame to SLCAN message
                        result = can2sl(
                            &message, false, 
                            slcan_config.timestamps_enabled, esp_timer_get_time() % 60000LL, 
                            response_buffer, sizeof(response_buffer)
                        );

                        // Sending response
                        btspp_send_msg(response_buffer, 1000);
                        ESP_LOGI(SLCAN_TAG, "Responding (len = %d): %s", result, response_buffer);

                    }
                } 
                while (err == ESP_OK);

                btspp_send_msg("A"OK, 1000);
                return true;
            } 
        }
        break;

        /** F[CR]
         * Read Status Flags.
         * This command is only active if the CAN channel is open.
         * 
         * Example 1: F[CR]
         * Read Status Flags.
         * 
         * Returns: An F with 2 bytes BCD hex value plus CR (Ascii 13) for OK. 
         * If CAN channel isn’t open it returns BELL (Ascii 7). 
         * This command also clear the RED Error LED. 
         * See availible errors below. E.g. F01[CR]
         * 
         * Bit 0 - CAN receive FIFO queue full
         * Bit 1 - CAN transmit FIFO queue full
         * Bit 2 - Error warning (EI), see SJA1000 datasheet
         * Bit 3 - Data Overrun (DOI), see SJA1000 datasheet
         * Bit 4 - Not used.
         * Bit 5 - Error Passive (EPI), see SJA1000 datasheet
         * Bit 6 - Arbitration Lost (ALI), see SJA1000 datasheet *
         * Bit 7 - Bus Error (BEI), see SJA1000 datasheet **
         * 
         * * Arbitration lost doesn’t generate a blinking RED light!
         * ** Bus Error generates a constant RED light!
         */
        case 'F': {
            if (cmd_len != 2 || cmd[1] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open) {
                // This command is only active if the CAN channel is open.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {

                uint32_t alerts = 0;
                esp_err_t err = ESP_OK;
                twai_status_info_t status_info = {};

                // Read TWAI driver status
                err = twai_get_status_info(&status_info);
                if (err != ESP_OK) {
                    // something went wrong
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }

                // Read TWAI driver alerts
                err = twai_read_alerts(&alerts, 0);

                if (err != ESP_OK || err != ESP_ERR_TIMEOUT) {
                    // something went wrong
                    btspp_send_msg(ERROR, 1000);
                    return false;
                }
                else {

                    // Status flags
                    const uint8_t status_flags = ( \
                          0x01 * (alerts & TWAI_ALERT_RX_QUEUE_FULL ? 1 : 0) \
                        + 0x02 * (status_info.msgs_to_tx >= CAN_TX_QUEUE_SIZE ? 1 : 0) \
                        + 0x04 * (alerts & TWAI_ALERT_ERR_ACTIVE ? 1 : 0) \
                        + 0x08 * (alerts & TWAI_ALERT_RX_FIFO_OVERRUN ? 1 : 0) \
                        + 0x10 * (0) \
                        + 0x20 * (alerts & TWAI_ALERT_ERR_PASS ? 1 : 0) \
                        + 0x40 * (alerts & TWAI_ALERT_ARB_LOST ? 1 : 0) \
                        + 0x80 * (alerts & TWAI_ALERT_BUS_ERROR ? 1 : 0) \
                    );

                    sprintf(response_buffer, "F%02X"OK, (const uint32_t) status_flags);
                    btspp_send_msg(response_buffer, 1000);
                    return true;
                }
            }
        }
        break;

        /** Xn[CR]
         * Sets Auto Poll/Send ON/OFF for received frames. 
         * This command is only active if the CAN channel is closed. 
         * The value will be saved in EEPROM and remembered next time the CAN232 is powered up.
         * It is set to OFF by default, to be compatible with old programs written for CAN232. 
         * Setting it to ON, will disable the P and A command plus change the reply back from using the t and T command 
         * (see these commands for more information on the reply). 
         * We strongly recommend that you set this feature and upgrade from the old polling mechanism. 
         * By doing this, you will save bandwith and increase number of CAN frames that can be sent to the CAN232. 
         * When this feature is set, CAN frames will be sent out on the RS232 as soon as the CAN channel is opened.
         * 
         * Example 1: X0[CR]
         * Turn OFF the Auto Poll/Send feature (default).
         * 
         * Example 2: X1[CR]
         * Turn ON the Auto Poll/Send feature.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'X': {
            if (cmd_len != 3 || cmd[2] != CR || !(cmd[1] == '0' || cmd[1] == '1')) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (can_channel_open) {
                // This command is only active if the CAN channel is closed.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                slcan_config.auto_poll_enabled = (bool) (cmd[1] - '0');
                save_slcan_config_to_eeprom();

                btspp_send_msg(OK, 1000);
                return true;
            }
        }
        break;

        /** Wn[CR]
         * Filter mode setting. By default CAN232 works in dual filter mode (0)
         * and is backwards compatible with previous CAN232 versions.
         * It is now possible to put this into single filter mode and the setting
         * is remembered on next startup since it is saved in EEPROM.
         * Command can only be sent if CAN232 is initiated but not open.
         * 
         * Example 1: W0[CR]
         * Set dual filter mode (default).
         * 
         * Example 2: W1[CR]
         * Set single filter mode.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'W': {
            if (cmd_len != 3 || cmd[2] != CR || !(cmd[1] == '0' || cmd[1] == '1')) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_initiated || can_channel_open) {
                // Command can only be sent if CAN232 is initiated but not open.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                filter_config.single_filter = (bool) (cmd[1] - '0');
                save_filter_config_to_eeprom();

                btspp_send_msg(OK, 1000);
                return true;
            }
        }
        break;

        /** Mxxxxxxxx[CR]
         * Sets Acceptance Code Register (ACn Register of SJA1000).
         * This command is only active if the CAN channel is initiated and not opened.
         * 
         * xxxxxxxx - Acceptance Code in hex with LSB first,
         *            AC0, AC1, AC2 & AC3.
         *            For more info, see Philips SJA1000 datasheet.
         * 
         * Example: M00000000[CR]
         * Set Acceptance Code to 0x00000000
         * This is default when power on, i.e. receive all frames.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'M': {
            if (cmd_len != 10 || cmd[9] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_initiated || can_channel_open) {
                // Command can only be sent if CAN232 is initiated but not open.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {


                uint32_t acceptance_code = 0;
                int result = sscanf(cmd, "M%08x"OK, &acceptance_code);
                if (result != 1) {
                    btspp_send_msg(ERROR, 1000);
                    return false; 
                }

                // Acceptance Code was send with LSB first but was parsed by sscanf as MSB first
                // reverse_elements_in_buffer((uint8_t*) &acceptance_code, sizeof(acceptance_code));
                acceptance_code = parse_uint32((uint8_t*) &acceptance_code, BIG_ENDIAN);
                
                filter_config.acceptance_code = acceptance_code;
                save_filter_config_to_eeprom();
                btspp_send_msg(OK, 1000);
                return true;
            }
        }
        break;

        /** mxxxxxxxx[CR]
         * Sets Acceptance Mask Register (AMn Register of SJA1000).
         * This command is only active if the CAN channel is initiated and not opened.
         * xxxxxxxx - Acceptance Mask in hex with LSB first,
         *            AM0, AM1, AM2 & AM3.
         *            For more info, see Philips SJA1000 datasheet.
         * 
         * Example: mFFFFFFFF[CR]
         * Set Acceptance Mask to 0xFFFFFFFF
         * This is default when power on, i.e. receive all frames.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'm': {
            if (cmd_len != 10 || cmd[9] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_initiated || can_channel_open) {
                // Command can only be sent if CAN232 is initiated but not open.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {


                uint32_t acceptance_mask = 0;
                int result = sscanf(cmd, "m%08x"OK, &acceptance_mask);
                if (result != 1) {
                    btspp_send_msg(ERROR, 1000);
                    return false; 
                }

                // Acceptance Mask was send with LSB first but was parsed by sscanf as MSB first
                // reverse_elements_in_buffer((uint8_t*) &acceptance_mask, sizeof(acceptance_mask));
                acceptance_mask = parse_uint32((uint8_t*) &acceptance_mask, BIG_ENDIAN);
                
                filter_config.acceptance_mask = acceptance_mask;
                save_filter_config_to_eeprom();
                btspp_send_msg(OK, 1000);
                return true;
            }
        }
        break;

        /** V[CR]
         * Get Version number of both CAN232 hardware and software
         * This command is only active always.
         * 
         * Example: V[CR]
         * Get Version numbers
         * 
         * Returns: V and a 2 bytes BCD value for hardware version and
         * a 2 byte BCD value for software version plus
         * CR (Ascii 13) for OK. E.g. V1013[CR]
         */
        case 'V': {
            if (cmd_len != 2 || cmd[1] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                btspp_send_msg("V01D0"OK, 1000);
                return true;
            }
        }
        break;

        /** N[CR]
         * Get Serial number of the CAN232.
         * This command is only active always.
         * 
         * Example: N[CR]
         * Get Serial number
         * 
         * Returns: N and a 4 bytes value for serial number plus
         * CR (Ascii 13) for OK. E.g. NA123[CR]
         * Note that the serial number can have both numerical
         * and alfa numerical values in it. The serial number is
         * also printed on the CAN232 for a quick reference,
         * but could e.g. be used in a program to identify a
         * CAN232 so the program know that it is set up in the
         * correct way (for parameters saved in EEPROM).
         */
        case 'N': {
            if (cmd_len != 2 || cmd[1] != CR) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                btspp_send_msg("N1118"OK, 1000);
                return true;
            }
        }
        break;

        /** Zn[CR]
         * Sets Time Stamp ON/OFF for received frames only.
         * This command is only active if the CAN channel is closed.
         * The value will be saved in EEPROM and remembered next time
         * the CAN232 is powered up. This command shouldn’t be used more
         * than when you want to change this behaviour. It is set to OFF by
         * default, to be compatible with old programs written for CAN232.
         * Setting it to ON, will add 4 bytes sent out from CAN232 with the A
         * and P command or when the Auto Poll/Send feature is enabled.
         * When using Time Stamp each message gets a time in milliseconds
         * when it was received into the CAN232, this can be used for real
         * time applications for e.g. knowing time inbetween messages etc.
         * Note however by using this feature you will decrease bandwith on
         * the RS232, since it adds 4 bytes to each message being sent.
         * If the Time Stamp is OFF, the incomming frames looks like this:
         * t10021133[CR] (a standard frame with ID=0x100 & 2 bytes)
         * If the Time Stamp is ON, the incomming frames looks like this:
         * t100211334D67[CR] (a standard frame with ID=0x100 & 2 bytes)
         * Note the last 4 bytes 0x4D67, which is a Time Stamp for this
         * specific message in milliseconds (and of course in hex). The timer
         * in the CAN232 starts at zero 0x0000 and goes up to 0xEA5F before
         * it loop arround and get’s back to 0x0000. This corresponds to exact
         * 60,000mS (i.e. 1 minute which will be more than enough in most systems).
         * 
         * Example 1: Z0[CR]
         * Turn OFF the Time Stamp feature (default).
         * 
         * Example 2: Z1[CR]
         * Turn ON the Time Stamp feature.
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'Z': {
            if (cmd_len != 3 || cmd[2] != CR || !(cmd[1] == '0' || cmd[1] == '1')) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (can_channel_open) {
                // This command is only active if the CAN channel is closed.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                slcan_config.timestamps_enabled = (bool) (cmd[1] - '0');
                save_slcan_config_to_eeprom();

                btspp_send_msg(OK, 1000);
                return true;
            }
        }
        break;

        /** Qn[CR]
         * Auto Startup feature (from power on). Command works only when
         * CAN channel is open. Use this function when you have set up CAN
         * speed and filters and you want the CAN232 to boot up with these
         * settings automatically on every power on. Perfect for logging etc. 
         * or when no master is availible to set up the CAN232.
         * 
         * Note: Auto Send is only possible (see X command), so
         * CAN frames are sent out automatically on RS232
         * when received on CAN side. No polling is allowed.
         * 
         * Example 1: Q0[CR]
         * Turns OFF the Auto Startup feature (default).
         * On next power up, the CAN232 works normally
         * waiting for commands for setup etc.
         * 
         * Example 2: Q1[CR]
         * Turn ON the Auto Startup feature in normal mode.
         * Filters etc. are save and used on next power up.
         * 
         * Example 3: Q2[CR]
         * Turn ON the Auto Startup feature in listen only mode.
         * Filters etc. are save and used on next power up.
         * This dissables t, T, r and R commands!
         * 
         * Returns: CR (Ascii 13) for OK or BELL (Ascii 7) for ERROR.
         */
        case 'Q': {
            if (cmd_len != 3 || cmd[2] != CR || !(cmd[1] == '0' || cmd[1] == '1' || cmd[1] == '2')) {
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else if (!can_channel_open) {
                // This command is only active if the CAN channel is closed.
                btspp_send_msg(ERROR, 1000);
                return false;
            }
            else {
                uint32_t value = (uint32_t) (cmd[1] - '0');
                switch (value) {
                    case 0:
                        slcan_config.auto_startup_enabled = false;
                        break;
                    case 1:
                        slcan_config.auto_startup_enabled = true;
                        slcan_config.startup_in_listen_mode = false;
                        break;
                    case 2:
                        slcan_config.auto_startup_enabled = true;
                        slcan_config.startup_in_listen_mode = true;
                        break;
                    default:
                        btspp_send_msg(ERROR, 1000);
                        return false;
                }
                
                save_slcan_config_to_eeprom();
                btspp_send_msg(OK, 1000);
                return true;
            }
        }
        break;

        // switch default
        default: {
            btspp_send_msg(ERROR, 1000);
            return false;
        }
        break;

    } // end of the 'switch (command)' statement

}



// The task for receiving and processing SLCAN messages
static void slcan_task(void* args) {

    ESP_LOGI(SLCAN_TAG, "Starting SLCAN Task");

    char request[128] = "";
    int data_len = 0;

    while (true) {

        // Wait for a message via bluetooth SPP
        data_len = btspp_recv_msg(request, sizeof(request), OK, 1000, 0);

        if (data_len > 0) {
            ESP_LOGV(SLCAN_TAG, "Checking for BT-OTA cmd...");
            
            // Check if the message is the command for starting 
            // the bluetooth OTA update process
            if ((strncmp(request, "START BT-OTA\r", 13) == 0) && (strlen(request) == 13)) {
                btspp_do_ota_update();
            }
            // Process the message as a SLCAN command
            else {
                slcan_process_cmd(request);
            }
        }
    }

    ESP_LOGI(SLCAN_TAG, "Stopping SLCAN Task");
    vTaskDelete(NULL);
}

// Start the task for receiving and processing SLCAN messages
// Restict it to the APP-CPU-Core so it doesn't interfere with the bluetooth task on the Pro-CPU-Core
static void start_slcan_task() {
    xTaskCreatePinnedToCore(slcan_task, "SLCAN-TASK", 8 * 1024, NULL, 15, NULL, 1);
}


// Initilize SLCAN (Restore configs from EEPROM and auto-startup)
bool slcan_init() {

    // Restore configs
    restore_timing_config_from_eeprom();
    restore_filter_config_from_eeprom();
    restore_slcan_config_from_eeprom();

    // Do auto-startup if enabled
    if (slcan_config.auto_startup_enabled) {
        ESP_LOGI(SLCAN_TAG, "Auto Startup...");
        can_channel_initiated = true;

        if (slcan_config.startup_in_listen_mode) {
            // Open in Linsten mode
            can_config.mode = TWAI_MODE_LISTEN_ONLY;
        }
        else {
            // Open in normal mode
            can_config.mode = TWAI_MODE_NORMAL;
        }

        // Open the CAN channel to receive and send CAN frames
        open_can_channel();
    }

    // Start the task for receiving and processing SLCAN messages
    start_slcan_task();

    return true;
}
