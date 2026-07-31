/* Teensyduino Core Library
 * http://www.pjrc.com/teensy/
 * Copyright (c) 2019 PJRC.COM, LLC.
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

//#if F_CPU >= 20000000

#define USB2_DESC_LIST_DEFINE
#include "usb2.h"

#include "usb2_names.h"
#include "imxrt.h"
#include "avr_functions.h"
#include "avr/pgmspace.h"

// At very slow CPU speeds, the OCRAM just isn't fast enough for
// USB to work reliably.  But the precious/limited DTCM is.  So
// as an ugly workaround, undefine DMAMEM so all buffers which
// would normally be allocated in OCRAM are placed in DTCM.
#if defined(F_CPU) && F_CPU < 30000000
#undef DMAMEM
#endif

// USB Descriptors are binary data which the USB host reads to
// automatically detect a USB device's capabilities.  The format
// and meaning of every field is documented in numerous USB
// standards.  When working with USB descriptors, despite the
// complexity of the standards and poor writing quality in many
// of those documents, remember descriptors are nothing more
// than constant binary data that tells the USB host what the
// device can do.  Computers will load drivers based on this data.
// Those drivers then communicate on the endpoints specified by
// the descriptors.

// To configure a new combination of interfaces or make minor
// changes to existing configuration (eg, change the name or ID
// numbers), usually you would edit "usb2_desc.h".  This file
// is meant to be configured by the header, so generally it is
// only edited to add completely new USB interfaces or features.



// **************************************************************
//   USB Device
// **************************************************************

#define LSB(n) ((n) & 255)
#define MSB(n) (((n) >> 8) & 255)

#ifdef USB2_CDC_IAD_DESCRIPTOR
#ifndef U2DEVICE_CLASS
#define U2DEVICE_CLASS 0xEF
#endif
#ifndef U2DEVICE_SUBCLASS
#define U2DEVICE_SUBCLASS 0x02
#endif
#ifndef U2DEVICE_PROTOCOL
#define U2DEVICE_PROTOCOL 0x01
#endif
#endif


// USB Device Descriptor.  The USB host reads this first, to learn
// what type of device is connected.
static uint8_t device_descriptor[] = {
        18,                                     // bLength
        1,                                      // bDescriptorType
        0x00, 0x02,                             // bcdUSB
#ifdef U2DEVICE_CLASS
        U2DEVICE_CLASS,                           // bDeviceClass
#else
	0,
#endif
#ifdef U2DEVICE_SUBCLASS
        U2DEVICE_SUBCLASS,                        // bDeviceSubClass
#else
	0,
#endif
#ifdef U2DEVICE_PROTOCOL
        U2DEVICE_PROTOCOL,                        // bDeviceProtocol
#else
	0,
#endif
        USB2_EP0_SIZE,                               // bMaxPacketSize0
        LSB(USB2_VENDOR_ID), MSB(USB2_VENDOR_ID),         // idVendor
        LSB(USB2_PRODUCT_ID), MSB(USB2_PRODUCT_ID),       // idProduct
#ifdef BCD_DEVICE
	LSB(BCD_DEVICE), MSB(BCD_DEVICE),       // bcdDevice
#else
  // For USB types that don't explicitly define BCD_DEVICE,
  // use the minor version number to help teensy_ports
  // identify which Teensy model is used.
  #if defined(__IMXRT1062__) && defined(ARDUINO_TEENSY40)
        0x79, 0x02, // Teensy 4.0
  #elif defined(__IMXRT1062__) && defined(ARDUINO_TEENSY41)
        0x80, 0x02, // Teensy 4.1
  #elif defined(__IMXRT1062__) && defined(ARDUINO_TEENSY_MICROMOD)
        0x81, 0x02, // Teensy MicroMod
  #else
        0x00, 0x02,
  #endif
#endif
        1,                                      // iManufacturer
        2,                                      // iProduct
        3,                                      // iSerialNumber
        1                                       // bNumConfigurations
};

PROGMEM static const uint8_t qualifier_descriptor[] = {	// 9.6.2 Device_Qualifier, page 264
	10,					// bLength
	6,					// bDescriptorType
	0x00, 0x02,				// bcdUSB
#ifdef U2DEVICE_CLASS
        U2DEVICE_CLASS,                           // bDeviceClass
#else
	0,
#endif
#ifdef U2DEVICE_SUBCLASS
        U2DEVICE_SUBCLASS,                        // bDeviceSubClass
#else
	0,
#endif
#ifdef U2DEVICE_PROTOCOL
        U2DEVICE_PROTOCOL,                        // bDeviceProtocol
#else
	0,
#endif
        USB2_EP0_SIZE,                               // bMaxPacketSize0
        1,					// bNumConfigurations
        0                                       // bReserved
};

// These descriptors must NOT be "const", because the USB DMA
// has trouble accessing flash memory with enough bandwidth
// while the processor is executing from flash.



// **************************************************************
//   HID Report Descriptors
// **************************************************************

// Each HID interface needs a special report descriptor that tells
// the meaning and format of the data.

// **************************************************************
//   USB Descriptor Sizes
// **************************************************************

// pre-compute the size and position of everything in the config descriptor
//
#define USB2_CONFIG_HEADER_DESCRIPTOR_SIZE	9

#define USB2_CDC_IAD_DESCRIPTOR_SIZE		8

#define USB2_CDC_DATA_INTERFACE_DESC_SIZE	9+5+5+4+5+7+9+7+7

#define USB2_CDC2_DATA_INTERFACE_DESC_SIZE   8 + 9+5+5+4+5+7+9+7+7

#define USB2_MTP_INTERFACE_DESC_SIZE		9+7+7+7


#define USB2_CONFIG_NORMAL_DESC_SIZE		USB2_CONFIG_HEADER_DESCRIPTOR_SIZE+USB2_CDC_IAD_DESCRIPTOR_SIZE+USB2_CDC_DATA_INTERFACE_DESC_SIZE
#define USB2_CONFIG_DEBUG_DESC_SIZE		USB2_CONFIG_NORMAL_DESC_SIZE + USB2_CDC2_DATA_INTERFACE_DESC_SIZE + USB2_MTP_INTERFACE_DESC_SIZE



// **************************************************************
//   USB Configuration
// **************************************************************

// USB Configuration Descriptor.  This huge descriptor tells all
// of the devices capabilities.

PROGMEM const uint8_t usb2_config_descriptor_Normal_480[USB2_CONFIG_NORMAL_DESC_SIZE] = {
        // configuration descriptor, USB spec 9.6.3, page 264-266, Table 9-10
        9,                                      // bLength;
        2,                                      // bDescriptorType;
        LSB(USB2_CONFIG_NORMAL_DESC_SIZE),                 // wTotalLength
        MSB(USB2_CONFIG_NORMAL_DESC_SIZE),
        USB2_NUM_NORMAL_INTERFACE,                          // bNumInterfaces
        1,                                      // bConfigurationValue
        0,                                      // iConfiguration
        0xC0,                                   // bmAttributes
        50,                                     // bMaxPower

        // interface association descriptor, USB ECN, Table 9-Z
        8,                                      // bLength
        11,                                     // bDescriptorType
        USB2_CDC_STATUS_INTERFACE,                   // bFirstInterface
        2,                                      // bInterfaceCount
        0x02,                                   // bFunctionClass
        0x02,                                   // bFunctionSubClass
        0x01,                                   // bFunctionProtocol
        0,                                      // iFunction

	// configuration for 480 Mbit/sec speed
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC_STATUS_INTERFACE,			// bInterfaceNumber
        0,                                      // bAlternateSetting
        1,                                      // bNumEndpoints
        0x02,                                   // bInterfaceClass
        0x02,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // CDC Header Functional Descriptor, CDC Spec 5.2.3.1, Table 26
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x00,                                   // bDescriptorSubtype
        0x10, 0x01,                             // bcdCDC
        // Call Management Functional Descriptor, CDC Spec 5.2.3.2, Table 27
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x01,                                   // bDescriptorSubtype
        0x01,                                   // bmCapabilities
        1,                                      // bDataInterface
        // Abstract Control Management Functional Descriptor, CDC Spec 5.2.3.3, Table 28
        4,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x02,                                   // bDescriptorSubtype
        0x06,                                   // bmCapabilities
        // Union Functional Descriptor, CDC Spec 5.2.3.8, Table 33
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x06,                                   // bDescriptorSubtype
        USB2_CDC_STATUS_INTERFACE,                   // bMasterInterface
        USB2_CDC_DATA_INTERFACE,                     // bSlaveInterface0
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_ACM_ENDPOINT | 0x80,                // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        LSB(USB2_CDC_ACM_SIZE),MSB(USB2_CDC_ACM_SIZE),    // wMaxPacketSize
        5,                                      // bInterval
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC_DATA_INTERFACE,                     // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        0x0A,                                   // bInterfaceClass
        0x00,                                   // bInterfaceSubClass
        0x00,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_RX_ENDPOINT,                        // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_RX_SIZE_480),MSB(USB2_CDC_RX_SIZE_480),// wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_TX_ENDPOINT | 0x80,                 // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_TX_SIZE_480),MSB(USB2_CDC_TX_SIZE_480),// wMaxPacketSize
        0,                                      // bInterval
};


PROGMEM const uint8_t usb2_config_descriptor_Normal_12[USB2_CONFIG_NORMAL_DESC_SIZE] = {
        // configuration descriptor, USB spec 9.6.3, page 264-266, Table 9-10
        9,                                      // bLength;
        2,                                      // bDescriptorType;
        LSB(USB2_CONFIG_NORMAL_DESC_SIZE),                 // wTotalLength
        MSB(USB2_CONFIG_NORMAL_DESC_SIZE),
        USB2_NUM_NORMAL_INTERFACE,                          // bNumInterfaces
        1,                                      // bConfigurationValue
        0,                                      // iConfiguration
        0xC0,                                   // bmAttributes
        50,                                     // bMaxPower

        // interface association descriptor, USB ECN, Table 9-Z
        8,                                      // bLength
        11,                                     // bDescriptorType
        USB2_CDC_STATUS_INTERFACE,                   // bFirstInterface
        2,                                      // bInterfaceCount
        0x02,                                   // bFunctionClass
        0x02,                                   // bFunctionSubClass
        0x01,                                   // bFunctionProtocol
        0,                                      // iFunction

	// configuration for 12 Mbit/sec speed
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC_STATUS_INTERFACE,			// bInterfaceNumber
        0,                                      // bAlternateSetting
        1,                                      // bNumEndpoints
        0x02,                                   // bInterfaceClass
        0x02,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // CDC Header Functional Descriptor, CDC Spec 5.2.3.1, Table 26
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x00,                                   // bDescriptorSubtype
        0x10, 0x01,                             // bcdCDC
        // Call Management Functional Descriptor, CDC Spec 5.2.3.2, Table 27
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x01,                                   // bDescriptorSubtype
        0x01,                                   // bmCapabilities
        1,                                      // bDataInterface
        // Abstract Control Management Functional Descriptor, CDC Spec 5.2.3.3, Table 28
        4,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x02,                                   // bDescriptorSubtype
        0x06,                                   // bmCapabilities
        // Union Functional Descriptor, CDC Spec 5.2.3.8, Table 33
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x06,                                   // bDescriptorSubtype
        USB2_CDC_STATUS_INTERFACE,                   // bMasterInterface
        USB2_CDC_DATA_INTERFACE,                     // bSlaveInterface0
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_ACM_ENDPOINT | 0x80,                // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        USB2_CDC_ACM_SIZE, 0,                        // wMaxPacketSize
        16,                                     // bInterval
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC_DATA_INTERFACE,                     // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        0x0A,                                   // bInterfaceClass
        0x00,                                   // bInterfaceSubClass
        0x00,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_RX_ENDPOINT,                        // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_RX_SIZE_12),MSB(USB2_CDC_RX_SIZE_12),// wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_TX_ENDPOINT | 0x80,                 // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_TX_SIZE_12),MSB(USB2_CDC_TX_SIZE_12),// wMaxPacketSize
        0,                                      // bInterval
};


PROGMEM const uint8_t usb2_config_descriptor_Debug_480[USB2_CONFIG_DEBUG_DESC_SIZE] = {
        // configuration descriptor, USB spec 9.6.3, page 264-266, Table 9-10
        9,                                      // bLength;
        2,                                      // bDescriptorType;
        LSB(USB2_CONFIG_DEBUG_DESC_SIZE),                 // wTotalLength
        MSB(USB2_CONFIG_DEBUG_DESC_SIZE),
        USB2_NUM_DEBUG_INTERFACE,                          // bNumInterfaces
        1,                                      // bConfigurationValue
        0,                                      // iConfiguration
        0xC0,                                   // bmAttributes
        50,                                     // bMaxPower

        // interface association descriptor, USB ECN, Table 9-Z
        8,                                      // bLength
        11,                                     // bDescriptorType
        USB2_CDC_STATUS_INTERFACE,                   // bFirstInterface
        2,                                      // bInterfaceCount
        0x02,                                   // bFunctionClass
        0x02,                                   // bFunctionSubClass
        0x01,                                   // bFunctionProtocol
        0,                                      // iFunction
	// configuration for 480 Mbit/sec speed
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC_STATUS_INTERFACE,			// bInterfaceNumber
        0,                                      // bAlternateSetting
        1,                                      // bNumEndpoints
        0x02,                                   // bInterfaceClass
        0x02,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // CDC Header Functional Descriptor, CDC Spec 5.2.3.1, Table 26
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x00,                                   // bDescriptorSubtype
        0x10, 0x01,                             // bcdCDC
        // Call Management Functional Descriptor, CDC Spec 5.2.3.2, Table 27
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x01,                                   // bDescriptorSubtype
        0x01,                                   // bmCapabilities
        1,                                      // bDataInterface
        // Abstract Control Management Functional Descriptor, CDC Spec 5.2.3.3, Table 28
        4,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x02,                                   // bDescriptorSubtype
        0x06,                                   // bmCapabilities
        // Union Functional Descriptor, CDC Spec 5.2.3.8, Table 33
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x06,                                   // bDescriptorSubtype
        USB2_CDC_STATUS_INTERFACE,                   // bMasterInterface
        USB2_CDC_DATA_INTERFACE,                     // bSlaveInterface0
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_ACM_ENDPOINT | 0x80,                // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        LSB(USB2_CDC_ACM_SIZE),MSB(USB2_CDC_ACM_SIZE),    // wMaxPacketSize
        5,                                      // bInterval
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC_DATA_INTERFACE,                     // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        0x0A,                                   // bInterfaceClass
        0x00,                                   // bInterfaceSubClass
        0x00,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_RX_ENDPOINT,                        // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_RX_SIZE_480),MSB(USB2_CDC_RX_SIZE_480),// wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_TX_ENDPOINT | 0x80,                 // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_TX_SIZE_480),MSB(USB2_CDC_TX_SIZE_480),// wMaxPacketSize
        0,                                      // bInterval

	// configuration for 480 Mbit/sec speed
        // interface association descriptor, USB ECN, Table 9-Z
        8,                                      // bLength
        11,                                     // bDescriptorType
        USB2_CDC2_STATUS_INTERFACE,                  // bFirstInterface
        2,                                      // bInterfaceCount
        0x02,                                   // bFunctionClass
        0x02,                                   // bFunctionSubClass
        0x01,                                   // bFunctionProtocol
        0,                                      // iFunction
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC2_STATUS_INTERFACE,                  // bInterfaceNumber
        0,                                      // bAlternateSetting
        1,                                      // bNumEndpoints
        0x02,                                   // bInterfaceClass
        0x02,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // CDC Header Functional Descriptor, CDC Spec 5.2.3.1, Table 26
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x00,                                   // bDescriptorSubtype
        0x10, 0x01,                             // bcdCDC
        // Call Management Functional Descriptor, CDC Spec 5.2.3.2, Table 27
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x01,                                   // bDescriptorSubtype
        0x01,                                   // bmCapabilities
        1,                                      // bDataInterface
        // Abstract Control Management Functional Descriptor, CDC Spec 5.2.3.3, Table 28
        4,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x02,                                   // bDescriptorSubtype
        0x06,                                   // bmCapabilities
        // Union Functional Descriptor, CDC Spec 5.2.3.8, Table 33
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x06,                                   // bDescriptorSubtype
        USB2_CDC2_STATUS_INTERFACE,                  // bMasterInterface
        USB2_CDC2_DATA_INTERFACE,                    // bSlaveInterface0
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC2_ACM_ENDPOINT | 0x80,               // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        USB2_CDC_ACM_SIZE, 0,                        // wMaxPacketSize
        5,                                      // bInterval
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC2_DATA_INTERFACE,                    // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        0x0A,                                   // bInterfaceClass
        0x00,                                   // bInterfaceSubClass
        0x00,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC2_RX_ENDPOINT,                       // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_RX_SIZE_480),MSB(USB2_CDC_RX_SIZE_480),// wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC2_TX_ENDPOINT | 0x80,                // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_TX_SIZE_480),MSB(USB2_CDC_TX_SIZE_480),// wMaxPacketSize
        0,                                      // bInterval

	// configuration for 480 Mbit/sec speed
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_MTP_INTERFACE,                          // bInterfaceNumber
        0,                                      // bAlternateSetting
        3,                                      // bNumEndpoints
        0x06,                                   // bInterfaceClass (0x06 = still image)
        0x01,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        4,                                      // iInterface
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_MTP_TX_ENDPOINT | 0x80,                 // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_MTP_TX_SIZE_480),MSB(USB2_MTP_TX_SIZE_480), // wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_MTP_RX_ENDPOINT,                        // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_MTP_RX_SIZE_480),MSB(USB2_MTP_RX_SIZE_480), // wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_MTP_EVENT_ENDPOINT | 0x80,              // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        USB2_MTP_EVENT_SIZE, 0,                      // wMaxPacketSize
        USB2_MTP_EVENT_INTERVAL_480,                 // bInterval
};


PROGMEM const uint8_t usb2_config_descriptor_Debug_12[USB2_CONFIG_DEBUG_DESC_SIZE] = {
        // configuration descriptor, USB spec 9.6.3, page 264-266, Table 9-10
        9,                                      // bLength;
        2,                                      // bDescriptorType;
        LSB(USB2_CONFIG_DEBUG_DESC_SIZE),                 // wTotalLength
        MSB(USB2_CONFIG_DEBUG_DESC_SIZE),
        USB2_NUM_DEBUG_INTERFACE,                          // bNumInterfaces
        1,                                      // bConfigurationValue
        0,                                      // iConfiguration
        0xC0,                                   // bmAttributes
        50,                                     // bMaxPower

        // interface association descriptor, USB ECN, Table 9-Z
        8,                                      // bLength
        11,                                     // bDescriptorType
        USB2_CDC_STATUS_INTERFACE,                   // bFirstInterface
        2,                                      // bInterfaceCount
        0x02,                                   // bFunctionClass
        0x02,                                   // bFunctionSubClass
        0x01,                                   // bFunctionProtocol
        0,                                      // iFunction
	// configuration for 12 Mbit/sec speed
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC_STATUS_INTERFACE,			// bInterfaceNumber
        0,                                      // bAlternateSetting
        1,                                      // bNumEndpoints
        0x02,                                   // bInterfaceClass
        0x02,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // CDC Header Functional Descriptor, CDC Spec 5.2.3.1, Table 26
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x00,                                   // bDescriptorSubtype
        0x10, 0x01,                             // bcdCDC
        // Call Management Functional Descriptor, CDC Spec 5.2.3.2, Table 27
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x01,                                   // bDescriptorSubtype
        0x01,                                   // bmCapabilities
        1,                                      // bDataInterface
        // Abstract Control Management Functional Descriptor, CDC Spec 5.2.3.3, Table 28
        4,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x02,                                   // bDescriptorSubtype
        0x06,                                   // bmCapabilities
        // Union Functional Descriptor, CDC Spec 5.2.3.8, Table 33
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x06,                                   // bDescriptorSubtype
        USB2_CDC_STATUS_INTERFACE,                   // bMasterInterface
        USB2_CDC_DATA_INTERFACE,                     // bSlaveInterface0
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_ACM_ENDPOINT | 0x80,                // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        USB2_CDC_ACM_SIZE, 0,                        // wMaxPacketSize
        16,                                     // bInterval
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC_DATA_INTERFACE,                     // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        0x0A,                                   // bInterfaceClass
        0x00,                                   // bInterfaceSubClass
        0x00,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_RX_ENDPOINT,                        // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_RX_SIZE_12),MSB(USB2_CDC_RX_SIZE_12),// wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC_TX_ENDPOINT | 0x80,                 // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_CDC_TX_SIZE_12),MSB(USB2_CDC_TX_SIZE_12),// wMaxPacketSize
        0,                                      // bInterval
	// configuration for 12 Mbit/sec speed
        // interface association descriptor, USB ECN, Table 9-Z
        8,                                      // bLength
        11,                                     // bDescriptorType
       USB2_CDC2_STATUS_INTERFACE,                  // bFirstInterface
        2,                                      // bInterfaceCount
        0x02,                                   // bFunctionClass
        0x02,                                   // bFunctionSubClass
        0x01,                                   // bFunctionProtocol
        0,                                      // iFunction
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC2_STATUS_INTERFACE,                  // bInterfaceNumber
        0,                                      // bAlternateSetting
        1,                                      // bNumEndpoints
        0x02,                                   // bInterfaceClass
        0x02,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // CDC Header Functional Descriptor, CDC Spec 5.2.3.1, Table 26
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x00,                                   // bDescriptorSubtype
        0x10, 0x01,                             // bcdCDC
        // Call Management Functional Descriptor, CDC Spec 5.2.3.2, Table 27
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x01,                                   // bDescriptorSubtype
        0x01,                                   // bmCapabilities
        1,                                      // bDataInterface
        // Abstract Control Management Functional Descriptor, CDC Spec 5.2.3.3, Table 28
        4,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x02,                                   // bDescriptorSubtype
        0x06,                                   // bmCapabilities
        // Union Functional Descriptor, CDC Spec 5.2.3.8, Table 33
        5,                                      // bFunctionLength
        0x24,                                   // bDescriptorType
        0x06,                                   // bDescriptorSubtype
        USB2_CDC2_STATUS_INTERFACE,                  // bMasterInterface
        USB2_CDC2_DATA_INTERFACE,                    // bSlaveInterface0
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC2_ACM_ENDPOINT | 0x80,               // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        USB2_CDC_ACM_SIZE, 0,                        // wMaxPacketSize
        64,                                     // bInterval
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_CDC2_DATA_INTERFACE,                    // bInterfaceNumber
        0,                                      // bAlternateSetting
        2,                                      // bNumEndpoints
        0x0A,                                   // bInterfaceClass
        0x00,                                   // bInterfaceSubClass
        0x00,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC2_RX_ENDPOINT,                       // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        USB2_CDC_RX_SIZE_12, 0,                      // wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_CDC2_TX_ENDPOINT | 0x80,                // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        USB2_CDC_TX_SIZE_12, 0,                      // wMaxPacketSize
        0,                                      // bInterval
	// configuration for 12 Mbit/sec speed
        // interface descriptor, USB spec 9.6.5, page 267-269, Table 9-12
        9,                                      // bLength
        4,                                      // bDescriptorType
        USB2_MTP_INTERFACE,                          // bInterfaceNumber
        0,                                      // bAlternateSetting
        3,                                      // bNumEndpoints
        0x06,                                   // bInterfaceClass (0x06 = still image)
        0x01,                                   // bInterfaceSubClass
        0x01,                                   // bInterfaceProtocol
        0,                                      // iInterface
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_MTP_TX_ENDPOINT | 0x80,                 // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_MTP_TX_SIZE_12),MSB(USB2_MTP_TX_SIZE_12),// wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_MTP_RX_ENDPOINT,                        // bEndpointAddress
        0x02,                                   // bmAttributes (0x02=bulk)
        LSB(USB2_MTP_RX_SIZE_12),MSB(USB2_MTP_RX_SIZE_12),// wMaxPacketSize
        0,                                      // bInterval
        // endpoint descriptor, USB spec 9.6.6, page 269-271, Table 9-13
        7,                                      // bLength
        5,                                      // bDescriptorType
        USB2_MTP_EVENT_ENDPOINT | 0x80,              // bEndpointAddress
        0x03,                                   // bmAttributes (0x03=intr)
        USB2_MTP_EVENT_SIZE, 0,                      // wMaxPacketSize
        USB2_MTP_EVENT_INTERVAL_12,                  // bInterval
};


__attribute__ ((section(".dmabuffers"), aligned(32)))
uint8_t usb2_descriptor_buffer[USB2_CONFIG_DEBUG_DESC_SIZE];



// **************************************************************
//   String Descriptors
// **************************************************************

// The descriptors above can provide human readable strings,
// referenced by index numbers.  These descriptors are the
// actual string data

/* defined in usb2_names.h
struct usb2_string_descriptor_struct {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint16_t wString[];
};
*/

