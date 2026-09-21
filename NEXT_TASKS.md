# Next tasks — implementation briefs

Five pieces of work, specified so they can be implemented one at a time and
reviewed one at a time. They are ordered: 1 and 2 are contained and measurable,
3 removes a duplication that will otherwise keep producing bugs, 4 and 5 close
recorded correctness gaps.

`IMPROVEMENT_TASKS.md` says what the project is trying to do and why.
`RUNTIME_HARDENING.md` is the log of what was measured and what it cost. This
file is the brief for the work that is next; each task here should end with an
entry there.

---

## How to work in this repository

### Builds

Two are needed for every task:

- **Release** (`RelWithDebInfo`) — the only build any timing comes from.
- **Sanitizer** (`Debug` with AddressSanitizer and UndefinedBehaviorSanitizer) —
  where `assert` is live and where the suite has teeth. Every task must pass the
  whole suite in both.

### Tests

```sh
ctest -E "ExecutesRepository" -j8          # 449 tests; ExecutesRepository needs the repo layout
scripts/conformance build/lumiere          # 21 cases, each run under --tw and --vm
scripts/check-leaks                        # both engines, cycles included
scripts/fuzz build/lumiere --iterations 60 # mutated corpus through both engines
```

`VmVerifier.AcceptedBytecodeSurvivesExecution` takes about 20 s; exclude it while
iterating and run it once before committing. `LUMIERE_FUZZ_SEED` and
`LUMIERE_FUZZ_ATTEMPTS` open it up for a longer campaign.

### Measuring

**No timing claim from a single run, and none from a single binary.** Build the
"before" binary from the parent commit into its own directory, keep it, and run
both back to back:

```sh
python3 scripts/benchmark.py /path/to/before/lumiere build/lumiere --runs 9
python3 scripts/compare-languages.py build/lumiere --runs 7   # the CPython ratio
```

Read medians. If the min–max bands of the two binaries overlap, the difference
is not established — re-run with more samples before believing it. The machine
gets noisy; a result that will not reproduce is not a result.

**Count before you time.** `lumiere --vm --stats programme.lum` reports
allocations, bytes and peak memory on stderr. Allocation counts are exact and
reproducible in one run, and they have named every problem in this runtime so
far: the integer loop allocates 602 times in a million iterations, which is why
it keeps pace with CPython, while a call allocates three times. Under the
sanitizers the counts read zero and say so — AddressSanitizer replaces the same
operators to pair every `new` with its `delete`, and that check is worth more
than a counter.

**If a change does not measure, revert it.** A plausible optimization that shows
nothing is a plausible optimization that cost readability for nothing.

### Commits

- One logical change per commit, small enough to read.
- Short messages. Say what was wrong, what it is now, and what it measured.
- **Do not mention the assistant, the model, or the tool** anywhere in a commit
  message or in the code.
- Update `RUNTIME_HARDENING.md` in the same commit as the change it describes,
  with the numbers, not adjectives.
- Never `git add -A` without reading `git status` first. `docs/` was untracked
  for a long time and got swept into an unrelated commit that way.

### Traps this repository has already sprung

- **`merge_module` in `src/interpreter/vm/compiler.cpp` copies a `LirFunction`
  field by field**, because blocks own their terminators through a `unique_ptr`.
  A new field added to `LirFunction` arrives empty in the linked module until it
  is listed there too. There is a comment saying so; keep it accurate.
- **`VmClassBody`'s member caches assume a class is immutable once made.** Its
  parent and descriptors are fixed at `Opcode::CLASS`. Anything that would change
  a class afterwards invalidates them, silently.
- **Both engines can be wrong in the same way.** Conformance compares them to
  each other; it cannot see a rule they both break. That is how the caret sat one
  token past its subject in both engines for months, and how
  `Texte.convertir_decimal` wrote decimals a second, wrong way. When touching a
  routine, ask what the language's rule is, not what the other engine does.
- **A fast path that assumes something should `assert` it.** Assertions are live
  in the Debug and sanitizer builds, where the whole suite runs. `vm_class_body`
  is the pattern: a tag identifies the type, and an `assert` checks that
  `dynamic_cast` agrees.

