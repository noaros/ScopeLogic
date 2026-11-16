#include <stdint.h>

#define RCC_CR      (*(volatile uint32_t *)0x40023800)
#define RCC_PLLCFGR (*(volatile uint32_t *)0x40023804)
#define RCC_CFGR    (*(volatile uint32_t *)0x40023808)
#define RCC_AHB1ENR (*(volatile uint32_t *)0x40023830)
#define RCC_APB1ENR (*(volatile uint32_t *)0x40023840)
#define RCC_APB2ENR (*(volatile uint32_t *)0x40023844)
#define FLASH_ACR   (*(volatile uint32_t *)0x40023C00)
#define GPIOA_MODER (*(volatile uint32_t *)0x40020000)
#define GPIOB_MODER (*(volatile uint32_t *)0x40020400)
#define GPIOB_BSRR  (*(volatile uint32_t *)0x40020418)
#define GPIOC_MODER   (*(volatile uint32_t *)0x40020800)
#define GPIOC_OSPEEDR (*(volatile uint32_t *)0x40020808)
#define GPIOC_AFRH    (*(volatile uint32_t *)0x40020824)
#define GPIOD_MODER (*(volatile uint32_t *)0x40020C00)
#define GPIOD_AFRH  (*(volatile uint32_t *)0x40020C24)
#define GPIOF_MODER (*(volatile uint32_t *)0x40021400)
#define GPIOF_ODR   (*(volatile uint32_t *)0x40021414)
#define GPIOF_BSRR  (*(volatile uint32_t *)0x40021418)
#define DEMCR       (*(volatile uint32_t *)0xE000EDFC)
#define DWT_CTRL    (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t *)0xE0001004)
#define USART3_SR   (*(volatile uint32_t *)0x40004800)
#define USART3_DR   (*(volatile uint32_t *)0x40004804)
#define USART3_BRR  (*(volatile uint32_t *)0x40004808)
#define USART3_CR1  (*(volatile uint32_t *)0x4000480C)
#define ADC1_SR     (*(volatile uint32_t *)0x40012000)
#define ADC1_CR2    (*(volatile uint32_t *)0x40012008)
#define ADC1_SMPR1  (*(volatile uint32_t *)0x4001200C)
#define ADC1_SMPR2  (*(volatile uint32_t *)0x40012010)
#define ADC1_SQR3   (*(volatile uint32_t *)0x40012034)
#define ADC1_DR     (*(volatile uint32_t *)0x4001204C)
#define ADC_CCR     (*(volatile uint32_t *)0x40012304)
#define VREFINT_CAL (*(volatile uint16_t *)0x1FFF7A2A)  /* factory reading of VREFINT at 3.3 V, 30 C */
#define GPIOE_MODER   (*(volatile uint32_t *)0x40021000)
#define GPIOE_OSPEEDR (*(volatile uint32_t *)0x40021008)
#define GPIOE_AFRH    (*(volatile uint32_t *)0x40021024)
#define TIM1_CR1    (*(volatile uint32_t *)0x40010000)
#define TIM1_SR     (*(volatile uint32_t *)0x40010010)
#define TIM1_EGR    (*(volatile uint32_t *)0x40010014)
#define TIM1_CCMR1  (*(volatile uint32_t *)0x40010018)
#define TIM1_CCER   (*(volatile uint32_t *)0x40010020)
#define TIM1_PSC    (*(volatile uint32_t *)0x40010028)
#define TIM1_ARR    (*(volatile uint32_t *)0x4001002C)
#define TIM1_CCR1   (*(volatile uint32_t *)0x40010034)
#define TIM1_BDTR   (*(volatile uint32_t *)0x40010044)

#define FS 100000u                                      /* sample rate in Hz: must divide 144 MHz exactly */

/* Globals: the startup code sets these up before main() runs */
uint32_t period = 1440;                                 /* .data: 1440 cycles at 144 MHz = 10 us */
uint32_t toggles;                                       /* .bss: starts at 0 */

#define NSAMP 1000
uint16_t samples[NSAMP];                                /* .bss: 2,000 bytes for one burst */

/* Send one byte: wait until the transmitter can take it (TXE), then hand it over */
static void uart_putc(char c)
{
    while (!(USART3_SR & (1u << 7))) {}                 /* TXE: transmit data register empty */
    USART3_DR = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

/* Send a number in decimal (no printf without a C library) */
static void uart_putu(uint32_t v)
{
    char digits[10];
    int n = 0;
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) uart_putc(digits[--n]);                   /* most significant digit first */
}

static void uart_field(const char *name, uint32_t v)   /* sends e.g. " a0=1234" */
{
    uart_puts(name);
    uart_putu(v);
}

/* One conversion, started by software, waited for by polling */
static uint32_t adc_read(uint32_t channel)
{
    ADC1_SQR3 = channel;                                /* the one channel to convert */
    ADC1_CR2 |= 1u << 30;                               /* SWSTART: start a conversion */
    while (!(ADC1_SR & (1u << 1))) {}                   /* wait for EOC: end of conversion */
    return ADC1_DR;                                     /* 12-bit result, 0..4095; reading it clears EOC */
}

