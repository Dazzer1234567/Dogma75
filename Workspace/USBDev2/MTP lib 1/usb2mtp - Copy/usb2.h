#pragma once

enum USB2Mode_e { Normal , Debug };
extern enum USB2Mode_e USB2Mode;
//extern int initDone;
//void switchUSB2Mode(enum USB2Mode_e newMode);

// could replace with a compiler #define on a different compiler.
#include "usb2_desc.h"
#include "usb2_dev.h"

