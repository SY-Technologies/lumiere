# Runtime hardening and performance

This is an implementation checkpoint, not a claim that Lumière is production-ready.
The priority is defined behavior, agreement between engines, and measured speed.

The project's performance target was Go. It is now the interpreter tier: ahead
of CPython on the benchmark suite, measured by `scripts/compare-languages.py`.
The Go target was withdrawn on evidence, not on preference — the measurements and
the reasoning are under "Where the VM's time actually goes" below. The short
version is that Go compiles ahead of time to native code and an interpreter that
dispatches one instruction at a time is 10x to 100x off compiled code however
well it is written, so matching Go means building a native backend and a precise
collector rather than optimizing this one. Nothing on the current roadmap is
wasted if that decision is revisited: the object model in priority 1 and a typed
LIR are prerequisites for a backend as much as for the interpreter.

## Contracts established in this change

- `Entier` is signed 64-bit. Addition, subtraction, multiplication, division,
  and negation trap if the mathematical result is not representable. Division
  truncates toward zero. `(-2^63) % -1` is zero; division by zero still traps.
- Decimal-to-integer conversion truncates toward zero and rejects NaN,
  infinities, and values outside `[-2^63, 2^63)`. Rounding helpers check the
  rounded value before converting. Comparing against a double representation
  of `INT64_MAX` is insufficient: that representation rounds up to `2^63`.
- Numeric text casts must consume the whole input, leading whitespace included:
  `" 1.5"` used to be accepted while `"1.5 "` was refused, because `std::stod`
  skips leading blanks and then counts them as consumed. Decimal parsing is now
  `std::from_chars`, which is locale-independent, so the language's own numbers
  no longer depend on the environment's decimal separator.
- A `Décimal` is printed as the shortest text that reads back as the same value
  and as the same type. Stream formatting defaults to six significant digits, so
  `123456789.125` came out as `1.23457e+08` and `0.1 + 0.2` as `0.3`: the runtime
  reported a number it had not computed, and printed output could not be pasted
  back into a program. `Maths.tan(Maths.pi / 4)` now prints `0.9999999999999999`,
  which is what it is.
- A whole-numbered `Décimal` keeps its point: `2.0` prints `2.0`, not `2`.
  `Entier` and `Décimal` are distinct types that are not even equal as dictionary
  keys, so printing `2.0` as `2` made two values that are not equal print
  identically, and the text no longer read back as the type it came from. An
  exponent already marks a value as a `Décimal`, so `1e10` prints `1e+10`.
- A decimal literal whose magnitude the type cannot hold is refused, with a
  source location, rather than rounded to zero or to an infinity — the same
  principle as trapping integer overflow instead of wrapping. A subnormal such
  as `5e-324` is representable and is accepted; it used to surface as
  `erreur: stod`, a C++ exception name reaching the user.
- `infini`, `-infini` and `non_nombre` print under the names the language gives
  them rather than C's `inf` and `nan`.
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

### One wording for every runtime diagnostic — 2026-09-17

