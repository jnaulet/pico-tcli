#include "arduino-mega2560.h"

static void arduino_mega2560_init_mux(void)
{
    struct mux PORTB;
    struct mux PORTE;

    (void)mux_avr_init(&PORTB, ADDR_PORTB);
    (void)mux_avr_init(&PORTE, ADDR_PORTE);

    /* COMMUNICATION */
    (void)mux_avr_output(&PORTE, (size_t)1, true);  /* UART_TX */
    (void)mux_avr_input(&PORTE, (size_t)0);         /* UART_RX */

    /* LED */
    (void)mux_avr_output(&PORTB, (size_t)27, true);  /* L */
}

static void arduino_mega2560_init_communication(/*@partial@*/ struct arduino_mega2560 *ctx)
{
    /* SERIAL */
    struct uart_settings UART_settings = {
        38400ul,
        (size_t)8,
        UART_PAR_NONE,
        UART_CSTOPB_1BIT
    };

    (void)uart_avr_init(&ctx->UART, ADDR_USART0, CLOCK_ATMEGA2560_CLKIO);
    (void)uart_setup(&ctx->UART, &UART_settings);
}

static void arduino_mega2560_init_led(/*@partial@*/ struct arduino_mega2560 *ctx)
{
    (void)gpio_avr_init(&ctx->LED, ADDR_PORTB, (size_t)27);
}

int arduino_mega2560_init(struct arduino_mega2560 *ctx)
{
    arduino_mega2560_init_mux();
    arduino_mega2560_init_communication(ctx);
    arduino_mega2560_init_led(ctx);

    return 0;
}
