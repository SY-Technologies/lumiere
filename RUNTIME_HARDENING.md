# Runtime hardening and performance

This is an implementation checkpoint, not a claim that Lumière is production-ready.
The priority is defined behavior, agreement between engines, and measured speed.

## Contracts established in this change

- `Entier` is signed 64-bit. Addition, subtraction, multiplication, division,
  and negation trap if the mathematical result is not representable. Division
  truncates toward zero. `(-2^63) % -1` is zero; division by zero still traps.
- Decimal-to-integer conversion truncates toward zero and rejects NaN,
  infinities, and values outside `[-2^63, 2^63)`. Rounding helpers check the
  rounded value before converting. Comparing against a double representation
  of `INT64_MAX` is insufficient: that representation rounds up to `2^63`.
- Numeric text casts must consume the whole input. Trailing junk no longer
  silently succeeds. They still use the existing C++ numeric parser; this
  change does not define a locale-independent numeric grammar.
- `Symbole` conversions reject UTF-16 surrogate code points.
- Text indexing, iteration, length, search positions, reversal, insertion,
  deletion, and slicing use Unicode scalar positions, not UTF-8 byte offsets.
  For example, `"aé中😀".taille()` is 4. Combining marks count separately;
  scalars are not grapheme clusters. Reversal reverses scalars, and no implicit
  normalization occurs. Full Unicode case mapping and whitespace handling
  remain unimplemented.
- Text insertion and slicing allow the position after the final scalar.
  Deletion clamps its length to the remaining text; slicing rejects an
  excessive length. Repeating empty text returns immediately, even for a huge
  repetition count.
- `pour chaque` snapshots sequence elements at entry in both engines.
  Replacing or appending elements during the loop does not change what is
  visited. This is a shallow snapshot: referenced objects remain shared.
- Importing `Texte` no longer causes its static function signatures to be used
  for text instance calls. Local bindings take precedence over builtin direct
  calls in the tree walker, matching the VM.

These are observable compatibility changes for programs relying on byte offsets,
partial numeric parsing, integer overflow, or live VM sequence iteration.

## Why the VM changes help

Previously each text-loop iteration copied the string, rescanned it to obtain
its length, and scanned again to retrieve the current character. A loop over
`n` scalars therefore did quadratic work. `ITERATION_SNAPSHOT` decodes once and
uses constant-time length and index operations afterward. Traversal becomes
linear, with O(n) snapshot storage, matching the tree walker's existing model.
This trades memory for predictable semantics and speed; a future iterator can
reduce storage only if it preserves the same observable behavior.

Call frames now store ordinary locals inline. A local is promoted to a shared
heap cell only when captured. Subsequent local accesses, sibling closures,
transitive closures, and captured methods use that same cell. This removes
per-local allocations from ordinary calls without changing closure lifetime or
mutation semantics. It does not solve reference cycles.

### Callback and value-transfer checkpoint — 2026-09-06

The VM now owns one runtime-services context per execution. Global initialization,
the entrypoint, and native callback re-entry share collection constraints. Previously
each entry created a fresh context: a callback could insert text into a declared
`Liste[Entier]`, and constraints created in a callback or initializer disappeared
on return. Bound native methods created in callbacks now retain a valid context
for the remainder of that execution. This does not extend them beyond `VM::run`.

Direct calls now resolve captured bindings before falling back to globals. This
also removes a duplicated lowering path. The new nested-callback regression
exercises a bound method through its captured name and after both callbacks return.

Consumed operand-stack values now move into argument vectors, collection literals,
and return values. The source stack entries are discarded immediately afterward;
local variables and shared objects are not moved out of their owners. Ordinary
returns also skip failure-trace construction, which previously copied even large
text values despite having no error trace to attach. Failure returns retain their
existing tracing behavior.
Integer stack extraction uses the integer payload directly; benchmarks showed
that the general variant-move path slightly regressed arithmetic loops.

