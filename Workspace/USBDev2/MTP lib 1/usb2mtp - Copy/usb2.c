#define USB2_DESC_LIST_DEFINE
#include "usb2.h"
#include "usb2_serial.h"
#include "usb2_mtp.h"
#include "core_pins.h" // for delay()
#include "avr/pgmspace.h"
#include <string.h>
#include "debug/printf.h"
#include <stdio.h>

//enum USB2Mode_e USB2Mode = Debug;
enum USB2Mode_e USB2Mode = Normal;

//#define LOG_SIZE  20
//uint32_t transfer_log_head=0;
//uint32_t transfer_log_count=0;
//uint32_t transfer_log[LOG_SIZE];

// device mode, page 3155

typedef struct endpoint_struct usb2_endpoint_t;

struct endpoint_struct {
	uint32_t config;
	uint32_t current;
	uint32_t next;
	uint32_t status;
	uint32_t pointer0;
	uint32_t pointer1;
	uint32_t pointer2;
	uint32_t pointer3;
	uint32_t pointer4;
	uint32_t reserved;
	uint32_t setup0;
	uint32_t setup1;
	transfer_t* first_transfer;
	transfer_t* last_transfer;
	void (*callback_function)(transfer_t* completed_transfer);
	uint32_t unused1;
};

/*struct transfer_struct {
	uint32_t next;
	uint32_t status;
	uint32_t pointer0;
	uint32_t pointer1;
	uint32_t pointer2;
	uint32_t pointer3;
	uint32_t pointer4;
	uint32_t callback_param;
};*/

usb2_endpoint_t usb2_endpoint_queue_head[(USB2_NUM_DEBUG_ENDPOINTS + 1) * 2] __attribute__((used, aligned(4096), section(".endpoint_queue")));

transfer_t usb2_endpoint0_transfer_data __attribute__((used, aligned(32)));
transfer_t usb2_endpoint0_transfer_ack  __attribute__((used, aligned(32)));


typedef union {
	struct {
		union {
			struct {
				uint8_t bmRequestType;
				uint8_t bRequest;
			};
			uint16_t wRequestAndType;
		};
		uint16_t wValue;
		uint16_t wIndex;
		uint16_t wLength;
	};
	struct {
		uint32_t word1;
		uint32_t word2;
	};
	uint64_t bothwords;
} setup_t;

static setup_t usb2_endpoint0_setupdata;
static uint32_t usb2_endpoint0_notify_mask = 0;
static uint32_t usb2_endpointN_notify_mask = 0;
//static int reset_count=0;
volatile uint8_t usb2_configuration = 0; // non-zero when USB host as configured device
volatile uint8_t usb2_high_speed = 0;    // non-zero if running at 480 Mbit/sec speed
static uint8_t usb2_endpoint0_buffer[8];
static uint8_t sof_usage = 0;
static uint8_t usb2_reboot_timer = 0;

extern uint8_t usb2_descriptor_buffer[]; // defined in usb2_desc.c
extern const uint8_t usb2_config_descriptor_Debug_480[];
extern const uint8_t usb2_config_descriptor_Debug_12[];
extern const uint8_t usb2_config_descriptor_Normal_480[];
extern const uint8_t usb2_config_descriptor_Normal_12[];



void (*usb2_timer0_callback)(void) = NULL;
void (*usb2_timer1_callback)(void) = NULL;

static void usb2_isr(void);
static void usb2_endpoint0_setup(uint64_t setupdata);
static void usb2_endpoint0_transmit(const void* data, uint32_t len, int notify);
static void usb2_endpoint0_receive(void* data, uint32_t len, int notify);
static void usb2_endpoint0_complete(void);


static void usb2_run_callbacks(usb2_endpoint_t* ep);
int initDone = 0;

FLASHMEM void switchUSB2Mode(enum USB2Mode_e newMode) {
  if (USB2Mode == newMode)
    return;
  USB2Mode = newMode;
  USB2_USBCMD = 0;
  delay(50);

  initDone = 0;
  usb2_init();
}


