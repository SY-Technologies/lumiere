# Typed result handling

Status: approved language design

This document defines recoverable errors, result consumption, explicit
handling, propagation, error unions, and result aliases in Lumiere.

## 1. Core model

A recoverable operation returns:

```lumiere
Résultat[T, E]
```

`T` is the success type. `E` must be an error type: `Erreur`, a class that
realizes `Erreur`, a subclass of such a class, or a union containing only such
types.

`Résultat[T, E]` has exactly two immutable variants:

```lumiere
Succès(valeur: T)
Échec(erreur: E)
```

Exactly one variant exists at runtime. A result cannot contain both a success
and an error, and cannot contain neither.

```lumiere
fonction diviser(a: Entier, b: Entier)
    -> Résultat[Entier, DivisionParZéro] {

    si b == 0 {
        retourne Échec(DivisionParZéro())
    }

    retourne Succès(a / b)
}
```

`Résultat`, `Succès`, and `Échec` are prelude names. `ignorer` and `propager`
are keywords used by the explicit result-control syntax.

`Résultat` requires exactly two type arguments. `Succès` and `Échec` each
require exactly one payload. Missing or additional arguments are static errors.

## 2. Construction

### 2.1 Explicit variants

A function returning a result must return a result variant or another
compatible result:

```lumiere
fonction calculer() -> Résultat[Entier, ErreurCalcul] {
    retourne Succès(42)
}
```

A raw success value is not implicitly wrapped:

```lumiere
fonction calculer() -> Résultat[Entier, ErreurCalcul] {
    retourne 42 // invalid
}
```

A raw error is not implicitly wrapped:

```lumiere
fonction calculer() -> Résultat[Entier, ErreurCalcul] {
    retourne ErreurCalcul() // invalid
}
```

### 2.2 Contextual typing

The expected result type supplies the variant's missing type parameter:

```lumiere
fonction lire() -> Résultat[Texte, ErreurFichier] {
    retourne Succès("contenu")
}
```

`Succès(value)` is compatible with `Résultat[T, E]` when `value` is assignable
to `T`. `Échec(error)` is compatible when `error` is assignable to `E`.

`Succès` and `Échec` are contextual constructors rather than independently
typed wrapper functions. Their complete `Résultat[T, E]` type must be supplied
by a result-returning function signature, an annotated binding or assignment,
a result-typed parameter, or an explicitly typed aggregate element:

```lumiere
soit résultat: Résultat[Entier, ErreurCalcul] = Succès(42)
```

Construction in a non-result context is rejected. This includes returning a
variant from a function whose return type is `Rien`, `Universel`, or any other
non-result type; passing a variant to `Universel`; and writing a bare variant
expression. A function without an explicit result return type cannot infer one
from `Succès` or `Échec`.

### 2.3 Nested results

Nested results are legal and preserve both layers:

```lumiere
Résultat[Résultat[Entier, ErreurInterne], ErreurExterne]
```

Matching or propagating the outer result unwraps only the outer layer.

## 3. Error types

`Erreur` is a built-in, zero-method marker interface. Every recoverable error
class opts into the error domain explicitly:

```lumiere
classe ErreurFichier réalise Erreur {
    soit chemin: Texte
    soit raison: Texte
}
```

Error values remain ordinary typed values. They may contain structured data
such as a path, source offset, status code, cause, or domain identifier.

`E` may be `Erreur` itself, one concrete error class, a subclass of an error
class, or a closed union composed entirely of error types:

```lumiere
Résultat[Texte, Erreur]
Résultat[Texte, ErreurFichier]
Résultat[Texte, ErreurFichier | ErreurPermission]
```

The rule is checked after alias expansion and recursively inside nested generic
types. Every alternative in an error union must satisfy it. Consequently these
types are invalid:

```lumiere
Résultat[Texte, Texte]
Résultat[Texte, Rien]
Résultat[Texte, Universel]
Résultat[Texte, ErreurFichier | Texte]
Résultat[Texte, Résultat[Entier, ErreurInterne]]
```

`T` may be `Rien`; `Résultat[Rien, E]` represents a fallible operation with no
success payload. `E` may never be `Rien`. An error class may contain arbitrary
fields, including another result, but a result type is not itself an error type.

## 4. Error unions

Several possible errors are written as a union inside the error parameter:

```lumiere
fonction charger(chemin: Texte)
    -> Résultat[Configuration, ErreurFichier | ErreurSyntaxe]
```

The success/error distinction remains explicit:

```lumiere
Résultat[Configuration, ErreurFichier | ErreurSyntaxe]
```

is not equivalent to:

```lumiere
Configuration | ErreurFichier | ErreurSyntaxe
```