extern struct usb2_string_descriptor_struct usb2_string_manufacturer_name
        __attribute__ ((weak, alias("usb2_string_manufacturer_name_default")));
extern struct usb2_string_descriptor_struct usb2_string_product_name
        __attribute__ ((weak, alias("usb2_string_product_name_default")));
extern struct usb2_string_descriptor_struct usb2_string_serial_number
        __attribute__ ((weak, alias("usb2_string_serial_number_default")));

PROGMEM const struct usb2_string_descriptor_struct usb2_string0 = {
        4,
        3,
        {0x0409}
};

PROGMEM const struct usb2_string_descriptor_struct usb2_string_manufacturer_name_default = {
        2 + USB2_MANUFACTURER_NAME_LEN * 2,
        3,
        USB2_MANUFACTURER_NAME
};
PROGMEM const struct usb2_string_descriptor_struct usb2_string_product_name_default = {
	2 + USB2_PRODUCT_NAME_LEN * 2,
        3,
        USB2_PRODUCT_NAME
};
struct usb2_string_descriptor_struct usb2_string_serial_number_default = {
        12,
        3,
        {0,0,0,0,0,0,0,0,0,0}
};
PROGMEM const struct usb2_string_descriptor_struct usb2_string_mtp = {
	2 + 3 * 2,
	3,
	{'M','T','P'}
};