FLASHMEM void usb2_init(void) {
	if (initDone)
		return;
	initDone = 1;
	// TODO: only enable when VBUS detected
	// TODO: return to low power mode when VBUS removed
	// TODO: protect PMU access with MPU
	PMU_REG_3P0 = PMU_REG_3P0_OUTPUT_TRG(0x0F) | PMU_REG_3P0_BO_OFFSET(6)
		| PMU_REG_3P0_ENABLE_LINREG;

	usb2_init_serialnumber();

	// assume PLL3 is already running - already done by usb2_pll_start() in main.c

	CCM_CCGR6 |= CCM_CCGR6_USBOH3(CCM_CCGR_ON); // turn on clocks to USB peripheral

	// Teensy 4.0 PLL & USB PHY powerup
	while (1) {
		uint32_t n = CCM_ANALOG_PLL_USB2;
		if (n & CCM_ANALOG_PLL_USB2_DIV_SELECT) {
			CCM_ANALOG_PLL_USB2_CLR = 0xC000; // get out of 528 MHz mode
			CCM_ANALOG_PLL_USB2_SET = CCM_ANALOG_PLL_USB2_BYPASS;
			CCM_ANALOG_PLL_USB2_CLR = CCM_ANALOG_PLL_USB2_POWER |
				CCM_ANALOG_PLL_USB2_DIV_SELECT |
				CCM_ANALOG_PLL_USB2_ENABLE |
				CCM_ANALOG_PLL_USB2_EN_USB_CLKS;
			continue;
		}
		if (!(n & CCM_ANALOG_PLL_USB2_ENABLE)) {
			CCM_ANALOG_PLL_USB2_SET = CCM_ANALOG_PLL_USB2_ENABLE; // enable
			continue;
		}
		if (!(n & CCM_ANALOG_PLL_USB2_POWER)) {
			CCM_ANALOG_PLL_USB2_SET = CCM_ANALOG_PLL_USB2_POWER; // power up
			continue;
		}
		if (!(n & CCM_ANALOG_PLL_USB2_LOCK)) {
			continue; // wait for lock
		}
		if (n & CCM_ANALOG_PLL_USB2_BYPASS) {
			CCM_ANALOG_PLL_USB2_CLR = CCM_ANALOG_PLL_USB2_BYPASS; // turn off bypass
			continue;
		}
		if (!(n & CCM_ANALOG_PLL_USB2_EN_USB_CLKS)) {
			CCM_ANALOG_PLL_USB2_SET = CCM_ANALOG_PLL_USB2_EN_USB_CLKS; // enable
			continue;
		}
		printf("USB2 PLL running");
		break; // USB2 PLL up and running
	}
	// turn on USB clocks (should already be on)
	CCM_CCGR6 |= CCM_CCGR6_USBOH3(CCM_CCGR_ON);
	// turn on USB2 PHY
	USBPHY2_CTRL_CLR = USBPHY_CTRL_SFTRST | USBPHY_CTRL_CLKGATE;
	USBPHY2_CTRL_SET = USBPHY_CTRL_ENDEVPLUGINDETECT | USBPHY_CTRL_ENUTMILEVEL2 | USBPHY_CTRL_ENUTMILEVEL3;
	USBPHY2_PWD = 0;

	printf("BURSTSIZE=%08lX\n", USB2_BURSTSIZE);
	//USB1_BURSTSIZE = usb2_BURSTSIZE_TXPBURST(4) | usb2_BURSTSIZE_RXPBURST(4);
	USB2_BURSTSIZE = 0x0404;
	printf("BURSTSIZE=%08lX\n", USB2_BURSTSIZE);
	printf("USB1_TXFILLTUNING=%08lX\n", USB2_TXFILLTUNING);

	// Before programming this register, the PHY clocks must be enabled in registers
	// USBPHYx_CTRLn and CCM_ANALOG_USBPHYx_PLL_480_CTRLn.

	//printf("USBPHY1_PWD=%08lX\n", USBPHY1_PWD);
	//printf("USBPHY1_TX=%08lX\n", USBPHY1_TX);
	//printf("USBPHY1_RX=%08lX\n", USBPHY1_RX);
	//printf("USBPHY1_CTRL=%08lX\n", USBPHY1_CTRL);
	//printf("USB1_USBMODE=%08lX\n", USB1_USBMODE);

	// turn on PLL3, wait for 480 MHz lock?
	// turn on CCM clock gates?  CCGR6[CG0]

	//USBPHY2_CTRL_CLR = USBPHY_CTRL_SFTRST | USBPHY_CTRL_CLKGATE;
	//USBPHY2_CTRL_SET = USBPHY_CTRL_ENDEVPLUGINDETECT | USBPHY_CTRL_ENUTMILEVEL2 | USBPHY_CTRL_ENUTMILEVEL3;

#if 1
	if ((USBPHY2_PWD & (USBPHY_PWD_RXPWDRX | USBPHY_PWD_RXPWDDIFF | USBPHY_PWD_RXPWD1PT1
			| USBPHY_PWD_RXPWDENV | USBPHY_PWD_TXPWDV2I | USBPHY_PWD_TXPWDIBIAS
			| USBPHY_PWD_TXPWDFS)) || (USB2_USBMODE & USB_USBMODE_CM_MASK)) {
		// USB controller is turned on from previous use
		// reset needed to turn it off & start from clean slate
		USBPHY2_CTRL_SET = USBPHY_CTRL_SFTRST; // USBPHY1_CTRL page 3292
		USB2_USBCMD |= USB_USBCMD_RST; // reset controller
		int count = 0;
		while (USB2_USBCMD & USB_USBCMD_RST) count++;
		NVIC_CLEAR_PENDING(IRQ_USB2);
		USBPHY2_CTRL_CLR = USBPHY_CTRL_SFTRST; // reset PHY
		//USB1_USBSTS = USB1_USBSTS; // TODO: is this needed?
		printf("USB reset took %d loops\n", count);
		//delay(10);
		//printf("\n");
		//printf("USBPHY1_PWD=%08lX\n", USBPHY1_PWD);
		//printf("USBPHY1_TX=%08lX\n", USBPHY1_TX);
		//printf("USBPHY1_RX=%08lX\n", USBPHY1_RX);
		//printf("USBPHY1_CTRL=%08lX\n", USBPHY1_CTRL);
		//printf("USB1_USBMODE=%08lX\n", USB1_USBMODE);
		delay(25);
	}
#endif
	// Device Controller Initialization, page 2351 (Rev 2, 12/2019)
	// USBCMD	pg 3216
	// USBSTS	pg 3220
	// USBINTR	pg 3224
	// DEVICEADDR	pg 3227
	// ENDPTLISTADDR   3229
	// USBMODE	pg 3244
	// ENDPTSETUPSTAT  3245
	// ENDPTPRIME	pg 3246
	// ENDPTFLUSH	pg 3247
	// ENDPTSTAT	pg 3247
	// ENDPTCOMPLETE   3248
	// ENDPTCTRL0	pg 3249

	USBPHY2_CTRL_CLR = USBPHY_CTRL_CLKGATE;
	USBPHY2_PWD = 0;
	//printf("USBPHY1_PWD=%08lX\n", USBPHY1_PWD);
	//printf("USBPHY1_CTRL=%08lX\n", USBPHY1_CTRL);

	USB2_USBMODE = USB_USBMODE_CM(2) | USB_USBMODE_SLOM;
	memset(usb2_endpoint_queue_head, 0, sizeof(usb2_endpoint_queue_head));
	usb2_endpoint_queue_head[0].config = (64 << 16) | (1 << 15);
	usb2_endpoint_queue_head[1].config = (64 << 16);
	USB2_ENDPOINTLISTADDR = (uint32_t)&usb2_endpoint_queue_head;
	//  Recommended: enable all device interrupts including: USBINT, USBERRINT,
	// Port Change Detect, USB Reset Received, DCSuspend.
	USB2_USBINTR = USB_USBINTR_UE | USB_USBINTR_UEE | /* usb2_USBINTR_PCE | */
		USB_USBINTR_URE | USB_USBINTR_SLE;
	//_VectorsRam[IRQ_USB1+16] = &usb2_isr;
	attachInterruptVector(IRQ_USB2, &usb2_isr);
	NVIC_ENABLE_IRQ(IRQ_USB2);
	//printf("USB1_ENDPTCTRL0=%08lX\n", USB1_ENDPTCTRL0);
	//printf("USB1_ENDPTCTRL1=%08lX\n", USB1_ENDPTCTRL1);
	//printf("USB1_ENDPTCTRL2=%08lX\n", USB1_ENDPTCTRL2);
	//printf("USB1_ENDPTCTRL3=%08lX\n", USB1_ENDPTCTRL3);
	USB2_USBCMD = USB_USBCMD_RS;
	//transfer_log_head = 0;
	//transfer_log_count = 0;
	//USB1_PORTSC1 |= usb2_PORTSC1_PFSC; // force 12 Mbit/sec

}