---

## Task 1 — One field table: no hashing, and a defined order

### Why

`LumiereObject::fields` is a `std::unordered_map<std::string, Value>`, so
reading `ici.total` hashes a string and probes a bucket. It is the largest
remaining cost in method calls, which are the worst workload against CPython
(1.87x). Objects have a handful of fields; a hash table is the wrong shape for
three entries.

It is also **unspecified iteration order** in a language that promises two
engines agree. Three places walk an object's fields —
`src/interpreter/runtime/value.cpp`, `src/interpreter/stdlib/lumitest.cpp`, and
`include/lumiere/interpreter/tree_walker/environment.hpp` — and nothing pins the
order they see.

### What

Replace the container with an insertion-ordered table that scans, behind a
façade that keeps the call sites unchanged.

`DictData` in `src/interpreter/runtime/value.cpp` already does exactly this and
is the pattern to follow: entries in a vector in insertion order, an
open-addressed index built only once the table grows past `kIndexThreshold`,
"which spares small dictionaries an allocation they would not profit from".

### How

1. Add `FieldTable` to `include/lumiere/interpreter/runtime/value.hpp`, holding
   `std::vector<std::pair<std::string, Value>>`. Give it exactly the surface the
   149 existing call sites use, and no more:
   `operator[]`, `find`, `end`, `begin`, `at`, `size`, `empty`, `clear`,
   `count`, `contains`, `emplace`, `insert`, `insert_or_assign`, `erase`, and
   range-for. **127 of the 149 are `operator[]`**, so that one carries the
   change: it inserts a default-constructed `Value` when the name is absent,
   exactly as the map's does, and it is the one to get right first. `emplace`
   and `insert` return `std::pair<iterator, bool>` as the map's do — check
   whether any caller reads the `bool` before simplifying it away.
2. Change `LumiereObject::fields` to `FieldTable`. The 149 sites span both
   engines, the standard library and all eight LumiNet files; nothing there
   should need to change. Anything that does not compile is a call site using a
   corner of `unordered_map` that the façade should either support or that
   should be rewritten — decide which, deliberately, rather than widening the
   façade reflexively.
3. **Scan only, to begin with.** Do not build an index until a measurement asks
   for one.
4. Add `benchmarks/wide_object.lum`: a class with 32 fields, in a loop reading
   the first and the last, 500,000 iterations — plus its CPython counterpart in
   `benchmarks/python/` and its entry in `scripts/workloads.py`. This is the
   workload where a scan could lose. If it regresses against the current build,
   add the index, following `DictData`.

### Acceptance

- All 449 tests pass in both builds; `scripts/conformance`, `scripts/check-leaks`
  and `scripts/fuzz` clean.
- `method_calls` improves measurably against the parent commit's binary, and no
  workload in `scripts/benchmark.py` regresses — including the new one.
- A conformance case pins field iteration order (build an object, print
  something that walks its fields, assert both engines produce the same text in
  declaration order).
- `RUNTIME_HARDENING.md` gains an entry with the numbers.

### Do not

Do not replace the map with fixed per-class slot indices in this task. That is
the right end state, but it changes the object's representation for the tree
walker, the standard library and all eight LumiNet files at once. This task is
the step that makes that one smaller and is worth having on its own.

---

## Task 2 — A call should allocate once, not three times

### Why

`lumiere --vm --stats benchmarks/function_calls.lum` reports three allocations
per call:

1. the `std::vector<RuntimeArgument>` built from the stack,
2. the `std::vector<Value>` that `normalize_closure_arguments` returns,
3. the `std::vector<LocalSlot>` inside the frame that is then filled from it.

The values are already sitting contiguously on the VM stack. They are copied
twice and a vector is allocated and freed for each copy, on every call.

### What

Two changes, each measurable on its own — do them as two commits.

