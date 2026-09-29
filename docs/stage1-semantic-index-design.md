# Stage 1 design: the semantic tooling index

Status: **complete**. Elaborates `docs/tooling-v2-spec.md`'s Stage 1
("semantic tooling index") into something implementable, and all five
rollout steps are now implemented, including step 5's deletion of the
AST-heuristic hover fallbacks (`member_declaration_inspection`,
`find_type_declaration`, `find_class_member_statement`,
`find_interface_member_statement`, `collect_statements`/`collect_statement`)
-- see below for the two prerequisites that made that deletion safe and the
audit that checked it before it happened, since an earlier version of this
doc claimed readiness prematurely once already: step 2 covers local
declarations too (not just
module-level `declare_value`/`declare_type`), step 3 covers
`IdentifierExpr` reads/writes and `MemberAccessExpr` reads (member *writes*
-- `objet.champ = valeur` -- aren't resolved by anything today, index or
not; see the analyzer's own EGAL handling, which only special-cases an
`IdentifierExpr` target), and step 4 tries `occurrence_at` first in both
`inspect_source` hover paths, formatting through the new
`inspection_from_symbol` (inspection.cpp).

Step 4 deliberately **kept** the old heuristics it was meant to switch
away from (`member_declaration_inspection`, the `collect_statements`
scan) as a fallback for what step 3 didn't index yet. That gap is now
mostly closed:

- `resolve_type`'s `NAMED` branch now records an occurrence when a type
  annotation (`soit p: Point`) resolves to a user class/interface --
  `record_value_occurrence` only ever saw *expression* reads, never a
  type annotation, which isn't one.