### 4.1 Union normalization

Unions are:

- unordered: `A | B` equals `B | A`
- idempotent: `A | A` equals `A`
- flattened: `A | (B | C)` equals `A | B | C`
- closed: all alternatives are known during semantic analysis

An empty union is invalid. A one-member union normalizes to that member.

### 4.2 Error compatibility

A concrete source error is compatible with a target error union when it is
assignable to at least one alternative.

A source error union is compatible with a target error type when every source
alternative is accepted by the target.

```text
ErreurFichier
    -> ErreurFichier | ErreurSyntaxe       valid

ErreurFichier | ErreurSyntaxe
    -> ErreurFichier                       invalid
```

Compatibility is checked from static types. The compiler cannot assume that a
wider source union will happen to contain a narrower alternative at runtime.

### 4.3 Open error set

The built-in `Erreur` interface is open. Matching it by concrete implementing
classes is not exhaustive. A catch-all failure branch is required.

Closed unions support exhaustive handling because every alternative is known.

## 5. Type aliases

Type aliases are transparent:

```lumiere
type Lecture = Résultat[Texte, ErreurFichier]
```

`Lecture` and `Résultat[Texte, ErreurFichier]` are the same semantic type.
The alias introduces no wrapper, constructor, variant, allocation, or runtime
identity.

### 5.1 Result aliases are allowed

A function may use a result alias in its signature:

```lumiere
type Lecture = Résultat[Texte, ErreurFichier]

fonction lire(chemin: Texte) -> Lecture {
    retourne Succès("contenu")
}
```

Because `Lecture` resolves to `Résultat[Texte, ErreurFichier]`:

- the returned value cannot be discarded
- the function is a valid propagation boundary
- `Succès` and `Échec` receive contextual types from the alias
- callers may match the result normally
- assignability uses the expanded type

The signature need not spell `Résultat` directly. It must explicitly name a
type that resolves transparently to `Résultat[T, E]`.

### 5.2 Alias chains

Alias chains are allowed:

```lumiere
type LectureBrute = Résultat[Texte, ErreurFichier]
type Lecture = LectureBrute
type Chargement = Lecture
```

`Chargement` resolves recursively to the same result type and carries every
result rule.

### 5.3 Aliased components

The success type, error type, and error union may also be aliases:

```lumiere
type Contenu = Texte
type ErreursLecture = ErreurFichier | ErreurPermission
type Lecture = Résultat[Contenu, ErreursLecture]
```

All compatibility and exhaustiveness checks use fully resolved types.

### 5.4 Alias cycles

Direct and indirect alias cycles are invalid:

```lumiere
type A = A

type B = C
type C = B
```

The compiler must report the complete cycle.

### 5.5 Aliases are not constructors

This is invalid:

```lumiere
Lecture("contenu")
```

The value must still be constructed with a result variant:

```lumiere
soit lecture: Lecture = Succès("contenu")
```

### 5.6 Parameterized aliases

This design defines concrete aliases only. It does not define generic alias
parameters such as:

```lumiere
type Sortie[T, E] = Résultat[T, E]
```

If generic aliases are added to Lumiere later, their fully instantiated form
must obey the same transparent-resolution rules.

### 5.7 Imported aliases

An exported alias remains transparent when imported. Only `public type`
declarations are importable from another module. Import aliases do not create
a nominal type:

```lumiere
importer Fichiers.{Lecture}

fonction charger() -> Lecture {
    // Lecture resolves to its declaration in Fichiers
}
```

Changing a public alias from a result to a non-result, or changing its success
or error type, changes the public API and requires dependent code to be checked
again.

A public alias may refer only to types that are themselves public or built in.
Exposing a module-private class, interface, or alias target is rejected at the
alias declaration.

### 5.8 Top-level result requirement

A signature qualifies as result-returning only when its fully resolved
top-level type is `Résultat[T, E]`.

These qualify:

```lumiere
Résultat[Texte, ErreurFichier]
Lecture // alias resolving directly to Résultat[Texte, ErreurFichier]
```

These do not qualify:

```lumiere
Liste[Résultat[Texte, ErreurFichier]]
Universel
Résultat[Texte, ErreurFichier] | Rien
```

A top-level function return type containing a result as one union alternative
is rejected because it hides whether the function has a fallible contract. An
API that needs optional success must place the optionality inside the result:

```lumiere
Résultat[Texte | Rien, ErreurFichier]
```

A batch API may intentionally return a collection of independent results:

```lumiere
Liste[Résultat[Texte, ErreurFichier]]
```

The collection itself is not subject to the result-return non-discard rule.
Its contained results are data to be processed by the consumer.

### 5.9 Declaration scope, visibility, and namespace

