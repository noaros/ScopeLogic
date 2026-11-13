#include <stdint.h>

/* Defined by the linker script */
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;

int main(void);

/* Runs first after every reset: set up RAM the way C expects, then call main() */
void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;      /* .data: copy starting values from flash */

    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;  /* .bss: zero */

    main();
    for (;;) {}                                 /* main() should never return */
}

/* Any fault or interrupt without its own handler ends up here. In a debugger,
   a program stuck in Default_Handler means something went wrong. */
void Default_Handler(void) { for (;;) {} }

/* "weak": a later step can define e.g. SysTick_Handler() in another file and it replaces this */
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));

/* The 16 Cortex-M4 entries. Peripheral interrupts (timer, ADC, DMA) get added in the steps that use them. */
__attribute__((section(".isr_vector"), used))
void (*const vectors[16])(void) = {
    (void (*)(void))&_estack,   /*  0: initial stack pointer */
    Reset_Handler,              /*  1 */
    NMI_Handler,                /*  2 */
    HardFault_Handler,          /*  3 */
    MemManage_Handler,          /*  4 */
    BusFault_Handler,           /*  5 */
    UsageFault_Handler,         /*  6 */
    0, 0, 0, 0,                 /*  7-10: reserved */
    SVC_Handler,                /* 11 */
    DebugMon_Handler,           /* 12 */
    0,                          /* 13: reserved */
    PendSV_Handler,             /* 14 */
    SysTick_Handler,            /* 15 */
};
