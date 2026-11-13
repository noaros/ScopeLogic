#include <stdint.h>

#define RCC_CR      (*(volatile uint32_t *)0x40023800)
#define RCC_PLLCFGR (*(volatile uint32_t *)0x40023804)
#define RCC_CFGR    (*(volatile uint32_t *)0x40023808)
#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830)
#define FLASH_ACR   (*(volatile uint32_t *)0x40023C00)
#define GPIOB_MODER (*(volatile uint32_t *)0x40020400)
#define GPIOB_BSRR  (*(volatile uint32_t *)0x40020418)
#define GPIOC_MODER   (*(volatile uint32_t *)0x40020800)
#define GPIOC_OSPEEDR (*(volatile uint32_t *)0x40020808)
#define GPIOC_AFRH    (*(volatile uint32_t *)0x40020824)
#define GPIOF_MODER (*(volatile uint32_t *)0x40021400)
#define GPIOF_ODR   (*(volatile uint32_t *)0x40021414)
#define GPIOF_BSRR  (*(volatile uint32_t *)0x40021418)
#define DEMCR       (*(volatile uint32_t *)0xE000EDFC)
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)

/* Globals: the startup code sets these up before main() runs */
uint32_t period = 1440;                                 /* .data: 1440 cycles at 144 MHz = 10 us */
uint32_t toggles;                                       /* .bss: starts at 0 */

int main(void)
{
    RCC_AHB1ENR |= (1u << 1) | (1u << 2) | (1u << 5);  /* GPIOB, GPIOC, GPIOF clocks */
    (void)RCC_AHB1ENR;

    /* MCO2 = SYSCLK / 5 on PC9 (set the divider before the pin starts outputting) */
    RCC_CFGR = (RCC_CFGR & ~(0x1Fu << 27)) | (7u << 27);   /* MCO2 = SYSCLK (00), MCO2PRE = /5 (111) */
    GPIOC_AFRH    &= ~(0xFu << 4);                          /* PC9 alternate function 0 = MCO2 */
    GPIOC_OSPEEDR |= 3u << 18;                              /* PC9 very high speed */
    GPIOC_MODER    = (GPIOC_MODER & ~(3u << 18)) | (2u << 18);  /* PC9 = alternate function */

    /* 1. HSE: the 8 MHz clock from the ST-LINK, fed in directly (bypass) */
    RCC_CR |= 1u << 18;                                     /* HSEBYP */
    RCC_CR |= 1u << 16;                                     /* HSEON */
    while (!(RCC_CR & (1u << 17))) {}                       /* wait for HSERDY */

    /* 2. PLL: 8 MHz / M=4 = 2 MHz, x N=144 = 288 MHz, / P=2 = 144 MHz (Q=6 gives 48 MHz) */
    RCC_PLLCFGR = (RCC_PLLCFGR & ~0x0F43FFFFu)
                | (4u << 0) | (144u << 6) | (0u << 16) | (1u << 22) | (6u << 24);
    RCC_CR |= 1u << 24;                                     /* PLLON */
    while (!(RCC_CR & (1u << 25))) {}                       /* wait for PLLRDY */

    /* 3. Flash: 4 wait states for 144 MHz at 3.3 V, plus prefetch, instruction and data caches */
    FLASH_ACR = 4u | (1u << 8) | (1u << 9) | (1u << 10);
    while ((FLASH_ACR & 0xFu) != 4u) {}

    /* 4. Bus dividers: AHB /1 (144), APB1 /4 (36), APB2 /2 (72) */
    RCC_CFGR = (RCC_CFGR & ~0xFCF0u) | (5u << 10) | (4u << 13);

    /* 5. Switch the system clock to the PLL */
    RCC_CFGR = (RCC_CFGR & ~3u) | 2u;
    while (((RCC_CFGR >> 2) & 3u) != 2u) {}                 /* wait until SWS reports PLL */

    GPIOB_MODER = (GPIOB_MODER & ~3u) | 1u;
    GPIOB_BSRR  = 1u << 0;                                  /* LD1 on: clock switch finished */
    GPIOF_MODER = (GPIOF_MODER & ~(3u << 30)) | (1u << 30);

    /* Cycle counter: counts every CPU clock (144 MHz) */
    DEMCR     |= 1u << 24;                              /* TRCENA: turn on the debug/trace block */
    DWT_CYCCNT = 0;
    DWT_CTRL  |= 1u << 0;                               /* CYCCNTENA: start counting */

    uint32_t next = DWT_CYCCNT;
    for (;;) {
        next += period;                                 /* next deadline, from the last one, not from now */
        while ((int32_t)(DWT_CYCCNT - next) < 0) {}     /* wait for it (works across wraparound) */
        GPIOF_BSRR = (GPIOF_ODR & (1u << 15)) ? 1u << (15 + 16) : 1u << 15;  /* flip PF15 */
        toggles++;
    }
}