Text methods now derive semantic signatures from the existing native module
manifest, with the explicit receiver omitted. Chained calls, bound methods,
optional arguments, collection return types, and conversion-result types work
without importing `Texte`. Previously most such calls inferred `Universel`,
which made valid typed assignments and function arguments fail analysis.
Incorrect argument types and arity are now diagnosed before execution; named
arguments remain disallowed, matching the text runtime contract.

The UDP regression test now keeps its native endpoint bound, lets the language
endpoint bind port zero, and waits for a readiness datagram before replying.
This removes a close-and-rebind port race and a fixed-delay/20-packet startup
workaround. It exercises UDP send as well as receive and passed 50 consecutive
Release runs on the validation host.

### Collection-ownership checkpoint — 2026-09-07

Collection constraints now live on their collection allocation instead of in
raw-pointer-keyed runtime maps. Previously a destroyed typed list could leave
behind an entry that incorrectly restricted a new, untyped list allocated at
the same address. This was reproduced in both engines. Metadata now dies with
the collection, remains shared by aliases, and survives the annotating runtime.
The lifetime rule also covers fixed lists, dictionaries, and sets.

Mutation checks read the owned metadata directly, avoiding registry lookups.
Optional metadata increases each collection object's size, including untyped
collections; it removes separate registry entries for typed collections. Peak
memory has not been measured, so this is not a blanket memory-saving claim.

That checkpoint changed ownership, not re-annotation policy: annotating an
alias can still replace an existing constraint with a weaker one. Nested-type
propagation also needs a dedicated cross-engine audit. Neither issue is solved
by keeping metadata alive, and reference cycles remain unresolved.

Validation: all 382 tests passed in Release and ASan/UBSan builds, including
loopback networking and the address-reuse regression in both engines. Sanitizer
validation enabled stack-use-after-return checks and disabled leak detection.

### Collection re-annotation checkpoint — 2026-09-08

Both engines now use the same conservative contract-merge rule. Repeating a
contract preserves it; an incoming `Universel` parameter preserves the existing
concrete parameter; an existing `Universel` parameter may be refined after the
normal value check. Distinct concrete parameters are rejected instead of
replacing each other. Fixed-list lengths must agree. Dictionary key and value
parameters are checked before committing either change.

For example, exposing a `Liste[Entier]` through a runtime `Liste[Universel]`
annotation no longer permits appending text. An empty integer list cannot be
re-annotated as a text list merely because it contains no counterexample yet.
The CLI analyzer already rejects many such aliases; the new tests exercise the
runtime APIs directly, where embedders and native modules also attach contracts.
This does not relax static assignability rules.

This policy deliberately does not attempt structural subtyping or intersections
of different concrete union/generic expressions. Such re-annotations are
rejected, even where a more advanced type relation could prove them safe.
The merge is atomic for a single collection's metadata, not an entire nested
object graph. Recursive annotation propagation and type-alias resolution still
need a cross-engine audit. Reference cycles remain unresolved.

Validation: all 387 tests passed in Release and ASan/UBSan, including loopback
networking. Stack-use-after-return detection was enabled; leak detection remains
disabled. No speedup is claimed for this checkpoint. The merge runs when
attaching annotations; element mutation checks retain their existing path.

### Nested collections and aliases — 2026-09-08

The VM now attaches contracts recursively to existing collection contents and
to collection values inserted later. Previously it checked nested contents only
at the boundary: an alias could subsequently append text to a child of a
`Liste[Liste[Entier]]`. Regression tests reproduce both declaration-time and
insertion-time gaps for lists and dictionary values, then verify both engines
allow integer inserts and reject text inserts.

The tree walker now resolves type aliases before storing collection metadata.
For example, `type Nombre = Entier` and `type Nombres = Liste[Nombre]` produce
the concrete `Liste[Entier]` contract. Previously the collection alias could
pass validation without installing a contract, or leave an unresolved element
alias in mutation checks. Resolution detects alias cycles. CLI tests exercise
both forms in addition to the direct runtime tests.