/*
FLASHMEM void _reboot_Teensyduino_(void)
{
	if (!(HW_OCOTP_CFG5 & 0x02)) {
		asm("bkpt #251"); // run bootloader
	} else {
		__disable_irq(); // secure mode NXP ROM reboot
		USB2_USBCMD = 0;
		IOMUXC_GPR_GPR16 = 0x00200003;
		// TODO: wipe all RAM for security
		__asm__ volatile("mov sp, %0" : : "r" (0x20201000) : );
		__asm__ volatile("dsb":::"memory");
		volatile uint32_t * const p = (uint32_t *)0x20208000;
		*p = 0xEB120000;
		((void (*)(volatile void *))(*(uint32_t *)(*(uint32_t *)0x0020001C + 8)))(p);
	}
	__builtin_unreachable();
}
*/

static void usb2_isr(void) {
	//printf("*");

	//  Port control in device mode is only used for
	//  status port reset, suspend, and current connect status.
	uint32_t status = USB2_USBSTS;
	USB2_USBSTS = status;

	// usb2_USBSTS_SLI - set to 1 when enters a suspend state from an active state
	// usb2_USBSTS_SRI - set at start of frame
	// usb2_USBSTS_SRI - set when USB reset detected

	if (status & USB_USBSTS_UI) {
		//printf("data\n");
		uint32_t setupstatus = USB2_ENDPTSETUPSTAT;
		//printf("USB1_ENDPTSETUPSTAT=%X\n", setupstatus);
		while (setupstatus) {
  			USB2_ENDPTSETUPSTAT = setupstatus;
			setup_t s;
			do {
				USB2_USBCMD |= USB_USBCMD_SUTW;
				s.word1 = usb2_endpoint_queue_head[0].setup0;
				s.word2 = usb2_endpoint_queue_head[0].setup1;
			} while (!(USB2_USBCMD & USB_USBCMD_SUTW));
			USB2_USBCMD &= ~USB_USBCMD_SUTW;
			//printf("setup %08lX %08lX\n", s.word1, s.word2);
			USB2_ENDPTFLUSH = (1 << 16) | (1 << 0); // page 3174
			while (USB2_ENDPTFLUSH & ((1 << 16) | (1 << 0)));
			usb2_endpoint0_notify_mask = 0;
			usb2_endpoint0_setup(s.bothwords);
			setupstatus = USB2_ENDPTSETUPSTAT; // page 3175
		}
		uint32_t completestatus = USB2_ENDPTCOMPLETE;
		if (completestatus) {
			USB2_ENDPTCOMPLETE = completestatus;
      
			//printf("USB1_ENDPTCOMPLETE=%lX\n", completestatus);
			if (completestatus & usb2_endpoint0_notify_mask) {
				usb2_endpoint0_notify_mask = 0;
				usb2_endpoint0_complete();
			}
			completestatus &= usb2_endpointN_notify_mask;
#if 0
			if (completestatus) {

				trackingFlags |= 1 << 5;

				// transmit:
				uint32_t tx = completestatus >> 16;
				while (tx) {
					int p = __builtin_ctz(tx);
					usb2_run_callbacks(usb2_endpoint_queue_head + p * 2 + 1);
					tx &= ~(1 << p);
				}

				// receive:
				uint32_t rx = completestatus & 0xffff;
				while (rx) {
					int p = __builtin_ctz(rx);
					usb2_run_callbacks(usb2_endpoint_queue_head + p * 2);
					rx &= ~(1 << p);
				};
			}
#else
			if (completestatus) {
				int i;   // TODO: optimize with __builtin_ctz()
				for (i = 2; i <= CURRENT_ENDPOINT_COUNT; i++) {
					if (completestatus & (1 << i)) { // receive
						usb2_run_callbacks(usb2_endpoint_queue_head + i * 2);
					}
					if (completestatus & (1 << (i + 16))) { // transmit
						usb2_run_callbacks(usb2_endpoint_queue_head + i * 2 + 1);
					}
				}
			}
#endif

		}
	}
	if (status & USB_USBSTS_URI) { // page 3164
		USB2_ENDPTSETUPSTAT = USB2_ENDPTSETUPSTAT; // Clear all setup token semaphores
		USB2_ENDPTCOMPLETE = USB2_ENDPTCOMPLETE; // Clear all the endpoint complete status
		while (USB2_ENDPTPRIME != 0); // Wait for any endpoint priming
		USB2_ENDPTFLUSH = 0xFFFFFFFF;  // Cancel all endpoint primed status
		if ((USB2_PORTSC1 & USB_PORTSC1_PR)) {
			//printf("reset\n");
		} else {
			// we took too long to respond :(
			// TODO; is this ever really a problem?
			//printf("reset too slow\n");
		}
		usb2_serial_reset();
		usb2_endpointN_notify_mask = 0;
		// TODO: Free all allocated dTDs
		//if (++reset_count >= 3) {
			// shut off USB - easier to see results in protocol analyzer
			//USB1_USBCMD &= ~usb2_USBCMD_RS;
			//printf("shut off USB\n");
		//}
	}
	if (status & USB_USBSTS_TI0) {
		if (usb2_timer0_callback != NULL) usb2_timer0_callback();
	}
	if (status & USB_USBSTS_TI1) {
		if (usb2_timer1_callback != NULL) usb2_timer1_callback();
	}
	if (status & USB_USBSTS_PCI) {
		if (USB2_PORTSC1 & USB_PORTSC1_HSP) {
			//printf("port at 480 Mbit\n");
			usb2_high_speed = 1;
		} else {
			//printf("port at 12 Mbit\n");
			usb2_high_speed = 0;
		}
	}
	if (status & USB_USBSTS_SLI) { // page 3165
		//printf("suspend\n");
	}
	if (status & USB_USBSTS_UEI) {
		//printf("error\n");
	}
	if ((USB2_USBINTR & USB_USBINTR_SRE) && (status & USB_USBSTS_SRI)) {
		//printf("sof %d\n", usb2_reboot_timer);
		if (usb2_reboot_timer) {
			if (--usb2_reboot_timer == 0) {
				usb2_stop_sof_interrupts(CURRENT_ENDPOINT_COUNT);
				_reboot_Teensyduino_();
			}
		}
	}
}


