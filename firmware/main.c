#include <stdint.h>

#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830)
#define GPIOB_MODER (*(volatile uint32_t *)0x40020400)
#define GPIOB_BSRR  (*(volatile uint32_t *)0x40020418)

void Reset_Handler(void)
{
    RCC_AHB1ENR |= 1u << 1;                  /* turn on GPIOB's clock */
    (void)RCC_AHB1ENR;                       /* wait for it to take effect */
    GPIOB_MODER = (GPIOB_MODER & ~3u) | 1u;  /* PB0 = output */
    GPIOB_BSRR  = 1u << 0;                   /* PB0 high: LD1 on */
    for (;;) {}
}

/* The two words the CPU reads at reset: initial stack pointer, reset address. */
__attribute__((section(".isr_vector"), used))
void *const vectors[2] = { (void *)0x20030000, Reset_Handler };
