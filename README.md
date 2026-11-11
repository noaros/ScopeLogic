# ScopeLogic

> Benchmarked polling, interrupt, and DMA-based ADC acquisition in bare-metal C on an STM32F429 (register-level, no HAL); using logic-analyzer GPIO instrumentation and oscilloscope timing measurements, showed DMA cut CPU load from **[A]%** to **[B]%**, reduced sampling jitter from **[C] µs** to **<[D] ns**, and raised max sustained sample rate **[E]×** to **[X] MSPS**.

## Iteration 1 — Bare-metal blinky

**Goal:** build and flash register-level C from the command line, and see it on the logic analyzer.

1. Install the tools: `sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils openocd`
2. Copy the register-definition headers (no HAL) into `firmware/vendor/`:
   - From [cmsis_device_f4](https://github.com/STMicroelectronics/cmsis_device_f4) `Include/`: `stm32f4xx.h`, `stm32f429xx.h`, `system_stm32f4xx.h`
   - From [CMSIS_5](https://github.com/ARM-software/CMSIS_5) `CMSIS/Core/Include/`: `core_cm4.h`, `cmsis_compiler.h`, `cmsis_gcc.h`, `cmsis_version.h`, `mpu_armv7.h`
3. `firmware/stm32f429zi.ld`: FLASH `0x08000000` (2048K), RAM `0x20000000` (192K), `_estack = 0x20030000`. Sections: `.isr_vector` first in FLASH (`KEEP`), `.text`, `.rodata`, `.data` (stored in FLASH, copied to RAM), `.bss`.
4. `firmware/startup.c`: a vector table of 16 + 91 entries (`[0]` = `&_estack`, `[1]` = `Reset_Handler`, all others = a `Default_Handler` that loops forever). `Reset_Handler` copies `.data` from FLASH to RAM, zeroes `.bss` and calls `main()`.
5. `firmware/main.c`: enable the GPIOF clock (`RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN`), make PF15 an output, and toggle it in a loop via `GPIOF->BSRR`.
6. `firmware/Makefile`:
   ```make
   CC      = arm-none-eabi-gcc
   CFLAGS  = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -O2 -g3 -Wall -ffreestanding -DSTM32F429xx -Ivendor
   LDFLAGS = -T stm32f429zi.ld -nostdlib -Wl,--gc-sections -lgcc

   build/fw.elf: startup.c main.c
   	mkdir -p build
   	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
   	arm-none-eabi-size $@

   flash: build/fw.elf
   	openocd -f board/st_nucleo_f4.cfg -c "program $< verify reset exit"

   clean:
   	rm -rf build
   ```

**Done when:** `make flash` succeeds, and the LA1010 shows a square wave on CH0 connected to PF15 (Nucleo pin D2, with the LA ground on GND).