void usb2_start_sof_interrupts(int interface) {
	__disable_irq();
	sof_usage |= (1 << interface);
	uint32_t intr = USB2_USBINTR;
	if (!(intr & USB_USBINTR_SRE)) {
		USB2_USBSTS = USB_USBSTS_SRI; // clear prior SOF before SOF IRQ enable
		USB2_USBINTR = intr | USB_USBINTR_SRE;
	}
	__enable_irq();
}

void usb2_stop_sof_interrupts(int interface) {
	sof_usage &= ~(1 << interface);
	if (sof_usage == 0) {
		USB2_USBINTR &= ~USB_USBINTR_SRE;
	}
}

/*
char myDebugBuffer[128];
void mydebug(char* data, int len) {
  while (len && *data) {
    usb_serial_putchar(*data);
    data++; len--;
  }
}  
void mydebugHex(char* data, int len) {
int count = 0;
  while (count<len) {
    if ((count % 16) == 0) {
    usb_serial_putchar('\n');
usb_serial_putchar('0');
usb_serial_putchar('x');
    }
    char b1 = (*data & 0xf0)>>4;
    if (b1>9)
      b1+='A'-10;
    else
      b1+='0';
    char b2 = (*data & 0x0f);
    if (b2>9)
      b2+='A'-10;
    else
      b2+='0';

    usb_serial_putchar(b1);
    usb_serial_putchar(b2);
    data++; count++;
  }
}  
*/



/*
struct transfer_struct { // table 55-60, pg 3159
	uint32_t next;
	uint32_t status;
	uint32_t pointer0;
	uint32_t pointer1;
	uint32_t pointer2;
	uint32_t pointer3;
	uint32_t pointer4;
	uint32_t unused1;
};
transfer_t endpoint0_transfer_data __attribute__ ((aligned(32)));;
transfer_t endpoint0_transfer_ack  __attribute__ ((aligned(32)));;
*/

static uint8_t reply_buffer[8];

