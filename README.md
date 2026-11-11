# ScopeLogic

> Benchmarked polling, interrupt, and DMA-based ADC acquisition in bare-metal C on an STM32F429 (register-level, no HAL); using logic-analyzer GPIO instrumentation and oscilloscope timing measurements, showed DMA cut CPU load from **[A]%** to **[B]%**, reduced sampling jitter from **[C] µs** to **<[D] ns**, and raised max sustained sample rate **[E]×** to **[X] MSPS**.

## Step 1 — Confirm the toolchain can talk to the board

**Goal:** before writing any code, confirm the compiler is installed and the PC can reach the STM32 through the Nucleo's ST-LINK.

1. Check the compiler: `arm-none-eabi-gcc --version`
2. Check the flasher: `STM32_Programmer_CLI --version`
3. Plug the Nucleo into USB through the ST-LINK connector (the end of the board with the ST-LINK chip on it).
4. Connect and read the chip: `STM32_Programmer_CLI -c port=SWD`

**Done when:** step 4 reports device ID `0x419` (the STM32F42x/F43x family) and a flash size of 2 MBytes.
