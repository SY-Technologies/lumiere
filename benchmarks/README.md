# Runtime benchmarks

Run from the repository root:

```sh
python3 scripts/benchmark.py build_release/lumiere --runs 7
python3 scripts/benchmark.py build_release/lumiere --backend tw --runs 7
python3 scripts/benchmark.py /path/to/before/lumiere build_release/lumiere --runs 7
```

Every run validates its result. These are end-to-end process timings, not
isolated VM dispatch timings. They include parsing and compilation. Use Release
builds with identical compiler options, and stop concurrent builds or tests
before collecting samples. The runner prints median/min/max after one warm-up.

## Recorded optimization checkpoint — 2026-09-05

Environment: macOS 26.6.2, arm64, Apple Clang 21.0.0, CMake Release (`-O3`,
`-DNDEBUG`). Seven measured runs per workload and binary, VM backend.

The baseline was saved from this branch **after checked arithmetic was added,
but before the text-loop and local-slot optimizations**. This is not a comparison
against an untouched release, and does not measure the cost of checked arithmetic.

| Workload | Before median (min–max), seconds | After median (min–max), seconds |
| --- | --- | --- |
| 1,000,000 integer-loop iterations | 0.131968 (0.130087–0.132817) | 0.132877 (0.131305–0.135598) |
| 30,000 Unicode scalars | 4.535706 (4.498860–4.545007) | 0.010436 (0.010375–0.010523) |
| 100,000 user-function calls | 0.058752 (0.057740–0.091093) | 0.051531 (0.050991–0.053741) |

Text iteration was about 435× faster in this workload after removing quadratic
scanning. Function-call time fell about 12%. The integer loop was effectively
unchanged. These results do not imply equivalent speedups for other programs;
the text snapshot also needs O(n) auxiliary storage. No peak-memory claim is made.

Validation at this checkpoint: all 374 tests passed in both Release and
AddressSanitizer/UndefinedBehaviorSanitizer builds, including loopback networking
tests run outside the filesystem/network sandbox. Leak detection was disabled
because reference cycles remain unresolved. Linux/Windows CI has been configured
but was not executed on this macOS host.

## Callback and value-transfer checkpoint — 2026-09-06

Same platform, compiler, Release flags, and runner methodology. The baseline is
the previous checkpoint's executable, saved before the runtime-context,
captured-call, text-signature, and operand-transfer changes. Seven measured
runs per workload, with the new binary measured first for this comparison.

| Workload | Before median (min–max), seconds | After median (min–max), seconds |
| --- | --- | --- |
| 1,000,000 integer-loop iterations | 0.138483 (0.137233–0.140370) | 0.122117 (0.120050–0.124426) |
| 30,000 Unicode scalars | 0.011842 (0.011738–0.012083) | 0.011794 (0.011049–0.014089) |
| 100,000 user-function calls | 0.053769 (0.053279–0.054006) | 0.049262 (0.048467–0.050125) |
| 3,000 identity calls carrying 128 KiB text | 0.115403 (0.111797–0.119529) | 0.063597 (0.060519–0.074986) |

Elapsed time fell about 12% for the integer loop, 8% for function calls, and 45%
for large-text calls. Text traversal was effectively unchanged. The new text
workload checks that both the returned content and original source survive;
the function accepts `Universel` so it also runs on the baseline, which lacked
complete text-method type inference.

An earlier general variant-move implementation regressed the integer loop by
about 2.6% in both 7-run and reversed-order 15-run comparisons. The final integer
payload fast path removed that regression. These measurements remain local
microbenchmarks, not a cross-language or cross-platform performance ranking.

Final validation: all 379 tests passed in Release and ASan/UBSan builds, with
stack-use-after-return detection enabled for the latter. The UDP test passed
50 consecutive Release runs after replacing its port-reuse race and timed
startup guesses with a readiness datagram. Leak detection remains disabled;
Linux and Windows execution still depends on CI rather than this macOS run.

