"""End-to-end CLI benchmarks with validated output; use Release binaries."""

import argparse
from pathlib import Path
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaries", type=Path, nargs="+")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--backend", choices=("vm", "tw"), default="vm")
    args = parser.parse_args()
    if args.runs < 1 or args.timeout <= 0:
        parser.error("runs and timeout must be positive")

    root = Path(__file__).resolve().parent.parent
    cases = {"integer_loop": "499999500000\n", "text_iteration": "1487580000\n",
             "function_calls": "14999950000\n", "text_calls": "vrai\n131072\n",
             "typed_list": "200000\n199999\n"}
    for binary in args.binaries:
        for name, expected in cases.items():
            command = [str(binary.resolve()), "--" + args.backend,
                       str(root / "benchmarks" / (name + ".lum"))]
            samples = []
            # One untimed warm-up, then independent fresh-process samples.
            for run in range(args.runs + 1):
                start = time.perf_counter()
                result = subprocess.run(command, capture_output=True, text=True,
                                        encoding="utf-8", timeout=args.timeout)
                elapsed = time.perf_counter() - start
                if result.returncode or result.stdout != expected or result.stderr:
                    raise RuntimeError(f"{name}: exit={result.returncode}, "
                                       f"stdout={result.stdout!r}, stderr={result.stderr!r}")
                if run:
                    samples.append(elapsed)
            print(f"{binary} {args.backend} {name}: median={statistics.median(samples):.6f}s "
                  f"min={min(samples):.6f}s max={max(samples):.6f}s n={args.runs}", flush=True)


if __name__ == "__main__":
    main()