**2a. Recycle frames.** `frames` is a `std::vector<CallFrame>` with 15 uses, all
in `src/interpreter/vm/vm.cpp`: 7 `push_back`, 2 `pop_back`, 2 `back`, 3
`empty`, 1 `size`, plus the traceback walk. Popping destroys the frame's
`locals` vector and its capacity; the next call at that depth allocates again.

Keep the frames and a depth instead: `pop` clears a frame's `locals` and
`captures` (keeping capacity) and decrements; `push` reuses `frames[depth]` when
it exists. A small `FrameStack` with `push`, `pop`, `back`, `empty`, `size` and
indexed access for the traceback is the clearest shape.

**Preserve the existing invariant, and write it down:** `CallFrame &frame =
frames.back()` at the top of the dispatch loop is invalidated by any push, and
every push site is immediately followed by `break`. That is load-bearing today
and nothing says so.

**2b. Build the frame's locals from the stack.** Have
`normalize_closure_arguments` write into the frame's `locals` rather than
returning a `std::vector<Value>` that `make_call_frame` then copies. The
positional fast path (no argument is named, which the compiler guarantees
whenever it knows the callee) should move values straight from the stack into
the frame.

Named binding, optional-parameter presence flags and every error message must
stay exactly as they are — see the comment on `normalize_closure_arguments` for
why the flags follow the binding rather than the argument count.

### Acceptance

- Allocations per call, from `--stats`, drop from 3 to at most 1. State the
  number in the commit message.
- `function_calls` and `method_calls` improve against the parent commit; nothing
  regresses.
- All tests in both builds, conformance, leaks, fuzz.
- The named-argument conformance case (`tests/conformance/arguments_nommes`)
  still passes unchanged — it covers out-of-order names, skipped optional
  parameters and methods reached through a value.

---

## Task 3 — One implementation of the collection and text members

### Why

`Liste.ajouter` exists twice: in `src/interpreter/tree_walker/tree_walker_sequences.cpp`
(475 lines) and in `execute_member_call` / `execute_sequence_member` /
`execute_texte_member` in `src/interpreter/vm/vm.cpp`. So do the dictionary, set
and text members. Two implementations of one language feature is a bug
generator; this is the largest remaining instance of the thing that produced
most of the divergences already fixed.

It is also the prerequisite for anything that changes how values are
represented, because today every such change has to be made twice.

### What

One implementation, in `src/interpreter/runtime/members.cpp`, called by both
engines through `IRuntime` — which already carries everything a member needs:
`call`, `raise_runtime_error`, `values_equal`, and the collection-constraint
enforcement.

Signature along the lines of:

```cpp
std::optional<Value> call_builtin_member(IRuntime &runtime,
                                         const Value &receiver,
                                         std::string_view member,
                                         const std::vector<RuntimeArgument> &args,
                                         const RuntimeSite &site);
```

`std::nullopt` means "not a builtin member of this receiver", which is what lets
each engine fall through to its own object-and-method dispatch.

### How

Do it **one receiver family at a time**, one commit each: Texte, Liste,
ListeFixe, Ensemble, Dictionnaire, Résultat. After each, the duplicate is
deleted, not left behind.

**The engines will disagree somewhere.** They have two independent
implementations and no test forces them to match on every member. When a
difference is found:

1. Decide which behaviour is the language's rule — the question is what the rule
   should be, not which engine to copy.
2. Add a conformance case pinning it.
3. Say so in the commit message.

Do not quietly adopt one engine's behaviour because it was easier to keep.

### Acceptance

- `tree_walker_sequences.cpp` and the VM's member implementations are gone,
  replaced by one caller each.
- A conformance case per family, exercising every member with its edge cases
  (empty receiver, index at the boundary, wrong argument type, wrong arity).
- Every difference found is recorded in `RUNTIME_HARDENING.md` with the rule
  chosen and why.
- No performance regression: these are hot paths, `typed_list` and
  `dictionary_lookup` are the workloads that watch them.

---

## Task 4 — Analysis that carries the shell's earlier submissions

### Why

Every rule that resolves a name stands down in the shell, because each
submission is analyzed on its own while the interpreter carries every earlier
one. `AnalysisOptions::incremental_submission` turns off LUM-S0055 and
LUM-S0057, so a typo in the shell is found only when the line runs.