int main(void)
{
    RCC_AHB1ENR |= (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5);  /* GPIOA-F clocks */
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

    /* USART3 on PD8 (TX), wired to the ST-LINK's virtual serial port: 115200 baud, 8N1 */
    RCC_APB1ENR |= 1u << 18;                            /* USART3 clock */
    (void)RCC_APB1ENR;
    GPIOD_AFRH  = (GPIOD_AFRH & ~0xFu) | 7u;            /* PD8 alternate function 7 = USART3_TX */
    GPIOD_MODER = (GPIOD_MODER & ~(3u << 16)) | (2u << 16);  /* PD8 = alternate function */
    USART3_BRR  = 313;                                  /* 36 MHz / 115200 = 312.5 -> 313 */
    USART3_CR1  = (1u << 13) | (1u << 3);               /* UE: USART on, TE: transmitter on */

    /* Cycle counter: counts every CPU clock (144 MHz) */
    DEMCR     |= 1u << 24;                              /* TRCENA: turn on the debug/trace block */
    DWT_CYCCNT = 0;
    DWT_CTRL  |= 1u << 0;                               /* CYCCNTENA: start counting */

    /* ADC1: PA3 (A0, channel 3) and the internal reference VREFINT (channel 17) */
    RCC_APB2ENR |= 1u << 8;                             /* ADC1 clock */
    (void)RCC_APB2ENR;
    GPIOA_MODER |= 3u << 6;                             /* PA3 = analog */
    ADC_CCR = (ADC_CCR & ~(3u << 16)) | (1u << 23);     /* ADCPRE = /2: 72 MHz APB2 -> 36 MHz ADC clock; TSVREFE: VREFINT on */
    ADC1_SMPR2 = (ADC1_SMPR2 & ~(7u << 9)) | (3u << 9);  /* channel 3: sample for 56 ADC cycles */
    ADC1_SMPR1 |= 7u << 21;                             /* channel 17: 480 cycles (VREFINT needs at least 10 us) */
    ADC1_CR2   |= 1u << 0;                              /* ADON: power up the ADC */
    uint32_t t0 = DWT_CYCCNT;
    while (DWT_CYCCNT - t0 < 20u * 144u) {}             /* wait 20 us for the ADC and VREFINT to settle */

    /* TIM1: the sample clock. Counts 144 MHz ticks from 0 to ARR, over and over */
    RCC_APB2ENR |= 1u << 0;                             /* TIM1 clock: APB2 is 72 MHz, so timers get 2 x 72 = 144 MHz */
    (void)RCC_APB2ENR;
    GPIOE_AFRH    = (GPIOE_AFRH & ~(0xFu << 4)) | (1u << 4);       /* PE9 alternate function 1 = TIM1_CH1 */
    GPIOE_OSPEEDR |= 3u << 18;                          /* PE9 very high speed: sharp edges for timing */
    GPIOE_MODER   = (GPIOE_MODER & ~(3u << 18)) | (2u << 18);      /* PE9 = alternate function */
    TIM1_PSC   = 0;                                     /* count every 144 MHz tick */
    TIM1_ARR   = 144000000u / FS - 1;                   /* 1440 ticks per period at 100 kHz: count 0..1439 */
    TIM1_CCR1  = (144000000u / FS) / 2;                 /* compare at the middle: 50% duty */
    TIM1_CCMR1 = (7u << 4) | (1u << 3);                 /* OC1M = PWM mode 2: PE9 goes high when CNT reaches CCR1; OC1PE */
    TIM1_CCER  = 1u << 0;                               /* CC1E: drive the pin */
    TIM1_BDTR  = 1u << 15;                              /* MOE: TIM1 is an "advanced" timer, its outputs stay off without this */
    TIM1_EGR   = 1u << 0;                               /* UG: load PSC, ARR and CCR1 now */
    TIM1_CR1   = 1u << 0;                               /* CEN: start counting */

    uint32_t next = DWT_CYCCNT;
    for (;;) {
        next += period;                                 /* next deadline, from the last one, not from now */
        while ((int32_t)(DWT_CYCCNT - next) < 0) {}     /* wait for it (works across wraparound) */
        GPIOF_BSRR = (GPIOF_ODR & (1u << 15)) ? 1u << (15 + 16) : 1u << 15;  /* flip PF15 */
        toggles++;

        if (toggles % 100000u == 0) {                   /* once a second */
            uint32_t missed = 0, first = 0, last = 0;
            TIM1_SR = ~(1u << 1);                       /* clear CC1IF: ignore ticks from before the burst */
            for (uint32_t i = 0; i < NSAMP; i++) {
                while (!(TIM1_SR & (1u << 1))) {}       /* wait for CC1IF: the next sample time */
                TIM1_SR = ~(1u << 1);                   /* clear it (writing 0 clears a flag, writing 1 leaves it alone) */
                last = DWT_CYCCNT;
                if (i == 0) first = last;
                samples[i] = (uint16_t)adc_read(3);
                if (TIM1_SR & (1u << 1)) missed++;      /* the next tick came before this sample finished */
            }
            uint32_t span = last - first;               /* cycles from the first sample's start to the last's */
            uint32_t vref = adc_read(17);
            uint32_t vdda_mV = vref ? 3300u * VREFINT_CAL / vref : 0;

            uint32_t lo = 4095, hi = 0;
            for (uint32_t i = 0; i < NSAMP; i++) {
                if (samples[i] < lo) lo = samples[i];
                if (samples[i] > hi) hi = samples[i];
            }

            uart_field("t=",        toggles / 100000u);
            uart_field(" vdda_mV=", vdda_mV);
            uart_field(" fs=",      FS);
            uart_field(" span=",    span);
            uart_field(" missed=",  missed);
            uart_field(" min=",     lo);
            uart_field(" max=",     hi);
            uart_puts("\r\n");

            uart_puts("s=");                            /* all 1,000 samples on one line */
            for (uint32_t i = 0; i < NSAMP; i++) {
                if (i) uart_putc(',');
                uart_putu(samples[i]);
            }
            uart_puts("\r\n");
        }
    }
}
