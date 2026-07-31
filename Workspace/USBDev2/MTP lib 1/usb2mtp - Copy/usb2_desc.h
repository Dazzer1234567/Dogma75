/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2017 PJRC.COM, LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * 1. The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * 2. If the Software is incorporated into a build system that allows
 * selection among a list of target devices, then similar target
 * devices manufactured by PJRC.COM must be included in the list of
 * target devices and selectable in the same manner.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

// This header is NOT meant to be included when compiling
// user sketches in Arduino.  The low-level functions
// provided by usb2_dev.c are meant to be called only by
// code which provides higher-level interfaces to the user.

#include <stdint.h>
#include <stddef.h>
#include "usb2.h"

// if these are defined then they override the defaults vendor and product IDs set later
#define RL_VENDOR_ID 0x1781  
#define RL_PRODUCT_ID 0x0A58  

#define ENDPOINT_TRANSMIT_UNUSED	0x00020000
#define ENDPOINT_TRANSMIT_ISOCHRONOUS	0x00C40000
#define ENDPOINT_TRANSMIT_BULK		0x00C80000
#define ENDPOINT_TRANSMIT_INTERRUPT	0x00CC0000
#define ENDPOINT_RECEIVE_UNUSED		0x00000002
#define ENDPOINT_RECEIVE_ISOCHRONOUS	0x000000C4
#define ENDPOINT_RECEIVE_BULK		0x000000C8
#define ENDPOINT_RECEIVE_INTERRUPT	0x000000CC

