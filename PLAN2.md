# ScopeLogic — Polling vs. Interrupt vs. DMA ADC Benchmark (bare metal)

The smallest set of steps that produces every number in the resume bullet. The firmware is register-level C on an STM32F429ZI: no HAL, no CubeMX, no IDE, no CMake. Only `arm-none-eabi-gcc`, a plain `Makefile`, OpenOCD and Python.

---

## Resume bullet (the goal)

> Benchmarked polling, interrupt, and DMA-based ADC acquisition in bare-metal C on an STM32F429 (register-level, no HAL); using logic-analyzer GPIO instrumentation and oscilloscope timing measurements, showed DMA cut CPU load from **[A]%** to **[B]%**, reduced sampling jitter from **[C] µs** to **<[D] ns**, and raised max sustained sample rate **[E]×** to **[X] MSPS**.

| Gap | Where the number comes from | Step |
|---|---|---|
| A, B | CPU load: polling (A) vs. DMA (B), at the same sample rate | 6, 8 |
| C, D | Sampling jitter: polling or interrupt (C) vs. DMA (D) | 6–8, 10 |
| E, X | Max sustained rate: DMA (X) ÷ interrupt mode | 9 |

Only use numbers you measured. If a result differs from what you expected, write the bullet around what you actually found.

---

## Definitions (decide these before measuring)

- **The three modes.** The timer fires at fs in every mode. What differs is who starts each conversion and who collects the result:
  - **POLL:** the CPU busy-waits on TIM1's CC1 flag, starts the conversion in software, then busy-waits for end of conversion (EOC) and reads the result.
  - **IRQ:** the TIM1 compare (CC) interrupt starts the conversion in software; the ADC's EOC interrupt reads the result.
  - **DMA:** the TIM1 CC1 event starts each conversion in hardware, and DMA2 moves every result into a circular buffer. The CPU only runs on half-complete and full-complete interrupts.
- **Background load (all modes).** A SysTick interrupt fires at 1 kHz, has the highest priority and busy-waits for 2 µs. It stands in for "the rest of the application." Without it, interrupt latency on a Cortex-M4 is nearly constant and every mode would show almost no jitter. With it, software-started sampling gets delayed sometimes, which is the realistic case.
- **Sample instant.** For POLL and IRQ, this is the moment software starts the conversion; the PF14 marker toggles immediately before that. For DMA it's the TIM1 CC1 edge on PE9.
- **Jitter.** The spread (maximum − minimum, plus the standard deviation) of the delay from the intended sample time (PE9 rising edge) to the actual sample instant.
- **CPU load.** The share of CPU time spent on acquisition, measured two ways:
  1. **Idle counter (primary):** `load = 1 − idle_cycles / elapsed_cycles`, compared against a baseline taken with acquisition off but the background load still running. This counts interrupt entry and exit overhead, which GPIO markers miss.
  2. **PF15 duty cycle on the LA (cross-check):** PF15 is high while acquisition code runs.
- **Max sustained rate.** The highest fs at which N = 4096 samples are captured in N/fs time (±1%), with no ADC overrun (OVR) and no missed timer events.

---

## Equipment and wiring

| Tool | Role |
|---|---|
| NUCLEO-F429ZI | Device under test |
| Hantek DSO2D15 | Signal generator (10 kHz sine), clock check, DMA jitter (1 ns resolution) |
| Innomaker LA1010 | GPIO markers, CPU-load duty cycle, POLL/IRQ jitter (10 ns at 100 MSa/s), UART decoding |

Verify every pin against the board's user manual (UM1974).