That flag exists because of a bug it was hiding: `soit base = 40` on one line
and `base = 60` on the next had been rejected since LUM-S0055 shipped — the line
silently never ran and the shell then printed the old value. **No test types two
dependent lines**, which is why it shipped at all.

### What

Let `analyze_source` start from what a previous analysis established.

`SemanticImportEnvironment` is the precedent: the analyzer already accepts
externally-supplied symbols for imported modules. The same shape works here —
an optional seed of value symbols, type symbols and callable signatures from the
previous submission's `SemanticModel`.

The REPL already keeps every accepted `Program` alive in `submissions`, so the
AST that those symbols point at outlives them. That is a precondition: write it
down where the seed is defined.

### How

1. Add the seed parameter to `analyze_source` and `analyze_semantics`, and carry
   the resulting model in `run_repl`.
2. Delete `AnalysisOptions::incremental_submission` and the two `if` statements
   that consult it. If the shell needs an exemption after this, it is a bug in
   the seeding, not a rule to switch off.
3. A submission that fails at run time must not contribute its declarations to
   the next one's environment — the binding may never have been made.

**Rejected approach, do not revisit:** re-analyzing the concatenated text of all
submissions. It re-reports earlier lines' diagnostics and numbers the new line
wrong.

### Acceptance

- A CLI test that types dependent lines: `soit base = 40`, then `base = 60`,
  then `base`, and expects `60`. Also a redefinition, a function defined on one
  line and called on the next, and a typo that must now be diagnosed with
  LUM-S0057 rather than at run time.
- `grep -r incremental_submission` finds nothing.
- The existing `ReplPreservesDefinitionsAndPrintsExpressionResults` test passes
  unchanged.

---

## Task 5 — State which token an error points at

### Why

The last recorded cross-engine difference. The two engines pick different tokens
for the same runtime failure, so the caret can sit a character apart. A spot fix
was tried once — pointing the VM at the callee's name — and it fixed one case
and broke another; it was reverted. The lesson recorded at the time: this needs
a stated rule, not a patch.

The analyzer's side is already settled: a token carries one position and it is
where the token starts.

### What

1. **Write the rule down first**, in `docs/` — one page: for each kind of runtime
   failure, which token the caret belongs on, and why. The general principle to
   start from: *the caret goes on the token that names the thing the message is
   about.* An unknown member points at the member's name, not at the parenthesis
   or the receiver; an arithmetic failure points at the operator; a failed
   argument points at that argument.
2. Enumerate the runtime error sites in both engines — the tree walker raises
   through `raise_runtime_error` in `tree_walker_runtime.cpp`, the VM builds a
   `RuntimeSite` from `chunk.locations[opcode_offset]` — and make each follow the
   rule.
3. A conformance case per family of runtime error, pinning the exact caret
   column with `expected.stderr`, not a substring match.

### Acceptance

- The rule exists as prose before any code moves.
- Conformance cases pin the column, so the next spot fix fails the build.
- No `divergence.connue` file remains anywhere under `tests/conformance`.

---

## What review will check

In roughly this order:

1. **Does it do what the brief said, and nothing else?** A commit that also
   reformats, renames, or sweeps in untracked files is harder to review than the
   change deserved.
2. **Is the claim measured?** Two binaries, medians, non-overlapping bands, and
   the numbers in the commit message and in `RUNTIME_HARDENING.md`. A stated
   percentage that cannot be reproduced is worse than no percentage.
3. **Does the comment say why, not what?** The comments this codebase keeps are
   the ones that record what was wrong before and what it cost — not the ones
   restating the line beneath them.
4. **What is the failure mode of the fast path, and is it asserted?** Every
   shortcut assumes something. The assumption belongs in an `assert` that runs in
   the sanitizer build, and in a sentence saying what breaks if it stops holding.
5. **Both engines, and the corpus.** Any change to semantics needs a conformance
   case. Any change that could not be seen by comparing the engines — because
   both would be wrong the same way — needs a case that pins the rule itself.
