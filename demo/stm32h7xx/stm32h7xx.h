#ifndef STM32H7XX_H
#define STM32H7XX_H

#include "picoRTOS_device.h"

#include "clock-stm32h7xx.h"
#include "gpio-stm32h7xx.h"
#include "mux-stm32h7xx.h"
#include "uart-stm32h7xx.h"

struct stm32h7xx {
    /* COMMUNICATION */
    struct uart UART;
    /* LED */
    struct gpio LED;
};

int stm32h7xx_init(/*@out@*/ struct stm32h7xx *ctx);

#endif