static void usb2_endpoint0_setup(uint64_t setupdata) {
	setup_t setup;
	uint32_t endpoint, dir, ctrl;
	const usb2_descriptor_list_t* list;

	setup.bothwords = setupdata;
	switch (setup.wRequestAndType) {
		case 0x0500: // SET_ADDRESS
			usb2_endpoint0_receive(NULL, 0, 0);
			USB2_DEVICEADDR = USB_DEVICEADDR_USBADR(setup.wValue) | USB_DEVICEADDR_USBADRA;
			return;
		case 0x0900: // SET_CONFIGURATION
			usb2_configuration = setup.wValue;
			// configure all other endpoints
			USB2_ENDPTCTRL2 = USB2_ENDPOINT2_CONFIG;
			USB2_ENDPTCTRL3 = USB2_ENDPOINT3_CONFIG;
			usb2_serial_configure();
			if (USB2Mode == Debug) {
  			USB2_ENDPTCTRL4 = USB2_ENDPOINT4_CONFIG;
	  		USB2_ENDPTCTRL5 = USB2_ENDPOINT5_CONFIG;
		  	USB2_ENDPTCTRL6 = USB2_ENDPOINT6_CONFIG;
			  USB2_ENDPTCTRL7 = USB2_ENDPOINT7_CONFIG;
				usb2_serial2_configure();
				usb_mtp_configure();
			}
			usb2_endpoint0_receive(NULL, 0, 0);
			return;
		case 0x0880: // GET_CONFIGURATION
			reply_buffer[0] = usb2_configuration;
			usb2_endpoint0_transmit(reply_buffer, 1, 0);
			return;
		case 0x0080: // GET_STATUS (device)
			reply_buffer[0] = 0;
			reply_buffer[1] = 0;
			usb2_endpoint0_transmit(reply_buffer, 2, 0);
			return;
		case 0x0082: // GET_STATUS (endpoint)
			endpoint = setup.wIndex & 0x7F;
			if (endpoint > 7) break;
			dir = setup.wIndex & 0x80;
			ctrl = *((uint32_t*)&USB2_ENDPTCTRL0 + endpoint);
			reply_buffer[0] = 0;
			reply_buffer[1] = 0;
			if ((dir && (ctrl & USB_ENDPTCTRL_TXS)) || (!dir && (ctrl & USB_ENDPTCTRL_RXS))) {
				reply_buffer[0] = 1;
			}
			usb2_endpoint0_transmit(reply_buffer, 2, 0);
			return;
		case 0x0302: // SET_FEATURE (endpoint)
			endpoint = setup.wIndex & 0x7F;
			if (endpoint > 7) break;
			dir = setup.wIndex & 0x80;
			if (dir) {
				*((volatile uint32_t*)&USB2_ENDPTCTRL0 + endpoint) |= USB_ENDPTCTRL_TXS;
			} else {
				*((volatile uint32_t*)&USB2_ENDPTCTRL0 + endpoint) |= USB_ENDPTCTRL_RXS;
			}
			usb2_endpoint0_receive(NULL, 0, 0);
			return;
		case 0x0102: // CLEAR_FEATURE (endpoint)
			endpoint = setup.wIndex & 0x7F;
			if (endpoint > 7) break;
			dir = setup.wIndex & 0x80;
			if (dir) {
				*((volatile uint32_t*)&USB2_ENDPTCTRL0 + endpoint) &= ~USB_ENDPTCTRL_TXS;
			} else {
				*((volatile uint32_t*)&USB2_ENDPTCTRL0 + endpoint) &= ~USB_ENDPTCTRL_RXS;
			}
			usb2_endpoint0_receive(NULL, 0, 0);
			return;
		case 0x0680: // GET_DESCRIPTOR
		case 0x0681:
			for (list = usb2_descriptor_list; list->addr != NULL; list++) {
				if (setup.wValue == list->wValue && setup.wIndex == list->wIndex) {
					uint32_t datalen;
          uint32_t desclen;
					if ((setup.wValue >> 8) == 3) {
						// for string descriptors, use the descriptor's
						// length field, allowing runtime configured length.
						datalen = *(list->addr);
					} else {
						datalen = list->length;
					}
					if (datalen > setup.wLength) datalen = setup.wLength;

					// copy the descriptor, from PROGMEM to DMAMEM
					if (setup.wValue == 0x200) {
						// config descriptor needs to adapt to speed          
						const uint8_t* src;
						if (USB2Mode == Normal) {
							desclen = USB2_ConfigDescriptionSize_Normal;
								if (usb2_high_speed)
									src = usb2_config_descriptor_Normal_480;
								else
									src = usb2_config_descriptor_Normal_12;
						} else {
							desclen = USB2_ConfigDescriptionSize_Debug;
								if (usb2_high_speed)
									src = usb2_config_descriptor_Debug_480;
								else
									src = usb2_config_descriptor_Debug_12;
						}
            if (datalen > desclen)
              datalen = desclen;
//            int dbLen = sprintf(myDebugBuffer, "\r\nMode %s,Speed %s,Len %d\r\n",(USB2Mode == Normal)?"Normal":"Debug",usb2_high_speed?"High":"Low",datalen);
 //           mydebug(myDebugBuffer, dbLen);

						memcpy(usb2_descriptor_buffer, src, datalen);
//            mydebugHex(usb2_descriptor_buffer,datalen);
					} else if (setup.wValue == 0x700) {
						// other speed config also needs to adapt
						const uint8_t* src;
						if (USB2Mode == Normal) {
							desclen = USB2_ConfigDescriptionSize_Normal;
								if (usb2_high_speed)
									src = usb2_config_descriptor_Normal_12;
								else
									src = usb2_config_descriptor_Normal_480;
						} else {
							desclen = USB2_ConfigDescriptionSize_Debug;
								if (usb2_high_speed)
									src = usb2_config_descriptor_Debug_12;
								else
									src = usb2_config_descriptor_Debug_480;
						}
            if (datalen > desclen)
              datalen = desclen;
						memcpy(usb2_descriptor_buffer, src, datalen);
//            int dbLen = sprintf(myDebugBuffer, "\r\n0x700 Mode %s,Speed %s,Len %d\r\n",(USB2Mode == Normal)?"Normal":"Debug",usb2_high_speed?"High":"Low",datalen);
//            mydebug(myDebugBuffer, dbLen);
						usb2_descriptor_buffer[1] = 7;
					} else {
						memcpy(usb2_descriptor_buffer, list->addr, datalen);
//            int dbLen = sprintf(myDebugBuffer, "\r\n0x%04X,Len %d\r\n",setup.wValue, datalen);
//            mydebug(myDebugBuffer, dbLen);
//            mydebugHex(usb2_descriptor_buffer,datalen);
					}
					// prep transmit
//					if (!((setup.wValue == 0x0304) && (USB2Mode == Normal))) { // transmit the data unless its for MTP when not in debug mode
						arm_dcache_flush_delete(usb2_descriptor_buffer, datalen);
						usb2_endpoint0_transmit(usb2_descriptor_buffer, datalen, 0);

//					}
					return;
				}
			}
			break;
		case 0x2221: // CDC_SET_CONTROL_LINE_STATE
			if (setup.wIndex == USB2_CDC_STATUS_INTERFACE) {
				usb2_cdc_line_rtsdtr_millis = systick_millis_count;
				usb2_cdc_line_rtsdtr = setup.wValue;
			}
			if (setup.wIndex == USB2_CDC2_STATUS_INTERFACE) {
				usb2_cdc2_line_rtsdtr_millis = systick_millis_count;
				usb2_cdc2_line_rtsdtr = setup.wValue;
			}
			__attribute__((fallthrough));
			// fall through to next case, to always send ZLP ACK
		case 0x2321: // CDC_SEND_BREAK
			usb2_endpoint0_receive(NULL, 0, 0);
			return;
		case 0x2021: // CDC_SET_LINE_CODING
			if (setup.wLength != 7) break;
			usb2_endpoint0_setupdata.bothwords = setupdata;
			usb2_endpoint0_receive(usb2_endpoint0_buffer, 7, 1);
			return;
		case 0x6421: // Cancel Request, Still Image Class 1.0, 5.2.1, page 8
			if (USB2Mode == Debug) {
				if (setup.wLength == 6) {
					usb2_endpoint0_setupdata.bothwords = setupdata;
					usb2_endpoint0_receive(usb2_endpoint0_buffer, setup.wLength, 1);
					return;
				}
			}
			break;
		case 0x65A1: // Get Extended Event Data, Still Image Class 1.0, 5.2.2, page 9
			break;
		case 0x6621: // Device Reset, Still Image Class 1.0, 5.2.3 page 10
			break;
		case 0x67A1: // Get Device Status, Still Image Class 1.0, 5.2.4, page 10
			if (USB2Mode == Debug) {
				if (setup.wLength >= 4) {
					usb2_endpoint0_buffer[0] = 4;
					usb2_endpoint0_buffer[1] = 0;
					usb2_endpoint0_buffer[2] = usb_mtp_status;
					usb2_endpoint0_buffer[3] = 0x20;
					usb2_endpoint0_transmit(usb2_endpoint0_buffer, 4, 0);
					//if (usb_mtp_status == 0x19) usb_mtp_status = 0x01; // testing only
					return;
				}
			}
			break;
	}
	printf("endpoint 0 stall\n");
	USB2_ENDPTCTRL0 = 0x000010001; // stall
}

