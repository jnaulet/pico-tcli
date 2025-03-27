#include "stm32h7xx.h"
#include "picoRTOS.h"
#include "picoRTOS_device.h"

static void clock_init(void)
{
    struct clock_settings RCC_settings = {
        CLOCK_STM32H7XX_HSI_16MHZ,
        25000000ul,                     /* hse_hz */
        CLOCK_STM32H7XX_CSI_OFF,        /* csi off */
        CLOCK_STM32H7XX_HSI48_OFF,      /* hsi48 off */
        CLOCK_STM32H7XX_PLLSRC_HSE_CK,
        {
            {
                400000000ul,    /* pll1 vco */
                2ul,            /* pll1_p */
                2ul,            /* pll1_q */
                0               /* pll1_r */
            },
            {
                0,      /* pll2 vco */
                0,      /* pll2_p */
                0,      /* pll2_q */
                0       /* pll2_r */
            },
            {
                0,      /* pll3 vco */
                0,      /* pll3_p */
                0,      /* pll3_q */
                0       /* pll3_r */
            }
        },
        CLOCK_STM32H7XX_SW_PLL1_P_CK,
    };

    /* main */
    (void)clock_stm32h7xx_init(&RCC_settings);
    /* per_ck is hse_ck */
    (void)clock_stm32h7xx_ker_sel(CLOCK_STM32H7XX_KER_CKPERSEL, 2u);

    /* gpio */
    (void)clock_stm32h7xx_enable(CLOCK_STM32H7XX_AHB4_GPIOA);

    /* uart */
    (void)clock_stm32h7xx_enable(CLOCK_STM32H7XX_APB2_USART1);
    (void)clock_stm32h7xx_ker_sel(CLOCK_STM32H7XX_KER_USART16SEL, 3u); /* hsi_ker_ck */
}

static void mux_init(void)
{
    static struct mux PORTA;

    (void)mux_stm32h7xx_init(&PORTA, ADDR_GPIOA);

    (void)mux_stm32h7xx_output(&PORTA, (size_t)1);          /* LED */
    (void)mux_stm32h7xx_alt(&PORTA, (size_t)10, (size_t)7); /* USART1_RX */
    (void)mux_stm32h7xx_alt(&PORTA, (size_t)9, (size_t)7);  /* USART1_TX */
}

static void gpio_init(/*@partial@*/ struct stm32h7xx *ctx)
{
    (void)gpio_stm32h7xx_init(&ctx->LED, ADDR_GPIOA, (size_t)1);
}

static void uart_init(/*@partial@*/ struct stm32h7xx *ctx)
{
    struct uart_settings UART_settings = {
        115200ul,
        (size_t)8,
        UART_PAR_NONE,
        UART_CSTOPB_1BIT,
    };

    (void)uart_stm32h7xx_init(&ctx->UART, ADDR_USART1, CLOCK_STM32H7XX_HSI_KER_CK);
    (void)uart_setup(&ctx->UART, &UART_settings);
}

int stm32h7xx_init(struct stm32h7xx *ctx)
{
    clock_init();
    mux_init();

    gpio_init(ctx);
    uart_init(ctx);

    return 0;
}