Type aliases are declared only at module/file scope in this design. They cannot
be declared inside functions, classes, interfaces, or ordinary blocks.

An unmodified alias is internal to its module. A public alias uses the existing
top-level visibility modifier:

```lumiere
public type Lecture = Résultat[Texte, ErreurFichier]
```

`privé type` is invalid at module scope, matching other top-level declarations.

Aliases, classes, interfaces, and built-in types share the type namespace. A
module cannot declare two type names that collide.

Aliases may refer to a type declared later in the same module. Resolution is
therefore at least two-pass: collect type declarations first, then resolve
their definitions and reject cycles.

## 6. Function signatures

Every recoverably fallible function, method, interface method, function
expression, and native function must explicitly declare a result return type
or a transparent alias resolving to one.

```lumiere
fonction charger() -> Résultat[Configuration, ErreurChargement]
```

### 6.1 No inferred propagation contract

Propagation cannot infer, insert, or widen a function's return type.

This is not a propagation boundary:

```lumiere
fonction charger() {
    // no declared Résultat return type
}
```

This is not a propagation boundary either:

```lumiere
fonction charger() -> Configuration {
}
```

The caller-visible error contract must appear explicitly in the signature,
possibly through a transparent alias.

Every reachable normal exit from a result-returning function must return a
compatible result. Falling off the end is invalid, including when `T` is
`Rien`. Such a function must explicitly return `Succès(rien)` when appropriate.

### 6.2 Nested functions

Only the nearest function boundary matters.

```lumiere
fonction externe() -> Résultat[Rien, ErreurExterne] {
    soit action = fonction() {
        // cannot propagate through externe
    }
}
```

The nested function must declare its own compatible result type:

```lumiere
fonction externe() -> Résultat[Rien, ErreurExterne] {
    soit action = fonction() -> Résultat[Rien, ErreurInterne] {
        // may propagate ErreurInterne here
    }
}
```

An alias resolving to that result type is equivalent.

### 6.3 Methods and interfaces

A fallible method must declare its result type. An interface method must expose
the same contract.

An overriding method must preserve the expanded result signature required by
the existing override rules. Two aliases with the same expansion are equal for
this check; aliases with different expansions are not.

### 6.4 Result erasure at return boundaries

A function cannot hide a directly returned result behind `Universel`, a union,
or another broad declared return type:

```lumiere
fonction lire() -> Universel {
    retourne Succès("contenu") // invalid: result contract is erased
}
```

To return results as ordinary aggregate data, the wrapper must be explicit in
the signature:

```lumiere
fonction lire_tous()
    -> Liste[Résultat[Texte, ErreurFichier]] {
    // valid batch-result API
}
```

### 6.5 Callable and overload resolution

The non-discard and propagation rules use the statically resolved callable
signature after overload resolution. They apply equally to named functions,
methods, function values, interface calls, and native functions.

If a callable's static return type is `Universel`, a runtime result value does
not retroactively make the call result-returning. A fallible API must not erase
its result contract behind such a signature.

## 7. Non-discard rule

The returned value of a function whose fully resolved return type is
`Résultat[T, E]` cannot be silently discarded.

```lumiere
Fichier.lire(chemin) // invalid
```

This rule also applies when the declared return type is a transparent result
alias:

```lumiere
type Lecture = Résultat[Texte, ErreurFichier]

fonction lire(chemin: Texte) -> Lecture

lire("notes.txt") // invalid
```

Ordinary returned values remain discardable:

```lumiere
calculer_total() // valid when it returns Entier
```

### 7.1 Consuming contexts

A returned result is not discarded when it is:

- returned from a compatible result-returning function
- matched with `agir selon`
- propagated according to section 9
- passed as an argument
- stored in a local, field, object, or collection
- used by another operation
- consumed by an `ignorer` statement
- displayed by the REPL

### 7.2 Bare calls

A result-returning call cannot be an expression statement:

```lumiere
Fichier.supprimer(chemin) // invalid
```

A bare ordinary call remains valid even when it returns a non-`Rien` value:

```lumiere
calculer_total() // valid
```

### 7.3 Result bindings

Storing a result in a local transfers the obligation to that binding:

```lumiere
soit résultat = Fichier.lire(chemin)
```

The binding must be used before it is overwritten or leaves scope.

```lumiere
soit résultat = Fichier.lire(chemin)
résultat = Fichier.lire(autre_chemin) // invalid if the first value was unused
```

The check is path-sensitive. Every reachable path that evaluates a
result-producing expression must use or explicitly ignore that result before
the responsible binding is overwritten or leaves scope.

```lumiere
soit résultat = Fichier.lire(chemin)

si condition {
    afficher(type_de(résultat))
}
// invalid: the false path leaves résultat unused
```

