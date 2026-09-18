# Lumière improvement tasks

Tasks are executed in order. A task is complete only when its acceptance
criteria pass in Release and sanitizer builds and the behavior is documented.

## T1 — Complete nominal-type identity invariants

Status: complete — 2026-09-17

- [x] Audit native classes and interfaces for stable, module-qualified identity.
- [x] Align semantic module identity with runtime identity.
- [x] Test chained aliases under lexical shadowing in both engines.
- [x] Preserve readable diagnostics; encoded identities remain internal.

Acceptance: same declarations remain compatible through imports and aliases;
equal-spelling declarations from different owners remain incompatible.

## T2 — Bound runtime memory with cycle collection

Status: complete — 2026-09-18

- [x] Give the runtime its own ownership: heap values carry an intrusive,
      non-atomic reference count instead of being held by `shared_ptr`.
- [x] Verify that everything except cycles is freed: `scripts/check-leaks` runs
      nine programs under a leak-detecting build and all are clean.
- [x] Roots are not needed: Bacon-Rajan collection works from the counts, so a
      value held only in a C++ local during a native call is accounted for.
- [x] Add allocation accounting (`RefCounted::live_count`) and a deterministic
      trigger (candidate buffer over a threshold, checked at loop back edges and
      function returns).
- [x] Collect object, class, and collection cycles safely. A cyclic stress
      program went from 129 MB to 5.9 MB of peak resident memory.
- [x] Enable leak detection for the VM: `scripts/check-leaks` is clean on nine
      programs, cycles included.
- [x] Make the tree walker's environments and function bodies counted objects,
      so their cycles come within the collector's reach. `Environment`,
      `RuntimeFunctionBody`, `RuntimeClassBody`, `RuntimeInterfaceBody` and
      `RuntimeModuleState` are counted and traced; capture cells are counted
      too, so a closure capturing itself is a cycle the collector can see.
- [x] Collect on the tree walker's loop back edges as the VM does, so a loop
      building cycles no longer grows without bound. The same stress program
      went from 135 MB to 5.2 MB of peak resident memory under `--tw`.
- [x] Close the native-handler hole. A capture inside a `std::function` cannot
      be enumerated, so the contract is now the reverse: a native handler
      captures a counted object as a raw pointer, and the owning reference is
      declared in `LumiereFunction::native_captures`, where tracing reaches it.
      `NativeState` gives the same treatment to the C++ state hanging off an
      instance, with both virtuals left pure so a new state must answer.

Acceptance: met. The complete suite of 441 tests passes with leak detection
enabled, and `scripts/check-leaks` is clean on both engines, cycles included.

## T3 — Specify and optimize dictionary/set semantics

Status: complete — 2026-09-17

- [x] Specify equality, numeric cross-type keys, duplicate handling, mutation,
      and iteration order.
- [x] Add a shared conformance suite before changing storage.
- [x] Introduce hash indexing with a hash function consistent with equality.
- [x] Build `Ensemble`: a `{1, 2, 3}` literal, `Liste.en_ensemble()`, the member
      and set-algebra surface, the same index and the same key rule as a
      dictionary.

Acceptance: met. Both engines agree on every conformance fixture, and 50,000
dictionary keys fell from 10.97 s to 0.057 s with no measurable change to the
other benchmark workloads.

## T4 — Build one cross-engine conformance and fuzzing corpus

Status: in progress — 2026-09-18; corpus and fuzzing in place, two divergences open

- [x] Run language and standard-library fixtures under both engines.
      `scripts/conformance` runs every case through the real CLI under `--tw`
      and `--vm`, checks each against its expectations, and diffs the engines
      against each other. It is a ctest entry, so a divergence fails the build.
- [x] Compare values, diagnostics, evaluation order, and side effects. The
      corpus covers evaluation and side-effect order, closure capture, dispatch,
      pattern matching, iteration under mutation, numeric boundaries, and the
      shape of a traceback.
- [x] Fuzz UTF-8, parser input, and numeric boundaries. `scripts/fuzz` mutates
      corpus sources and generates boundary programs; every finding is shrunk to
      a minimal reproduction on disk. A crash, a hang, a leaked C++ artifact in a
      message, or a disagreement between the engines all count as findings.
