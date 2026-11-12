#include <stdint.h>

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830)
#define GPIOB_MODER (*(volatile uint32_t *)0x40020400)
#define GPIOB_BSRR  (*(volatile uint32_t *)0x40020418)
#define GPIOF_MODER (*(volatile uint32_t *)0x40021400)
#define GPIOF_BSRR  (*(volatile uint32_t *)0x40021418)

void Reset_Handler(void)
{
    RCC_AHB1ENR |= (1u << 1) | (1u << 5);         /* turn on GPIOB and GPIOF clocks */
    (void)RCC_AHB1ENR;                            /* wait for them to take effect */
    GPIOB_MODER = (GPIOB_MODER & ~3u) | 1u;       /* PB0 = output */
    GPIOB_BSRR  = 1u << 0;                        /* LD1 on: program is running */
    GPIOF_MODER = (GPIOF_MODER & ~(3u << 30)) | (1u << 30);  /* PF15 = output */

    for (;;) {
        GPIOF_BSRR = 1u << 15;                    /* PF15 high */
        GPIOF_BSRR = 1u << (15 + 16);             /* PF15 low */
    }
}

/* The two words the CPU reads at reset: initial stack pointer, reset address. */
__attribute__((section(".isr_vector"), used))
void *const vectors[2] = { (void *)0x20030000, Reset_Handler };