- `pour`-loop variables get their own `SemanticSymbolKind::LOOP_VARIABLE`
  (they have no backing `VarDeclStmt` -- there's no `soit`/`fixe` in
  `pour x dans ...` -- so `inspection_from_symbol` couldn't format them
  as a plain `VARIABLE` the way a real local's `Stmt` lets it).
- `agir selon` pattern bindings and local `importer` bindings (both
  `declare_local`'d with no backing `Stmt` either) get a generic
  `VARIABLE`-with-no-declaration formatting in `inspection_from_symbol` --
  new coverage, since `collect_statements` never had an entry for either.
- `SemanticIndex::set_type` plus a `set_local_type` update lets a local's
  *inferred* type reach the index after the fact (declare-time is always
  before the initializer/signature is resolved), so parameter and
  `pour`-variable hovers now carry a real `return_type` instead of
  nothing (parameters) or `collect_statements`' old hardcoded `"Entier"`
  regardless of what's actually being iterated (`pour`-variables).

Type aliases are now indexed too, closing that gap:

- `type Nombre = Entier` declares a `SemanticSymbolKind::TYPE_ALIAS` Symbol
  in `SymbolNamespace::Type` at `collect_module_declarations` time, same
  scope as a class/interface -- but with a null `type`, since the target
  isn't resolved yet (aliases can forward-reference each other; `resolve_
  alias` is what actually walks the chain, later, possibly lazily). Its
  `SymbolId` is kept in a new `m_alias_symbols` map so `resolve_alias` can
  call `SemanticIndex::set_type` once it has the resolved type -- the same
  "declare now, fill in the type once it's known" shape `set_local_type`
  already uses for locals.
- `resolve_type`'s `NAMED` branch used to try `resolve_alias` first and
  return immediately on success, *before* reaching the occurrence-recording
  code below it -- which only ever ran on the `find_type` (class/interface)
  path. So every alias reference was silently never indexed, no matter how
  much step 3/4 work landed. Fixed by resolving through `resolve_alias` or
  `find_type` into one `resolved_type` first, then recording the occurrence
  once against whichever namespace entry `syntax.name` names in
  `SymbolNamespace::Type` -- an alias and a class/interface are now handled
  identically here.
- `inspection_from_symbol` gained a `TypeAliasDeclStmt` branch (aliases were
  never covered by `declaration_inspection_from_stmt`, same as classes/
  interfaces): labelled `"alias de type"`, detail type read from
  `symbol.type` (the *resolved* target, following any alias chain) rather
  than re-deriving it from `alias->target`'s syntax, falling back to that
  syntax only if `set_type` hasn't run yet (an alias declared but never
  referenced).

The last named gap is closed too: `bind_imported_value`'s `module_level`
branch used to write straight to `m_value_symbols` and return, the one
`declare_value`/`declare_local` call site that never reached the index
(one `ImportStmt` can bind many names -- a bare `importer Module`, or one
call per member of `importer Module.{x comme y}` -- unlike the single-name
declarations `declare_value` covers). It now calls `SemanticIndex::declare`
itself on the same success path, right after the `m_value_symbols.emplace`
that guards against a duplicate name, using the site token's own span (its
lexeme is always exactly `binding`, since `resolve_import` only ever calls
this with the alias/member token whose lexeme it just read) and `&import`
as the backing declaration. Hover is unaffected -- `imported_value_
inspection` still runs first and `inspection_from_symbol` has no
`ImportStmt` branch, so it falls through to that old heuristic exactly as
before -- but reads of the binding itself (`Maths` in `Maths.absolu(...)`,
not just `absolu`) now resolve to a real indexed Symbol instead of nothing,
via the same `diagnose_value_read`/`record_value_occurrence` path every
other identifier already went through.

With this, every `declare_value`/`declare_local`/`declare_type` call site
the analyzer has feeds the index -- but attempting the actual step 5
deletion surfaced two gaps that "every declaration site feeds the index"
doesn't cover, because they're not about *declaring* into the index at
all:

- **No hover ever worked on a declaration's own name through the index.**
  `occurrence_at` only ever finds a *reference* -- `declare` never calls
  `record_occurrence` for the token it's declaring. `member_declaration_
  inspection`/`collect_statements` are what currently answer "hover the
  `doubler` in `fonction doubler(...)` itself", not just its call sites,
  and `DescribesFunctionDeclarationsAndReferences` pins exactly this (it
  hovers both the declaration and a reference and expects the same
  answer). Deleting `collect_statements` outright would have silently
  broken every declaration-site hover in the language. Fixed by a new
  `SemanticIndex::declaration_at(document, byte_offset)` -- `occurrence_
  at`'s complement, scanning `Symbol::declaration_span` instead of
  `Occurrence::span` -- wired into `inspect_source` right after the
  `occurrence_at` check, using the same `inspection_from_symbol`
  formatting either way. This is a net *improvement*, not just parity:
  parameters and pattern bindings (never in `collect_statements` at all)
  and a class field's own declaration (`collect_statement`'s `VarDeclStmt`
  branch always says `"variable"`, even inside a class -- only a
  *reference* to the field ever said `"champ"`) now hover correctly at
  their declaration site too.
- **Interface-typed member access was never indexed.** `record_member_
  occurrence`'s three call sites (`ici`, `parent`, the general receiver
  path) all resolved the receiver's declaration through `class_
  declaration` -- a plain `ClassDeclStmt*`. A receiver typed as an
  *interface* made that return null, so `member_type`/`record_member_
  occurrence` never ran, and `find_interface_member_statement` (the old
  heuristic) was the only thing that ever answered a hover through one --
  still load-bearing, not dead fallback. Fixed by `record_interface_
  member_occurrence`, the same shape as `record_member_occurrence` but
  built on `find_interface_method` (already used by `callable_signature`
  for call resolution, now reused here too), wired into `inferred_type`'s
  `MemberAccessExpr` branch right after the class check.

Both are prerequisites, not step 5 itself: `member_declaration_
inspection`, `find_type_declaration`, `find_class_member_statement`,
`find_interface_member_statement`, and the `collect_statements` scan are
all still present. With these two landed, though, every case they cover
has been checked against the index and confirmed to resolve the same way
(or better) -- so the deletion itself is the next piece of work, not
something still blocked on a correctness gap.

A follow-up audit, done deliberately before attempting the deletion rather
than after (the prior "no remaining gap" claim had already been wrong
once), walked every case the old heuristics cover and cross-checked it
against `declaration_at`/`record_interface_member_occurrence`. It found no
new correctness gap, but did confirm one more of the same shape as the
class-field one above: `collect_statement`'s `FunctionDeclStmt` branch
always labels a declaration `"fonction"`, even recursing into a class's
own members, so a *method's own declaration line* used to hover as
`"fonction"` rather than `"méthode"` -- only a reference to it ever got
the right label. `declaration_at` corrects this too, for free (`is_member_
declaration` doesn't care whether it's called from a declaration site or a
reference site). The audit also confirmed two things stay equivalently
*un*-covered by both the old and new paths, so deleting the old one
doesn't regress them: a class or interface declared locally (nested, not
at module level) is invisible to `class_declaration`/`interface_
declaration` (both only ever consult `find_value`, the module-level flat
table) exactly as it was to `find_type_declaration` (restricted to
top-level statements by its own doc comment); and a receiver whose static
type is a union or a generic instantiation was never resolved by either
path, since both require an exact `CLASS`/`INTERFACE` type kind match.

Regression gate: all 15 pre-existing `SourceInspection` tests pass
unchanged throughout; 26 new ones cover what the index newly resolves that
the old heuristics never did (a parameter reference or its own declaration
site, neither ever indexed by `collect_statements`; a module-level import
binding's own reads, previously invisible to the index entirely; a member
accessed through an interface-typed receiver, previously never recorded)
or resolves more precisely (an inherited field, a type annotation, a type
alias reference through a chain of two aliases, and now every remaining
declaration kind's own site -- class, interface, method, `pour`-variable,
match pattern binding -- all exercised end to end through `inspect_source`
rather than only at the `SemanticIndexBinding` layer; a loop variable's
*real* element type instead of a hardcoded one).

**Step 5 itself is done.** With the audit above confirming every case
covered by hand, `member_declaration_inspection`, `find_type_declaration`,
`find_class_member_statement`, `find_interface_member_statement`, and
`collect_statements`/`collect_statement`/`push_declaration` (plus the
`Declaration` struct and `declaration_to_inspection`, which existed only to
feed that scan) were deleted from `inspection.cpp`: -276 lines net (1235 ->
977). `inspect_source`'s member-access branch now tries `qualified_member_
inspection` (stdlib) then the index, full stop; its plain-identifier path
tries `occurrence_at` then `declaration_at` then gives up to `builtin_
inspection` -- no AST re-walk left under either. All 577 tests pass
unchanged, and a manual `lumiere inspect` CLI check confirmed a class
field's own declaration, a class field reference, and a class declaration
all still resolve correctly end to end.

Stage 1 (the semantic tooling index) is complete: every declaration the
analyzer makes is indexed, every reference kind (identifier read/write,
member access on a class or interface receiver, type annotation, alias
chain, import binding) is recorded against it, hover reads exclusively
from the index, and the AST-heuristic code Stage 1 was meant to replace no
longer exists. What Stage 1 does *not* cover, left for whatever stage of
`docs/tooling-v2-spec.md` picks it up next: member *writes*
(`objet.champ = valeur`, never resolved by anything, index or not),
find-references/rename (the index already has `occurrences_of`, but
nothing surfaces it), and hovering a bare module-level import alias by
itself rather than a member accessed through it (`ModuleLevelImportBindingsHaveNoHoverYet`
pins this as a known, pre-existing gap, not a regression).

## Where we actually start from

Before proposing new types it's worth being precise about what
`SemanticModel`/`SemanticAnalyzer` (`src/analysis/semantic_analysis.cpp`,
4400 lines) already do, because Stage 1 is smaller than it looks once this is
accounted for:

- **There is already a real scope stack.** `SemanticAnalyzer::m_scopes` is a
  `std::vector<std::unordered_map<std::string, LocalBinding>>`, pushed and
  popped around every block, function body, loop, and branch
  (`push_scope`/`pop_scope`, ~15 call sites). Local reads already resolve by
  walking it back-to-front (`m_scopes.rbegin()`). This is the scope-nesting
  behavior the spec asks for — it just doesn't outlive the call that built
  it: `pop_scope()` discards each `LocalBinding` once its block ends.
- **Module-level declarations already have a symbol table**, but a flat one:
  `SemanticModel::m_value_symbols` / `m_type_symbols` are
  `unordered_map<string, SemanticSymbol>`, one entry per name, no notion of
  "this specific token is a use of that specific declaration."
  `find_value`/`find_type` look up by name, not by position.
- **There is no occurrence record at all.** Nothing maps a source span to
  "the declaration this identifier resolved to." `inspection.cpp` gets hover
  answers by re-walking the AST at query time and pattern-matching the node
  under the cursor (`find_type_declaration`, `member_declaration_inspection`,
  `qualified_member_inspection`, `imported_value_inspection`, ~900 lines of
  this) — repeating work the analyzer already did once, informally.
- **Spans already have a byte-offset-first representation.**
  `SourceRange` (`diagnostics/diagnostic.hpp`) is `{start, end, line,
  column}` in zero-based UTF-8 bytes, and every `Token` already carries
  `start_offset`/`end_offset`. Diagnostics already build a `SourceRange` from
  a token via a `source_range(token)` helper. There is no existing
  equivalent to `SourceId` — file identity is threaded around as a bare
  `std::string source_path`.

So Stage 1's real content is: **stop throwing the scope stack and name
resolution away, give every declaration a stable id, record every read/write
occurrence against that id with a span, and let `inspection.cpp` query the
result instead of re-deriving it.** It is additive to the existing
`SemanticAnalyzer` walk, not a rewrite of it.

## New identities

### `SourceId`

```cpp
// include/lumiere/analysis/source_id.hpp
enum class SourceId : std::uint32_t {};
```

An opaque, snapshot-local handle for one source document, interned from its
logical path (the same string `analyze_source`'s `source_path` already
carries). A single `AnalysisResult`/`SemanticModel` — one call to
`analyze_source`, which may pull in several imported modules' worth of
declarations via `build_import_environment` — owns a small
`SourceRegistry`: `path -> SourceId` and back. `SourceId{0}` is always the
buffer passed to `analyze_source` itself; imported modules get 1, 2, 3...
in import order. This mirrors what already happens informally: imports are
already read from disk by `build_import_environment` and their statements
merged into the analysis (see `src/analysis/analysis.cpp`); they just aren't
tagged with an id today; occurrences and diagnostics from them are currently
labelled by the imported module's own `source_path` after the fact
(`append_diagnostics`).

Not a global, not persisted, not compared across separate `analyze_source`
calls — exactly as the spec specifies, and matching `SemanticTypeRef`'s
existing per-`TypeInterner` scoping (`same_type` already only means
anything within one interner; `SourceId` gets the same discipline).

### `SourceSpan`

```cpp
// include/lumiere/analysis/source_id.hpp
struct SourceSpan
{
    SourceId source;
    std::size_t start;  // zero-based UTF-8 byte offset, inclusive
    std::size_t end;    // zero-based UTF-8 byte offset, exclusive
};
```

This is `SourceRange` plus a `SourceId` — deliberately, not a competing
representation. `SourceRange` stays exactly as it is (diagnostics don't need
multi-file identity, they already stamp `source_path` on the outside via
`append_diagnostics`). A free function

```cpp
[[nodiscard]] SourceSpan span_of(const Token &token, SourceId source);
```

builds one from a token the same way `source_range(token)` builds a
`SourceRange` today, so call sites that already have `const Token &`
add one argument, not a new construction path.

### `SymbolId`

```cpp
// include/lumiere/analysis/source_id.hpp
enum class SymbolId : std::uint32_t {};
```

One id per declaration recorded in a snapshot: every `SOIT`/`FIXE` binding
(module-level or local), every parameter, every `fonction`, `classe`,
`interface`, `type` alias, and every imported name bound by an `importer`
statement. Assigned in declaration order as `SemanticAnalyzer` walks the
tree — `declare_value`/`declare_type`/`push_scope`'s local bindings are the
exact existing call sites. `SymbolId` is snapshot-local exactly like
`SemanticTypeRef` already is: stable for the `SemanticModel` that produced
it, meaningless compared against another one, never serialized across a
process boundary. That constraint is already how this codebase treats
pointer-keyed data (`m_expression_types` is keyed by `const Expr *`), so
`SymbolId` is a safer version of the same idea — an opaque handle instead of
a raw pointer, so nothing outside `SemanticModel` can dereference it wrong
or outlive the AST it points into.

## The index: `Scope`, `Symbol`, `Occurrence`

```cpp
// include/lumiere/analysis/semantic_index.hpp

struct Symbol
{
    SymbolId id;
    SemanticSymbolKind kind;      // reuse the existing enum
    std::string name;
    SourceSpan declaration_span;  // the declaring token, e.g. the name in `soit x = ...`
    SourceSpan enclosing_span;    // the whole declaration's extent, for "declares" queries
    SemanticTypeRef type;         // nullptr until inference reaches it, same as today
    std::string documentation;    // hoisted from declaration_inspection_from_stmt's doc-comment scan
    ScopeId scope;                // the scope this symbol is visible in
};

enum class ScopeId : std::uint32_t {};

struct Scope
{
    ScopeId id;
    ScopeId parent;               // ScopeId{0} (the module scope) has no parent
    SourceSpan span;              // the block/function/module extent
    std::vector<SymbolId> symbols_by_declaration_order;
};

struct Occurrence
{
    SourceSpan span;      // where this identifier/member-name token sits
    SymbolId symbol;       // what it resolved to
    bool is_write;         // assignment target vs. read, mirrors diagnose_assignment_target
};

class SemanticIndex
{
public:
    [[nodiscard]] const Symbol *symbol(SymbolId) const;
    [[nodiscard]] const Scope *scope(ScopeId) const;
    // The hover/definition entry point: what, if anything, sits at this byte.
    [[nodiscard]] const Occurrence *occurrence_at(SourceId, std::size_t byte_offset) const;
    [[nodiscard]] std::span<const Occurrence> occurrences_of(SymbolId) const;
    // Name resolution as of a specific point, for completion later (Stage 2+)
    // and for tests: "what does `x` mean standing at this scope."
    [[nodiscard]] const Symbol *lookup(ScopeId, std::string_view name) const;

private:
    std::vector<Symbol> m_symbols;               // indexed by SymbolId
    std::vector<Scope> m_scopes;                  // indexed by ScopeId
    std::vector<Occurrence> m_occurrences;         // sorted by (source, start) for occurrence_at's binary search
    std::vector<SourceId> m_source_order;          // SourceRegistry's path<->id table
    std::vector<std::string> m_source_paths;
};
```

`SemanticIndex` becomes a new member of `SemanticModel`
(`SemanticModel::index`), populated alongside the existing maps rather than
replacing them. Nothing currently reading `m_value_symbols`,
`m_expression_types`, etc. changes in this stage — `find_value`/`find_type`/
`type_of` keep working exactly as they do, so the interpreter, the VM
compiler, and every existing analysis consumer are untouched. `SemanticIndex`
is additive, queried only by the new hover path.

### Populating it: hooking the existing walk, not adding a new one

Every place that already knows about a declaration or a resolved name gets
one more call:

- `declare_value` / `declare_type` (module level) and the `LocalBinding`
  insertions inside `push_scope`'s scopes (local level): after the existing
  `emplace`, also call `m_index.declare(kind, name_token, declaration_span,
  enclosing_span, current_scope)`, which allocates the next `SymbolId`,
  fills in `Symbol`, and appends it to the current `Scope`. `pop_scope`
  keeps discarding `m_scopes.back()` exactly as it does today — the walk's
  own resolution stack is still transient — but the `Symbol`/`Scope`
  records it produced along the way are not.
- Every place that already calls `find_value(name)`/`find_type(name)` to
  resolve an `IdentifierExpr` or `MemberAccessExpr` (the call sites at
  `semantic_analysis.cpp:1825`, `1978`, `2006`, `2426`, `3086`, etc. already
  found and enumerated above) additionally calls `m_index.record_occurrence
  (span_of(token, m_source), resolved_symbol_id, is_write)`. This is the
  bulk of the new code, but it is mechanical: each call site already has the
  token and already has the resolved symbol (or bails with a diagnostic if
  it doesn't) — it is adding one line next to work already done, not
  redoing the resolution.
- Member accesses (`MemberAccessExpr`) resolve through `type_of(object)` +
  a class/interface member lookup, which today lives partly in
  `semantic_analysis.cpp` (`diagnose_*` paths) and partly duplicated in
  `inspection.cpp` (`find_type_declaration` + `find_class_member_statement`).
  Stage 1 consolidates that lookup into one function used by both — the
  analyzer's own member-access type-checking and the new occurrence
  recording call it once and share the answer, instead of the class member
  being looked up once for type-checking and a second time, heuristically,
  by `inspection.cpp` at hover time. This is the direct fix for the
  cross-file member-doc gap noted while testing the Stage 0 inspection fix
  (`inspect_source`'s `member_declaration_inspection` only ever found a
  member declared in the same file, because it re-searched
  `analysis.statements` instead of consulting anything that knew about
  imported classes) — with a real index, a member on an imported class
  resolves to a `Symbol` whose `declaration_span` names the imported file,
  because that file's statements were already walked into the same
  `SemanticIndex` as part of `build_import_environment`'s existing traversal.

### Recovery

The spec calls for "compiler-unit tests for binding and recovery" — the
recovery half matters because this index is fed by a tree the parser may
have partially recovered from a syntax error (`Parser` already does error
recovery; `AnalysisResult` still returns partial `statements` with
diagnostics attached). Two rules keep the index well-formed even from a
partial tree:

1. **A `Symbol` is only ever recorded for a statement that survived parsing.**
   If the parser dropped a malformed declaration, there is nothing to index
   — same as today, where `m_value_symbols` simply doesn't gain an entry.
2. **An occurrence with no resolvable symbol is not recorded, never recorded
   with a dangling/sentinel `SymbolId`.** `occurrence_at` returning `nullptr`
   is a normal, expected answer (same contract `inspect_source` already has
   for "nothing hoverable here"), not a distinct error case callers must
   special-case.

Both rules mean `occurrence_at`/`occurrences_of` are safe to call on any
snapshot, including one built from source with diagnostics — which matters
because editor hover requests arrive constantly while the user is mid-edit,
i.e. exactly when the tree is most likely to be incomplete.

## What `inspection.cpp` looks like after this

`inspect_source`'s job shrinks to: lex for the token under the cursor (kept,
cheap, still needed to turn a byte offset into "which token"), run
`analyze_source` as it does today, then:

```cpp
if (const Occurrence *occ = analysis.model->index.occurrence_at(SourceId{0}, byte_offset))
{
    const Symbol &sym = *analysis.model->index.symbol(occ->symbol);
    return inspection_from_symbol(sym);   // one small formatter, replaces
                                           // declaration_inspection_from_stmt,
                                           // member_declaration_inspection,
                                           // find_type_declaration, and the
                                           // hand-rolled cross-file gap
}
// stdlib / keyword / builtin fallback paths (qualified_member_inspection's
// stdlib-registry half, keyword_detail) are untouched -- they don't name a
// Symbol because there isn't a user declaration to index.
```

`imported_value_inspection` (resolving `Module.name` documentation for a
*module-qualified* reference, not a member access on a value) folds in the
same way once `importer` bindings get their own `Symbol`s.

This is also where Stage 1 pays for itself immediately, independent of
Stage 2+: it is a straight correctness and simplicity win for the *existing*
one-shot `inspect` command, not something that only starts working once the
persistent service (Stage 4) exists.

## Rollout inside Stage 1

1. Add `source_id.hpp` (`SourceId`, `SourceSpan`, `SourceRegistry`) and
   `semantic_index.hpp` (`Symbol`, `Scope`, `Occurrence`, `SemanticIndex`) —
   no analyzer changes yet, just the types, with unit tests directly against
   `SemanticIndex` (empty-index queries, manually constructed small
   indexes) to pin down the API before wiring it into a 4400-line file.
2. Wire `SymbolId` allocation into `declare_value`/`declare_type`/local
   bindings only — no occurrence recording yet. Add binding tests: every
   `TEST(SemanticAnalysis, ...)` fixture that currently checks
   `model.find_value(...)` gets a twin assertion that a `Symbol` exists with
   the right kind/name/span, run side by side so a regression shows up as a
   mismatch between the old and new bookkeeping, not just a new test that
   can silently drift from what the old path actually does.
3. Wire occurrence recording into the existing resolution call sites, one
   expression kind at a time (`IdentifierExpr` reads, then writes, then
   `MemberAccessExpr`), each with recovery tests: feed a source with a
   deliberate syntax error near/inside the construct and assert
   `occurrence_at` never returns a dangling id and never throws.
4. Switch `inspect_source`'s member-access and identifier-hover paths over
   to `occurrence_at`, keeping the stdlib/keyword fallback paths as they are.
   Run the full existing `SourceInspection` suite unchanged as the
   regression gate — every current test should still pass with the new path
   underneath it; this is what proves the index is a faithful superset, not
   a divergent second implementation.
5. Only then delete the heuristics it replaced
   (`find_type_declaration`, `member_declaration_inspection`,
   `qualified_member_inspection`'s user-declaration half,
   `find_class_member_statement`/`find_interface_member_statement`) so there
   is a clean point to revert to if step 4 surfaces a behavioral gap.

## Explicit non-goals for Stage 1

- No wire protocol, no `lumiere-tooling` changes. This is a pure `lumiere`
  compiler-side data structure and query API. Stage 4 ("persistent service")
  is what exposes it over stdio.
- No `DocumentVersion`/`ContentVersion`/overlay handling — that's Stage 2
  ("workspace engine"). `SemanticIndex` here is built fresh by one
  `analyze_source` call, same lifetime as `AnalysisResult` today.
- No definition/references/rename/completion features — `occurrences_of`
  exists because hover's own correctness benefits from it now (a hover on a
  declaration should be able to say how it's used) and because it is the
  obvious foundation those Stage 3 features will need, not because Stage 1
  implements them.
- Thread-safety is not a design constraint yet (one-shot process, same as
  today); the data model above avoids anything that would make it hard
  later (no shared mutable state outside `SemanticModel` construction, ids
  instead of pointers so a future immutable-snapshot wrapper is a type
  change, not a rewrite) but Stage 1 itself doesn't need to solve it.

## Open questions for review

1. **Scope granularity**: should every `si`/`pour`/`tant que` block get its
   own `ScopeId`, matching `push_scope`'s current granularity exactly, or is
   that finer than any Stage 1+ consumer needs? Matching it exactly is
   listed above because it's free (the call sites already exist) and
   diverging would be its own source of bugs — flagging in case there's a
   reason to collapse it.
2. **Symbol documentation**: `declaration_inspection_from_stmt` currently
   re-scans leading comment tokens for a doc comment at format time. Moving
   that scan to declaration time (stored on `Symbol`) is proposed above
   since it only ever needs to happen once per declaration; confirming that's
   wanted rather than kept lazy.
3. **`Résultat` propagation/obligation tracking** (`m_obligations`,
   `ResultObligation`) is scope-stack-adjacent but is a distinct correctness
   feature (unused-Résultat diagnostics), not a naming/hover concern. This
   design leaves it entirely alone — flagging so it's clear that's a
   deliberate exclusion, not an oversight.

## Addendum (after implementing step 1/2): `SymbolNamespace`

The original `Symbol`/`Scope` design above stored one flat, name-keyed list
per scope. Wiring `declare_value`/`declare_type` into it surfaced a real gap:
a `classe Point` declaration calls *both* `declare_type` (registering
"Point" as a type, for annotations) *and* `declare_value` (registering
"Point" as a value, for its constructor call `Point()`) — exactly mirroring
`SemanticModel`'s own `m_type_symbols`/`m_value_symbols` split. A single
flat per-scope symbol list let the value declaration shadow the type
declaration for `lookup`, so a query for "Point" the type silently got back
"Point" the constructor instead, with `type == nullptr`. A binding test
(`SemanticIndexBinding.RecordsAModuleLevelTypeDeclarationMatchingFindType`)
caught this immediately, comparing against `find_type` as the design's
rollout step 2 intended.

Fixed by adding `SymbolNamespace { Value, Type }` to `Symbol`, threaded
through `declare()` and `lookup()`. This is additive to the types section
above, not a revision of it: `Scope::symbols` still holds one list, `Symbol`
just carries which of the compiler's two existing namespaces it belongs to,
and `lookup(scope, name, space)` filters on it. `occurrence_at` needs no
change — an occurrence's `SymbolId` already points at the one, specific
`Symbol` (type or value) its containing analyzer call site resolved against
`find_type`/`find_value`, so the ambiguity only existed in the *lookup*
convenience function, not in the record itself.