static void usb2_endpoint0_transmit(const void* data, uint32_t len, int notify) {
  printf("EP0_tx. Len %u, notify %d\r\n",len,notify);
	//printf("tx %lu\n", len);
	if (len > 0) {
		// Executing A Transfer Descriptor, page 3182
		usb2_endpoint0_transfer_data.next = 1;
		usb2_endpoint0_transfer_data.status = (len << 16) | (1 << 7);
		uint32_t addr = (uint32_t)data;
		usb2_endpoint0_transfer_data.pointer0 = addr; // format: table 55-60, pg 3159
		usb2_endpoint0_transfer_data.pointer1 = addr + 4096;
		usb2_endpoint0_transfer_data.pointer2 = addr + 8192;
		usb2_endpoint0_transfer_data.pointer3 = addr + 12288;
		usb2_endpoint0_transfer_data.pointer4 = addr + 16384;
		//  Case 1: Link list is empty, page 3182
		usb2_endpoint_queue_head[1].next = (uint32_t)&usb2_endpoint0_transfer_data;
		usb2_endpoint_queue_head[1].status = 0;
		USB2_ENDPTPRIME |= (1 << 16);
		while (USB2_ENDPTPRIME);
	}
	usb2_endpoint0_transfer_ack.next = 1;
	usb2_endpoint0_transfer_ack.status = (1 << 7) | (notify ? (1 << 15) : 0);
	usb2_endpoint0_transfer_ack.pointer0 = 0;
	usb2_endpoint_queue_head[0].next = (uint32_t)&usb2_endpoint0_transfer_ack;
	usb2_endpoint_queue_head[0].status = 0;
	USB2_ENDPTCOMPLETE = (1 << 0) | (1 << 16);
	USB2_ENDPTPRIME |= (1 << 0);
	usb2_endpoint0_notify_mask = (notify ? (1 << 0) : 0);
	while (USB2_ENDPTPRIME);
}

static void usb2_endpoint0_receive(void* data, uint32_t len, int notify) {
	//printf("rx %lu\n", len);
	if (len > 0) {
		// Executing A Transfer Descriptor, page 3182
		usb2_endpoint0_transfer_data.next = 1;
		usb2_endpoint0_transfer_data.status = (len << 16) | (1 << 7);
		uint32_t addr = (uint32_t)data;
		usb2_endpoint0_transfer_data.pointer0 = addr; // format: table 55-60, pg 3159
		usb2_endpoint0_transfer_data.pointer1 = addr + 4096;
		usb2_endpoint0_transfer_data.pointer2 = addr + 8192;
		usb2_endpoint0_transfer_data.pointer3 = addr + 12288;
		usb2_endpoint0_transfer_data.pointer4 = addr + 16384;
		//  Case 1: Link list is empty, page 3182
		usb2_endpoint_queue_head[0].next = (uint32_t)&usb2_endpoint0_transfer_data;
		usb2_endpoint_queue_head[0].status = 0;
		USB2_ENDPTPRIME |= (1 << 0);
		while (USB2_ENDPTPRIME);
	}
	usb2_endpoint0_transfer_ack.next = 1;
	usb2_endpoint0_transfer_ack.status = (1 << 7) | (notify ? (1 << 15) : 0);
	usb2_endpoint0_transfer_ack.pointer0 = 0;
	usb2_endpoint_queue_head[1].next = (uint32_t)&usb2_endpoint0_transfer_ack;
	usb2_endpoint_queue_head[1].status = 0;
	USB2_ENDPTCOMPLETE = (1 << 0) | (1 << 16);
	USB2_ENDPTPRIME |= (1 << 16);
	usb2_endpoint0_notify_mask = (notify ? (1 << 16) : 0);
	while (USB2_ENDPTPRIME);
}

/*typedef union {
 struct {
	union {
	 struct {
				uint8_t bmRequestType;
				uint8_t bRequest;
	 };
				uint16_t wRequestAndType;
	};
				uint16_t wValue;
				uint16_t wIndex;
				uint16_t wLength;
 };
 struct {
				uint32_t word1;
				uint32_t word2;
 };
	uint64_t bothwords;
} setup_t; */


