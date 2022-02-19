#ifndef BTSPP_H
#define BTSPP_H

// Some standard header
#include <stdint.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

#define BTSPP_MSG_MAX_SIZE 950

// Init everything nedded for SPP
void btspp_init(const char* device_name, const uint32_t ringbuf_size);


// Wait for CTS event and send data via SPP
bool btspp_send(const uint8_t* const data, const uint32_t len, const uint32_t timeout_ms);

// Wait for and read data via SPP
// Two calls to btspp_recv() are required if the bytes wrap around the end of the ring buffer.
int btspp_recv(uint8_t* data, const uint32_t bufsize, const uint32_t timeout_ms);




// Wait for CTS event and send data via SPP
bool btspp_send_data(const uint8_t* const data, const uint32_t len, const uint32_t timeout_ms);


// Wait for CTS event and send message via SPP
bool btspp_send_msg(const char* const msg, const uint32_t timeout_ms);



// Wait for and read data via SPP
// Try to read as much data from the ringbuffer as possible
int btspp_recv_data(uint8_t* data, const uint32_t bufsize, const uint32_t timeout_ms, const uint32_t delay_ms);


// Wait for and read data via SPP
// Read single characters until a delimiter string is detected
int btspp_recv_msg(char* msg, const uint32_t bufsize, const char* delimiter, const uint32_t timeout_ms, const uint32_t delay_ms);


// Register a callback that gets called when new data arrives
typedef void (btspp_da_cb_t) (void* const ctx, const uint8_t* data, const uint32_t len);
void btspp_register_data_available_callback(btspp_da_cb_t* const callback, void* const ctx);


// Do a OTA updade via Bluetooth SPP
// You need my custom python script for that
bool btspp_do_ota_update();



#ifdef __cplusplus
}
#endif

#endif // BTSPP_H