Using a result does not consume or move the runtime value. It may be inspected
again. The analysis tracks whether the obligation has been satisfied, not
whether ownership has moved.

### 7.4 Result parameters

A parameter whose type resolves directly to `Résultat[T, E]` begins with an
unsatisfied use obligation. Every reachable function exit must use, transfer,
or explicitly ignore it.

```lumiere
fonction traiter(résultat: Résultat[Texte, ErreurFichier]) {
    // invalid if résultat is unused
}
```

Parameters containing results indirectly, such as
`Liste[Résultat[T, E]]`, do not receive this automatic obligation.

### 7.5 Assignment and copying

Assigning a result to another result binding transfers the outstanding
obligation to the destination for static analysis:

```lumiere
soit premier = Fichier.lire(chemin)
soit second = premier
```

If either binding is subsequently observed, returned, stored, passed, matched,
or ignored, the obligation is satisfied. Runtime representation may copy the
immutable value; no move-only semantics are introduced.

Producing another result creates another obligation.

A declared but uninitialized result binding creates no obligation until a
result value is assigned. Ordinary definite-initialization rules still forbid
reading it before assignment.

### 7.6 Branches and loops

Obligations are checked on every reachable control-flow edge:

- before `retourne`
- before `arrêter`
- before `continuer`
- at normal scope exit
- before overwriting the responsible binding
- on each loop iteration that produces a new result

A result produced only on one branch creates an obligation only on paths where
that branch executes.

### 7.7 Passing and storage

Passing a result as an argument satisfies the caller's immediate obligation and
transfers responsibility to the receiving API.

If the receiving parameter is statically typed as `Résultat[T, E]`, section
7.4 requires the callee to use it.

Passing a result through `Universel` or storing it in a broad container counts
as an explicit transfer at the call site. This design does not track the
result's eventual handling after type erasure or arbitrary storage.

Storing a result in a field or collection likewise satisfies the immediate
obligation. The container may later expose the result for handling.

A pure cast or widening conversion does not by itself satisfy the obligation
when its converted value is immediately discarded:

```lumiere
Fichier.lire(chemin) en Universel // invalid
```

The converted value must be passed, stored, returned through an allowed
aggregate API, otherwise used, or the original result must be consumed by an
`ignorer` statement.

### 7.8 Explicit discard

`ignorer` is the only direct way to intentionally discard a result:

```lumiere
ignorer Fichier.supprimer(chemin_temporaire)
```

It is a statement, not a function call. Its operand is evaluated exactly once
and must statically resolve directly to `Résultat[T, E]`. The statement
completes with `Rien`. Function-call spelling such as `ignorer(expression)` is
rejected.

Assignment to `_` is not an alternative:

```lumiere
_ = Fichier.supprimer(chemin) // invalid
```

An exhaustive match with wildcard payloads is also an explicit use:

```lumiere
agir selon Fichier.supprimer(chemin) {
    Succès(_) -> {}
    Échec(_) -> {}
}
```

### 7.9 REPL

The REPL displays a top-level result and therefore does not discard it.

```text
>>> Fichier.lire("notes.txt")
Succès("...")
```

Source files do not display expression statements implicitly.

### 7.10 Guarantee boundary

The rule guarantees that a directly produced result is not silently dropped at
its call site or through an obviously unused result binding or parameter.

It does not prove eventual handling after the result is stored in arbitrary
object graphs, collections, foreign code, or type-erased values. That stronger
guarantee requires an ownership or effect system and is outside this design.

The rule guarantees use, not recovery. Logging, comparing, forwarding, storing,
or explicitly ignoring a result satisfies the rule even when no recovery is
attempted.

## 8. Explicit handling

`agir selon` supports result constructor patterns and may be used as a
statement or expression.

### 8.1 Statement form

```lumiere
agir selon Fichier.lire(chemin) {
    Succès(texte) -> afficher(texte)
    Échec(erreur) -> afficher(erreur)
}
```

### 8.2 Expression form

```lumiere
soit texte = agir selon Fichier.lire(chemin) {
    Succès(valeur) -> valeur
    Échec(_) -> contenu_par_défaut
}
```

Every normally completing branch of an expression-valued match must produce a
compatible type. A branch ending in `retourne` does not contribute a value to
the branch result type.

### 8.3 Patterns

Result patterns are:

```lumiere
Succès(valeur)
Succès(_)
Échec(erreur)
Échec(erreur: TypeErreur)
Échec(_)
```

Bindings exist only inside their branch. The matched expression is evaluated
exactly once.

### 8.4 Exhaustiveness

A match over `Résultat[T, E]` must handle success and every possible error.

For a concrete error type:

