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

Status: collecting — 2026-09-18; tree-walker environments outstanding

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
- [ ] Make the tree walker's environments and function bodies counted objects,
      so their cycles come within the collector's reach. An `Environment` owns
      its parent by `shared_ptr` and a function's closure owner is the
      environment holding it, so every program leaks that pair today.
- [ ] Decide what to do about captures inside a native handler's `std::function`,
      which cannot be enumerated and so cannot be traced.

Acceptance: met for the VM. The complete suite passes with leak detection
enabled once the tree walker's environments are counted.

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

Status: pending

- Run language and standard-library fixtures under both engines.
- Compare values, diagnostics, evaluation order, and side effects.
- Fuzz UTF-8, parser input, numeric boundaries, and malformed bytecode.

Acceptance: one command runs the corpus and stores minimal reproductions for
every discovered mismatch or crash.

## T5 — Profile representative workloads

Status: baseline measured — 2026-09-17; optimization blocked on T2

- [x] Measure against a compiled baseline rather than against our own history:
      `scripts/compare-languages.py` reports 430x C and 1.39x slower than CPython.
- [x] Attribute the cost: the 48-byte non-trivial `Value` is about 40% of
      execution on the integer loop.
- [x] Remove the per-instruction source-location lookup (8.6%).
- [ ] Add allocation, peak-memory, and VM-instruction counters.
- [ ] Establish representative workloads in addition to microbenchmarks.
- [ ] Replace the value representation — blocked on T2, since a trivially
      copyable value cannot hold a `shared_ptr`.

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
