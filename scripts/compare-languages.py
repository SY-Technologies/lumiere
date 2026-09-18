"""Compare Lumière against a native baseline and another interpreter.

The project's target is Go-level performance, so the gap has to be measured
against compiled code rather than against Lumière's own history. Go is not
installed everywhere; C compiled with -O2 stands in for its lower bound on a
scalar loop, and CPython stands in for a mature bytecode interpreter.

    python3 scripts/compare-languages.py build_release/lumiere
"""

import argparse
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

LOOP_C = """#include <stdio.h>
int main(void) {
    long long total = 0;
    for (long long i = 0; i < 1000000; i++) total += i;
    printf("%lld\\n", total);
    return 0;
}
"""

LOOP_PY = """total = 0
i = 0
while i < 1000000:
    total += i
    i += 1
print(total)
"""

EXPECTED = "499999500000\n"


def measure(command, runs, timeout):
    samples = []
    for run in range(runs + 1):
        start = time.perf_counter()
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        elapsed = time.perf_counter() - start
        if result.returncode or result.stdout != EXPECTED:
            raise RuntimeError(f"{command[0]}: exit={result.returncode}, stdout={result.stdout!r}")
        if run:
            samples.append(elapsed * 1000)
    return statistics.median(samples), min(samples), max(samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    loop = root / "benchmarks" / "integer_loop.lum"
    cases = [("Lumière VM", [str(args.binary.resolve()), "--vm", str(loop)]),
             ("Lumière tree-walk", [str(args.binary.resolve()), "--tw", str(loop)])]

    with tempfile.TemporaryDirectory() as workspace:
        area = Path(workspace)
        if shutil.which("go"):
            source = area / "loop.go"
            source.write_text(LOOP_C.replace("#include <stdio.h>\n", "package main\n\nimport \"fmt\"\n")
                              .replace("int main(void) {", "func main() {")
                              .replace("long long total = 0;", "\ttotal := 0")
                              .replace("for (long long i = 0; i < 1000000; i++) total += i;",
                                       "\tfor i := 0; i < 1000000; i++ {\n\t\ttotal += i\n\t}")
                              .replace("printf(\"%lld\\n\", total);", "\tfmt.Println(total)")
                              .replace("return 0;\n}", "}"), encoding="utf-8")
            binary = area / "loop_go"
            if subprocess.run(["go", "build", "-o", str(binary), str(source)]).returncode == 0:
                cases.insert(0, ("Go (go build)", [str(binary)]))
        if shutil.which("cc") or shutil.which("gcc"):
            source = area / "loop.c"
            source.write_text(LOOP_C, encoding="utf-8")
            binary = area / "loop_c"
            compiler = shutil.which("cc") or shutil.which("gcc")
            if subprocess.run([compiler, "-O2", "-o", str(binary), str(source)]).returncode == 0:
                cases.insert(0, ("C (-O2)", [str(binary)]))
        source = area / "loop.py"
        source.write_text(LOOP_PY, encoding="utf-8")
        cases.append(("CPython", ["python3", str(source)]))

        results = []
        for label, command in cases:
            try:
                results.append((label,) + measure(command, args.runs, args.timeout))
            except Exception as failure:  # a missing toolchain is not a benchmark failure
                print(f"{label}: skipped ({failure})")

    baseline = next((median for label, median, *_ in results if label.startswith(("Go", "C "))), None)
    for label, median, low, high in results:
        ratio = f"{median / baseline:8.0f}x" if baseline else "       -"
        print(f"{label:<20} median={median:9.2f} ms  min={low:8.2f}  max={high:8.2f}  {ratio}")


if __name__ == "__main__":
    main()
