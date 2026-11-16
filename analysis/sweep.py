#!/usr/bin/env python3
"""Step 15: flash one MODE at several sample rates and check that each one keeps up."""
import argparse
import subprocess

import serial

def lines_from_board(port, n):
    """Return the header fields of n complete 't=' lines, skipping the first (it may be cut off)."""
    out = []
    with serial.Serial(port, 115200, timeout=5) as ser:
        ser.reset_input_buffer()
        ser.readline()
        while len(out) < n:
            line = ser.readline().decode(errors="replace").strip()
            if not line:
                raise SystemExit("no data from the board")
            if line.startswith("t="):
                try:
                    out.append(dict(kv.split("=") for kv in line.split()))
                except ValueError:
                    pass                                # cut-off line: skip it
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", type=int, choices=(1, 2, 3))
    ap.add_argument("fs", type=int, nargs="+", help="sample rates in Hz; each must divide 144 MHz")
    ap.add_argument("--lines", type=int, default=15, help="lines to check per rate (one a second)")
    ap.add_argument("--port", default="/dev/ttyACM0")
    args = ap.parse_args()

    key = "ndtr" if args.mode == 3 else "missed"
    rule = "ndtr=0" if args.mode == 3 else "missed=0 and lost=0"
    print(f"MODE {args.mode}, {args.lines} lines per rate; a rate passes if {rule} on every line")
    print(f"{'fs (kHz)':>9} {'cycles':>6} {'worst ' + key:>12} {'lost':>5} {'load %':>13} {'late_max':>9}  result")
    for fs in args.fs:
        subprocess.run(["make", "-C", "firmware", "flash", f"MODE={args.mode}", f"FS={fs}"],
                       check=True, capture_output=True)
        rows = lines_from_board(args.port, args.lines)
        worst = max(int(r[key]) for r in rows)
        lost = max(int(r.get("lost", 0)) for r in rows)  # polling and interrupts: ticks lost without a trace
        loads = [int(r["load"]) / 100 for r in rows]
        late = max(int(r["late_max"]) for r in rows) if args.mode != 3 else None
        ok = worst == 0 and lost == 0
        print(f"{fs / 1e3:9g} {144_000_000 // fs:6d} {worst:12d} {'-' if args.mode == 3 else lost:>5} "
              f"{min(loads):6.2f}-{max(loads):<6.2f} {'-' if late is None else late:>9}  {'pass' if ok else 'FAIL'}")

if __name__ == "__main__":
    main()