## Collection-ownership checkpoint — 2026-09-07

Same platform, compiler, Release flags, and VM runner. The baseline was saved
immediately before moving collection constraints from runtime address maps onto
collection allocations. Seven measured runs, baseline first:

| Workload | Before median (min–max), seconds | After median (min–max), seconds |
| --- | --- | --- |
| 1,000,000 integer-loop iterations | 0.120218 (0.119752–0.120851) | 0.119611 (0.119004–0.120238) |
| 30,000 Unicode scalars | 0.010748 (0.010485–0.011126) | 0.010581 (0.010455–0.011183) |
| 100,000 user-function calls | 0.048337 (0.047955–0.048950) | 0.048354 (0.048105–0.050639) |
| 3,000 identity calls carrying 128 KiB text | 0.063781 (0.059437–0.067854) | 0.062714 (0.059563–0.065416) |
| 200,000 typed-list appends | 0.055887 (0.055033–0.056997) | 0.054058 (0.053015–0.055015) |

A reversed-order comparison with 15 measured runs gave typed-list medians of
0.055751 seconds before (0.054820–0.056180) and 0.054200 after
(0.053341–0.056395). That is about 3% less elapsed time in this workload.
The other workloads did not establish a consistent material speedup: for
example, large-text medians reversed direction to 0.066612 before and 0.070225
after, with overlapping ranges of 0.060969–0.078259 and 0.062354–0.098054.

The primary result is correctness: recycled addresses no longer inherit dead
collections' type constraints. Metadata is now embedded, increasing collection
object size but eliminating separate constraint-map entries and lookups.
Peak memory has not been measured. The typed-list workload validates both the
final size and last element; it does not isolate mutation-check cost from loop,
dispatch, allocation, or startup costs.

All 382 tests passed in Release and ASan/UBSan, including networking tests.
Stack-use-after-return detection was enabled; leak detection remains disabled.

## Dictionary indexing checkpoint — 2026-09-17

Environment: Ubuntu 22.04 on aarch64, GCC 11.4.0, CMake RelWithDebInfo, four
cores. Both binaries built from this tree with identical options; the baseline
is the commit immediately before dictionary lookup was indexed. Five measured
runs per workload after one warm-up, VM backend, baseline first.

| Workload | Before median (min–max), seconds | After median (min–max), seconds |
| --- | --- | --- |
| 1,000,000 integer-loop iterations | 0.143643 (0.141471–0.149304) | 0.139439 (0.138398–0.144340) |
| 30,000 Unicode scalars | 0.009514 (0.009151–0.010253) | 0.008497 (0.008414–0.008588) |
| 100,000 user-function calls | 0.050309 (0.049855–0.050473) | 0.048771 (0.048316–0.050289) |
| 3,000 identity calls carrying 128 KiB text | 0.115194 (0.112363–0.115589) | 0.110566 (0.107923–0.111369) |
| 200,000 typed-list appends | 0.071028 (0.067765–0.086133) | 0.066865 (0.065972–0.072548) |
| 50,000 dictionary writes then reads | 10.973501 (10.960202–11.080839) | 0.057346 (0.057151–0.057710) |

The dictionary workload is new and was added with this change. Lookup used to
scan every entry, so writing and then reading n keys did work proportional to
n squared; it is now proportional to n. At 50,000 keys that is about 191× less
elapsed time, and the ratio grows with n, so this number describes this
workload and not dictionaries in general. The other five workloads moved
between 1% and 11% in the same direction, which is within this host's run-to-run
spread and is not claimed as an improvement.

This host is not the macOS arm64 machine used for the earlier checkpoints, so
these absolute numbers are not comparable with the tables above — only the
before and after columns here are comparable with each other. Peak memory was
not measured. All 415 tests passed in RelWithDebInfo and in an
AddressSanitizer/UndefinedBehaviorSanitizer Debug build with leak detection
disabled, excluding the five example tests, which need to delete files the
sandbox used here forbids.
