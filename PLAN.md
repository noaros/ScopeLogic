# ScopeLogic — Polling vs. Interrupt vs. DMA ADC Benchmark

Sample the same analog signal three ways on an STM32F429ZI (polling, interrupt per sample, DMA) and measure each method's **CPU load**, **sample timing jitter**, **maximum sustained sample rate** and **data quality (ENOB)**. Measurements use an oscilloscope, a logic analyzer and GPIO timing markers toggled by the firmware.

---

## Resume bullet

> Benchmarked polling, interrupt, and DMA-based ADC acquisition on an STM32F429 at up to **[X] MSPS**; using logic-analyzer GPIO instrumentation and oscilloscope timing measurements, showed DMA cut CPU load from **[A]%** to **[B]%**, reduced sampling jitter from **[C] µs** to **<[D] ns**, and raised max sustained sample rate **[E]×** (up to **7.2 MSPS** with triple-interleaved ADCs).

Replace every `[bracketed]` value with a measured result from the Results table below. Never use a number you didn't measure.

**Alternate wordings** (pick based on the job posting):
- *Firmware/performance roles:* "Optimized STM32 ADC acquisition pipeline using timer-triggered DMA with circular double buffering, reducing CPU utilization by [N]% and enabling [X] MSPS continuous capture; verified with a logic analyzer and oscilloscope."
- *Test/validation roles:* "Built a Python measurement harness (FFT, SNR, ENOB) to characterize STM32 ADC acquisition modes, correlating logic-analyzer timing captures with oscilloscope measurements to quantify CPU load and jitter."

---

## Equipment

| Tool | Model | Role |
|---|---|---|
| Oscilloscope | Hantek DSO2D15 (150 MHz, 1 GSa/s, built-in generator) | Signal source, input verification, fine jitter measurement (~1 ns) |
| Logic analyzer | Innomaker LA1010 (Kingst; up to 100 MSa/s) | GPIO markers, CPU-load duty cycle, UART decoding (10 ns resolution) |
| MCU board | NUCLEO-F429ZI | Device under test |
| Software | STM32CubeMX, arm-none-eabi-gcc + CMake, OpenOCD, KingstVIS / PulseView, Python (numpy, scipy, matplotlib, pyserial) | |

## Pin map

Verify every pin against the board's user manual (UM1974) before wiring.