- [ ] Look into one unreproduced crash. `InterpreterBuiltinModules.
      SupportsLumiNetCanalStandalone` segfaulted once under a parallel ctest run
      and has not recurred in the several full runs since, in either build. That
      test drives a real socket from the main thread while the interpreter runs
      on a worker, so it is the one place two threads are live at once; a crash
      there is worth a look even when it will not reproduce on demand.
- [ ] Fuzz malformed bytecode against the verifier. The verifier's guarantee —
      that anything it accepts cannot make the interpreter read out of bounds —
      is the one property here with no test behind it.
- [x] Decide whether `principal` is required. It is: a program has a place to
      start, a module does not. The analyzer reports LUM-S0051 when a file is
      about to be run and has no `principal`, so both engines are told the same
      thing before either starts. `lumiere check` on a module and `lumiere
      tester` are unaffected. Pinned by
      `tests/conformance/point_entree_obligatoire`.
- [ ] Close the gap where the analyzer is weaker than the VM's compiler. The VM
      compiles a whole module before running it and rejects things `lumiere
      check` accepted, so it fails at a different moment and with different words
      from the tree walker, which only fails on reaching them. Two instances are
      recorded: assignment to an undeclared name
      (`tests/conformance/divergence_globale_non_declaree`) and `parent` outside
      a method (`tests/conformance/divergence_parent_hors_methode`). Both are
      analyzer errors waiting to be written; the divergence is a symptom. Worth a
      sweep of the VM compiler's own rejections for the rest of the family.
- [ ] Settle which token a runtime error points at. The two engines pick
      different tokens for the same failure, so the caret can sit one character
      apart. A spot fix traded one divergence for another; this needs a stated
      rule, not a patch.

What it found on the first run, all since fixed: the VM showed its synthetic
`__module_init__` frame in tracebacks the tree walker had no frame for; the VM
shared one local slot across loop iterations, so every closure made in a loop saw
the last iteration's value; and five families of runtime diagnostic were worded
differently by the two engines, including every binary arithmetic and comparison
operator. Three of the five were found by the fuzzer rather than by hand.

Acceptance: `scripts/conformance` and `scripts/fuzz` each run from one command
and store minimal reproductions. Met, except that malformed bytecode is not yet
fuzzed and the divergences above are recorded rather than closed.

## T5 — Profile representative workloads

Status: baseline measured — 2026-09-17; value representation unblocked by T2

- [x] Measure against a compiled baseline rather than against our own history:
      `scripts/compare-languages.py` reports 430x C and 1.39x slower than CPython.
- [x] Attribute the cost: the 48-byte non-trivial `Value` is about 40% of
      execution on the integer loop.
- [x] Remove the per-instruction source-location lookup (8.6%).
- [ ] Add allocation, peak-memory, and VM-instruction counters.
- [ ] Establish representative workloads in addition to microbenchmarks.
- [ ] Replace the value representation. No longer blocked: `Value` holds a
      `Ref`, not a `shared_ptr`, and is down to 24 bytes.

Acceptance: benchmark reports include reproducible baselines and explain each
optimization using measured time and memory changes. Note that no interpreter
reaches the Go target; see the hardening notes.

## T6 — Define international text contracts

Status: pending

- Specify normalization, grapheme segmentation, collation, case folding, and
  locale behavior while preserving scalar APIs.
- Use maintained Unicode data rather than handwritten tables.

Acceptance: versioned text behavior is covered by Unicode conformance data and
documented complexity guarantees.

## T7 — Complete adoption infrastructure

Status: pending

- Publish an executable language specification and compatibility policy.
- Add reproducible packages/installers, a formatter, and a standard test runner.
- Improve editor diagnostics and document interoperability ownership rules.

Acceptance: a clean supported machine can install, format, test, and run a
versioned project using documented commands.

## T8 — Prepare and validate the release

Status: pending; depends on T1–T7

- Organize the working tree into reviewable commits and release notes.
- Run network-enabled CI and platform installation tests.
- Review compatibility changes and version them explicitly.

Acceptance: all supported-platform checks pass and the release artifacts are
reproducible from the tagged source.