void usb2_init_serialnumber(void)
{
	char buf[11];
	uint32_t i, num;

	num = HW_OCOTP_MAC0 & 0xFFFFFF;
	// add extra zero to work around OS-X CDC-ACM driver bug
	if (num < 10000000) num = num * 10;
	ultoa(num, buf, 10);
	for (i=0; i<10; i++) {
		char c = buf[i];
		if (!c) break;
		usb2_string_serial_number_default.wString[i] = c;
	}
	usb2_string_serial_number_default.bLength = i * 2 + 2;
}


// **************************************************************
//   Descriptors List
// **************************************************************

// This table provides access to all the descriptor data above.
const int USB2_ConfigDescriptionSize_Normal = USB2_CONFIG_NORMAL_DESC_SIZE;
const int USB2_ConfigDescriptionSize_Debug = USB2_CONFIG_DEBUG_DESC_SIZE;

const usb2_descriptor_list_t usb2_descriptor_list[] = {
	//wValue, wIndex, address,          length
	{0x0100, 0x0000, device_descriptor, sizeof(device_descriptor)},
	{0x0600, 0x0000, qualifier_descriptor, sizeof(qualifier_descriptor)},
	{0x0200, 0x0000, usb2_config_descriptor_Debug_480, USB2_CONFIG_DEBUG_DESC_SIZE},
	{0x0700, 0x0000, usb2_config_descriptor_Debug_12, USB2_CONFIG_DEBUG_DESC_SIZE},
	{0x0304, 0x0409, (const uint8_t *)&usb2_string_mtp, 0},
        {0x0300, 0x0000, (const uint8_t *)&usb2_string0, 0},
        {0x0301, 0x0409, (const uint8_t *)&usb2_string_manufacturer_name, 0},
        {0x0302, 0x0409, (const uint8_t *)&usb2_string_product_name, 0},
        {0x0303, 0x0409, (const uint8_t *)&usb2_string_serial_number, 0},
	{0, 0, NULL, 0}
};