Collection alternatives in unions are now annotated through the first matching
alternative, rather than treating an entire union as a generic argument string
or ignoring its contract. This is conservative: where multiple collection
alternatives match, it retains the selected concrete contract rather than
allowing the collection to switch alternatives after mutation.

VM annotation parsing owns its type text before modifying metadata; this avoids
invalidating string views when the incoming annotation belongs to a collection
being re-annotated. Scalar values exit the propagation path immediately.

Remaining work includes imported aliases whose definitions reference private
module-local aliases, contracts inside result payloads, and transactional
annotation of entire object graphs. Reference-cycle collection is still absent.
No speedup is claimed for this correctness checkpoint.

Validation: all 391 tests passed in Release and ASan/UBSan, including loopback
networking. Stack-use-after-return detection was enabled; leak detection remains
disabled. The working-tree diff passes whitespace checks.

### Result payloads and imported aliases — 2026-09-09

Both engines now follow the active payload when attaching a `Résultat`
annotation. A list wrapped in `Succès` therefore retains its element constraint
through existing aliases, including when the result is itself stored in a list.
Regression tests failed in both engines before the fix. The CLI test consumes
the result explicitly, permits a valid integer insert, then rejects a text
insert through the original alias. Result origins and traces are unchanged.

The VM linker now carries public type exports separately from runtime value
exports. Selective imports no longer try to load type aliases as globals.
Both engines resolve exported aliases in the declaring module before sharing
them with the importer. A public `Liste[Interne]`, where private `Interne` means
`Entier`, stays an integer list even if the importer defines `Interne = Texte`.
The private alias itself is not imported.

A shared resolver preserves the type-expression structure, expands nested
aliases, and detects cycles. Tests cover selective, renamed, namespace-qualified,
and transitive re-exports, both valid inserts and rejected mutations. A separate
test checks repeated sibling aliases without mistaking them for recursion and
rejects a real alias cycle.

This completes module-level alias imports for the tested collection cases, not
a complete module-scope audit. Function-local type imports and module-local
nominal types still need dedicated conformance coverage. Whole-object-graph
annotation is not transactional, and reference-cycle collection remains absent.
No speedup is claimed for these correctness changes.

Final validation: all 394 tests passed in Release and ASan/UBSan, including
loopback networking. Stack-use-after-return detection was enabled; leak
detection remains disabled. The working-tree diff passes whitespace checks.

### Alias assignments and function-local imports — 2026-09-09

Tree-walker variable and parameter bindings now store resolved annotation names.
Previously `type Nombre = Entier; soit valeur: Nombre = 1` accepted declaration
but rejected the valid subsequent assignment `valeur = 2`: assignment checking
read the unresolved name from the environment. Resolution now happens when the
binding is established, not on every reassignment. Tests cover scalar,
collection, class, parameter, and imported aliases, as well as invalid list
replacement through the direct runtime API.

The VM now receives type exports for imports inside functions as well as at
module level. Each function lowerer owns its compile-time alias table; block
entry saves the table and block exit restores it. Nested functions receive the
alias context at their declaration. This adds no runtime scope-table operations
to the VM. Tests cover selective, renamed, namespace-qualified, and transitive
imports inside functions, plus block-local shadowing and nested functions.

The tree walker's general alias scope/capture model is not changed by the
binding fix. Block-local aliases can still affect later lookups through its
shared alias table, and module-local nominal type identity needs further work.
Those are separate from the now-working function-local import cases. No new
speedup or reference-cycle fix is claimed.

Validation completed on 2026-09-10: all 396 tests passed in Release and
ASan/UBSan, including loopback networking. Stack-use-after-return detection
was enabled; leak detection remains disabled. The diff passes whitespace checks.

