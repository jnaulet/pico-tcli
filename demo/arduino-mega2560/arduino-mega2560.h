#ifndef ARDUINO_MEGA2560_H
#define ARDUINO_MEGA2560_H

#include "picoRTOS_device.h"

#include "clock-atmega2560.h"
#include "gpio-avr.h"
#include "mux-avr.h"
#include "uart-avr.h"

struct arduino_mega2560 {
    /* COMMUNICATION */
    struct uart UART;
    /* LED */
    struct gpio LED;
};

int arduino_mega2560_init(/*@out@*/ struct arduino_mega2560 *ctx);

#endif
