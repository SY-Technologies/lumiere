# Stdlib / Backend Architecture

## Scope

This document defines the architecture Lumiere will implement for sharing the
standard library across execution backends.

It is not a survey of options. It is the target design.

The target is:

- one stdlib semantic implementation
- no tree-walker-only stdlib code paths
- no VM-only stdlib code paths
- one shared runtime value model
- one shared native stdlib ABI
- one shared backend bridge used by both backends

If later implementation work drifts away from these constraints, that should be
treated as architectural regression, not harmless variation.

## Design goals

The design must guarantee all of the following:

1. `Texte`, `Maths`, `Fichier`, `Chemin`, and future stdlib modules are each implemented once.
2. Both the tree-walker and the VM call the same stdlib code.
3. Stdlib code does not depend on AST visitors, environments, VM stacks, opcodes, or parser tokens.
4. Adding a new backend does not require rewriting stdlib semantics.
5. Error reporting remains high quality across backends.
6. The code remains modular and readable.

## Non-goals

This architecture is not trying to:

- build a giant runtime abstraction layer for every subsystem
- hide all backend differences behind dozens of interfaces
- introduce separate runtime value models per backend
- solve future optimization design in advance

The goal is a narrow, strong seam around stdlib execution, not an abstract
"everything runtime."

## Final architectural decision

Lumiere will standardize on four shared pieces.

### 1. Shared runtime data model

All backends will use the same language runtime value types.

This includes:

- `Value`
- `ListeData`
- `DictData`
- `EnsembleData`
- `LumiereFunction`
- `LumiereObject`
- `Module`

No backend gets its own variant of these types.

This is mandatory. If the VM and tree-walker diverge here, the stdlib seam
collapses immediately into adapter branches and duplicated semantics.

Current reference:

- [include/lumiere/interpreter/runtime/value.hpp](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/runtime/value.hpp)
- [docs/value-and-runtime-data.md](./value-and-runtime-data.md)

Long-term, the header path may be renamed to reflect shared ownership more
clearly, but the data model itself stays shared.

### 2. Shared native stdlib ABI

All native stdlib functions will use one backend-neutral calling convention.

Conceptually:

```cpp
using NativeFn = Value(*)(IRuntime& runtime, NativeArgs args);
```

`NativeArgs` will be a simple normalized runtime argument payload, not a
backend-specific structure.

It will represent:

- optional receiver
- positional arguments
- named arguments only if native functions are intended to support them
- source location of the call

The stdlib must receive fully normalized arguments and must not care whether
the original call came from:

- tree-walker AST execution
- VM bytecode dispatch
- future REPL or tooling execution

### 3. Shared runtime bridge

The stdlib will depend on a small backend-neutral runtime bridge.

This bridge is the only backend-facing interface stdlib code may use.

Conceptually:

```cpp
class IRuntime {
public:
    virtual ~IRuntime() = default;

    virtual Value call(Value callee, NativeArgs args) = 0;
    virtual [[noreturn]] void raise_runtime_error(const RuntimeSite&, const std::string&) = 0;
    virtual bool is_equal(const Value&, const Value&) const = 0;
    virtual std::string to_text(const Value&) const = 0;
    virtual void annotate_value(const Value&, std::string_view type_name, const RuntimeSite&) = 0;
};
```

This interface may grow slightly if a genuine language-level need appears, but
it must stay narrow and language-oriented.

It must not expose:

- `Environment`
- `StackFrame`
- AST node types
- `Token`
- opcodes
- bytecode chunks
- instruction pointers
- backend storage internals

The rule is simple:

- if the stdlib needs a service because it is part of language semantics, the
  bridge may expose it
- if the stdlib needs a service only because a backend implementation is
  awkward, the bridge must not absorb that awkwardness

### 4. Shared runtime source location

The stdlib will not depend on parser `Token`.

Instead, all backend-facing stdlib APIs will use a backend-neutral runtime
location structure:

```cpp
struct RuntimeSite {
    std::string source_path;
    int line = 0;
    int column = 0;
};
```