### Tree-walker lexical alias environments — 2026-09-10

Aliases now live in the existing lexical `Environment` chain instead of one
interpreter-wide map. Leaving a block restores its outer aliases automatically,
including stack unwinding. Functions already retain their declaring environment,
so returned closures and imported functions now retain their type-alias scope as
well. Alias tables are allocated lazily: a scope with no alias declarations or
type imports stores only a null owning pointer, not an allocated map.

Alias dependencies resolve in the environment where the alias was defined.
An inner `Base = Texte` therefore cannot change an outer `Nombre = Base` whose
defining environment has `Base = Entier`. Cycle detection tracks alias bindings,
not merely their names, so equal spellings in different scopes are distinct.
Return annotations are resolved against the function's declaring environment
before return handling restores the caller's environment.

Tests cover block restoration, returned functions called after the caller changes
an alias of the same name, definition-scope dependencies, alias cycles, and an
imported function whose private parameter/return alias is declared later in its
module and conflicts with an alias in the importer. Module-local nominal type
identity, method declaration contexts, and cross-engine behavior for more complex
chained-alias shadowing remain separate audit targets. Reference cycles are not
collected, and no speedup is claimed.

Validation: all 399 tests passed in Release and ASan/UBSan, including loopback
networking. Stack-use-after-return checks were enabled; leak detection remains
disabled. The diff passes whitespace checks.

### Class declaration environments — 2026-09-13

Tree-walker classes now retain their declaring environment. Inherited member
lookup reports the class that actually declared the member, so direct and bound
method calls capture that class's environment, not the caller's. Imported methods
therefore keep their private module variables and annotation aliases. Methods
from returned local classes also retain their captured variables.

Field annotations resolve in the declaring class's environment during
construction and assignment. Constructor argument expressions still evaluate in
the caller's environment. Regression tests exercise both engines, including an
imported inherited method with conflicting caller aliases and variables, a
returned local class, and valid/invalid field assignments after alias shadowing.
Strong environment ownership does not collect reference cycles; no new speedup
or leak-freedom claim is made.

Validation: all 402 tests passed in Release and ASan/UBSan when the suites ran
sequentially, including loopback networking. Stack-use-after-return detection
was enabled; leak detection remains disabled. The diff passes whitespace checks.

Two confirmed issues remain separate from this fix:

- Importing two modules' `Objet` classes as `Premier` and `Second`, constructing
  `Premier()`, and testing `est Premier` / `est Second` currently prints
  `faux` / `faux` in both engines. Nominal checks need declaration identity,
  consistently carried through imports and annotations, rather than spelling.
- The analyzer rejects named constructor arguments for fields inherited from an
  imported class. The field-scope regression therefore exercises the runtimes
  directly; it is not evidence that this imported-constructor case works.

### Imported constructor contracts — 2026-09-14

Semantic exports now include public classes' analyzed constructor signatures.
Subclass analysis uses that signature when the parent comes from an import,
instead of silently losing inherited fields because no local parent AST exists.
Field order and optionality survive export. Resolved parameter types retain
private aliases and are recursively re-interned in the receiving analyzer's
type table; simply sharing types across analyzers fails pointer-based equality.
Nominal parameter names are qualified during transfer, including inside generics.

CLI regressions cover selective and renamed imports, transitive imported
inheritance, multiple local inheritance levels, positional and named arguments,
invalid argument names/types, and private generic/union aliases. This resolves
the imported inherited-constructor analysis issue recorded above. Runtime
cross-module nominal identity remains unresolved: fixing only `est` would leave
parameter, return, field, and collection contracts inconsistent. No runtime
speedup or cycle-collection improvement is claimed.

Validation: all 404 tests passed in Release and ASan/UBSan, run sequentially
with loopback networking enabled. Stack-use-after-return detection was enabled;
leak detection remains disabled. Whitespace checks pass.

### Runtime class declaration identity — 2026-09-14