static void usb2_endpoint0_complete(void) {
	setup_t setup;

	setup.bothwords = usb2_endpoint0_setupdata.bothwords;
	//printf("complete %x %x %x\n", setup.word1, setup.word2, endpoint0_buffer[0]);
	// 0x2021 is CDC_SET_LINE_CODING
	if (setup.wRequestAndType == 0x2021 && setup.wIndex == USB2_CDC_STATUS_INTERFACE) {
		memcpy(usb2_cdc_line_coding, usb2_endpoint0_buffer, 7);
		printf("usb2_cdc_line_coding, baud=%u\n", usb2_cdc_line_coding[0]);
		if (usb2_cdc_line_coding[0] == 134) {
			usb2_start_sof_interrupts((USB2Mode == Normal) ? USB2_NUM_NORMAL_INTERFACE : USB2_NUM_DEBUG_INTERFACE);
			usb2_reboot_timer = 80; // TODO: 10 if only 12 Mbit/sec
		}
	}
	if (setup.wRequestAndType == 0x2021 && setup.wIndex == USB2_CDC2_STATUS_INTERFACE) {
		if (USB2Mode == Debug) {
			memcpy(usb2_cdc2_line_coding, usb2_endpoint0_buffer, 7);
			printf("usb2_cdc2_line_coding, baud=%u\n", usb2_cdc2_line_coding[0]);
			if (usb2_cdc2_line_coding[0] == 134) {
				usb2_start_sof_interrupts((USB2Mode == Normal) ? USB2_NUM_NORMAL_INTERFACE : USB2_NUM_DEBUG_INTERFACE);
				usb2_reboot_timer = 80; // TODO: 10 if only 12 Mbit/sec
			}
		}
	}
	if (setup.wRequestAndType == 0x6421) {
		if (USB2Mode == Debug) {
			if (usb2_endpoint0_buffer[0] == 0x01 && usb2_endpoint0_buffer[1] == 0x40) {
				printf("MTP cancel, transaction ID=%08X\n",
							 usb2_endpoint0_buffer[2] | (usb2_endpoint0_buffer[3] << 8) |
							 (usb2_endpoint0_buffer[4] << 16) | (usb2_endpoint0_buffer[5] << 24));
				usb_mtp_status = 0x19; // 0x19 = host initiated cancel
			}
		}
	}
}

static void usb2_endpoint_config(usb2_endpoint_t* qh, uint32_t config, void (*callback)(transfer_t*)) {
	memset(qh, 0, sizeof(usb2_endpoint_t));
	qh->config = config;
	qh->next = 1; // Terminate bit = 1
	qh->callback_function = callback;
}

void usb2_config_rx(uint32_t ep, uint32_t packet_size, int do_zlp, void (*cb)(transfer_t*)) {
	uint32_t config = (packet_size << 16) | (do_zlp ? 0 : (1 << 29));
	if (ep < 2 || ep > CURRENT_ENDPOINT_COUNT) return;
	usb2_endpoint_config(usb2_endpoint_queue_head + ep * 2, config, cb);
	if (cb) usb2_endpointN_notify_mask |= (1 << ep);
}

void usb2_config_tx(uint32_t ep, uint32_t packet_size, int do_zlp, void (*cb)(transfer_t*)) {
	uint32_t config = (packet_size << 16) | (do_zlp ? 0 : (1 << 29));
	if (ep < 2 || ep > CURRENT_ENDPOINT_COUNT) return;
	usb2_endpoint_config(usb2_endpoint_queue_head + ep * 2 + 1, config, cb);
	if (cb) usb2_endpointN_notify_mask |= (1 << (ep + 16));
}

void usb2_config_rx_iso(uint32_t ep, uint32_t packet_size, int mult, void (*cb)(transfer_t*)) {
	if (mult < 1 || mult > 3) return;
	uint32_t config = (packet_size << 16) | (mult << 30);
	if (ep < 2 || ep > CURRENT_ENDPOINT_COUNT) return;
	usb2_endpoint_config(usb2_endpoint_queue_head + ep * 2, config, cb);
	if (cb) usb2_endpointN_notify_mask |= (1 << ep);
}

void usb2_config_tx_iso(uint32_t ep, uint32_t packet_size, int mult, void (*cb)(transfer_t*)) {
	if (mult < 1 || mult > 3) return;
	uint32_t config = (packet_size << 16) | (mult << 30);
	if (ep < 2 || ep > CURRENT_ENDPOINT_COUNT) return;
	usb2_endpoint_config(usb2_endpoint_queue_head + ep * 2 + 1, config, cb);
	if (cb) usb2_endpointN_notify_mask |= (1 << (ep + 16));
}



void usb2_prepare_transfer(transfer_t* transfer, const void* data, uint32_t len, uint32_t param) {
	transfer->next = 1;
	transfer->status = (len << 16) | (1 << 7);
	uint32_t addr = (uint32_t)data;
	transfer->pointer0 = addr;
	transfer->pointer1 = addr + 4096;
	transfer->pointer2 = addr + 8192;
	transfer->pointer3 = addr + 12288;
	transfer->pointer4 = addr + 16384;
	transfer->callback_param = param;
}

#if 0
void usb2_print_transfer_log(void) {
	uint32_t i, count;
	printf("log %d transfers\n", transfer_log_count);
	count = transfer_log_count;
	if (count > LOG_SIZE) count = LOG_SIZE;

	for (i = 0; i < count; i++) {
		if (transfer_log_head == 0) transfer_log_head = LOG_SIZE;
		transfer_log_head--;
		uint32_t log = transfer_log[transfer_log_head];
		printf(" %c %X\n", log >> 8, (int)(log & 255));
	}
}
#endif