The two engines detect the same conditions in separate code, and each owned its
wording. A corpus of twenty-six failing programs showed fifteen of them reported
differently: the same division was "division par zero" from the VM and "division
par zero interdite" from the tree walker, an unknown member named the member in
one and blamed the receiver in the other ("acces membre impossible: la cible
avant '.' doit etre un Objet"), and a failed element-type check named the
received type in one and not the other.

Every message a program can reach now lives in `diagnostics/runtime_messages.hpp`
and both engines call it. An out-of-bounds index names the index, the length and
the collection instead of saying only "indice hors limites". A dictionary write
that violates its declared types reports the key and the value separately, as
the tree walker already did. A cross-engine test runs that corpus and fails if
the two ever disagree again.

Runtime messages also carry their accents now. The header was "erreur
d'execution", and the prose under it was written without them while the
analyzer's diagnostics had them, so one program could produce "clé" and "cle"
in the same session. The sweep covered message text only: dispatch keys, member
names and identifiers inside diagnostics are untouched, which is why
`lire_decimal` and `en_liste_fixe` still read as they are written in source.

### Result types for builtin collection members — 2026-09-17

The analyzer typed `taille` on a builtin collection and nothing else, so every
other member call was `Universel`. `Universel` is assignable to a declared type
in neither direction, so no collection member call could initialize one:
`soit t: ListeFixe[Entier, 3] = notes.en_liste_fixe(3)` was rejected even though
`docs/implemented-language-overview.md` documents that exact line, and the
runtime has carried the element type through that conversion all along. The unit
fixtures did not catch it because they drive the tree walker directly, without
analysis; only the CLI path ran the analyzer.

Member results now follow the receiver: `Dictionnaire[K, V].clés()` is a
`Liste[K]`, `Liste[T].en_ensemble()` an `Ensemble[T]`, `Ensemble[T].union` an
`Ensemble[T]`, `Liste[T].retirer_a` a `T`. `ListeFixe` carries its length in its
type, so `en_liste_fixe(n)` is read at the call site, where the literal is
visible. Chained calls work because each link now has a type.

Parameters stay `Universel`. Element and key types are enforced at run time
against the constraint carried by the allocation, which sees through aliases the
analyzer cannot follow, so declaring them here would reject programs the runtime
accepts. That is a separate change, listed as a priority below.

This is the first checkpoint that makes a mistyped result a compile error:
assigning `Dictionnaire[Texte, Entier].clés()` to a `Liste[Entier]` now reports
`attend Liste[Entier]; reçu Liste[Texte]` instead of running.

### Where the VM's time actually goes — 2026-09-17

Earlier checkpoints compared Lumière against its own history, which can only
show that a change helped. The stated target is Go, so the gap has to be
measured against compiled code. `scripts/compare-languages.py` runs the same
1,000,000-iteration integer loop through whatever is installed; on the
validation host below, seven runs after one warm-up:

| Runtime | Median | Versus C |
| --- | --- | --- |
| C, `-O2` | 0.30 ms | 1× |
| CPython 3.10 | 91.62 ms | 310× |
| Lumière VM | 127.04 ms | 430× |
| Lumière tree-walk | 444.84 ms | 1505× |

Process startup is 0.9 ms, so almost all of that is execution. Two things follow.
The VM is 430× a compiled baseline, and it is 1.39× *slower* than CPython, which
is the reference point for a slow interpreter. Being behind CPython is not a
property of interpreters in general; it is specific to this one.

Most of the difference is the value representation. `Value` is a
`std::variant` with a `std::string` alternative: 48 bytes, neither trivially
copyable nor trivially destructible, so every push, pop, local read and local
write runs a generated fourteen-way visitor instead of moving bytes. Modelling
one iteration of that loop — the same pushes, pops, local reads and local writes
the VM performs — costs 50.0 ms per million iterations with the current `Value`
and 13.9 ms with a 16-byte trivially copyable tagged value. That is about 40% of
execution, and removing it would land the loop near 90 ms: level with CPython,
still 300× C.

One concrete waste was found and removed rather than modelled. The dispatch loop
read `chunk.locations[ip]` on every instruction, a bounds check and a struct
copy, to build a source location that only six call opcodes use. Computing it
where it is used took the loop from 138.9 ms to 127.0 ms, 8.6% less, with no
change in behaviour.

Hoisting the `try` out of the per-instruction loop was also tried, on the theory
that re-entering the region stopped the compiler from keeping state in
registers. Measured over twenty-one runs it was 128.2 ms against 127.0 ms —
neutral, and it was reverted. Zero-cost exception handling means the
non-throwing path was never paying for the entry. It is recorded here so the
idea is not tried a third time.

The conclusion is a sequencing one, and it changes the order of the priorities
below. A trivially copyable `Value` cannot hold a `shared_ptr`, so the small
value depends on the runtime owning its heap objects through its own reference
counting — which is the object model that cycle collection needs anyway. Bounded
memory is therefore not only a reliability milestone; it is the prerequisite for
the representation change that the speed depends on.

None of this reaches Go. An interpreter that dispatches one instruction at a
time is 10× to 100× off compiled code even when it is written well, and the
measurements above put the achievable interpreter target near 90 ms against C's
0.30 ms. Matching Go means compiling to native code, ahead of time or through a
JIT, and that is a decision about what the project is, not an optimization.

### A verifier, and what it bought — 2026-09-18

The interpreter tested the instruction pointer against the code size on every
byte it read, which is two or more comparisons per instruction on the hottest
path in the runtime. Those tests are now a single pass: `verify_module` walks
each function once before anything runs and proves every opcode known, every
operand present, every table index in range, every jump landing on the first
byte of an instruction, and every function ending where it cannot fall past its
own code.

Jump boundaries are a new guarantee. The interpreter only checked that a target
was inside the chunk, so a jump into the middle of an instruction would have
been decoded as one. That is now refused, along with unknown opcodes, truncated
operands and indices past their table — at load, naming the function and the
offset, rather than part-way through a run.

The interpreter's memory safety now rests on that pass instead of on per-read
tests, so it is tested directly: nine cases build modules by hand and check each
rejection, and the reads assert their bound in the Debug and sanitizer builds,
where the whole suite runs with the assertions live.

This was worth 25.9% on the integer loop, 16.7% on function calls and 14.8% on
Unicode traversal.

**The revised target is met.** On the validation host, nine runs: CPython at
80.94 ms and the VM at 78.50 ms on the same 1,000,000-iteration loop. The four
changes in this round — a lazily built source location, shared text buffers, one
type-erased handle in place of fourteen variant alternatives, and verified
bytecode — took the loop from 138.9 ms to 78.3 ms, 44% less, each measured
separately and each kept or reverted on its own evidence.

The next lever inside the interpreter is still the object model: a trivially
copyable value would remove the last third of the copy cost the variant used to
charge, and the same work is what bounded memory needs.

### The runtime owns its heap values — 2026-09-18

Every heap value was owned by a `std::shared_ptr`, which keeps its count in a
separate control block and updates it with an atomic read-modify-write. Nothing
in the runtime creates a thread, so that atomic was paid on every copy of every
value for a guarantee nothing needed, and releasing a reference called out of
line where a compare-and-branch would do.

The count now lives in the object. `RefCounted` carries it, every heap type
derives from it, and `Ref<T>` is the handle. `Value` holds one `Ref<RefCounted>`
and downcasts on access, so it is 24 bytes rather than 32. Ownership and
destruction order are unchanged; only the mechanism moved.

Every workload that touches a heap value is 4% to 8% faster and the integer loop
is unchanged, which is the opposite shape to what an isolated model predicted —
see the benchmark notes, where that mistake is written down, because it is a
lesson about models rather than about values.

**Leak detection works now, for everything except cycles.** This is the first
part of T2 to land. `scripts/check-leaks` runs nine example and benchmark
programs under a leak-detecting sanitizer build, and all nine are clean: a
value's last reference destroys it, deterministically. A program that builds a
reference cycle still retains it, which is exactly the remaining work, and it is
now the *only* remaining source of retained memory rather than one of several.
That also makes the cycle collector testable: anything the collector fails to
reclaim will now show up on its own rather than in a crowd of ordinary leaks.

### Cycle collection — 2026-09-18

Reference counting frees everything whose last reference goes, and nothing whose
references only point at each other. A cyclic stress program — 200,000 pairs of
objects, each holding the other, all discarded — grew to 129 MB of resident
memory. It now peaks at 5.9 MB.

The collector is Bacon and Rajan's synchronous cycle collection. It suits a
counted runtime for a reason worth stating: it never needs to know where the
roots are. A tracing collector must enumerate every root, including values held
only in a C++ local during a native call, and the tree walker keeps values in
C++ locals everywhere. This one works from the counts instead, so an unknown
reference is simply a reference like any other, and the tree walker needs no
shadow stack.

The contract sits on `RefCounted::trace_references`, which must report every
reference an object holds, exactly once. The asymmetry matters: reporting too
few leaves a cycle uncollected, which is only a leak, while reporting an edge
that is not held can free something still in use. That is what makes the
collector safe to grow one type at a time.

Two edges are deliberately unreported, and both are visible in what still leaks:
the captures inside a native handler's `std::function`, which C++ gives no way
to enumerate, and the tree walker's environments and function bodies, which are
not counted objects yet.

Collection runs at a loop's back edge and at a function return — points where no
object is part-way through being updated — and once more after the backend is
destroyed, which is the first moment a program's own globals are gone. The check
for whether a collection is due is inline, two loads and a comparison, because a
call there would have cost more than the collection saves.

Three bugs were found by the tests rather than by reading:

- The sweep subtracted each cycle edge twice. `mark_grey` had already removed
  the internal references from the counts, so clearing the real references
  removed them again and freed an object the sweep still held a pointer to. The
  counts are now restored before the links are broken.
- An object that lost its last reference while buffered as a cycle candidate was
  left for the next collection rather than freed, because removing it from the
  buffer was a linear scan. It now remembers its slot and leaves in constant
  time.
- Buffering every surviving decrement cost 16.7% on dictionary workloads, almost
  all of it text: a value that holds no references can never be in a cycle, so
  text is marked acyclic and skips the candidate buffer entirely. That brought
  the collector's whole cost to between nothing and 5%, mostly 1% to 3%.

**Leak detection is on for the VM.** `scripts/check-leaks` runs nine programs
under both engines; the VM is clean on all of them, cycles included. The tree
walker still retains its environments on every program, including the smallest:
an `Environment` owns its parent by `shared_ptr`, and a function's closure owner
is the environment holding that function, so the global environment and
`principal` are a two-node cycle in any program at all. That is pre-existing —
leak detection being disabled is what kept it invisible — and it is the next
piece of T2: making environments and function bodies counted objects would bring
them within reach of the collector.

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
sanitizers. Undefined behavior is fatal. Leak detection is now enabled: the
complete suite of 441 tests passes with `ASAN_OPTIONS=detect_leaks=1`, and
`scripts/check-leaks` runs nine programs under both engines and requires every
one of them clean. A sanitizer run is therefore evidence about leaks again.
Sanitized builds get a longer per-test timeout, because a handful of CLI tests
spawn a dozen subprocesses each and every one of them runs several times slower
under a sanitizer. Example CLI tests isolate their output files so parallel
CTest runs cannot overwrite each other's results.

## Reference cycles

Collection is Bacon-Rajan, run from the reference counts themselves. That
choice is what makes it usable here: a tracing collector needs to enumerate its
roots, and the tree walker holds `Value`s in C++ locals throughout, so there is
no root set to enumerate. Counting-based collection does not need one.

The safety property the whole design rests on is that tracing is allowed to be
incomplete but never wrong. Reporting fewer edges than an object holds leaves a
cycle uncollected -- a leak. Reporting an edge the object does not hold frees
something still in use. Everything below follows from keeping errors on the
first side.

Both engines are now covered. `Environment`, the three runtime body types and
`RuntimeModuleState` are counted objects that report what they hold, and
capture cells are counted too, so a closure capturing itself is a cycle the
collector can see. Collection runs on loop back edges and function returns in
both engines, so a loop that builds cycles stays bounded rather than growing
until the program ends: a 200,000-pair stress program peaks at 5.9 MB under the
VM and 5.2 MB under the tree walker, against 129 MB and 135 MB before.

The collector's state is **per-thread**, and that is a correctness property
rather than a synchronisation detail: one runtime's garbage is not another's to
collect. It used to be process-global, which meant two interpreters on two
threads shared one candidate buffer — and shared it badly. `collect()` swaps the
buffer into a local; the other thread's `on_destroyed` then does its swap-removal
against the *new* buffer, so a freed object stays in the collector's copy and is
read there. A test that drives an interpreter from a worker thread crashed about
one run in six on it.

The alternative was a lock, and it was built and measured rather than dismissed:
one recursive mutex over the collector with an atomic for the inline due-check
costs up to 39% on the VM (`text_iteration`) and 45% on the tree walker
(`typed_list`), concentrated where allocation is heaviest. Per-thread state costs
nothing measurable. The contract this settles is written where it binds — on
`RefCounted` itself: a value belongs to the thread that created it, and two
runtimes on two threads are independent.

Two kinds of reference are invisible to tracing by construction, and both are
handled by inverting the ownership rather than by trying to see into them:

- A capture inside a native handler's `std::function` cannot be enumerated in
  C++. The contract is therefore that a native handler captures a counted
  object as a raw pointer, and the owning reference is declared in
  `LumiereFunction::native_captures`, which tracing does reach. A handler bound
  to an instance declares that instance, and reaches its state through it.
- The C++ state hanging off an instance -- a socket, a server's route table --
  used to be a `std::shared_ptr<void>`, which is opaque for the same reason. It
  is now a `NativeState`, a counted object like any other, with both tracing
  virtuals left pure so that a new state has to answer the question rather than
  inherit a silent "I hold nothing".

Both holes were real and both leaked: a `ServeurHTTP` holding its handlers, and
LumiTest's context object holding the very methods bound onto it, each retained
their whole defining scope. Debug builds keep a registry of live objects
(`live_objects()`) so that a cycle which survives a collection can be identified
by type instead of inferred; it is how those two were found.

The cost is at the noise floor on the VM and roughly 0 to 5 per cent on the tree
walker, measured by building the previous commit in the same session and running
the two binaries back to back.

## Cross-engine conformance

The tree walker and the bytecode VM have to be the same language. Nothing
enforced that: the fixture corpus under `tests/fixtures/interpreter` ran only
through the tree walker, in process, skipping the analyzer — so it was checking a
pipeline no user takes, and three of its expectations had drifted onto a runtime
message for a program the CLI rejects statically with a better one.

`scripts/conformance` replaces that. It runs every case through the real CLI
under both engines and asks two separate questions: does the case still do what
it is documented to do, and do the two engines do the same thing. The second
question is the one a single-engine replay cannot ask, and it is where the bugs
were. It is a ctest entry, so a divergence fails the build.

`scripts/fuzz` attacks the same property from the other side: mutated corpus
sources through the analyzer and both engines, invalid UTF-8 spliced into source,
and generated programs on the numeric boundaries. Its invariants are independent
of meaning — no crash, no hang, no C++ artifact such as a bare `stoll` or a
`terminate called` reaching the user, and no disagreement between the engines.
Every finding is shrunk, lines then characters, to the smallest input that still
fails.

The first runs found, and these are now fixed:

- The VM showed `__module_init__`, the synthetic function it wraps top-level code
  in, as a traceback frame. The tree walker runs that code with no frame at all,
  so the same failure read differently depending on the engine.
- The VM reused one local slot for every iteration of a loop. Invisible until a
  closure captured it: all closures made in the loop then shared one cell and
  reported the last iteration's value, where the tree walker gives each iteration
  its own binding. Fixed with a `CLEAR_LOCALS` instruction at the top of a loop
  body, costing nothing measurable.
- Five families of runtime diagnostic were worded differently by the two engines,
  among them every binary arithmetic and comparison operator, unknown symbols,
  unknown text members, and unmet type annotations. The VM's type assertions now
  carry the context that required the type, so they say "la variable 'f' attend
  …" rather than naming only the types, which is what the tree walker always did.

Whether `principal` is required was the first recorded divergence, and it is now
settled: a program has a place to start, a module does not, so the analyzer
reports it when a file is about to be run and both engines are told the same
thing before either starts. The corpus case that recorded the divergence failed
the moment the engines agreed, which is what that mechanism is for.

The family where the analyzer was weaker than the VM's compiler is closed, and
closing it was worth more than the two recorded instances suggested. Running
every rule the VM's compiler enforces against `lumiere check` turned up five
missing rules, not two: `arrêter` and `continuer` outside a loop, `parent`
outside a method, assignment to a name nothing declares, and assignment to a
`soit fixe` binding. All five are analyzer rules now (LUM-S0052 to LUM-S0056),
so both engines are told the same thing before either of them starts.

Two of the five were not divergences at all. Outside a loop, `arrêter` and
`continuer` reached the tree walker, which threw a signal nothing caught: the
process aborted with `terminate called after throwing an instance of
'lumiere::BreakSignal'` — a C++ type name, in English, from a two-line program.
`scripts/fuzz` watches for exactly that shape and had never produced one, because
mutation rarely writes a bare `arrêter` at the top of a function body. Taking one
engine's rules and asking the other about each of them found in minutes what
random search had not, which is worth remembering: a fuzzer explores, a sweep
enumerates, and they fail differently.

What remains recorded is which token a runtime error's caret points at. The
larger hole the sweep exposed — the analyzer diagnosing no unknown identifier at
all — is closed; see "Names have to resolve" below.

### Fuzzing the verifier — 2026-09-20

The verifier's whole purpose is to let the interpreter read operands without
checking them. That makes it load-bearing in a way nothing tested: the nine
hand-built cases show it rejects the mistakes we thought of, not that it rejects
every mistake.

`VmVerifier.AcceptedBytecodeSurvivesExecution` attacks it from the other side.
It corrupts a compiled module's instruction stream — a few bytes, mostly small
nudges rather than wholly random values, because a nudge is what turns a valid
operand into a neighbouring one — verifies the result, and runs whatever was
accepted. Rejection is the ordinary outcome and proves nothing on its own; an
acceptance that then crashes is the finding.

Two details make it a test rather than a noise generator. Each candidate runs in
a forked child, so a crash arrives as a signal the parent can name instead of
taking the suite down with it. And the child is given a deadline, because a
verified module may loop forever — a legitimate program may too, and termination
was never part of the guarantee — so a child that does not finish is skipped
rather than failed. The test also asserts that something reached the interpreter
at all, so a run where the verifier rejected every mutation cannot pass while
proving nothing.

The suite runs 400 mutations; `LUMIERE_FUZZ_SEED` and `LUMIERE_FUZZ_ATTEMPTS`
open it up. 7,500 mutations across three seeds under AddressSanitizer and
UndefinedBehaviorSanitizer, about 1,060 of them accepted and executed, found no
hole. The sanitizer build is where this has teeth: in a release build an
unchecked out-of-bounds read may not fault at all, so a clean release run says
much less than it appears to.

### Named arguments, bound by position — 2026-09-20

The VM bound a call's arguments by position and dropped their names, so
`f(b: 1, a: 10)` computed `1 - 10`. It was silent: no crash, no diagnostic, just
the wrong number.

It hid because the compiler covers the common case. When it knows the callee it
reorders the arguments itself and emits them positionally, so every direct call
was right. The names only survive into the bytecode when the callee is not known
until run time — a function held in a variable, or any method call — and there
the interpreter read them into a `RuntimeArgument` and never looked at them
again. Nor could the analyzer catch it: the program is valid, and a valid
program that computes the wrong answer is exactly what static checking cannot
see.

Two things had to change. `FunctionBytecode` now carries its source-level
parameter names, which meant threading them through the LIR — and `merge_module`
copies a function field by field, because blocks own their terminators through a
`unique_ptr`, so the new field arrived empty in the linked module until it was
listed there too. That trap is now written down where the next field will be
added. The binding itself is the tree walker's rule, stated once: a named
argument goes to the parameter it names, a positional one to the next parameter
still unbound, and the presence flag that tells the prologue whether to evaluate
a default follows the binding rather than the argument count.

The first version cost 4.6% on method calls, because it allocated a mapping on
every call to hold what is, for a positional call, the identity. Positional
binding now materialises nothing; only a call that really names an argument
allocates. Measured back to parity against the same baseline binary.

There was no method-call benchmark to measure this with — the existing
`function_calls` case goes through the compiler's direct path and never reaches
argument binding at all. `benchmarks/method_calls.lum` is that gap closed.

`tests/conformance/arguments_nommes` is the regression test, and the point is
where it lives: a single-engine test would have agreed with whichever engine
wrote it. Only running both and diffing them says which one is wrong.

### Names have to resolve — 2026-09-20

`afficher(nom_absent)` passed `lumiere check`. A typo was found when the line
ran, by whichever engine was running it, and the two engines found it in
completely different code: that is why their carets sat a character apart, and
why the fuzzer kept rediscovering the same divergence in new disguises. A name
that cannot resolve is not an engine's business. LUM-S0057 says so once, in the
analyzer, before either engine starts.

A read resolves against locals and parameters, then the module level: its own
declarations, its imports, and the type, class and interface names. The risk
here is the opposite of the hole — rejecting a program that works is worse than
accepting one that does not — so the corpus was the guard rather than an
argument. Every `.lum` file under `examples`, `tests` and the standard library
went through `lumiere check`: five were flagged, and all five genuinely name
something nothing declares. Forward references to functions, classes and
module-level values still resolve, because the module level is collected before
any body is walked, and so do loop variables, pattern bindings, closure
captures, module aliases and type names. A local does not escape its block.

The shell is where this got interesting, because it exposed a bug the narrower
assignment rule had already shipped without anyone noticing. Each submission is
analyzed on its own, while the interpreter carries every earlier one forward.
So `soit base = 40` followed by `base = 60` had been rejected since LUM-S0055
landed — as an assignment to an undeclared name — and the line silently never
ran: the shell then printed 40 and looked like it had worked. There is no
test that types two dependent lines, so nothing caught it.

Both rules now stand down for an incremental submission, which is stated in
`AnalysisOptions` rather than inferred: in a buffer that holds one line, an
unknown name cannot be told apart from one declared earlier, and the interpreter
— which does have the earlier lines — still catches it when the line runs.
Making analysis carry submissions forward is the real answer and is recorded as
its own work; a half-version that re-analyzes the accumulated text would report
earlier lines' diagnostics again and number the new line wrong.

### One position per token — 2026-09-20

A `Token` carried `line`/`column` and `start_line`/`start_column`. The tokenizer
set the first pair from the scanner *after* it had consumed the lexeme, so they
were the position just past the token; the second pair was the position it
began at. The lexer and the parser used the second. Everything else — the
analyzer's `source_range`, the tree walker's `raise_runtime_error`, the VM's
source locations, and every synthetic token that carries a position along — used
the first.

So every caret sat past the thing it was about. LUM-S0057 on `valeur_absente`
pointed at the `)` after it, and `t.membre_absent()` reported column 29, the
closing paren, thirteen characters past the member that does not exist. The
byte range in `lumiere check --format=json` was right the whole time, because it
was built from the offsets, so an editor underlined the correct span while the
terminal underlined the wrong character.

A token now has one position and it is where the token starts. Fixing the
tokenizer fixed every consumer at once, which is the reason to prefer it to a
sweep of the twenty-odd places that pass a position around, and the duplicate
pair could then be deleted rather than left as a trap. `start_offset` and
`end_offset` still give the span.

Both engines were wrong identically, so conformance could not see it: agreement
is not correctness. Five expectations moved, each read against its own source
line rather than regenerated and accepted.

### A module that is not there — 2026-09-20

The fuzzer produced `importer n`, and the two engines answered differently: the
tree walker with a full traceback and a source line, the VM with a bare
`erreur: VM: module introuvable: n`.

Neither should have been reached. When an import resolved to no file and named
no builtin, analysis entered an *empty* set of exports for it and carried on, so
the program passed `lumiere check` and the missing module was left for whichever
engine got there first. LUM-S0012 existed for exactly this and was unreachable.

Leaving the module out of the environment is all it takes: the name then fails
to resolve and the diagnostic fires, once, before either engine starts. The
message lost the word "sémantique", which meant nothing to anyone reading it.

Worth knowing for later: the analyzer looks for a module beside the file that
imports it, and so does the runtime, but the runtime also searches
`m_import_paths`, which nothing currently fills. The moment something does, the
analyzer will reject an import the runtime could have satisfied. One resolver
shared by both is the real answer.

### Where the time actually went — 2026-09-20

The target — ahead of a mature bytecode interpreter — was being read off one
workload. Measured against CPython on all seven, program for program, the VM was
1.34x slower at the median: ahead on text, level on the integer loop, and behind
on calls, dictionaries and typed lists, worst of all on method calls at 3.5x.
The integer loop was the one that had been optimized.

`lumiere --stats` said why before any profiler did. The integer loop allocates
602 times in a million iterations, which is exactly why it keeps up. A call
allocated four times and a list append three. The gap is not interpretation; it
is what each operation does on the heap and in strings.

A profile then named three things, and all three were work nobody had asked for.

**A type was re-read at every check.** A type is text in the source and text in
the bytecode, so every assertion scanned for a union bar, looked for a generic
bracket, and compared the head against seven builtin names. `valeur: Entier`
paid that twice per call, once for the parameter and once for the return.
Classifying each of a module's types once turns a builtin scalar into one tag
comparison; anything with a bracket, a bar, or an unfamiliar name still goes
through the full rules. Function calls fell 20%.

**Reading an object took a reference to it.** `as_objet()` returns a `Ref`,
which is an increment now and, later, a decrement plus a cycle-candidate check —
the collector has to do that check because a surviving decrement is how an
object becomes a cycle root. Dispatching one method call asked its receiver for
the object five times and walked the class chain taking a reference per
ancestor. That was 68 million candidate notifications for two million calls, a
quarter of the run. Borrowing accessors (`as_objet_ptr` and friends) and raw
pointers through the class walk took it to 12 million.

**A cast asked the type system.** A runtime body is reached through a base
pointer and `dynamic_cast` walks the hierarchy to get back; that happened eight
million times. Each engine defines exactly one body of each kind, so a one-byte
tag identifies the concrete type and the cast becomes a comparison — with an
`assert` that the `dynamic_cast` agrees, live in the Debug and sanitizer builds
where the whole suite runs.

Together: method calls 1.09s to 0.78s (-28%), function calls -13%, typed lists
-7%. The integer loop is about 1.5% slower, which is the dispatch loop's code
layout shifting under a bigger switch rather than any work added to it — it was
measured with and without the allocation counter to be sure the counter was not
the cause. Against CPython the median ratio went from 1.34x to 1.14x, and method
calls from 3.5x to 2.5x.

A fourth followed from the same profile. **A built-in member call built its own
name.** `valeurs.ajouter(index)` went through helpers taking `const std::string
&`, and every literal handed to them — `"Liste"`, then `family + ".ajouter"` —
built a string, twice per call, on a path whose whole job is to push one value.
Taking a view instead, and composing the signature only on the error path that
prints it, took typed lists down another 18%, to 1.56x CPython.

What is left is visible in the same profile: a field access still walks the
class chain comparing strings to find out whether the field is private, and a
field still lives in a hash table keyed by its name. Both are the same shape of
problem as the three above — a name resolved at run time that a compiler already
knew — and both want a slot index rather than a faster lookup.

### Building text ran through a stream — 2026-09-20

`Value::to_string` constructed a `std::ostringstream` for every conversion,
including turning `123` into `"123"`. Constructing one sets up a stream buffer
and consults the locale; it measured around 350 ns to build a seven-character
dictionary key. That function sits under every `afficher`, every
`texte + nombre`, and every key built from a number, so it was most of what
building text cost at all. Appending to a `std::string` instead: concatenation
-62%, and the dictionary workload -43%, from 1.79x CPython to about level.

Reading that code turned up a correctness bug beside the slow one.
**`Texte.convertir_decimal` was a second way of writing a Décimal**, and it
disagreed with the first: a stream's six significant digits gave `"1.23457e+08"`
for `123456789.125`, `"0.3"` for `0.1 + 0.2`, and `"2"` for `2.0` — losing
precision, reporting a number that was not computed, and losing the type. Those
are the three defects fixed for `afficher` in `1856f13`, still alive in a
function nobody had connected to it. Both engines were wrong identically, so
conformance could not see it, and a unit test had the wrong answer pinned as the
expectation. It uses `numeric::decimal_to_text` now, like everything else, and
`tests/conformance/decimaux_fidelite` covers the conversion functions.

Twice now the same shape: a fast path and a correctness bug in the same code,
because both come from a routine written without asking what the language's rule
was. Agreement between the engines does not catch it; only having one rule does.

### A member resolved once per class, not once per access — 2026-09-20

Every field access and every method call asked the same question again: walk
the class and its ancestors, comparing the member's name against each field and
each method. Six million string walks for two million calls. Twice over, in
fact, because reading a field looked the descriptor up only to find out whether
it was private — and then computed whether the frame was the object's own
method by searching the frame's name for a dot, whether or not privacy was at
stake.

The answer cannot change: a class's parent is fixed when the class is made, and
so are its descriptors. So each class body now remembers what a member index
resolves to, filled the first time it is asked — a vector indexed by the
module's member table, one pointer per member per class. A class the tree walker
made has no such body and falls back to the walk. Privacy is asked about only
when the member is actually private.

The resolved field carries its type's shape too, so `ici.total = ...` no longer
re-reads the word "Entier" on every assignment -- the same fix as for
annotations, in the last place that still did it.

Method calls -20%, function calls -16%.

This is the last of the "a name resolved while the program runs that the
compiler already knew" family that can be fixed without changing how an object
is laid out. What remains is the layout itself: a field still lives in a hash
table keyed by its name, so reading one hashes a string. Giving a field a fixed
offset in the object is the next step, and it is a bigger one -- the object
representation is shared with the tree walker and with every native in the
standard library.

### Object fields keep their order — 2026-09-20

An object with one field paid for an `unordered_map`: a bucket allocation,
string hashing and a node lookup. It also had no iteration order, even though
the collector, LumiTest and nominal-value lookup all walk the table. Fields now
live once in an insertion-ordered vector. Tables below eight entries scan it;
larger tables add an open-addressed position index while the vector remains the
source of truth.

The wide case changed the implementation rather than being dismissed. A
scan-only version made 500,000 reads of the first and last fields of a 32-field
object take 121.6 ms against 63.2 ms before. The index brought it back to the
noise band. Its power-of-two capacity folds the high bits of `std::hash` down:
the raw low bits collided for the benchmark's last field, and assuming an
implementation-defined hash distributes those bits made the index slower for
reasons unrelated to its load.

Two reversed-order comparisons, 15 and 25 runs per binary after a warm-up:

| Workload | Before median (min–max), s | After median (min–max), s |
| --- | --- | --- |
| method_calls, 15 runs | 0.555027 (0.549692–0.582038) | 0.541568 (0.532833–0.568067) |
| wide_object, 15 runs | 0.063067 (0.062690–0.063839) | 0.063331 (0.062012–0.086030) |
| method_calls, reversed, 25 runs | 0.556874 (0.548380–0.589966) | 0.537997 (0.530055–0.583477) |
| wide_object, reversed, 25 runs | 0.063150 (0.062446–0.065119) | 0.064774 (0.062477–0.067698) |

Method-call medians fell 2.4% and 3.4%; the ranges still overlap, so this is a
small repeatable direction rather than a claimed isolated speedup. The wide
case moves in opposite directions across the two comparisons and is neutral.
The rest of the suite overlaps its baseline ranges. Unit tests pin map-compatible
insertion and replacement, indexed lookup after growth, removal and iteration
order. There is no source-level object-field enumeration API to express that
last invariant as a language conformance program.

### Call frames keep their storage — 2026-09-20

Returning from a VM call destroyed its frame, including the capacity of the
locals and captures vectors. The next call at the same depth allocated them
again. The frame stack now separates active depth from storage: pop releases
the Values and cells but retains both vectors' capacity, and push initializes
the frame already stored at that depth.

This invariant is explicit beside the stack implementation: pushing may grow
the backing vector and invalidate the dispatch loop's `CallFrame` reference,
so every push remains the final action before `break`.

Allocation counts are exact. On 100,000 direct function calls they fell from
300,805 to 200,806; on two million method calls, from 6,000,976 to 4,000,977.
That is one allocation removed per call. Fifteen measured runs per binary after
one warm-up, baseline first:

| Workload | Before median (min–max), s | After median (min–max), s | Change |
| --- | --- | --- | --- |
| function_calls | 0.032200 (0.031459–0.032550) | 0.029575 (0.028738–0.030421) | −8.2% |
| method_calls | 0.537143 (0.529541–0.552883) | 0.485106 (0.483121–0.500320) | −9.7% |

The ranges do not overlap.

**Correction, same day.** "The frame stack does no work unless a call pushes or
pops a frame" was an argument from the design, not a measurement, and it was
wrong. The dispatch loop binds `CallFrame &frame = frames.back()` once per
*instruction*, not once per call, and `back()` computed `m_frames[m_depth - 1]`
— a frame is 88 bytes, so that is a load of the data pointer, a load of the
depth, and a multiply, on every instruction the VM executes. The integer loop,
which makes one call and then runs thirteen million instructions, was 4.5%
slower, and the noise on this machine hides 4.5% at low run counts: it took
eleven runs and a three-way comparison against separately built binaries to
separate it.

Keeping the top frame as a pointer, updated on push and pop, makes `back()` one
load. Against the same baseline, eleven runs: the integer loop went from 4.5%
slower to 2.6% **faster**, and method calls from -9.7% to -20%.

The lesson is the one the benchmark harness exists to enforce: a workload that
"should not be affected" is still a workload that has to be run. `scripts/
benchmark.py` runs all of them for exactly this reason.

### The shell resolves names again — 2026-09-20

Every rule that resolves a name stood down in the shell. `AnalysisOptions`
carried a flag saying so, because each submission was analyzed on its own while
the interpreter carried every earlier one, and a name declared on line one
looked undeclared on line two.

The flag was put there to stop a bug it was actually hiding: `soit base = 40`
followed by `base = 60` had been refused since LUM-S0055 landed — an assignment
to an undeclared name — so the line never ran and the shell then printed 40 as
though nothing had happened. **No test typed two dependent lines**, which is the
whole reason it shipped.

An analysis now starts from what the previous one settled: its names, its type
symbols and its signatures. The subtle part is the type interner. `same_type`
compares `SemanticTypeRef` by pointer, which only means anything among types one
interner produced, so a fresh interner would have disagreed with the last one
about `Entier` while both printed the same word. The new analysis adopts the old
interner's table — the same shared objects, not copies — before any symbol that
names a type is carried across.

Only a submission that *ran* contributes. One that raised part-way may never have
made the binding its declaration promised, and the next line must not be told
otherwise.

`incremental_submission` is gone rather than left switched off, and the shell
diagnoses an unknown name with LUM-S0057 before the line runs, like a file does.
`CliIntegration.ReplResolvesNamesDeclaredByEarlierSubmissions` types the lines
that would have caught the original bug, including a rejected redefinition.

The first implementation tried to adopt the previous interner with
non-overwriting insertion. That left every built-in already created by the new
analyzer in place, contrary to the pointer-identity invariant above. Adoption
now replaces the table. `SemanticTypes.AdoptReplacesAlreadyInternedTypes` pins
the case directly instead of relying on assignability rules to expose it.

## One implementation of the collection members

`Liste.ajouter` existed twice: once in `tree_walker_sequences.cpp` and once in
`execute_member_call` in `vm.cpp`. So did every other member of every
collection. Nothing forced the two to agree, and conformance could not see the
difference either, because it compares the engines only on cases someone wrote
— and nobody had written one that made a member fail.

They are now written once, in `src/interpreter/runtime/members.cpp`, reached
through `IRuntime`. Each engine keeps only the part that is really its own: the
tree walker binds the member to its receiver where it is written, the VM looks
it up when the call runs. `find_builtin_member` answers the first question and
`call_builtin_member` the second, so neither engine decides what a member does.

`IRuntime` gained one method for this, `matches_declared_type`, because a
collection's declared element type has to be checked against the engine's own
class and interface tables before a value is let in. Everything else a member
needs — `call`, `raise_runtime_error`, `is_equal`, `to_text`, `annotate_value` —
was already there.

### Liste: five wordings that had drifted

Built from the parent commit and from this one, the same seven programs:

| programme | tree walker | VM (avant) |
| --- | --- | --- |
| `l.retirer_a(5)` | `indice hors limites : 5 pour Liste de taille 1` | `indice hors limites` |
| `l.retirer_a(-1)` | `indice hors limites : -1 pour Liste de taille 1` | `indice hors limites` |
| `l.en_liste_fixe(3)` | `... de taille exacte 3` | `... de taille exacte` |
| `l.en_liste_fixe(-1)` | `la taille d'une ListeFixe ne peut pas être négative` | `... de taille exacte` |
| `l.joindre(2)` | `une valeur de type Texte est attendue` | `Liste.joindre attend un Texte` |
| `l.inserer("a", 2)` | `l'indice d'une séquence doit être un Entier` | `Liste.inserer attend un Entier` |
| `l.inserer(5, 2)` | `indice d'insertion hors limites` | same |

Six of the seven differed. The rule chosen in each case:

- **An index that is out of range says which index and how large the receiver
  is.** The message that names neither cannot be acted on. `messages::indice_hors_limites`
  already worded it; the VM was not using it.
- **A length that cannot be a length is a different mistake from a length that
  does not match.** `en_liste_fixe(-1)` is not "your list is not 3 long".
- **An argument of the wrong type names the member and the type wanted**, as the
  rest of the stdlib already does through `stdlib_expect_text`. The tree walker's
  wording named neither, and reused the *index* message for `inserer`'s index,
  which is not what that message is for.

The three wordings every builtin shares — wrong count, a named argument, an
argument of the wrong type — now live in `runtime_messages.hpp` and are used by
both `stdlib_helpers.cpp` and the members, so there is one sentence per rule
rather than one per caller.

### ListeFixe: the same drift, and a contract that could be laundered

`ListeFixe` had one member of its own, `en_liste`, and answered the shared
sequence members through each engine's own copy of them — so it inherited the
same `joindre` divergence the Liste family had (`une valeur de type Texte est
attendue` against `ListeFixe.joindre attend un Texte`).

`en_liste` itself already agreed, and the rule it states is worth naming because
it is easy to lose: **the Liste it hands back carries the ListeFixe's element
type.** Without that, `mots.en_liste_fixe(3).en_liste().ajouter(3)` would launder
a `Liste[Texte]` into an unconstrained list by a round trip through a type that
cannot even be written to. Both engines happened to do this; nothing pinned it.
`tests/conformance/membres_liste_fixe` and the parity test now do.

### Ensemble: one wording, and one mechanism for the contract

The two engines already agreed on the Ensemble members, and on every message
they produced — this family had not drifted. What it had was two of everything,
including two copies of a `union`/`intersection`/`difference` body that branched
on the member's own name to decide what it was.

Two things were changed rather than moved, and both are stated here because
neither was a difference between the engines:

- **A set operation given something that is not a set now says it the way every
  other builtin says it**: `Ensemble.union attend une valeur de type Ensemble`,
  not `Ensemble.union attend un Ensemble`. The rule is that an argument of the
  wrong type reads the same sentence everywhere; leaving this one would have put
  two spellings of the same complaint inside one family, since `Ensemble.joindre`
  already used the other.
- **A derived set takes its contract the same way every other derived collection
  does**, through `annotate_value`, rather than by assigning `constraint`
  directly and *then* annotating. The two are not quite the same operation — the
  annotation also reaches the elements, which is what makes an `Ensemble[Liste[Entier]]`
  keep its inner contract — so doing both was doing one of them twice.

`tests/conformance/membres_ensemble` runs every member, both spellings of
`différence`, both directions of `sous_ensemble_de`, and the empty receiver on
each side of every operation.

### Dictionnaire: a contract the VM was not giving out

`cles`, `valeurs` and `paires` each hand out a new `Liste`, and the rule is that
the list carries what can be derived from the dictionary's contract. Both engines
did that for `cles` and `valeurs`. Neither had a test for `paires`, and the VM
annotated it with nothing at all:

```
soit d: Dictionnaire[Texte, Texte] = {"a": "x"}
d.paires().ajouter(1)
```

The tree walker refused that — `Liste.ajouter attend une valeur de type
ListeFixe[Texte, 2]` — and the VM ran it to completion, putting an `Entier` in a
list of pairs. The rule kept is the tree walker's: **a pair carries an element
type only when the key and the value have the same one**, and its length, 2, is
known either way, so the annotation is `ListeFixe[T, 2]` or
`ListeFixe[Universel, 2]` and the list of them is `Liste[` that `]`.

The one wording that differed was invisible: the tree walker reported an arity
failure of `cles()` as `Dictionnaire.clés`, whichever spelling was written. A
diagnostic quotes what was written.

With this family moved, `tree_walker_sequences.cpp` is gone, and with it the
tree walker's own member machinery — `make_tree_walker_native_method`, its
`NativeMethodHandler` signature, and `require_positional_args` — along with the
VM's `require_member_arity`, `member_integer`, `member_text` and
`member_signature`. `execute_member_call` is now three lines: text, the shared
members, and the member that does not exist.

### What the four commits measured

The VM used to copy a member call's arguments into a second vector before
dispatching, and the tree walker used to build a closure per member access. One
implementation needs neither: the arguments are passed through as they arrived,
and the binding captures a pointer into a static table.

Built from `c3e1a77` and from the end of this series, both RelWithDebInfo,
medians of nine runs with one untimed warm-up:

| workload | moteur | avant | après |
| --- | --- | --- | --- |
| typed_list | vm | 0.0380s (0.0378–0.0385) | 0.0320s (0.0311–0.0358) |
| typed_list | tw | 0.1426s (0.1364–0.1694) | 0.1273s (0.1255–0.1307) |
| dictionary_lookup | vm | 0.0282s (0.0277–0.0291) | 0.0285s (0.0276–0.0292) |
| dictionary_lookup | tw | 0.0707s (0.0693–0.0758) | 0.0701s (0.0688–0.0729) |

Allocation counts, which are exact in one run:

| workload | moteur | avant | après |
| --- | --- | --- | --- |
| typed_list | vm | 400 722 | 200 722 |
| typed_list | tw | 1 200 486 | 800 484 |
| dictionary_lookup | vm | 150 870 | 150 870 |
| dictionary_lookup | tw | 350 540 | 350 537 |

One allocation per `ajouter` in the VM and two in the tree walker, for 200 000
iterations. `dictionary_lookup` indexes rather than calling members, which is
why it moves neither way and is the control here. `integer_loop`,
`function_calls`, `method_calls`, `text_calls`, `text_iteration` and
`wide_object` are unchanged within their bands.

The speed was not the point and was not sought; it is what a duplicate costs
when one of the two copies is on a hot path and nobody is looking at it.

### What the arity check is, and is not

Analysis rejects `l.ajouter(4, 5)` as LUM-S0015 before anything runs, so the
runtime arity check is unreachable from ordinary source. It is kept because the
runtime is also reached from the REPL and from native callers, and because a
member that assumed its arity without checking would be a fast path with an
unasserted assumption. It is stated once for all members, in the table, rather
than repeated as the first line of each.

### What is pinned

`tests/conformance/membres_liste` runs every Liste member through both engines,
including the empty receiver and both ends of the index range.

The member failures are now also in the exact-stderr conformance corpus. The
runtime-location rule below removed the caret difference that had kept them out.

## One source token for each runtime failure — 2026-09-21

The tree walker used the token available at the point it noticed a failure; the
VM used the bytecode opcode's location. Those are implementation details, not a
language rule. They made a bad member argument point at `(` in one engine and
the member name in the other, while an out-of-range index pointed at `[` rather
than at the index that was wrong.

The rule is now stated in `docs/runtime-diagnostic-locations.md`: **the caret
points at the token that names the thing the message is about.** A call-wide
failure points at the callable, an argument failure at that argument, an index
failure at the index expression, a non-iterable value at the iterable expression,
and a failed conversion at its target type.

That required preserving information rather than guessing later. `Argument`
records its source token. `RuntimeArgument` carries the resulting `RuntimeSite`.
The VM stores one source location per argument on call instructions, copies it
when modules are linked, writes it beside the encoded argument name, and
restores it when the stack values become runtime arguments. A site-aware
`VmRuntimeError` then lets shared runtime code report that location without
knowing which engine called it. Call and expression-start token selection is
shared AST logic, so the two lowering paths do not encode separate policies.

Five exact-stderr cases pin the previously unpinned families:
`diagnostic_appel`, `diagnostic_argument_membre`, `diagnostic_indice`,
`diagnostic_iteration` and `diagnostic_conversion`. Existing conformance cases
continue to pin symbols, members, operators, imports and control flow. The
traceback fixture now also records callable-token frame locations. No
`divergence.connue` remains under `tests/conformance`.

This is a correctness change, not a performance claim; no timing result is
attributed to it.

### The token an expression starts at, asked so it cannot be forgotten

The location rule needs the first token of an arbitrary expression, and that was
answered by a chain of fourteen `dynamic_cast`s whose last line was
`dynamic_cast<const PropagationExpr &>(expr)` -- a reference cast, which throws
rather than returning null. `AgirSelonStmt` derives from `Stmt` *and* `Expr`,
and it was not in the chain, so every expression position the rule reaches
aborted on it:

```
pour chaque x dans agir selon n { 1 -> [1, 2]  sinon -> [3] } { afficher(x) }
```

printed `1` and `2` before the rule landed and `erreur: std::bad_cast` after, in
both engines -- the VM while lowering, the tree walker while evaluating. Using
one as an index, on either side of an assignment, or as an operand of an index
expression did the same. A C++ exception's type name is not a diagnostic of this
language.

`Expr::start_token()` is now pure virtual, so a node that does not answer does
not compile, which is the guarantee `ExprVisitor` already gives for evaluation
and which a cast chain cannot give at all. Each node answers in one line;
`agir selon` answers with its keyword, like every other control-flow construct
in the rule's table. The chain is gone, and with it a `dynamic_cast` on every
index access and every call lowering.

`tests/conformance/agir_selon_expression` runs `agir selon` in each position
that used to abort, and `tests/conformance/diagnostic_agir_selon` pins the
caret on its keyword.

### What carrying an argument's position cost, and what it should cost

Giving every argument its own source position is what lets a diagnostic name the
argument that failed, and it is worth having. The first implementation paid for
it by copying the source *path* into every argument's `RuntimeSite` on every
call, in both engines. Measured against `6d340c4`, the commit before the rule
landed:

| workload | moteur | avant | avec le chemin | corrigé |
| --- | --- | --- | --- | --- |
| typed_list | vm | 200 722 | 400 730 | 200 728 |
| method_calls | vm | 2 001 058 | 4 001 066 | 2 001 064 |
| function_calls | vm | 100 834 | 200 838 | 100 837 |
| method_calls | tw | 36 000 565 | 42 000 565 | 36 000 565 |
| function_calls | tw | 1 800 496 | 2 100 496 | 1 800 496 |

One allocation per argument per call, which doubled two of these and undid the
whole of the preceding commit's measured reduction on `typed_list`. The path
bought nothing: **the frame that raises an argument diagnostic is the frame that
wrote the argument.** A native member pushes no frame of its own, every
argument-binding failure is thrown before the callee's frame exists, and the
tree walker's current path only changes while a module is being loaded, which
cannot happen between evaluating an argument and reporting on it. Both engines
already fell back to exactly that path when the site carried none.

Two other per-call costs went with it. `call_user_function` had grown a second
vector, parallel to `bound_arguments`, to hold the positions; one vector of
pointers into the arguments holds both, and copies the value once instead of
twice. And the token handed to the parameter's type check carried the
parameter's name, which nothing reads -- it is consulted for its line and
column.

What remains is the real price of the feature: an argument is 40 bytes larger
because it carries a position. Three binaries in one run, medians of nine:

| workload | avec le chemin | corrigé | avant la règle |
| --- | --- | --- | --- |
| function_calls | 0.02578s | 0.02463s | 0.02409s |
| method_calls | 0.5005s | 0.4786s | 0.4703s |
| typed_list | 0.03621s | 0.03380s | 0.03194s |
| integer_loop (témoin) | 0.07279s | 0.07462s | 0.07324s |

`integer_loop` executes no call and cannot have changed; it moves 2.5% between
these binaries, which is this machine's floor. `function_calls` and
`method_calls` are back inside it. `typed_list` is not: about 6% is still there,
and it is not allocation, since the counts above match. It is the size of a
`RuntimeArgument`, whose `RuntimeSite` spends 32 bytes on a `std::string` that
is empty on every call. Making a position a pair of integers, with the path
beside it rather than inside it, is the lever if that 6% is wanted back.

## The target, re-read — 2026-09-23

"Ahead of CPython at the median" was measured, and it was measured wrong. Two
things in the comparison favoured Lumière, and neither was either interpreter.

**The Python programs ran at module level.** A Lumière workload runs inside
`principal()`, where a `soit` is a slot in the frame. Its Python counterpart ran
as a top-level script, where every variable is a global looked up in a
dictionary on every read. Put in a `principal()` and called, the same Python
code does the same work in 14% to 75% less time -- 75% on the integer loop,
which was the workload the target had first been claimed on. The C reference
already kept its variables local, so only the Python column was affected.

**CPython's startup was counted as execution.** CPython spends 8.2 ms before its
first line and Lumière 0.7 ms, and every comparison timed the whole process. On
the two text workloads CPython *executes* for about 1 and 2.5 ms, so what was
being compared there was mostly two startups: the "0.24x" and "0.47x" leads on
text were CPython launching. `compare-languages.py` now measures each runtime's
empty program, subtracts it, prints the startups on their own line, and leaves
out of the median any workload whose CPython execution is under ten times the
spread of CPython's own startup -- below that, the subtraction alone can move
the ratio.

Same binary, same machine, one session:

| workload | old method | Python in a function | and net of startup |
| --- | --- | --- | --- |
| integer_loop | 0.88x | 1.56x | 1.88x |
| method_calls | 1.54x | 1.87x | 1.93x |
| function_calls | 1.02x | 1.27x | 2.10x |
| typed_list | 1.26x | 1.71x | 2.95x |
| dictionary_lookup | 0.90x | 1.10x | 1.58x |
| wide_object | 0.98x | 1.26x | 1.49x |
| text_iteration | 0.47x | 0.49x | (1.80x) |
| text_calls | 0.30x | 0.24x | (1.21x) |
| **median** | **0.94x** | **1.27x** | **1.91x** |

In parentheses: too short to rank. **The VM executes every workload in the
suite more slowly than CPython, by 1.5x to 3x, and by 1.9x at the median.** Its
startup is eleven times shorter, which is real and worth saying, and belongs
on its own line rather than inside every ratio.

Nothing here is a regression: the binary is the one every recent entry
measured. What changed is that the comparison now compares interpreters. The
recorded optimizations were each measured Lumière against Lumière, binary
against binary, and those measurements stand; what does not stand is the
claim, made on top of them, that the target had been reached. The note of
2026-09-20 names this exact failure -- the target read off a proxy for it --
and it happened again one level down, in how the proxy was built.

## Next engineering priorities

1. **Runtime lifetime and type invariants.** Collection constraints now belong
   to their allocations, with conservative non-weakening re-annotation rules.
   Next audit native nominal identity, semantic module identity, and
   chained-alias shadowing,
   then decide whether richer mutable-generic type relations are needed. Cycle
   collection and leak checks are done; see "Reference cycles" above.
2. **Argument types for builtin members.** Result types are now known, but the
   parameters of a builtin collection member are still `Universel`, so
   `notes.ajouter("x")` on a `Liste[Entier]` is caught at run time rather than
   at analysis. Tightening them needs the analyzer to follow the aliasing that
   the allocation-carried constraints already handle, which is the same work as
   richer mutable-generic relations in priority 1.
3. **One conformance corpus.** Built; see "Cross-engine conformance" above.
   Malformed bytecode is fuzzed against the verifier now. What remains is
   closing the recorded divergences, of which name resolution is the larger.
4. **Value representation.** Profiling said the 48-byte non-trivial `Value` was
   about 40% of execution. It is now 24 bytes and holds a `Ref` rather than a
   `shared_ptr`, which was the part that had to wait for the runtime to own its
   own heap objects. Going further — a small trivially copyable value — is now
   unblocked and remains the largest lever inside the interpreter. Measure
   allocation and instruction counts alongside it, and peak memory as well as
   time.
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