User-defined classes now have a runtime type identity separate from their display
name. It identifies the lexical declaration using canonical source path, source
offset, and name. Paths are encoded to avoid interfering with generic and union
syntax. This is an internal representation, not a portable serialization format
or ABI. Repeated instances of one declaration share its type identity.

The VM carries identities through class descriptors, imports, aliases, and nested
annotations. A class import binds both its type identity and its callable value;
type-only aliases still bind no runtime value. The tree walker resolves class
references in their lexical environments, including module-private aliases and
field contracts. Root/module environments record their source path so a class
created inside an imported function is not identified using the caller's path.

Regression coverage now includes two modules declaring `Objet`, renamed and
namespace imports of the same declaration, exported class aliases, typed factory
returns, inherited identity, field/parameter/list rejection of the other module's
class, local class shadowing, and function-local imports. The same tests exposed
and fixed VM namespace constructor dispatch: `Module.Boite(...)` now invokes the
class constructor instead of rejecting the member as non-callable. Normal type
diagnostics retain display names rather than exposing encoded source paths.

This closes the reported file-backed class-identity reproduction, not the entire
nominal-type audit. Interfaces and native classes still use their existing naming
rules. Anonymous compilation units, REPL redefinition/hot reload, and semantic
analysis's module-name identity rules require further work. No speedup or
reference-cycle collection claim is made.

Validation completed on 2026-09-15: all 406 tests passed in Release and
ASan/UBSan, including loopback networking, with the suites run sequentially.
Stack-use-after-return detection was enabled; leak detection remains disabled.
The final sanitizer run took 19.90 seconds. Subsequent Git/toolchain checks are
blocked by the host's unaccepted Xcode license; no license was accepted by the
agent. Whitespace checks passed before that host-state change.

### Runtime interface declaration identity — 2026-09-15

User-defined interfaces now carry the same declaration identity scheme as
classes. Runtime implementation maps use that identity, so equal interface names
from different modules no longer imply compatibility. Identities propagate
through VM descriptors, renamed/namespace imports, annotations, and collection
contracts. The built-in `Erreur` marker retains its existing shared identity.

Implementation clauses need interface values, while annotations need type
identities. Alias resolution now distinguishes those two uses. Namespace-qualified
implementation clauses load the actual namespace member in both engines, and
the tree walker validates the resulting interface's required methods.

CLI regressions exercise same-named interfaces in two modules, renamed and
namespace references, local aliases, inherited implementation, valid calls and
list insertions, and runtime rejection of incompatible values or missing
required methods. Type-only exported aliases without a corresponding imported
runtime interface binding remain outside this implementation-clause fix. Native
type identity, anonymous units/REPL/hot reload, and semantic module identity
remain audit targets. No runtime speedup is claimed.

The Xcode license blocker was resolved by the user. Both build directories were
reconfigured to the installed `MacOSX.sdk`, replacing their stale `MacOSX26.5.sdk`
cache paths; Release and sanitizer configurations were preserved.

### Type-only interface implementation aliases — 2026-09-15

Public type aliases that resolve to nominal declarations now carry a separate,
hidden runtime-value export. This lets `réalise Alias` retrieve the actual
interface descriptor without making ordinary type aliases into source-level
values. Selective, renamed, namespace-qualified, function-local, and transitive
alias imports use the same declaration identity in both engines.

The tree walker records the nominal value beside each exported alias after its
closed type has resolved. The VM linker carries the corresponding global symbol
beside the type export and binds it only for runtime type operations. Aliases to
structural types continue to have no runtime value. Same-named interfaces from
different modules remain incompatible.

Regression coverage includes direct and namespace aliases, a re-export through
another module, a function-local import, and a negative identity check. All 408
tests passed in Release and ASan/UBSan builds. Stack-use-after-return checks were
enabled and leak detection remained disabled. Loopback networking tests were
skipped where the sandbox denied socket support. The diff passes whitespace
checks.