static void usb2_schedule_transfer(usb2_endpoint_t* endpoint, uint32_t epmask, transfer_t* transfer) {
	// when we stop at 6, why is the last transfer missing from the USB output?
	//if (transfer_log_count >= 6) return;

	//uint32_t ret = (*(const uint8_t *)transfer->pointer0) << 8;
	if (endpoint->callback_function) {
		transfer->status |= (1 << 15);
	}
	__disable_irq();
	//digitalWriteFast(1, HIGH);
	// Executing A Transfer Descriptor, page 2468 (RT1060 manual, Rev 1, 12/2018)
	transfer_t* last = endpoint->last_transfer;
	if (last) {
		last->next = (uint32_t)transfer;
		if (USB2_ENDPTPRIME & epmask) goto end;
		//digitalWriteFast(2, HIGH);
		//ret |= 0x01;
		uint32_t status, cyccnt = ARM_DWT_CYCCNT;
		do {
			USB2_USBCMD |= USB_USBCMD_ATDTW;
			status = USB2_ENDPTSTATUS;
		} while (!(USB2_USBCMD & USB_USBCMD_ATDTW) && (ARM_DWT_CYCCNT - cyccnt < 2400));
		//USB1_USBCMD &= ~usb2_USBCMD_ATDTW;
		if (status & epmask) goto end;
		//ret |= 0x02;
		endpoint->next = (uint32_t)transfer;
		endpoint->status = 0;
		USB2_ENDPTPRIME |= epmask;
		goto end;
	}
	//digitalWriteFast(4, HIGH);
	endpoint->next = (uint32_t)transfer;
	endpoint->status = 0;
	USB2_ENDPTPRIME |= epmask;
	endpoint->first_transfer = transfer;
end:
	endpoint->last_transfer = transfer;
	__enable_irq();
	//digitalWriteFast(4, LOW);
	//digitalWriteFast(3, LOW);
	//digitalWriteFast(2, LOW);
	//digitalWriteFast(1, LOW);
	//if (transfer_log_head > LOG_SIZE) transfer_log_head = 0;
	//transfer_log[transfer_log_head++] = ret;
	//transfer_log_count++;
}
// ENDPTPRIME -  Software should write a one to the corresponding bit when
//		 posting a new transfer descriptor to an endpoint queue head.
//		 Hardware automatically uses this bit to begin parsing for a
//		 new transfer descriptor from the queue head and prepare a
//		 transmit buffer. Hardware clears this bit when the associated
//		 endpoint(s) is (are) successfully primed.
//		 Momentarily set by hardware during hardware re-priming
//		 operations when a dTD is retired, and the dQH is updated.

// ENDPTSTATUS - Transmit Buffer Ready - set to one by the hardware as a
//		 response to receiving a command from a corresponding bit
//		 in the ENDPTPRIME register.  . Buffer ready is cleared by
//		 USB reset, by the USB DMA system, or through the ENDPTFLUSH
//		 register.  (so 0=buffer ready, 1=buffer primed for transmit)

//  USBCMD.ATDTW - This bit is used as a semaphore to ensure proper addition
//		   of a new dTD to an active (primed) endpoint's linked list.
//		   This bit is set and cleared by software.
//		   This bit would also be cleared by hardware when state machine
//		   is hazard region for which adding a dTD to a primed endpoint
//		    may go unrecognized.

/*struct endpoint_struct {
	uint32_t config;
	uint32_t current;
	uint32_t next;
	uint32_t status;
	uint32_t pointer0;
	uint32_t pointer1;
	uint32_t pointer2;
	uint32_t pointer3;
	uint32_t pointer4;
	uint32_t reserved;
	uint32_t setup0;
	uint32_t setup1;
	transfer_t *first_transfer;
	transfer_t *last_transfer;
	void (*callback_function)(transfer_t *completed_transfer);
	uint32_t unused1;
};*/

static void usb2_run_callbacks(usb2_endpoint_t* ep) {
	//printf("run_callbacks\n");
	transfer_t* first = ep->first_transfer;
	if (first == NULL) return;

	// count how many transfers are completed, then remove them from the endpoint's list
	uint32_t count = 0;
	transfer_t* t = first;
	while (1) {
		if (t->status & (1 << 7)) {
			// found a still-active transfer, new list begins here
			//printf(" still active\n");
			ep->first_transfer = t;
			break;
		}
		count++;
		t = (transfer_t*)t->next;
		if ((uint32_t)t == 1) {
			// reached end of list, all need callbacks, new list is empty
			//printf(" end of list\n");
			ep->first_transfer = NULL;
			ep->last_transfer = NULL;
			break;
		}
	}
	// do all the callbacks
	while (count) {
		transfer_t* next = (transfer_t*)first->next;
		ep->callback_function(first);
		first = next;
		count--;
	}
}

void usb2_transmit(int endpoint_number, transfer_t* transfer) {
	if (endpoint_number < 2 || endpoint_number > CURRENT_ENDPOINT_COUNT) return;
	usb2_endpoint_t* endpoint = usb2_endpoint_queue_head + endpoint_number * 2 + 1;
	uint32_t mask = 1 << (endpoint_number + 16);
	usb2_schedule_transfer(endpoint, mask, transfer);
}

void usb2_receive(int endpoint_number, transfer_t* transfer) {
	if (endpoint_number < 2 || endpoint_number > CURRENT_ENDPOINT_COUNT) return;
	usb2_endpoint_t* endpoint = usb2_endpoint_queue_head + endpoint_number * 2;
	uint32_t mask = 1 << endpoint_number;
	usb2_schedule_transfer(endpoint, mask, transfer);
}

uint32_t usb2_transfer_status(const transfer_t* transfer) {
	if (USB2Mode == Debug) {
		uint32_t status, cmd;
		//int count=0;
		cmd = USB2_USBCMD;
		while (1) {
			__disable_irq();
			USB2_USBCMD = cmd | USB_USBCMD_ATDTW;
			status = transfer->status;
			cmd = USB2_USBCMD;
			__enable_irq();
			if (cmd & USB_USBCMD_ATDTW) return status;
			//if (!(cmd & USB_USBCMD_ATDTW)) continue;
			//if (status & 0x80) break; // for still active, only 1 reading needed
			//if (++count > 1) break; // for completed, check 10 times
		}
	} else
		return transfer->status;
}
