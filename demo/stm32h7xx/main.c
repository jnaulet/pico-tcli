#include "picoRTOS.h"
#include "stm32h7xx.h"

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
    tcli_out_cb(arg, "^C\r\n");
}

static int cmd_help(void *arg)
{
    tcli_out_cb(arg, "\r\n");
    tcli_out_cb(arg, "Supported list of commands:\r\n");
    tcli_out_cb(arg, "\r\n");
    tcli_out_cb(arg, " help   : displays this message\r\n");
    tcli_out_cb(arg, " load   : loads the configuration from EEPROM\r\n");
    tcli_out_cb(arg, " display: displays the current configuration\r\n");
    tcli_out_cb(arg, " set    : modifies a parameter. Available parameters are:\r\n");
    tcli_out_cb(arg, "          board_id [0-7]  : the current board identifier\r\n");
    tcli_out_cb(arg, "          pwr_sel_in [0-1]: the default power-in selection\r\n");
    tcli_out_cb(arg, " save   : saves the configuration to EEPROM\r\n");
    tcli_out_cb(arg, "\r\n");

    return 0;
}

static int cmd_load(void *arg)
{
  tcli_out_cb(arg, "Configuration loaded to RAM\r\n");
  return 0;
}

static int cmd_display(void *arg)
{
  tcli_out_cb(arg, "Empty\r\n");
  return 0;
}

static int cmd_set(void *arg, int argc, const char **argv)
{
    if (argc != 3) {
        tcli_out_cb(arg, "wrong number of parameters\r\n");
        return -1;
    }

    tcli_out_cb(arg, "Parameter '");
    tcli_out_cb(arg, argv[1]);
    tcli_out_cb(arg, "' set to '");
    tcli_out_cb(arg, argv[2]);
    tcli_out_cb(arg, "'\r\n");

    return 0;
}

static int cmd_save(void *arg)
{
  tcli_out_cb(arg, "Configuration saved to EEPROM\r\n");
  return 0;
}

int tcli_exec_cb(void *arg, int argc, const char **argv)
{
#define STRNCMP_LITERAL(a, b) strncmp(a, b, sizeof(a))
    if (STRNCMP_LITERAL("help", argv[0]) == 0) return cmd_help(arg);
    if (STRNCMP_LITERAL("load", argv[0]) == 0) return cmd_load(arg);
    if (STRNCMP_LITERAL("display", argv[0]) == 0) return cmd_display(arg);
    if (STRNCMP_LITERAL("set", argv[0]) == 0) return cmd_set(arg, argc, argv);
    if (STRNCMP_LITERAL("save", argv[0]) == 0) return cmd_save(arg);

    /* not found */
    tcli_out_cb(arg, "Command '");
    tcli_out_cb(arg, argv[0]);
    tcli_out_cb(arg, "' not found\r\n");

    return -1;
}

void tcli_out_cb(void *arg, const char *str)
{
    while (*str != '\0') {
        if (uart_write((struct uart*)arg, str, sizeof(char)) == -EAGAIN) {
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
        "CPU:   armv7e-m\r\n"                                                  \
        "Model: STM32H743VI\r\n"                                   \
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
    static struct stm32h7xx stm32;

    picoRTOS_init();
    (void)stm32h7xx_init(&stm32);

    struct picoRTOS_task task;
    static picoRTOS_stack_t stack0[CONFIG_DEFAULT_STACK_COUNT];
    static picoRTOS_stack_t stack1[CONFIG_DEFAULT_STACK_COUNT * 4];

    /* blink */
    picoRTOS_task_init(&task, led_main, &stm32.LED, stack0, (size_t)CONFIG_DEFAULT_STACK_COUNT);
    picoRTOS_add_task(&task, picoRTOS_get_next_available_priority());
    /* serial console */
    picoRTOS_task_init(&task, console_main, &stm32.UART, stack1, (size_t)CONFIG_DEFAULT_STACK_COUNT);
    picoRTOS_add_task(&task, picoRTOS_get_next_available_priority());

    /* Start the scheduler. */
    picoRTOS_start();

    /* we're not supposed to end here */
    picoRTOS_assert_void(false);
    return -1;
}
