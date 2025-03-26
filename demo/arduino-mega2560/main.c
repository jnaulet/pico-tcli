#include "picoRTOS.h"
#include "arduino-mega2560.h"

#include "uart.h"
#include "tcli.h"

/* libc is mandatory here unfortunately */
#include <string.h>

static void led_main(void *priv)
{
    picoRTOS_assert_fatal(priv != NULL, return );

    struct gpio *LED = (struct gpio*)priv;
    picoRTOS_tick_t ref = picoRTOS_get_tick();

    for (;;) {
        gpio_write(LED, false);
        picoRTOS_sleep(PICORTOS_DELAY_MSEC(60ul));
        gpio_write(LED, true);
        picoRTOS_sleep(PICORTOS_DELAY_MSEC(60ul));
        gpio_write(LED, false);
        picoRTOS_sleep(PICORTOS_DELAY_MSEC(120ul));
        gpio_write(LED, true);

        /* until next second */
        picoRTOS_sleep_until(&ref, PICORTOS_DELAY_SEC(1));
    }
}

void tcli_sigint_cb(void *arg)
{
    struct uart *UART = (struct uart*)arg;

    (void)uart_write(UART, "^", sizeof(char));
    picoRTOS_schedule();
    (void)uart_write(UART, "C", sizeof(char));
}

int tcli_exec_cb(void *arg, int argc, const char **argv)
{
    struct uart *UART = (struct uart*)arg;

    if (strncmp("set", argv[0], (size_t)4) == 0)
        (void)uart_write(UART, "!", sizeof(char));

    return 0;
}

void tcli_out_cb(void *arg, const char *str)
{
    char c;
    struct uart *UART = (struct uart*)arg;

    while ((c = *str) != '\0') {
        if (uart_write(UART, &c, sizeof(c)) == -EAGAIN) {
            picoRTOS_schedule();
            continue;
        }
        /* increment */
        str++;
    }
}

static void console_greet(struct uart *UART)
{
    static const char prompt[] = {
        "\r\n"                                                            \
        "tcli for picoRTOS v0.0.1a (" __DATE__ " - " __TIME__ ")\r\n"     \
        "\r\n"                                                            \
        "CPU:   avr\r\n"                                                  \
        "Model: Arduino ATMega2560\r\n"                                   \
        "READY.\r\n"                                                      \
        "\r\n"
    };

    int res;
    size_t n = 0;

    while (n < sizeof(prompt)) {
        if ((res = uart_write(UART, &prompt[n], sizeof(prompt) - n)) < 0) {
            picoRTOS_schedule();
            continue;
        }

        n += (size_t)res;
    }
}

static void console_main(void *priv)
{
    picoRTOS_assert_fatal(priv != NULL, return );

    static tcli_t tcli;
    struct uart *UART = (struct uart*)priv;

    console_greet(UART);
    tcli_init(&tcli, UART);

    for (;;) {

        char c = (char)0;

        /* just echo */
        if (uart_read(UART, &c, sizeof(c)) == -EAGAIN) {
            picoRTOS_schedule();
            continue;
        }

        tcli_input_char(&tcli, c);
    }
}

int main( void )
{
    static struct arduino_mega2560 mega;

    picoRTOS_init();
    (void)arduino_mega2560_init(&mega);

    struct picoRTOS_task task;
    static picoRTOS_stack_t stack0[CONFIG_DEFAULT_STACK_COUNT];
    static picoRTOS_stack_t stack1[CONFIG_DEFAULT_STACK_COUNT];

    /* blink */
    picoRTOS_task_init(&task, led_main, &mega.LED, stack0, (size_t)CONFIG_DEFAULT_STACK_COUNT);
    picoRTOS_add_task(&task, picoRTOS_get_next_available_priority());
    /* serial console */
    picoRTOS_task_init(&task, console_main, &mega.UART, stack1, (size_t)CONFIG_DEFAULT_STACK_COUNT);
    picoRTOS_add_task(&task, picoRTOS_get_next_available_priority());

    /* Start the scheduler. */
    picoRTOS_start();

    /* we're not supposed to end here */
    picoRTOS_assert_void(false);
    return -1;
}