### Incremental nominal identity — 2026-09-15

Each tree-walker incremental submission now has a distinct declaration-unit
identity, separate from its diagnostic source path. Previously two REPL inputs
could place same-named local classes at the same byte offset; their encoded
runtime identities then collided, so an instance from one declaration passed a
nominal check against the other.

Functions capture their declaration-unit identity with their lexical
environment. Classes retain it for methods that are bound after later REPL
submissions have executed. Entering a function restores that identity for local
class and interface declarations. File-backed modules continue to use their
canonical source path, and diagnostic paths remain unchanged.

Tests cover the actual CLI REPL and direct incremental embedding with an empty
source path. Both construct equal-spelling classes in separate submissions at
equal offsets and verify that a cross-instance check is false. The VM currently
runs one closed linked program per execution, so it has no cross-submission
value lifetime to disambiguate.

All 409 tests passed in Release and ASan/UBSan builds. Stack-use-after-return
checks were enabled and leak detection remained disabled. Loopback networking
tests were skipped where the sandbox denied socket support.

### Nominal identity completion — 2026-09-17

Native nominal types now use stable module-qualified runtime identities instead
of display-name equality. Built-in type exports are available to selective,
renamed, and namespace imports in both engines. Native instances and errors use
the same encoded identity as their exported annotations, while diagnostics strip
the encoding and retain source-level names. Tests cover `Temps` values, a user
class with the same display name, and a renamed `Fichier` error type.

Semantic type exports now retain the resolved owner of public aliases. A type
re-exported through another module therefore remains the original declaration's
type instead of becoming an unresolved bottom type or being rebound to the
relay module. The analyzer now rejects assignments between equal-spelling
classes from different original modules and reports readable qualified names.

VM aliases are closed over their lexical definition scope. An inner alias can
no longer reinterpret an outer chained alias by shadowing one of its dependency
names. Nominal aliases separately retain the runtime class or interface value
needed by `réalise`, so closing annotation identity does not turn a valid
interface alias into a missing global.

Validation: all 412 tests passed in Release and ASan/UBSan builds; 11
socket-dependent tests were skipped because the sandbox denied their bindings.
Stack-use-after-return checks were enabled. Leak detection remains disabled
pending cycle collection. The working-tree diff passes whitespace checks.

### Dictionary keys and hash indexing — 2026-09-17

A dictionary was an association list: every write appended, so a literal could
hold two entries under one key. `{"a": 1, "b": 2, "a": 3}` had size three,
lookup returned the first `"a"`, and `retirer("a")` uncovered the second. The
same shape reached `Dictionnaire[Texte, Texte]` values built by LumiNet, where a
repeated header name produced a repeated key. Insertion, lookup and removal now
go through `DictData`, which holds at most one entry per key; reassigning a key
overwrites in place and leaves it in its original position.

Contracts fixed by this change:

- Keys compare by `Value::operator==`, which both engines now share instead of
  each carrying its own copy. `Entier` and `Décimal` never compare equal, so `1`
  and `1.0` are two keys; `0.0` and `-0.0` are one. Text compares by bytes, with
  no normalization. `Liste`, `Dictionnaire`, objects, functions, classes and
  interfaces compare by identity, so two lists with equal contents are two keys.
- A `ListeFixe` compares by content and is now immutable, so it can stay equal
  to a key it was stored under. Element assignment is refused in both engines,
  and the element-type checks that guarded those writes are gone.
- A non-number is refused as a key. It is not equal to itself, so its entry
  could never be found again, and `"nan".en_decimal()` reaches that value.
- Entries keep insertion order. Removing a key and inserting it again puts it
  at the end.

