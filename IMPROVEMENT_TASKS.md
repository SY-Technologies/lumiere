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

Status: in progress — 2026-09-20; corpus and fuzzing in place, two divergences open

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
- [x] **Fix the collector's thread safety.** Its state is per-thread now, so a
      runtime owns its own collector and two interpreters on two threads never
      share a candidate buffer. A thread reclaims its own cycles as it ends,
      because no other thread can: the buffer belongs to it.

      The choice was measured rather than argued. A lock-based variant — one
      recursive mutex over the collector, an atomic for the inline due-check —
      was built from the same commit and run against it: the lock costs up to
      39% (VM, text_iteration) and 45% (tree walker, typed_list), concentrated
      exactly where allocation is heaviest. Per-thread is free.

      `CycleCollector.KeepsTwoThreadsOutOfEachOthersCollector` is the regression
      test. Against the old collector it fails every run, where the LumiNet test
      that first exposed this only failed about one in six.

- [x] Fuzz malformed bytecode against the verifier. The verifier's guarantee is
      that anything it accepts cannot make the interpreter read out of bounds;
      every unchecked operand read in the interpreter rests on it, and nothing
      held it to account. `VmVerifier.AcceptedBytecodeSurvivesExecution`
      corrupts a compiled module's instruction stream at random, verifies it,
      and runs whatever the verifier accepted in a forked child, so a crash
      arrives as a signal instead of taking the suite with it. A rejection is
      the ordinary outcome; an acceptance that then dies is the finding.

      Two things it deliberately does not claim: a mutated module may compute
      nonsense, which is no concern of the verifier's, and it may loop forever,
      because a legitimate program may too — termination was never part of the
      guarantee, so a child that does not finish is skipped rather than failed.
      The test also asserts that some mutation reached the interpreter, so a run
      where the verifier rejected everything cannot pass while proving nothing.

      The suite runs 400 mutations; `LUMIERE_FUZZ_SEED` and
      `LUMIERE_FUZZ_ATTEMPTS` open it up for a longer campaign. 7,500 mutations
      across three seeds under AddressSanitizer and UndefinedBehaviorSanitizer —
      about 1,060 of them accepted and executed — found no hole. That is where
      the test has teeth: in a release build an unchecked out-of-bounds read may
      not fault at all.
- [x] Decide whether `principal` is required. It is: a program has a place to
      start, a module does not. The analyzer reports LUM-S0051 when a file is
      about to be run and has no `principal`, so both engines are told the same
      thing before either starts. `lumiere check` on a module and `lumiere
      tester` are unaffected. Pinned by
      `tests/conformance/point_entree_obligatoire`.
- [x] Close the gap where the analyzer is weaker than the VM's compiler. Every
      rule the VM's compiler enforces was run against `lumiere check`, and five
      were missing rather than the two that had been recorded: `arrêter` and
      `continuer` outside a loop (LUM-S0052, LUM-S0053), `parent` outside a
      method (LUM-S0054), assignment to a name nothing declares (LUM-S0055), and
      assignment to a `soit fixe` binding (LUM-S0056). All five are analyzer
      rules now, so both engines are told the same thing before either starts.

      The first two were not divergences but crashes: outside a loop the tree
      walker threw a signal nothing caught, and the process aborted with
      `terminate called after throwing an instance of 'lumiere::BreakSignal'`.
      `scripts/fuzz` watches for exactly that shape of output and had never
      produced one, because mutation rarely writes a bare `arrêter` at the top of
      a function body. A systematic sweep of one engine's rules against the
      other's found in minutes what random search had not.

- [x] **Resolve names.** A name that is read now has to resolve to something:
      a local or parameter, a module-level declaration, an import, or a type,
      class or interface name. LUM-S0057 otherwise. `afficher(nom_absent)` and
      `appel_absent()` used to be accepted by `lumiere check` and fail only at
      run time, in different code under each engine, which is why their carets
      sat a character apart — one diagnostic from the analyzer ends that whole
      family at the source.

      Rejecting a valid program is worse than the hole, so the corpus was the
      guard: every `.lum` file under `examples`, `tests` and the standard
      library was run through `lumiere check`, and the five it flagged all
      genuinely reference names nothing declares. Forward references to
      functions, classes and module-level values still resolve, as do loop
      variables, pattern bindings, closure captures, module aliases and type
      names; a local does not escape its block.

      The shell is the exception, and it exposed a bug the assignment rule had
      already shipped: each submission is analyzed on its own while the
      interpreter carries every earlier one, so `soit base = 40` on one line and
      `base = 60` on the next was refused as an assignment to an undeclared
      name — the line simply never ran. Both rules now stand down for an
      incremental submission, where an unknown name cannot be told from one
      declared earlier, and the interpreter still catches it when the line runs.
      Analysis that carries earlier submissions forward is its own piece of work
      and is not done here.

- [ ] Settle which token a runtime error points at. The two engines pick
      different tokens for the same failure, so the caret can sit one character
      apart. A spot fix traded one divergence for another; this needs a stated
      rule, not a patch. Name resolution removed the largest family of these by
      moving the diagnostic into the analyzer, where there is only one of it.

- [x] Point the caret at the token, not past it. A Token carried two positions:
      `line`/`column`, which the tokenizer set *after* the lexeme, and
      `start_line`/`start_column`, which is where it begins. Only the lexer and
      the parser read the second pair; everything else drew its caret from the
      first, so an error about `valeur` pointed at the space after it and
      `t.membre_absent()` pointed at the closing paren, thirteen characters
      past the member it was about.

      A token now has one position and it is the start. That is one change in
      the tokenizer rather than a sweep of every caller, and the duplicate pair
      is gone. The byte span `start_offset..end_offset` still says how far the
      token reaches, which is what an editor reads.

      Both engines were wrong in the same way, so no conformance case caught it
      — five expectations moved, each checked against the source by hand rather
      than regenerated on faith.

What it found on the first run, all since fixed: the VM showed its synthetic
`__module_init__` frame in tracebacks the tree walker had no frame for; the VM
shared one local slot across loop iterations, so every closure made in a loop saw
the last iteration's value; and five families of runtime diagnostic were worded
differently by the two engines, including every binary arithmetic and comparison
operator. Three of the five were found by the fuzzer rather than by hand.

Acceptance: `scripts/conformance` and `scripts/fuzz` each run from one command
and store minimal reproductions. Met; the divergences above are recorded rather
than closed.

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