This structure may later grow fields such as:

- symbol name
- module name
- instruction offset

but `Token` itself is not part of the shared stdlib seam.

Backend adaptation responsibility:

- tree-walker converts `Token` to `RuntimeSite`
- VM converts instruction/debug metadata to `RuntimeSite`

This preserves strong diagnostics without forcing the VM to pretend it has
parser tokens at runtime.

## Why the smaller `ICallable` proposal is not sufficient

A minimal callable-only interface like:

```cpp
class ICallable {
public:
    virtual Value call(Value callee, const std::vector<Value>& args) = 0;
};
```

is directionally correct but insufficient for Lumiere.

It solves only one problem:

- invoking Lumiere callables

Real stdlib code also needs to:

- raise runtime errors
- refer to source context
- compare values using language equality
- stringify values using language semantics
- attach runtime annotations when relevant

If those services do not exist in the bridge, then one of two bad things
happens:

1. stdlib code becomes unable to produce correct behavior or diagnostics
2. stdlib code starts depending on backend-specific internals anyway

Therefore Lumiere will not implement a call-only bridge. It will implement the
larger but still narrow `IRuntime`-style bridge described above.

## What stdlib code is allowed to know

Stdlib code is allowed to know:

- Lumiere runtime values
- stdlib function and method semantics
- argument validation rules
- language equality behavior
- language text conversion behavior
- the shared backend bridge
- the shared runtime-site structure

Stdlib code is not allowed to know:

- how tree-walker environments chain
- how VM frames are stored
- how AST nodes are visited
- how bytecode dispatch works
- how parser tokens are produced
- how a backend records stack traces internally

This is the main boundary rule for implementation review.

## Current repository state

The current repository is partway to the target design.

### Already aligned with the target

Stdlib logic already lives in dedicated files:

- [src/interpreter/stdlib/texte.cpp](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/texte.cpp)
- [src/interpreter/stdlib/maths.cpp](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/maths.cpp)
- [src/interpreter/stdlib/fichier.cpp](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/fichier.cpp)
- [src/interpreter/stdlib/chemin.cpp](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/chemin.cpp)

Stdlib-facing headers are grouped under:

- [include/lumiere/interpreter/stdlib/modules.hpp](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/stdlib/modules.hpp)
- [include/lumiere/interpreter/stdlib/helpers.hpp](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/stdlib/helpers.hpp)

`Texte` module helpers and `Texte` instance methods already share a single
semantic core:

- [src/interpreter/stdlib/texte.cpp](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/texte.cpp)

That is important because it proves the core pattern works:

- one semantic implementation
- thin module-call adapter
- thin method-call adapter

### Not yet aligned with the target

The current stdlib helper layer still depends directly on `TreeWalker`.

See:

- [include/lumiere/interpreter/stdlib/helpers.hpp](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/stdlib/helpers.hpp)
- [src/interpreter/stdlib_helpers.cpp](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib_helpers.cpp)

Examples of current coupling:

- helper functions take `TreeWalker &`
- value requirements are enforced through tree-walker methods
- errors are raised through tree-walker methods
- runtime annotations are applied through tree-walker methods

This means the current code is "extracted stdlib for the tree-walker," not yet
"backend-neutral stdlib for Lumiere."

That distinction is the reason this refactor still needs to happen.

## Concrete target file layout

The architecture we will implement should settle into a structure like this.

### Shared runtime-facing types

- `include/lumiere/interpreter/runtime/value.hpp`
- `include/lumiere/interpreter/runtime/runtime_site.hpp`
- `include/lumiere/interpreter/runtime/native_args.hpp`
- `include/lumiere/interpreter/runtime/iruntime.hpp`

These files define the shared runtime contract used by stdlib and backends.

### Shared stdlib layer