`value_hash` mirrors `operator==` case for case and is the only thing the index
consults; a unit test asserts that equal values across every runtime type hash
equally, because a hash that disagrees with equality turns faster lookup into
silently missing entries. The index is open-addressed with linear probing over
positions into the entry vector. Below eight entries there is no index at all
and lookup scans, so small dictionaries pay no allocation. Removal shifts later
positions, so it rebuilds the index rather than patching it; the vector erase it
follows is already linear. The entry vector became private for that reason —
an index beside a publicly mutable vector is a trap.

Measured on the validation host below, VM backend, five runs after one warm-up,
inserting and then reading 50,000 text keys: 10.973501 s (10.960202–11.080839)
before, 0.057346 s (0.057151–0.057710) after. The other five workloads moved by
between 1% and 11% in the same direction, which is within this host's noise and
is not claimed as an improvement. Peak memory was not measured; the index adds
about one machine word per 0.75 entries above the threshold.

## Verification and measurement

```sh
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build_release --parallel 4
ctest --test-dir build_release --output-on-failure --parallel 4
python3 scripts/benchmark.py build_release/lumiere --runs 7
python3 scripts/benchmark.py build_release/lumiere --backend tw --runs 7
```

The benchmark runner verifies stdout and exit status, rejects unexpected stderr,
warms up once, and reports median/min/max of fresh-process runs. Timings include
startup, parsing, analysis, compilation, execution, and exit. Compare Release
binaries with the same compiler and options; do not compare a Debug baseline
against an optimized build. The script accepts multiple binaries for comparisons.
Recorded results and environment details are in the
[benchmark report](./benchmarks/README.md).

Run Release and sanitizer suites sequentially: CLI fixtures currently share
temporary source/output paths between builds. Concurrent suites can overwrite
or remove each other's files even though parallel tests within one suite pass.

Linux CI additionally runs address, undefined-behavior, and float-cast-overflow
sanitizers. Undefined behavior is fatal. Leak detection is explicitly disabled
until reference cycles are handled; a passing sanitizer run is not evidence
that the runtime is leak-free. Example CLI tests now isolate their output files
so parallel CTest runs cannot overwrite each other's results.

## Next engineering priorities

1. **Runtime lifetime and type invariants.** Collection constraints now belong
   to their allocations, with conservative non-weakening re-annotation rules.
   Next audit native nominal identity, semantic module identity, and
   chained-alias shadowing,
   then decide whether richer mutable-generic type relations are needed.
   Implement
   cycle collection with explicit roots and allocation accounting, then enable
   leak checks. Long-running applications need bounded memory behavior.
2. **Set semantics and construction.** Dictionaries are specified and indexed.
   `Ensemble` is not: it has a type, a runtime representation and constraint
   handling, but nothing constructs one, so no program can hold a set. Decide
   whether to give it a literal and the same key contract as a dictionary, or
   to withdraw the type until it exists.
3. **One conformance corpus.** Run language and stdlib fixtures under both
   engines, comparing values, errors, evaluation order, and side effects.
   Fuzz UTF-8, parser inputs, numeric boundaries, and malformed bytecode.
4. **Profile representative workloads.** Add allocation and instruction counts,
   then target value copies, temporary-slot lifetimes, and native-call argument
   construction. Do not start a JIT or replace the value representation based
   on a single microbenchmark. Track peak memory as well as time.
5. **International text support.** Choose explicit normalization, grapheme,
   collation, case-folding, and locale contracts. Use maintained Unicode data
   rather than hand-written accent tables. Preserve the scalar APIs so their
   complexity and results remain predictable.
6. **Adoption infrastructure.** Publish a versioned executable specification,
   compatibility policy, reproducible package workflow, editor diagnostics,
   formatter, and standard test runner. Exercise installers and stdlib I/O on
   supported operating systems. Prefer a small dependable API over rapid API
   expansion; add interoperability only with a documented ownership boundary.

These are separate milestones with tests and release criteria, not a mandate
for a wholesale rewrite. Mainstream adoption cannot be guaranteed by runtime
speed alone.
