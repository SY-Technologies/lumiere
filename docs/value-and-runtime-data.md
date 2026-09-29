# Value and Runtime Data

## Scope

This note explains the current shared runtime data model used by Lumiere:

- `Value`
- `LumiereFunction`
- `LumiereObject`
- `Module`
- backend-specific payload bodies behind shared runtime descriptors

These types define the execution model much more than any single visitor function does.

## Design goal

The runtime needs a single carrier type that can:

- hold primitive values
- hold heap-allocated aggregates
- represent callable entities
- represent user instances
- represent the absence of a value

That carrier is `Value`.

## `Value` as a tagged runtime union

`Value` combines:

- a manual tag: `Value::Type`
- a payload: `Value::Data`, implemented as `std::variant`

This is a hybrid design. `std::variant` already has an active alternative, so why keep `Type`?

Because `Type` is doing language-facing work:

- it gives readable checks like `is_entier()`
- it lets the runtime talk in Lumiere categories instead of variant indices
- it avoids scattering raw `std::holds_alternative<T>` checks everywhere

The downside is synchronization risk. The code already acknowledges that by freezing enum order and guarding variant size with `static_assert`.

### Practical invariant

If you add or remove a runtime alternative, you must update all of these together:

- `Value::Type`
- `Value::Data`
- factory methods
- accessors
- `is_*` helpers
- `to_string()`
- `type_name()`
- equality semantics if needed

If one of those is skipped, the runtime model becomes internally inconsistent very quickly.

## Why some alternatives are stored by value and others by pointer

### Stored directly

- `int64_t`
- `double`
- `bool`
- `char32_t`
- `std::string`

These are natural value types. Copying them is either cheap enough or semantically appropriate.

### Stored indirectly with `std::shared_ptr`

- `ListeData`
- `DictData`
- `EnsembleData`
- `LumiereObject`
- `LumiereFunction`

This is not arbitrary. These types have at least one of the following properties:

- large or variable-size payload
- identity beyond one expression
- expected aliasing across runtime code paths
- lifetime that may exceed a single stack frame

Using `shared_ptr` here chooses convenience and simple ownership semantics over maximal performance. That is a reasonable trade for the current stage of the project.

## Equality semantics

`Value::operator==` currently implements “same runtime type, then same payload,” with a special-case success for `RIEN`.

That means equality is currently structural for many payloads only insofar as the underlying C++ payloads support it. It is not yet a complete language-level equality protocol.

This matters because there are at least three distinct equality questions in a language runtime:

1. Are the runtime tags the same?
2. Are the payloads bitwise / structurally equal?
3. Should the language consider these two values equal?

For primitives these are close together. For objects and collections they often diverge.

As the language grows, equality will likely need to evolve from “payload equality” toward “language-defined equality semantics.”

## `LumiereFunction`

`LumiereFunction` is now a shared runtime callable descriptor.

It stores:

- `name`
- `receiver`: bound receiver for methods, or `rien` for free functions
- `native_handler`: the C++ implementation for native stdlib-backed callables
- `body`: an opaque `RuntimeFunctionBody` payload for backend-specific user-function state

This is the key architectural shift that makes the standard library shareable across backends.

The shared runtime layer knows that a callable exists, may have a bound receiver, and may be native or user-backed. It does not know how the tree-walker or a future VM chooses to represent the internals of a user function.

That backend-specific state now lives behind the `RuntimeFunctionBody` abstraction.

## `LumiereClass` and `LumiereInterface`

Classes and interfaces are also shared runtime descriptors now.

They store:

- `name`
- opaque backend payload (`RuntimeClassBody` or `RuntimeInterfaceBody`)

This mirrors the `LumiereFunction` direction: shared runtime code understands the language-level entity, while backend-specific semantic state stays behind an abstraction boundary.

## `LumiereObject`

`LumiereObject` is currently small:

- pointer-like shared reference to `LumiereClass`
- map of fields

This is enough for object identity and field storage, but not yet enough for:

- inheritance-aware layout
- method tables
- visibility enforcement beyond ad hoc checks

That is fine. The object model is still in the early semantic stage.

## `Module`

`Module` stores:

- name
- backend-owned opaque state (`RuntimeModuleState`)
- member table
- set of public members

This shows an important design direction: modules are runtime entities, not just parser sugar. That matters even more now that the runtime supports:

- file-backed package modules
- built-in modules resolved before filesystem lookup
- explicit export sets
- selective import binding

The backend-specific state is where the tree-walker currently keeps its module environment. Shared runtime code does not depend on that environment type directly anymore.

## Tree-walker-specific state

The tree-walker still has backend-owned structures such as:

- `Environment`
- `ScopeGuard`

Those remain important, but they are no longer part of the shared runtime model itself.

## `Environment`

`Environment` is the scope chain. Each environment owns:

- a value map
- a set of immutable names
- a parent pointer

The API is intentionally minimal:

- `define`
- `define_fixe`
- `get`
- `assign`
- `contains`

This is a good design because it concentrates tree-walker name-resolution rules in one place.

### Semantics enforced here

- redeclaration in the same runtime scope is rejected
- assignment walks outward to find an existing binding
- `fixe` immutability is enforced centrally

That means visitor code can stay smaller: it asks the environment to perform scope rules instead of reimplementing them ad hoc.

## `ScopeGuard`

`ScopeGuard` is one of the most important C++ choices in the runtime.

It manages interpreter scope transitions with RAII:

- constructor pushes a child environment
- destructor restores the previous environment

Why this matters:

- runtime code can throw `ReturnSignal`, `BreakSignal`, `ContinueSignal`, or `RuntimeError`
- scope restoration must still happen

Without RAII, every early exit path would need manual cleanup, which is exactly the kind of bug-prone code interpreters accumulate.

## Current weak spots

### 1. Manual tag + variant duplication

This is acceptable, but it is a maintenance hazard.

### 2. Pointer-rich runtime state

This keeps ownership simple, but it means reasoning about aliasing becomes more important over time.

### 3. Shared model versus shared semantics

The shared runtime data model is in a much better state than shared runtime semantics. Important behaviors such as member resolution, subtype checks, and annotation propagation still largely live in backend code.

That is acceptable for now, but it means “shared runtime model” should not be confused with “fully shared runtime semantics.”

## What to keep in mind while extending `Value`

Before adding a new runtime alternative, answer these questions:

1. Is this truly a new runtime type, or just a different AST form?
2. Does it need identity, sharing, or mutation?
3. Should equality be structural, referential, or custom?
4. Should it be stored inline or behind a smart pointer?
5. What does `to_string()` mean for it?
6. What does `type_name()` mean for it?

That checklist will prevent a lot of “just add one more variant arm” drift.

## C++ notes

- `std::variant` is a strong fit here because it is explicit and type-safe.
- `std::shared_ptr` is not “bad C++”; it is a tradeoff. Here it buys simpler runtime ownership.
- RAII is doing real architectural work in this interpreter.
- The most important discipline is keeping runtime invariants localized instead of spread across visitors.