- `include/lumiere/interpreter/stdlib/modules.hpp`
- `include/lumiere/interpreter/stdlib/helpers.hpp`
- `src/interpreter/stdlib/helpers.cpp`
- `src/interpreter/stdlib/texte.cpp`
- `src/interpreter/stdlib/maths.cpp`
- `src/interpreter/stdlib/fichier.cpp`
- `src/interpreter/stdlib/chemin.cpp`

These files hold stdlib behavior only.

### Backend implementations

- `include/lumiere/interpreter/tree_walker/...`
- `src/interpreter/tree_walker/...`
- `include/lumiere/interpreter/vm/...`
- `src/interpreter/vm/...`

These files implement backend execution and adapt backend state to the shared
stdlib bridge.

This exact file naming may still shift slightly during refactor, but this is
the ownership model we are implementing.

## Concrete target responsibilities

### Shared runtime layer owns

- runtime value types
- native argument containers
- backend-neutral runtime source locations
- the runtime bridge interface

### Shared stdlib layer owns

- stdlib function behavior
- stdlib method behavior
- stdlib module registration
- stdlib-local argument validation helpers
- backend-neutral method/member helpers that operate only on shared runtime data

### Backend layers own

- evaluating user-defined Lumiere functions
- invoking native functions through backend-local call plumbing
- stack traces and frame storage
- scope/environment storage
- debug info extraction
- converting backend execution position into `RuntimeSite`

## Method resolution policy

Method/member resolution must be separated from actual invocation.

There are two distinct problems:

1. resolve what `obj.nom` means
2. execute the resolved result

The semantic rules of member resolution should be shared whenever they operate
only on the shared runtime data model.

That includes lookup order such as:

- instance fields first
- class methods next
- superclass methods after that

Backends may differ in how they execute the resolved callable, but they must not
diverge in what member lookup means.

## Implementation sequence

This is the order Lumiere should implement.

### Step 1. Introduce the shared seam types

Add:

- `RuntimeSite`
- `NativeArgs`
- `IRuntime`

without immediately migrating every stdlib module.

### Step 2. Repoint stdlib helpers to the shared seam

The current helper layer must stop depending on `TreeWalker &`.

This is the first required structural change because it converts the stdlib
from "tree-walker-aware" to "backend-bridge-aware."

### Step 3. Migrate `Texte`

`Texte` is the pilot slice because it is already the cleanest extracted module
and already proves shared semantics across module and method forms.

The `Texte` migration is the architectural test case.

### Step 4. Migrate `Maths`, `Chemin`, and `Fichier`

These modules should move onto the same shared bridge once the `Texte` pattern
is validated.

### Step 5. Extract `Liste` and `Dictionnaire` method semantics

These are still largely embedded in tree-walker dispatch. They should be moved
to the same shared stdlib-style pattern.

### Step 6. Implement the tree-walker adapter completely

The tree-walker should fully implement the shared runtime bridge before the VM
is asked to use it.

### Step 7. Implement the VM adapter

The VM should then implement the same bridge without requiring stdlib semantic
changes.

That is the acceptance gate. If VM support requires editing stdlib semantics,
the architecture is still wrong.

## Acceptance criteria

The architecture is considered correct only if all of these are true.

### Criterion 1

Stdlib source files do not mention:

- `TreeWalker`
- `Environment`
- `Token`
- `Opcode`
- VM frame/storage internals
- AST node types

### Criterion 2

The same stdlib fixture programs behave identically under both backends.

### Criterion 3

Adding or improving the VM backend does not require stdlib semantic rewrites.

### Criterion 4

Backend differences remain confined to execution plumbing, not language
semantics.

### Criterion 5

Error quality remains strong under both backends because both supply
`RuntimeSite` data to the shared stdlib layer.

## Why this is the final design

This is the best tradeoff for Lumiere because it is strong enough to support a
real VM without overcomplicating the codebase.

It avoids:

- duplicated stdlib semantics
- backend leakage into shared code
- fake token dependencies in the VM
- overly broad abstraction layers

It gives us exactly one carefully chosen seam:

- shared runtime data
- shared stdlib ABI
- shared runtime bridge
- shared runtime site

That is the design the repository should now converge toward.