/*
Each group of #define lines below corresponds to one of the
settings in the Tools > USB Type menu.  This file defines what
type of USB device is actually created for each of those menu
options.

Each "interface" is a set of functionality your PC or Mac will
use and treat as if it is a unique device.  Within each interface,
the "endpoints" are the actual communication channels.  Most
interfaces use 1, 2 or 3 endpoints.  By editing only this file,
you can customize the USB Types to be any collection of interfaces.

To modify a USB Type, delete the XYZ_INTERFACE lines for any
interfaces you wish to remove, and copy them from another USB Type
for any you want to add.

Give each interface a unique number, and edit NUM_INTERFACE to
reflect the total number of interfaces.

Next, assign unique endpoint numbers to all the endpoints across
all the interfaces your device has.  You can reuse an endpoint
number for transmit and receive, but the same endpoint number must
not be used twice to transmit, or twice to receive.

Most endpoints also require their maximum size, and some also
need an interval specification (the number of milliseconds the
PC will check for data from that endpoint).  For existing
interfaces, usually these other settings should not be changed.

Edit NUM_ENDPOINTS to be at least the largest endpoint number used.

Edit the ENDPOINT*_CONFIG lines so each endpoint is configured
the proper way (transmit, receive, or both).

If you are using existing interfaces (making your own device with
a different set of interfaces) the code in all other files should
automatically adapt to the new endpoints you specify here.

If you need to create a new type of interface, you'll need to write
the code which sends and receives packets, and presents an API to
the user.  Usually, a pair of files are added for the actual code,
and code is also added in usb2_dev.c for any control transfers,
interrupt-level code, or other very low-level stuff not possible
from the packet send/receive functons.  Code also is added in
usb2_inst.c to create an instance of your C++ object.  This message
gives a quick summary of things you will need to know:
https://forum.pjrc.com/threads/49045?p=164512&viewfull=1#post164512

You may edit the Vendor and Product ID numbers, and strings.  If
the numbers are changed, Teensyduino may not be able to automatically
find and reboot your board when you click the Upload button in
the Arduino IDE.  You will need to press the Program button on
Teensy to initiate programming.

Some operating systems, especially Windows, may cache USB device
info.  Changes to the device name may not update on the same
computer unless the vendor or product ID numbers change, or the
"bcdDevice" revision code is increased.

If these instructions are missing steps or could be improved, please
let me know?  http://forum.pjrc.com/forums/4-Suggestions-amp-Bug-Reports
*/

  #define USB2_VENDOR_ID             RL_VENDOR_ID // 0x16C0
  #define USB2_PRODUCT_ID            RL_PRODUCT_ID // 0x048C
  #define USB2_MANUFACTURER_NAME     {'T','e','e','n','s','y','d','u','i','n','o'}
  #define USB2_MANUFACTURER_NAME_LEN 11
  #define USB2_PRODUCT_NAME          {'D','u','a','l',' ','S','e','r','i','a','l','M','T'}
  #define USB2_PRODUCT_NAME_LEN      13
  #define USB2_EP0_SIZE              64
  #define USB2_NUM_NORMAL_ENDPOINTS         3
  #define USB2_NUM_NORMAL_INTERFACE         2
  #define USB2_NUM_DEBUG_ENDPOINTS         7
  #define USB2_NUM_DEBUG_INTERFACE         5
  #define USB2_CDC_IAD_DESCRIPTOR    1       // Serial
  #define USB2_CDC_STATUS_INTERFACE  0
  #define USB2_CDC_DATA_INTERFACE    1
  #define USB2_CDC_ACM_ENDPOINT      2
  #define USB2_CDC_RX_ENDPOINT       3
  #define USB2_CDC_TX_ENDPOINT       3
  #define USB2_CDC_ACM_SIZE          16
  #define USB2_CDC_RX_SIZE_480       512
  #define USB2_CDC_TX_SIZE_480       512
  #define USB2_CDC_RX_SIZE_12        64
  #define USB2_CDC_TX_SIZE_12        64
  #define USB2_CDC2_STATUS_INTERFACE 2       // SerialUSB1
  #define USB2_CDC2_DATA_INTERFACE   3
  #define USB2_CDC2_ACM_ENDPOINT     4
  #define USB2_CDC2_RX_ENDPOINT      5
  #define USB2_CDC2_TX_ENDPOINT      5
  #define USB2_ENDPOINT2_CONFIG	ENDPOINT_RECEIVE_UNUSED + ENDPOINT_TRANSMIT_INTERRUPT
  #define USB2_ENDPOINT3_CONFIG	ENDPOINT_RECEIVE_BULK + ENDPOINT_TRANSMIT_BULK
  
  #define USB2_ENDPOINT4_CONFIG	ENDPOINT_RECEIVE_UNUSED + ENDPOINT_TRANSMIT_INTERRUPT
  #define USB2_ENDPOINT5_CONFIG	ENDPOINT_RECEIVE_BULK + ENDPOINT_TRANSMIT_BULK

  #define USB2_MTP_INTERFACE		4	// MTP Disk
  #define USB2_MTP_TX_ENDPOINT	6
  #define USB2_MTP_TX_SIZE_12	64
  #define USB2_MTP_TX_SIZE_480	512
  #define USB2_MTP_RX_ENDPOINT	6
  #define USB2_MTP_RX_SIZE_12	64
  #define USB2_MTP_RX_SIZE_480	512
  #define USB2_MTP_EVENT_ENDPOINT	7
  #define USB2_MTP_EVENT_SIZE	32
  #define USB2_MTP_EVENT_INTERVAL_12	10	// 10 = 10 ms
  #define USB2_MTP_EVENT_INTERVAL_480 7	// 7 = 8 ms
  #define USB2_ENDPOINT6_CONFIG	ENDPOINT_RECEIVE_BULK + ENDPOINT_TRANSMIT_BULK
  #define USB2_ENDPOINT7_CONFIG	ENDPOINT_RECEIVE_UNUSED + ENDPOINT_TRANSMIT_INTERRUPT

#define CURRENT_ENDPOINT_COUNT ((USB2Mode==Normal)?USB2_NUM_NORMAL_ENDPOINTS:USB2_NUM_DEBUG_ENDPOINTS)

extern const int USB2_ConfigDescriptionSize_Normal;
extern const int USB2_ConfigDescriptionSize_Debug;


#ifdef USB2_DESC_LIST_DEFINE
// NUM_ENDPOINTS = number of non-zero endpoints (0 to 7)
extern const uint32_t usb2_endpoint_config_table[USB2_NUM_DEBUG_ENDPOINTS];

typedef struct {
	uint16_t	wValue;
	uint16_t	wIndex;
	const uint8_t	*addr;
	uint16_t	length;
} usb2_descriptor_list_t;

extern const usb2_descriptor_list_t usb2_descriptor_list[];
#endif // usb2_DESC_LIST_DEFINE