| Signal | Pin | Nucleo header | Connects to |
|---|---|---|---|
| ADC1 channel 3 input | PA3 | A0 | Generator → **1 kΩ** resistor → PA3, with **1 nF** from PA3 to GND; scope CH1 |
| TIM1_CH1 PWM (sample clock, alternate function 1) | PE9 | D6 | Scope CH2, LA CH2 |
| CPU-busy marker | PF15 | D2 | LA CH0 |
| Sample marker | PF14 | D4 | LA CH1 |
| USART3 TX (to the ST-LINK's USB serial port, AF7) | PD8 | — | LA CH3 (optional) |
| MCO1 clock output (only for the clock check, AF0) | PA8 | — | Scope CH2 |
| GND | GND | any | Generator, scope probes and LA ground |

The 1 nF capacitor feeds the ADC's sampling capacitor during the short 3-cycle sampling time, so the 1 kΩ resistor doesn't cause errors. Together they make a low-pass filter at about 160 kHz, which doesn't affect a 10 kHz test signal.

**Generator safety:** set a sine at 2 Vpp with +1.65 V offset, and check it on CH1 *before* connecting PA3. It should swing about 0.65–2.65 V. If it reads about 1.3–4.0 V, the generator assumes a 50 Ω load: halve both the amplitude and the offset. Clip every ground to the Nucleo's GND.

---

## Repo layout (final)

```
firmware/
  Makefile
  stm32f429zi.ld        linker script
  startup.c             vector table, Reset_Handler, memcpy/memset
  main.c                clock, gpio, uart, systick load, mode select, frame TX
  acq_poll.c  acq_irq.c  acq_dma.c
  vendor/               CMSIS headers only (no HAL)
analysis/
  capture.py            read one frame → .npy + metrics
  la_jitter.py          LA CSV export → jitter / duty-cycle stats
  sweep.sh              build + flash + capture across modes and fs
docs/img/               scope / LA screenshots, plots
results.csv
README.md
```

---

## Steps

Each step ends with a **Check**. Commit after each one passes.

### Step 1 — Toolchain, startup code, blinky

1. Install the tools (Arch):
   `sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-gdb openocd python-pyserial python-numpy python-scipy python-matplotlib`
   Also install KingstVIS from Kingst's website.
2. Copy headers into `firmware/vendor/`. These are register definitions only, no HAL code:
   - From `github.com/STMicroelectronics/cmsis_device_f4`, `Include/`: `stm32f4xx.h`, `stm32f429xx.h`, `system_stm32f4xx.h`.
   - From `github.com/ARM-software/CMSIS_5`, `CMSIS/Core/Include/`: `core_cm4.h`, `cmsis_compiler.h`, `cmsis_gcc.h`, `cmsis_version.h`, `mpu_armv7.h`.
3. `stm32f429zi.ld`: FLASH at `0x08000000`, 2048K; RAM at `0x20000000`, 192K (SRAM1+2+3 as one block); `_estack = 0x20030000`. Sections: `.isr_vector` (first in FLASH, `KEEP`), `.text`, `.rodata`, `.data` (stored in FLASH, copied to RAM; export `_sidata`, `_sdata`, `_edata`), `.bss` (`_sbss`, `_ebss`).
   - **Leave out the 64K CCM RAM at `0x10000000`. DMA can't reach it,** so keeping it out of the linker script means buffers can't end up there.
4. `startup.c`:
   - A vector table of `16 + 91` entries in section `.isr_vector`: `[0]` = `&_estack`, `[1]` = `Reset_Handler`, every other entry = `Default_Handler` (an endless loop) unless named below. Fill in by index: `[15]` SysTick, `[16+18]` ADC, `[16+27]` TIM1_CC, `[16+56]` DMA2_Stream0.
   - `Reset_Handler`: copy `.data` from FLASH to RAM, zero `.bss`, call `main()`.
   - Provide `memcpy` and `memset`, because GCC can emit calls to them even with `-nostdlib`.
5. `Makefile`:
   ```make
   CC      = arm-none-eabi-gcc
   MODE   ?= POLL            # POLL | IRQ | DMA
   FS     ?= 100000          # sample rate in Hz
   CFLAGS  = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -O2 -g3 -Wall -Wextra \
             -ffunction-sections -fdata-sections -ffreestanding \
             -DSTM32F429xx -DMODE_$(MODE) -DFS=$(FS) -Ivendor
   LDFLAGS = -T stm32f429zi.ld -nostdlib -Wl,--gc-sections -Wl,-Map=build/fw.map -lgcc
   # targets: all (build/fw.elf + size), flash, clean
   flash: build/fw.elf
   	openocd -f board/st_nucleo_f4.cfg -c "program $< verify reset exit"
   ```
   The firmware does no floating-point math, so `-mfloat-abi=soft` avoids having to enable the FPU at startup.
6. `main.c`: enable the GPIOF clock (`RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN`), set PF14 and PF15 as outputs at very high speed, and toggle PF15 in a loop using `GPIOF->BSRR`.
   - Define marker macros now and use them everywhere later. Writing to BSRR sets or clears a pin in a single store:
     `#define BUSY_HI() (GPIOF->BSRR = 1u<<15)`, `BUSY_LO() (GPIOF->BSRR = 1u<<(15+16))`, `SMP_TGL()` (for PF14).

**Check:** `make flash` succeeds, and the LA shows a square wave on CH0.

### Step 2 — 144 MHz system clock and the cycle counter

144 MHz is chosen because it gives a 72 MHz APB2 bus, divided by 2 = a 36 MHz ADC clock. That's the ADC's maximum clock, which allows 2.4 MSPS. Running at 180 MHz would cap the ADC at 1.5 MSPS.

1. HSE in bypass mode: the Nucleo's ST-LINK supplies an 8 MHz clock by default. Set `RCC_CR_HSEBYP | RCC_CR_HSEON` and wait for `HSERDY`.
   - If you'd rather not rely on that, use the internal HSI (16 MHz) with PLLM = 8 instead.
2. Enable the PWR clock and set `PWR->CR` VOS to scale 1.
3. `FLASH->ACR = LATENCY_4WS | PRFTEN | ICEN | DCEN`. 4 wait states are correct for 120–150 MHz at 3.3 V.
4. PLL: source HSE, M = 4 (2 MHz into the PLL), N = 144, P = 2 (144 MHz), Q = 6. Turn it on and wait for `PLLRDY`.
5. Bus dividers: AHB ÷1, APB1 ÷4 (36 MHz), APB2 ÷2 (72 MHz). Switch the system clock to the PLL and wait until `SWS` reports PLL.
   - Because the APB2 divider isn't 1, **TIM1 runs at 2 × 72 = 144 MHz**.
6. Clock check: set MCO1 = PLL ÷4 (`RCC_CFGR` MCO1 = `11`, MCO1PRE = `110`) and put PA8 in alternate function 0.
7. Enable the cycle counter: `CoreDebug->DEMCR |= TRCENA; DWT->CYCCNT = 0; DWT->CTRL |= CYCCNTENA`.

**Check:** the scope measures **36.00 MHz** on PA8. Then turn MCO1 off again.

### Step 3 — UART output and frame format

1. USART3 on PD8 (TX) and PD9 (RX), alternate function 7. Its bus clock is 36 MHz, so for 921600 baud set `BRR = 36000000 / 921600 ≈ 39` (0.16% error). Enable `UE | TE`.
2. Write a blocking `uart_write(const void*, len)` that waits for `TXE` before each byte.
3. Frame format, little-endian:
   ```
   u32 magic 'SCOP' | u8 mode | u8[3] pad | u32 fs | u32 n
   u32 elapsed_cycles | u32 idle_iters | u32 baseline_cycles_per_iter_x1000
   u32 overruns | u32 missed | u16 samples[n] | u32 sum_of_sample_bytes
   ```
4. `analysis/capture.py`: open the serial port (`/dev/ttyACM0`), search for the magic word, unpack with `struct` (`'<4sB3xIIIIIII'`), check the checksum, and save the result to `.npy` along with its metadata as JSON.
5. Test: the firmware sends one frame of a counting ramp, 0…4095.

**Check:** `capture.py` receives the ramp with no errors, and the LA's UART decoder on CH3 shows the `SCOP` header bytes.

### Step 4 — ADC single conversion

1. Clocks: enable `GPIOAEN`, `ADC1EN`. Put PA3 in analog mode (`MODER` = `11`).
2. `ADC->CCR` ADCPRE = `00` (÷2 → 36 MHz). `ADC1->SMPR2` SMP3 = `000` (3 cycles, so each conversion takes 15 cycles = 417 ns). `ADC1->SQR1` L = 0, `ADC1->SQR3` SQ1 = 3. `ADC1->CR2 |= ADON`, then wait about 3 µs (measure with the cycle counter).
3. Software conversion: set `SWSTART`, wait for `SR.EOC`, read `DR`.
4. Set the generator to a **DC output** of 1.0 V, then 2.0 V, and send the reading each time.

**Check:** the readings are about 1241 and 2482 (raw value × 3.3 / 4095 V). Then switch the generator back to a 10 kHz sine, 0.65–2.65 V.

### Step 5 — Sample clock and background load

1. TIM1: enable `TIM1EN`. Put PE9 in alternate function 1 (enable `GPIOEEN`). Set `PSC = 0`, `ARR = 144000000/FS − 1`, `CCR1 = (ARR+1)/2`.
   - `CCMR1` OC1M = `111` (PWM mode 2: the pin goes **high at the CC1 match**), `OC1PE` on. `CCER` CC1E on. **Set `BDTR |= MOE`**: TIM1 is an "advanced" timer and its outputs stay off without it. Set `EGR = UG`, then `CR1 |= CEN`.
   - Every fs used in this plan divides 144 MHz exactly: 10k → 14400, 100k → 1440, 1M → 144, 2.4M → 60.
2. Background load: `SysTick_Config(144000)` for 1 kHz at priority 0. The SysTick handler busy-waits 288 cycles (2 µs) using the cycle counter.
3. Priorities: SysTick 0; TIM1_CC, ADC and DMA2_Stream0 all 1.

**Check:** the scope measures exactly fs on PE9 at 10 kHz, 100 kHz and 1 MHz, with a 50% duty cycle.

### Step 6 — POLL mode (the baseline)

`acq_poll.c`, all on the CPU:
```c
for (i = 0; i < N; i++) {
    while (!(TIM1->SR & TIM_SR_CC1IF)) ;          // wait for the intended sample time
    TIM1->SR = ~TIM_SR_CC1IF;
    BUSY_HI(); SMP_TGL();
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC1->SR & ADC_SR_EOC)) ;
    buf[i] = ADC1->DR;
    if (TIM1->SR & TIM_SR_CC1IF) missed++;         // the next event already came: we are too slow
    BUSY_LO();
}
```
1. Record the elapsed cycle count. The CPU load is 100% by definition, since the CPU never leaves the loop; PF15's duty cycle shows the share spent on actual ADC work.
2. Analysis in `capture.py`: an FFT peak within 0.1% of 10 kHz, no jumps between consecutive samples larger than the sine's steepest slope allows, and a plotted waveform. Optional: compute ENOB from the SINAD.
3. Jitter: capture LA **CH1 (PF14) and CH2 (PE9) only, at 100 MSa/s**. The LA can only reach its full rate with a few channels on, so capture CH0 for the CPU-load cross-check separately. Export the capture to CSV from KingstVIS. `la_jitter.py` pairs each PE9 rising edge with the next PF14 edge and reports the minimum, maximum, max − min and standard deviation of the delay. Adjust the CSV parsing to match KingstVIS's export columns.

**Check:** the captured sine is valid at fs = 100 kHz. The jitter results show a tight cluster of delays plus outliers of about 2 µs, which are the samples hit by the SysTick load. Save an LA screenshot of one of those outliers.

### Step 7 — IRQ mode

`acq_irq.c`:
1. `TIM1->DIER |= CC1IE`, enable `TIM1_CC_IRQn`. The handler: clear CC1IF; `BUSY_HI(); SMP_TGL(); SWSTART; BUSY_LO();`.
2. `ADC1->CR1 |= EOCIE`, `ADC1->CR2 |= EOCS` (enables overrun detection). Enable `ADC_IRQn`. The handler: `BUSY_HI(); buf[i++] = DR; if (SR & OVR) {overruns++; clear}; BUSY_LO();`, and once i == N, disable both interrupts.
3. While capturing, `main` runs the idle loop: `while (!done) idle_iters++;` (`volatile`). Measure the baseline cycles per idle iteration first, with acquisition off and SysTick on, over a fixed cycle count.
4. Compute `load = 1 − idle_iters × cycles_per_iter / elapsed_cycles`.

**Check:** the sine is valid at 100 kHz. CPU load is measured at 10k, 100k and 1M. Jitter is measured the same way as in step 6, and the LA screenshot shows the ISR pulses on PF15 lining up with the PE9 edges.

### Step 8 — DMA mode

`acq_dma.c`:
1. Enable `DMA2EN`. Configure DMA2 Stream0 while it's disabled (wait for `EN` to read 0):
   - `PAR = &ADC1->DR`, `M0AR = ring`, `NDTR = 2*H` (use `uint16_t ring[2*H]`, H = 256).
   - `CR`: CHSEL = 0, PL = high, MSIZE = PSIZE = `01` (16-bit), `MINC`, `CIRC`, DIR = peripheral→memory, `HTIE | TCIE`. Enable `DMA2_Stream0_IRQn`, then set `EN`.
2. ADC: `CR2` EXTSEL = `0000` (TIM1_CC1), EXTEN = `01` (rising edge), `DMA | DDS`, `EOCS`.
3. DMA handler: check `DMA2->LISR` for HTIF0 or TCIF0, clear it through `LIFCR`, then `BUSY_HI();` copy the finished half of the ring buffer into `buf` until N samples are collected; `BUSY_LO();`. When done, stop the timer and the DMA. Check `ADC1->SR & OVR` and count overruns.
4. Run the same idle-loop CPU load measurement as in step 7.
5. Check the edge alignment once: temporarily enable EOCIE with a marker toggle. The EOC marker should come about 417 ns after the **PE9 rising edge**. If it lines up with the falling edge instead, set EXTEN to falling.
6. Jitter: here the sample instant is the PE9 edge, produced by hardware. On the scope, trigger on CH2 rising, set the horizontal delay to one period, zoom in to about 2 ns/div and turn on **infinite persistence**, with the SysTick load running. Read the width of the band of edges and save a screenshot. Expect it to be at or below the scope's resolution; if so, report it as "< X ns (limited by the measurement)".

**Check:** the sine is valid at 100 kHz and at 2.4 MSPS, with 0 overruns. CPU load is a few percent.

### Step 9 — Max sustained rate sweep

1. `analysis/sweep.sh`: for each mode in POLL, IRQ, DMA, and each fs in 100k, 200k, 300k, 500k, 750k, 1M, 1.5M, 2M, 2.4M (all of which divide 144 MHz exactly), run `make flash MODE=$m FS=$f`, then `capture.py`, and append a row to `results.csv`.
2. A run passes when: `elapsed_cycles` is within 1% of `N × 144e6 / fs`, `missed == 0`, `overruns == 0`, and the sine is valid.
3. Record each mode's highest passing fs. Expect DMA to reach 2.4 MSPS (the ADC's own limit) and IRQ to top out far lower. Save an LA screenshot of IRQ mode at its first failing fs, showing the ISRs falling behind.

