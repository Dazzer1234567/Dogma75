// always have at least one serial port. If not why are we using the usb2 code
#include "src/usb2mtp/usb2_serial.h"
#include "src/usb2mtp/usb2_mtp.h"

  #include <SD.h>
  #include "src/usb2mtp/MTP.h"
// https://github.com/KurtE/MTP_Teensy/tree/main witch changes for 2nd port.
// call MTP.send_DeviceResetEvent(); each time a file is closed to force a refresh of the file indexs on the PC.
MTPStorage_SD storage;
MTPD    mtpd(&storage);

void setup(){

  SD.begin(BUILTIN_SDCARD);

  #if USE_EVENTS==1
    usb2_init_events();
  #endif
  usb_mtp_configure();
  storage.addFilesystem(SD, "SD Card");

  Serial.begin(115200);
  USB2Serial.begin(115200);
  USB2Serial1.begin(115200);
}

uint32_t lastStatus =0;

void loop() {

  mtpd.loop();  //This is mandatory to be placed in the loop code.

  if ((millis() - lastStatus) > 10000) {
    lastStatus = millis();
//    Serial.printf("USB2 tracking flags = %08lx\r\n",trackingFlags);
//    Serial.printf("USB2 mode = %s\r\n",(USB2Mode==Normal)?"Normal":"Debug");
  }

  while (Serial.available()) {
    auto in = Serial.read();
    if (in == 'N') {
      Serial.println("Switching to Normal mode. Reconnect port 2.");
      switchUSB2Mode(Normal);
    }
    if (in == 'D') {
      Serial.println("Switching to Debug mode. Reconnect port 2.");
      switchUSB2Mode(Debug);
    }
    if (USB2Serial.availableForWrite())
      USB2Serial.write(in);
    if (USB2Serial1.availableForWrite())
      USB2Serial1.write(in);
  }

  while (USB2Serial.available()) {
    auto in = USB2Serial.read();
    if (Serial.availableForWrite())
      Serial.write(in);
    if (USB2Serial1.availableForWrite())
      USB2Serial1.write(in);
  }

  while (USB2Serial1.available()) {
    auto in = USB2Serial1.read();
    if (Serial.availableForWrite())
      Serial.write(in);
    if (USB2Serial.availableForWrite())
      USB2Serial.write(in);
  }
}