```lumiere
agir selon résultat {
    Succès(valeur) -> utiliser(valeur)
    Échec(erreur) -> signaler(erreur)
}
```

For a closed union:

```lumiere
agir selon résultat {
    Succès(valeur) -> utiliser(valeur)
    Échec(e: ErreurFichier) -> réparer(e)
    Échec(e: ErreurSyntaxe) -> signaler(e)
}
```

`Échec(error)` and `Échec(_)` cover every remaining error alternative.

Matching an open interface requires a catch-all failure branch. Concrete
implementation branches alone are not exhaustive.

### 8.5 Overlapping patterns

Branches are evaluated in source order. A branch whose error type accepts a
later branch's entire type makes the later branch unreachable and is rejected.

```lumiere
agir selon résultat {
    Succès(valeur) -> utiliser(valeur)
    Échec(e: Universel) -> signaler(e)
    Échec(e: ErreurFichier) -> réparer(e) // unreachable
}
```

### 8.6 Aliased results

Matching a transparent result alias is identical to matching its expanded
result type. Exhaustiveness uses the expanded error type and union.

## 9. Propagation

Propagation extracts a success value for local use and returns an unchanged
failure from the nearest enclosing function.

The concise propagation form is:

```lumiere
soit texte = Fichier.lire(chemin) ou propager
```

`ou propager` is a postfix result-control form. It is not boolean `ou`, does
not evaluate a right-hand expression, and is valid only at a compatible
function boundary.

### 9.0 Normative invariants

Every implementation and backend must preserve all of these invariants:

1. After complete alias expansion, the operand's top-level type is exactly
   `Résultat[T,E]`. A container, union, `Universel`, or object containing a
   result does not qualify.
2. The nearest enclosing callable explicitly declares a return type resolving
   directly to `Résultat[U,F]`. An outer function, caller, class, block, loop,
   or inferred return type cannot supply the boundary.
3. Every possible member of `E` is assignable to `F`. Compatibility is static;
   a runtime value that happens to be narrower cannot justify propagation.
4. Success produces the original payload with static type `T`. Propagation
   does not wrap, convert, clone, or widen it.
5. Failure exits the nearest callable with the original `Échec` value. The
   variant tag, payload identity, concrete error type, and diagnostic origin
   are preserved.
6. The operand is evaluated exactly once on both success and failure.
7. Failure follows the same lexical-scope and frame cleanup rules as an
   ordinary `retourne`, but performs no exception-handler search.
8. Propagation never catches or converts a trap. Traps and recoverable
   `Échec` values remain disjoint.
9. Propagation never infers, widens, or changes a callable signature and never
   translates an error. Translation requires an explicit new `Échec`.
10. The operand's Result obligation is consumed. If `T` is itself a Result,
    the exposed inner Result retains a non-discard obligation.
11. Every previously pending Result obligation must already be satisfied on
    the possible failure exit. Code after `ou propager` cannot retroactively
    consume a value on the path that propagation exits.
12. In `agir selon`, `-> propager` is legal only for a branch consisting
    exclusively of `Échec` patterns. Its propagated error type is the union of
    the branch's narrowed failure patterns, not automatically the operand's
    complete error type.
13. A concrete branch may narrow an open `Erreur` operand and propagate that
    concrete error, but the remaining open set still requires an untyped
    `Échec(...)` catch-all or `sinon`.
14. Aliases, imported types, subclasses, error unions, methods, anonymous
    functions, default-parameter expressions, and module qualification do not
    weaken any rule above.
15. Both semantic analysis and runtime backends enforce the boundary. Runtime
    checks are defensive and do not replace static rejection.

The equivalent explicit match remains available:

```lumiere
soit texte = agir selon Fichier.lire(chemin) {
    Succès(valeur) -> valeur
    Échec(erreur) -> retourne Échec(erreur)
}
```

### 9.1 Static requirements

Given:

```text
operand: Résultat[T, E]
enclosing return type: Résultat[U, F]
```

propagation is valid only when:

1. the operand's fully resolved top-level type is `Résultat[T, E]`
2. the nearest function explicitly declares `Résultat[U, F]` or a transparent
   alias resolving to it
3. `E` is assignable to `F`

The compiler cannot infer or modify the enclosing signature.

### 9.2 Runtime behavior

The operand is evaluated exactly once.

```text
Succès(value) -> the match expression evaluates locally to value
Échec(error)  -> the enclosing function returns Échec(error)
```

Only the nearest function is exited. Loops, conditionals, and ordinary blocks
do not form propagation boundaries.

### 9.3 Aliased signatures and operands

Aliases are resolved before propagation checks:

```lumiere
type Lecture = Résultat[Texte, ErreurFichier]
type Chargement = Résultat[Configuration, ErreurFichier | ErreurSyntaxe]

fonction charger() -> Chargement {
    soit texte = lire() ou propager // lire() returns Lecture

    retourne analyser(texte)
}
```

This is valid because both aliases resolve to compatible result types.

### 9.4 Error widening

An error may propagate into a wider accepted union or into `Erreur`. No new
error value is constructed.

```text
ErreurFichier
    -> ErreurFichier | ErreurSyntaxe
```

### 9.5 Incompatible errors

Propagation is rejected when any possible operand error is not accepted by the
enclosing error type:

```lumiere
fonction charger() -> Résultat[Configuration, ErreurSyntaxe] {
    soit texte = Fichier.lire("app.conf") ou propager // invalid ErreurFichier
}
```

### 9.6 Error translation

Propagation never converts errors. Translation requires explicit construction:

```lumiere
soit texte = agir selon Fichier.lire(chemin) {
    Succès(valeur) -> valeur

    Échec(cause) -> retourne Échec(
        ErreurConfiguration(cause: cause)
    )
}
```

### 9.7 Direct forwarding

When the success value is not needed locally, the complete result can be
returned directly:

```lumiere
fonction charger(chemin: Texte)
    -> Résultat[Configuration, ErreurChargement] {

    retourne analyser_configuration(chemin)
}
```

The returned result must be assignable to the declared result type.

### 9.8 Nested functions

An outer result signature cannot authorize propagation through a nested
function without its own result signature.

```lumiere
fonction externe() -> Résultat[Rien, ErreurExterne] {
    soit action = fonction() {
        soit valeur = opération() ou propager // invalid in this nested function
    }
}
```

### 9.9 Nested results

Propagation of:

```lumiere
Résultat[Résultat[T, EInterne], EExterne]
```

returns `EExterne` on outer failure and produces
`Résultat[T, EInterne]` locally on outer success. The inner result retains its
own non-discard obligation when it originates from a result-returning call.

### 9.10 Control-flow locations

Propagation is valid inside loops, conditionals, and nested ordinary blocks
when their nearest function has a compatible result signature.

Any language-managed lexical scope cleanup required by an ordinary `retourne`
must also occur when propagation exits the function. No source-level exception
handler search occurs.

### 9.11 Diagnostic origin

Creating an `Échec` associates its source location with the failure when no
origin is already present. Forwarding the same error preserves its original
origin. Constructing a different domain error establishes a new origin and may
store the previous error explicitly as its cause.

Aliases do not affect diagnostic metadata. Implementations may additionally
record forwarding sites, but those sites are not observable program data and
do not affect type identity.

### 9.12 Match-branch terminators

A result match may use `propager` directly only on a branch composed
exclusively of `Échec` patterns:

```lumiere
agir selon opération() {
    Succès(v) -> utiliser(v)
    Échec(e) -> propager
}
```

The propagated error alternatives must be assignable to the nearest explicit
result return type. `Succès(...) -> propager`, propagation from a non-result
match, and propagation through an implicit or non-result function boundary are
static errors.

`ignorer` may terminate a branch composed exclusively of `Succès` or `Échec`
patterns. It explicitly discards the matched result:

```lumiere
agir selon opération() {
    Succès(_) -> ignorer
    Échec(_) -> ignorer
}
```

### 9.13 Propagation edge cases

| Case | Required behavior |
|---|---|
| Operand is not a top-level `Résultat[T,E]` | Reject |
| Operand is `Universel` whose runtime value is a Result | Reject from its static type |
| Operand is `Liste[Résultat[T,E]]` | Reject; select and handle an element explicitly |
| Operand is `Résultat[T,E] | Rien` | Reject; Result cannot be a return-union alternative |
| Nearest function has no explicit return type | Reject |
| Nearest function returns a non-Result type or `Universel` | Reject |
| An outer function returns Result but the nested function does not | Reject in the nested function |
| Operand error is a subtype of the declared error | Allow |
| Operand error is one member of the declared error union | Allow |
| Operand error union contains one unaccepted member | Reject the whole postfix propagation |
| Operand error is `Erreur`, target is one concrete error | Reject |
| Operand error is concrete, target is `Erreur` | Allow |
| Typed `Échec` branch narrows open `Erreur` to a concrete accepted error | Allow; require coverage of the open remainder |
| One propagating branch contains several typed `Échec` patterns | Validate the union of all those patterns |
| Propagating branch contains `Succès`, literal, type-binding, or wildcard patterns | Reject |
| Success payload is `Rien` | Continue locally with `rien` |
| Success payload is another Result | Preserve the inner non-discard obligation |
| Bare propagation exposes an inner Result and discards it | Reject |
| Another Result is pending before propagation | Reject unless it is already consumed on the failure path |
| Result is consumed only by code after propagation | Reject; that code is absent from the failure path |
| Operand has side effects | Execute it exactly once |
| Failure crosses several propagating functions | Preserve the original error object and origin through every frame |
| Error must change domains | Require explicit matching and `Échec(NewError(...))` |
| `retourne source() ou propager` where success is a raw `T` | Reject unless `T` is itself assignable to the declared Result; normally return the complete Result directly |
| Propagation occurs in a loop, conditional, or ordinary block | Use the nearest function as the boundary |
| Propagation occurs at module top level or in a field initializer | Reject because no callable Result boundary exists |
| Propagation occurs in a default argument | Use the callable owning that default expression as the boundary |
| Failure reaches Result-returning `principal` | Preserve the failure; report an unsuccessful process exit |
| Operand traps while being evaluated | Propagate no Result; report the trap normally |
| `a ou propager ou b` or `propager` without `ou` | Reject syntactically |

