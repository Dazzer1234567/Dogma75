#pragma once
#include "imxrt.h"
#include "usb2.h"

typedef struct transfer_struct transfer_t;
struct transfer_struct {
        uint32_t next;
        volatile uint32_t status;
        uint32_t pointer0;
        uint32_t pointer1;
        uint32_t pointer2;
        uint32_t pointer3;
        uint32_t pointer4;
        uint32_t callback_param;
};

#ifdef __cplusplus
extern "C" {
#endif

void usb2_init(void);
void usb2_init_serialnumber(void);
void switchUSB2Mode(enum USB2Mode_e newMode);

void usb2_config_rx(uint32_t ep, uint32_t packet_size, int do_zlp, void (*cb)(transfer_t *));
void usb2_config_tx(uint32_t ep, uint32_t packet_size, int do_zlp, void (*cb)(transfer_t *));
void usb2_config_rx_iso(uint32_t ep, uint32_t packet_size, int mult, void (*cb)(transfer_t *));
void usb2_config_tx_iso(uint32_t ep, uint32_t packet_size, int mult, void (*cb)(transfer_t *));

void usb2_prepare_transfer(transfer_t *transfer, const void *data, uint32_t len, uint32_t param);
void usb2_transmit(int endpoint_number, transfer_t *transfer);
void usb2_receive(int endpoint_number, transfer_t *transfer);
uint32_t usb2_transfer_status(const transfer_t *transfer);

void usb2_start_sof_interrupts(int interface);
void usb2_stop_sof_interrupts(int interface);

extern void (*usb2_timer0_callback)(void);
extern void (*usb2_timer1_callback)(void);

#ifdef __cplusplus
}
#endif

