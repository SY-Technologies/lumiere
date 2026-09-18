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

Status: in progress

- Define explicit roots for globals, frames, callbacks, and native handles.
- Add allocation accounting and a deterministic collection trigger.
- Collect closure, environment, object, class, and collection cycles safely.
- Enable leak detection in sanitizer validation.

Acceptance: cyclic stress programs have bounded retained memory and the complete
suite passes with leak detection enabled.

## T3 — Specify and optimize dictionary/set semantics

Status: pending; depends on T1

- Specify equality, numeric cross-type keys, duplicate handling, mutation, and
  iteration order.
- Add a shared conformance suite before changing storage.
- Introduce hash indexing only with a hash function consistent with equality.

Acceptance: both engines produce identical observable behavior and benchmarks
show a material lookup improvement without semantic regressions.

## T4 — Build one cross-engine conformance and fuzzing corpus

Status: pending

- Run language and standard-library fixtures under both engines.
- Compare values, diagnostics, evaluation order, and side effects.
- Fuzz UTF-8, parser input, numeric boundaries, and malformed bytecode.

Acceptance: one command runs the corpus and stores minimal reproductions for
every discovered mismatch or crash.

## T5 — Profile representative workloads

Status: pending; benefits from T2 accounting

- Add allocation, peak-memory, and VM-instruction counters.
- Establish representative workloads in addition to microbenchmarks.
- Optimize measured value copies, temporary lifetimes, and native-call setup.

Acceptance: benchmark reports include reproducible baselines and explain each
optimization using measured time and memory changes.

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
