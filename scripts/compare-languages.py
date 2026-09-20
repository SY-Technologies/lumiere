"""Compare Lumière against another interpreter, and against compiled code.

Comparing a runtime against its own history shows that a change helped; it
cannot show how far the target is. The target is to be ahead of a mature
bytecode interpreter, so CPython runs the same workloads, program for program --
they are in benchmarks/python/, next to the Lumière sources, so the claim can be
checked rather than taken on trust.

C compiled with -O2 anchors the scalar loop. It runs that one workload only: a C
dictionary or Unicode string is a different program, and timing two different
programs says nothing.

    python3 scripts/compare-languages.py build_release/lumiere
    python3 scripts/compare-languages.py build_release/lumiere --workload integer_loop
"""

import argparse
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from workloads import SCALAR_WORKLOAD, WORKLOADS

LOOP_C = """#include <stdio.h>
int main(void) {
    long long total = 0;
    long long index = 0;
    while (index < 1000000) { total = total + index; index = index + 1; }
    printf("%lld\\n", total);
    return 0;
}
"""


def measure(command, expected, runs, timeout):
    """Median, min and max of `runs` timed runs, in milliseconds."""
    samples = []
    for run in range(runs + 1):  # the first is an untimed warm-up
        start = time.perf_counter()
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", timeout=timeout)
        elapsed = time.perf_counter() - start
        if result.returncode or result.stdout != expected:
            raise RuntimeError(f"{command[0]}: exit={result.returncode}, "
                               f"stdout={result.stdout!r}")
        if run:
            samples.append(elapsed * 1000)
    return statistics.median(samples), min(samples), max(samples)


def compiled_baseline(area, runs, timeout):
    """The scalar loop in C, or None when there is no compiler here."""
    compiler = shutil.which("cc") or shutil.which("gcc")
    if compiler is None:
        return None
    source, binary = area / "loop.c", area / "loop_c"
    source.write_text(LOOP_C, encoding="utf-8")
    if subprocess.run([compiler, "-O2", "-o", str(binary), str(source)]).returncode:
        return None
    return measure([str(binary)], WORKLOADS[SCALAR_WORKLOAD], runs, timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--workload", action="append", choices=sorted(WORKLOADS))
    parser.add_argument("--python", default="python3")
    parser.add_argument("--skip-tree-walker", action="store_true",
                        help="the tree walker is several times slower; skip it for a quick read")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    binary = str(args.binary.resolve())
    names = args.workload or list(WORKLOADS)

    print(f"{'atelier':<20}{'VM':>12}{'arbre':>12}{'CPython':>12}{'VM/CPython':>12}")
    print("-" * 68)
    ratios = []
    for name in names:
        expected = WORKLOADS[name]
        runners = [("vm", [binary, "--vm", str(root / "benchmarks" / f"{name}.lum")]),
                   ("tw", [binary, "--tw", str(root / "benchmarks" / f"{name}.lum")]),
                   ("py", [args.python, str(root / "benchmarks" / "python" / f"{name}.py")])]
        if args.skip_tree_walker:
            runners.pop(1)

        medians = {}
        for label, command in runners:
            try:
                medians[label] = measure(command, expected, args.runs, args.timeout)[0]
            except Exception as failure:  # a missing runtime is not a benchmark failure
                print(f"{name} {label}: ignoré ({failure})")

        def cell(label):
            return f"{medians[label]:>9.2f} ms" if label in medians else f"{'-':>12}"

        ratio = ""
        if "vm" in medians and "py" in medians:
            value = medians["vm"] / medians["py"]
            ratios.append(value)
            ratio = f"{value:>11.2f}x"
        print(f"{name:<20}{cell('vm')}{cell('tw')}{cell('py')}{ratio:>12}")

    if ratios:
        print("-" * 68)
        print(f"{'médiane des rapports':<20}{'':>36}{statistics.median(ratios):>11.2f}x")
        print("Sous 1.00x, la VM est plus rapide que CPython sur cet atelier.")

    if SCALAR_WORKLOAD in names:
        with tempfile.TemporaryDirectory() as workspace:
            baseline = compiled_baseline(Path(workspace), args.runs, args.timeout)
        if baseline is not None:
            print(f"\nRéférence compilée, {SCALAR_WORKLOAD} en C -O2 : "
                  f"{baseline[0]:.2f} ms (min {baseline[1]:.2f}, max {baseline[2]:.2f}).")


if __name__ == "__main__":
    main()