**Check:** `results.csv` covers every mode × fs combination. E = (DMA max) ÷ (IRQ max), and X = DMA max.

### Step 10 — Final measurements at one comparison rate

1. Choose one rate that all three modes pass (e.g., 100 kHz) and record each mode's CPU load and jitter in the Results table below.
2. Save these to `docs/img/`: the POLL jitter outlier (LA), the IRQ jitter (LA), the DMA persistence capture (scope), the IRQ failure at max rate (LA), and the captured-sine and FFT plots.

**Check:** the Results table is fully filled in. A, B, C, D, E and X are all known.

### Step 11 — Write-up

1. `README.md`: the goal, the wiring table, the mode definitions (copy from above), how to build and flash (`make flash MODE=DMA FS=2400000`), the results table, screenshots, and three findings:
   1. Why DMA's sample timing is set by hardware.
   2. Why the background load causes jitter only in the modes where software starts each conversion.
   3. The 144 MHz vs. 180 MHz trade-off.
2. Fill in the resume bullet with the measured values.

**Check:** someone else could clone the repo and reproduce the results with `make` and `sweep.sh`.

### Optional — Triple-interleaved ADC (about 7.2 MSPS)

ADC1, ADC2 and ADC3 take turns sampling PA3 in triple-interleaved mode (`ADC->CCR` MULTI = `10111`), with DMA mode 2 reading from `ADC->CDR`. If it works, add "and 7.2 MSPS with triple-interleaved ADCs" to the bullet. The FFT will show small spurs caused by the three ADCs not matching perfectly.

---

## Results

Comparison rate: fs = 100 kHz, N = 4096, SysTick load of 2 µs at 1 kHz.

| Mode | CPU load (idle counter) | CPU load (PF15 duty) | Jitter p-p | Jitter σ | Max sustained fs | Overruns / missed |
|---|---|---|---|---|---|---|
| POLL | 100% | | | | | |
| IRQ | | | | | | |
| DMA | | | | | | |