## 10. Result assignability

`Résultat` is immutable. Result assignability is covariant in both parameters:

```text
Résultat[T1, E1] -> Résultat[T2, E2]
```

is valid when:

```text
T1 -> T2
E1 -> E2
```

Examples:

```text
Résultat[Chien, ErreurFichier]
    -> Résultat[Animal, ErreurFichier | ErreurSyntaxe]  valid

Résultat[Animal, ErreurFichier | ErreurSyntaxe]
    -> Résultat[Chien, ErreurFichier]                   invalid
```

Aliases are expanded before this check.

## 11. Results and traps

Use `Résultat` for failures a reasonable caller may handle, including external
I/O, invalid external data, refused operations, timeouts, and domain
validation.

Use a trap for programmer defects and invalid runtime state, including broken
bytecode, corrupted interpreter state, and failed invariants.

Traps are not `Échec` values, are not matched by `agir selon`, and cannot be
propagated as results unless an API explicitly catches a lower-level host
failure and constructs a typed Lumiere error at its boundary.

An API may provide both forms:

```lumiere
liste[index]         // traps when the index invariant is violated
liste.obtenir(index) // returns Résultat[T, ErreurIndice]
```

## 12. Entry point

`principal` may return `Rien` or a result.

```lumiere
fonction principal() {
}
```

```lumiere
fonction principal() -> Résultat[Rien, Erreur] {
    retourne démarrer()
}
```

A transparent alias is allowed:

```lumiere
type SortieApplication = Résultat[Rien, Erreur]

fonction principal() -> SortieApplication {
    retourne démarrer()
}
```

At process exit:

- `Succès(rien)` produces status `0`
- `Échec(error)` produces a nonzero status and prints the error value with its
  origin location

## 13. Static diagnostics

Discarded direct result:

```text
le résultat retourné n'est pas utilisé : Résultat[Texte, ErreurFichier]
retournez-le, stockez-le, propagez-le, traitez-le avec 'agir selon' ou utilisez 'ignorer'
```

Discarded aliased result:

```text
le résultat retourné n'est pas utilisé : Lecture
type développé : Résultat[Texte, ErreurFichier]
```

Unused result binding:

```text
le résultat 'lecture' quitte cette portée sans être utilisé
```

Overwritten result:

```text
le résultat précédent de 'lecture' est remplacé sans avoir été utilisé
```

Missing propagation boundary:

```text
impossible de retourner cette erreur depuis 'charger'
la fonction doit déclarer Résultat[Valeur, Erreur] ou un alias équivalent
```

Incompatible propagated error:

```text
ErreurFichier ne peut pas être retournée par 'charger'
erreurs déclarées : ErreurSyntaxe
```

Alias cycle:

```text
cycle d'alias de types : A -> B -> A
```

Non-exhaustive result match:

```text
traitement incomplet de Résultat[Configuration, ErreurChargement]
cas manquant : Échec(ErreurPermission)
```

Raw return:

```text
la fonction retourne Résultat[Entier, ErreurCalcul]
utilisez Succès(...) ou Échec(...)
```

## 14. Grammar

```ebnf
type_alias       = [ "public" ] "type" IDENT "=" type_expression ;

type_expression  = union_type ;
union_type       = generic_type { "|" generic_type } ;
generic_type     = IDENT [ "[" type_expression
                   { "," (type_expression | INTEGER) } "]" ] ;

result_pattern   = "Succès" "(" (IDENT | "_") ")"
                 | "Échec" "(" error_pattern ")" ;

error_pattern    = IDENT [ ":" type_expression ] | "_" ;

match_expression = AGIR_SELON expression "{" match_branch+ "}" ;
match_branch     = pattern "->" (expression | block) ;
```

