#!/usr/bin/env python3
"""Step 9: read one ADC burst from the board and measure the sine in it."""
import argparse

import numpy as np
import serial

CPU_HZ = 144_000_000

def read_burst(port, nsamp=1000):
    """Return (header fields, samples) from the second complete burst.

    The first one may be old data held since before the port was opened (Step 7 lesson).
    """
    found = 0
    header = None
    with serial.Serial(port, 115200, timeout=3) as ser:
        ser.reset_input_buffer()
        while True:
            line = ser.readline().decode(errors="replace").strip()
            if not line:
                raise SystemExit("no data from the board")
            if line.startswith("t="):
                try:
                    header = dict(kv.split("=") for kv in line.split())
                except ValueError:
                    header = None                       # cut-off line
            elif line.startswith("s=") and header:
                values = line[2:].split(",")
                if len(values) == nsamp and all(v.isdigit() for v in values):
                    found += 1
                    if found == 2:
                        return header, np.array([int(v) for v in values])
                header = None

def rising_crossings(mv, dt):
    """Times where the signal rises through its middle level, with hysteresis against noise."""
    mid = (mv.max() + mv.min()) / 2
    hyst = (mv.max() - mv.min()) / 4
    times, armed = [], False
    for i in range(1, len(mv)):
        if mv[i] < mid - hyst:
            armed = True
        elif armed and mv[i - 1] < mid <= mv[i]:
            frac = (mid - mv[i - 1]) / (mv[i] - mv[i - 1])  # between the two samples
            times.append((i - 1 + frac) * dt)
            armed = False
    return times

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default="/dev/ttyACM0")
    ap.add_argument("--plot", metavar="PNG", help="save a plot of the burst")
    args = ap.parse_args()

    header, s = read_burst(args.port)
    vdda = int(header["vdda_mV"])
    if "fs" in header:                              # Step 10 on: samples paced by TIM1
        dt = 1 / int(header["fs"])
        print(f"burst: {len(s)} samples at {int(header['fs']) / 1e3:g} kHz ({dt * 1e6:.3f} us apart), "
              f"span {header['span']} cycles, missed {header['missed']}, VDDA {vdda} mV")
    else:                                           # Step 9: back to back, spacing from the burst time
        burst = int(header["burst"])
        dt = burst / len(s) / CPU_HZ
        print(f"burst: {len(s)} samples in {burst} cycles = {burst / CPU_HZ * 1e3:.3f} ms, "
              f"{dt * 1e6:.3f} us apart ({1 / dt / 1e3:.1f} kHz), VDDA {vdda} mV")
    mv = s * vdda / 4095
    t = np.arange(len(s)) * dt

    print(f"min {mv.min():.0f} mV, max {mv.max():.0f} mV, mean {mv.mean():.0f} mV")

    fit = None
    swing = mv.max() - mv.min()
    times = rising_crossings(mv, dt) if swing > 500 else []  # under 500 mV: no sine, just noise
    if len(times) >= 2:
        f = (len(times) - 1) / (times[-1] - times[0])
        w = 2 * np.pi * f * t
        a = np.column_stack([np.sin(w), np.cos(w), np.ones_like(t)])
        coef, *_ = np.linalg.lstsq(a, mv, rcond=None)
        fit = a @ coef
        amp = np.hypot(coef[0], coef[1])
        resid = mv - fit
        print(f"frequency {f:.1f} Hz ({len(times)} rising crossings)")
        print(f"sine fit: amplitude {amp:.0f} mV ({2 * amp:.0f} mVpp), offset {coef[2]:.0f} mV")
        print(f"residual: rms {resid.std():.1f} mV, worst {np.abs(resid).max():.0f} mV")
    else:
        print("no sine found (swing under 500 mV or fewer than 2 rising crossings): "
              f"spread {mv.std():.1f} mV rms around the mean")

    if args.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        rows = 2 if fit is not None else 1
        fig, ax = plt.subplots(rows, 1, sharex=True, figsize=(10, 3 * rows), squeeze=False)
        ax[0][0].plot(t * 1e3, mv, ".", ms=2, label="samples")
        if fit is not None:
            ax[0][0].plot(t * 1e3, fit, lw=1, label="sine fit")
            ax[1][0].plot(t * 1e3, mv - fit, ".", ms=2)
            ax[1][0].set_ylabel("sample − fit (mV)")
        ax[0][0].set_ylabel("A0 (mV)")
        ax[0][0].legend(loc="upper right")
        ax[-1][0].set_xlabel("time (ms)")
        fig.tight_layout()
        fig.savefig(args.plot, dpi=120)
        print(f"saved {args.plot}")

if __name__ == "__main__":
    main()
