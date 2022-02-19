#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// CAN-Bus
#define HAREWARE_CONFIG_CAN_TX_PIN 5 // Hardware dependend
#define HAREWARE_CONFIG_CAN_RX_PIN 35 // Hardware dependend

// Bluetooth
#define HAREWARE_CONFIG_BT_DEVICE_NAME "SLCAN-BT-Adapter"
#define HARDWARE_CONFIG_SPP_SERVICE_NAME "SLCAN"
#define HARDWARE_CONFIG_SPP_CHANNEL 0

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_CONFIG_H