`Succès` and `Échec` are lexed as identifiers and resolved as result
constructor patterns in pattern position.

Types must be represented by a structured type AST. Alias expansion, union
normalization, result recognition, assignability, and exhaustiveness cannot be
implemented by comparing flattened type strings.


## 16. Edge-case matrix

| Case | Required behavior |
|---|---|
| Bare call returning `Résultat[T, E]` | Reject as discarded result |
| Bare call returning a result alias | Reject after alias expansion |
| Bare `Succès(...)` or `Échec(...)` expression | Reject because no complete result context exists |
| Bare call returning `Entier`, `Texte`, object, or `Rien` | Allow |
| Function returns `Universel` containing a result | Reject direct result erasure at the return statement |
| Function signature returns `Résultat[T, E] | Rien` | Reject the return signature; use `Résultat[T | Rien, E]` |
| Function returns `Liste[Résultat[T, E]]` | Allow as explicit batch-result data |
| Function returns an object containing result fields | Allow as explicit aggregate data |
| Nominal wrapper around a result | Does not qualify as a result signature |
| Result-returning function falls off the end | Reject, including `Résultat[Rien, E]` |
| Result-returning function uses empty `retourne` | Reject; return a compatible variant |
| Overload resolves to a result return type | Apply result rules to that call |
| Overload resolves to an ordinary return type | Do not apply result rules to that call |
| Function value or interface method returns a result | Apply result rules from its static signature |
| Static return type is `Universel`, runtime value is a result | Treat from static type; fallible API signature is invalidly erased |
| Result alias chain | Resolve fully and apply all result rules |
| Cyclic result alias | Reject the alias cycle |
| Generic result alias declaration | Not part of this design |
| Alias used by `principal` | Treat as its expanded result type |
| Alias to a union containing a result | Does not qualify as a result alias |
| `Résultat` with other than two type arguments | Reject |
| `Succès` or `Échec` with other than one payload | Reject |
| `T` is `Rien` | Allow fallible no-value success |
| `E` is `Erreur` | Allow as an open error set |
| `E` is a class realizing `Erreur` | Allow |
| `E` is a subclass of an error class | Allow |
| `E` is a union exclusively of error types | Allow |
| `E` contains a non-error union member | Reject after alias expansion |
| `E` is `Rien`, `Universel`, a primitive, or an ordinary class | Reject |
| `E` is itself a result | Reject; an error object may contain a result field instead |
| Empty error union | Reject |
| `Succès` or `Échec` returned from a non-result function | Reject |
| `Succès` or `Échec` passed to `Universel` | Reject |
| Context supplied by a result-typed binding, assignment, parameter, aggregate element, or return | Allow and validate the payload |
| Result stored in a local and never used | Reject on every affected exit path |
| Uninitialized result local | No obligation until assignment; normal initialization rules apply |
| Pending result overwritten | Reject before overwrite |
| Result used on only one conditional path | Reject paths on which it remains unused |
| Result produced inside a loop | Require use in each producing iteration |
| Result used as a default argument value | Transfer to the result-typed parameter when evaluated |
| Result passed to a `Résultat` parameter | Transfer obligation; callee must use it |
| Result passed through `Universel` | Count as transfer; no tracking after erasure |
| Result cast to `Universel` and immediately discarded | Reject; pure conversion is not use |
| Result stored in a field or collection | Count as transfer; no eventual-use proof |
| Result explicitly consumed by `ignorer` | Allow |
| Aliased result consumed by `ignorer` | Allow after alias expansion |
| Result assigned to `_` | Reject |
| Result displayed by REPL | Count as use |
| Exhaustive wildcard result match | Count as explicit use |
| Closed error union fully matched | Allow |
| Closed error union incompletely matched | Reject |
| Open `Erreur` result matched without catch-all | Reject |
| Wider error union propagated to narrower error type | Reject statically |
| Concrete error propagated to accepting union | Allow without conversion |
| Error translated to another type | Require explicit `Échec(NewError(...))` |
| Existing error forwarded unchanged | Preserve its original diagnostic origin |
| New domain error constructed from a cause | Record a new origin; preserve the cause explicitly |
| Propagation in function without result signature | Reject |
| Propagation in function returning result alias | Allow after alias expansion |
| Propagation inside unannotated nested function | Reject at nested boundary |
| Outer propagation of nested result | Unwrap outer layer only |
| Directly return compatible result | Allow |
| Directly return incompatible result | Reject |
| Return raw success value from result function | Reject; require `Succès` |
| Return raw error value from result function | Reject; require `Échec` |
| Trap occurs during result-producing operation | Trap; do not construct `Échec` implicitly |