| Function | MCU pin | Nucleo header | Connects to |
|---|---|---|---|
| ADC input (ADC1/2/3 channel 3) | PA3 | A0 | Generator output through a 1 kΩ series resistor; scope CH1 |
| Sample trigger (TIM1_CH1 PWM, also triggers the ADC) | PE9 | D6 | Scope CH2; LA CH2 |
| "CPU busy" marker | PF15 | D2 | LA CH0 |
| "Sample taken" marker | PF14 | D4 | LA CH1 |
| USART3 TX (to the ST-LINK's USB serial port) | PD8 | see UM1974 | LA CH3 |
| System clock output (MCO1), used once to check clocks | PA8 | — | Scope CH2 |
| Ground | GND | any | Generator ground, all probe grounds, LA ground |

Keep these pins free: Ethernet (PA1, PA2, PA7, PC1, PC4, PC5, PG11, PG13, PB13) and LEDs (PB0, PB7, PB14).

## Safety checklist (check before connecting the generator)

- [ ] Generator set to sine, 2 Vpp, +1.65 V offset.
- [ ] Output checked on scope CH1: it swings about **0.65–2.65 V**. If it reads about 1.3–4.0 V, the generator assumes a 50 Ω load: halve both the amplitude and the offset.
- [ ] Scope CH1 probe setting (1X or 10X) matches the probe actually in use.
- [ ] 1 kΩ series resistor in place between the generator and PA3.
- [ ] All grounds (generator, both scope probes, LA) clipped to the Nucleo's GND.

---

## Iterations

Each iteration is small, ends in something you can test, and has a clear **Done when** check. Commit after each one.

### Phase 0 — Foundation

**It 0 — Toolchain and blinky**
- Create a CubeMX project for the NUCLEO-F429ZI and generate a CMake project into `firmware/`.
- Build with arm-none-eabi-gcc and flash with OpenOCD over the onboard ST-LINK.
- Toggle PF15 in a loop.
- **Done when:** the LA shows a square wave on CH0. This confirms the build, flash and LA setup all work.

**It 1 — UART output over the ST-LINK**
- Set up USART3 (PD8/PD9) and add a `printf` replacement or a minimal write function.
- **Done when:** text shows up in a serial terminal on the PC, *and* the LA's UART decoder on CH3 decodes the same text.

**It 2 — Check the clock speed**
- Add a build option for a 180 MHz or 144 MHz system clock.
- Route the system clock divided down to the MCO1 pin (PA8).
- **Done when:** the scope measures the expected frequency on PA8 in both configurations. Write down which configuration allows 2.4 MSPS (expected: 144 MHz, because ADCCLK = 72 MHz / 2 = 36 MHz).

**It 3 — Signal source**
- Configure the DSO2D15 generator and work through the safety checklist above.
- **Done when:** a 10 kHz sine at 0.65–2.65 V is confirmed on CH1, and the wiring to PA3 is finished.

### Phase 1 — Polling baseline

**It 4 — Polling ADC capture**
- Read ADC1 channel 3 in a loop of start conversion → wait → read result → store into a buffer of N samples.
- Hold PF15 high while the CPU works on ADC operations, and toggle PF14 once per sample.
- After each block, send it over UART in binary with a simple header (sync word, mode, sample rate, N).
- **Done when:** the LA shows the PF14 toggles and the decoded UART frames.

**It 5 — Python capture and plot**
- Write `analysis/capture.py`: read frames over pyserial, check them, save to `.npy`, and plot the time-domain waveform.
- **Done when:** the plot shows a clean sine at the generator's frequency.

**It 6 — Signal quality metrics**
- Write `analysis/metrics.py`: windowed FFT, frequency estimate, SNR, SINAD, ENOB (= (SINAD − 1.76) / 6.02).
- **Done when:** it prints the polling-mode ENOB, and the estimated frequency matches the generator to within 0.1%.

**It 7 — Polling measurements**
- CPU load: measure PF15's duty cycle on the LA (expected ≈ 100%). Cross-check by counting idle-loop iterations per second; also expected ≈ 0.
- Jitter: from the PF14 toggle period statistics on the LA, record the minimum, maximum and standard deviation of the period.
- **Done when:** the polling row of the Results table is filled in, with LA screenshots saved to `docs/img/`.

### Phase 2 — Timer-triggered interrupts

**It 8 — Hardware sample clock**
- Set up TIM1 channel 1 as PWM on PE9 at sample rate fs, and use the TIM1 CC1 event as the ADC's external trigger.
- **Done when:** the scope measures fs on PE9 (exact frequency and duty cycle), and the LA sees it on CH2.

**It 9 — Interrupt per sample**
- On each end-of-conversion interrupt, store the sample. Hold PF15 high for the length of the interrupt handler and toggle PF14 inside it.
- **Done when:** the captured data passes the It 6 metrics, and the LA shows one interrupt handler pulse per PE9 edge.

**It 10 — Interrupt-mode measurements**
- CPU load: PF15's duty cycle at several sample rates (e.g., 10k, 100k, 500k, 1M samples per second).
- Jitter: the delay from the PE9 edge to the PF14 toggle (this is interrupt latency) and how much it varies.
- **Done when:** the interrupt rows of the Results table are filled in.

### Phase 3 — DMA

**It 11 — Circular DMA**
- ADC1 → DMA2 stream 0 channel 0, circular mode, buffer of 2 × N samples, triggered by TIM1.
- In the half-complete and full-complete interrupts, set PF15 high only while handling (or sending) that half of the buffer.
- **Done when:** a continuous capture shows no gaps or discontinuities in the Python plot.

**It 12 — DMA-mode measurements**
- CPU load: PF15's duty cycle, which should be a few percent.
- Jitter: in DMA mode the PE9 edge *is* the sample instant. Measure it with scope **infinite persistence**: trigger on one edge, zoom into the next, and the width of the band of edges is the peak-to-peak jitter. Save screenshots.
- **Done when:** the DMA rows of the Results table are filled in.

### Phase 4 — Comparison and stress tests

**It 13 — Switch modes over UART**
- A single firmware image that changes mode, sample rate and N on a UART command.
- `analysis/run_suite.py` steps through each mode and sample rate, captures data, computes metrics and writes `results.csv`.
- **Done when:** one command produces the whole comparison table.

**It 14 — Find each method's maximum sample rate**
- Increase fs until a method fails. Detect failure from the ADC overrun (OVR) flag, a sample count that doesn't match, or gaps between PF14 toggles on the LA.
- **Done when:** the maximum sustained rate is recorded for each mode, and you have LA screenshots showing where interrupt mode starts missing samples.

**It 15 — Interference stress test**
- Add a busy higher-priority interrupt (e.g., a timer interrupt firing at 50 kHz with a fixed amount of work, or a flood of incoming UART data).
- **Done when:** the results show interrupt-mode jitter and ENOB getting worse while DMA mode stays the same, with before-and-after scope persistence screenshots.

### Phase 5 — Stretch goals

**It 16 — Triple-interleaved ADC (about 7.2 MSPS)**
- Set ADC1, ADC2 and ADC3 to take turns sampling PA3, using DMA in the common data register mode.
- **Done when:** a sine is captured at about 7.2 MSPS and ENOB is reported. Note how interleaving mismatch between the three ADCs shows up as spurs in the FFT.

**It 17 — Automated LA capture (optional)**
- Use `sigrok-cli` with the `kingst-la2016` driver (if it supports the LA1010) to take LA captures from `run_suite.py` and compute duty cycle and jitter automatically.
- **Done when:** CPU load and jitter come from scripts, not manual reading.

### Phase 6 — Write-up

**It 18 — Documentation**
- README: goal, wiring diagram, method, results table, FFT plots, LA and scope screenshots, and what you learned (e.g., why DMA jitter is set by hardware, and the trade-off between the 144 MHz and 180 MHz clocks).
- Fill in the resume bullet's `[bracketed]` values from the measured results.
- **Done when:** someone else could reproduce the results from the README alone.

---

## Results (fill in as you go)

| Mode | fs (SPS) | CPU load (%) | Jitter p-p | Jitter σ | Max sustained fs | SNR (dB) | ENOB | Notes |
|---|---|---|---|---|---|---|---|---|
| Polling | | | | | | | | |
| Interrupt | 10k | | | | | | | |
| Interrupt | 100k | | | | | | | |
| Interrupt | 1M | | | | | | | |
| DMA | 10k | | | | | | | |
| DMA | 100k | | | | | | | |
| DMA | 1M | | | | | | | |
| DMA | 2.4M | | | | | | | 144 MHz system clock |
| DMA (triple interleaved) | 7.2M | | | | | | | |

## Repo layout

```
firmware/     STM32 CubeMX + CMake project
analysis/     capture.py, metrics.py, run_suite.py
docs/img/     Scope and LA screenshots, plots
PLAN.md       This file
README.md     Final write-up
```
