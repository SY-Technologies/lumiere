# VM Roadmap

This document breaks the Lumiere VM effort into small sequential milestones.

It is intentionally execution-focused:

- each phase should be understandable on its own
- each phase should produce a testable artifact
- each phase should leave the codebase in a stable state

The architecture assumed here is the one described in
[`docs/vm-design.md`](./vm-design.md):

```text
AST -> LIR -> Bytecode -> VM
```

## Current status

Completed or effectively completed:

- Phase 0 — Architecture freeze
- Phase 1 — LIR scaffolding
- Phase 2 — Bytecode scaffolding
- Phase 3 — Minimal VM shell
- Phase 4 — LIR to bytecode for constants and returns
- Phase 5 — Locals and temporaries
- Phase 6 — Arithmetic and comparison
- Phase 7 — Function lowering and entrypoint
- Phase 8 — Builtins and uniform call support
- Phase 9 — Branching and block control flow
- Phase 10 — Short-circuit boolean logic
- Phase 11 — User-defined function calls
- Phase 12 — Loops
- Phase 13 — Module and global model
- Phase 14 — Objects and methods
- Phase 15 — Exceptions

Collection lowering completed beyond the original phase list:

- list and dictionary literals
- indexed reads for lists, fixed lists, text, and dictionaries
- indexed assignment for mutable lists and dictionaries
- list and text iteration

Additional expression and control-flow lowering completed:

- explicit primitive casts
- runtime type checks with generic collection annotations
- `agir selon` literal, typed-binding, `rien`, and `sinon` branches
- explicit VM call frames with a shared operand stack
- direct short/long global-call bytecode without pseudo-string callables
- default parameters and named-argument normalization
- parameter, local, return, and `fixe` enforcement
- native text and collection member dispatch
- short/long member-name bytecode references
- persistent generic mutation constraints for shared collection values
- bound native member values through `GET_MEMBER`
- anonymous and nested functions with mutable transitive captures
- typed throw/catch, ordered handlers, runtime-error conversion, and `finally`
- typed mutable globals and compiled top-level initialization
- cached user and builtin module imports with aliases, visibility, and cycles
- class/interface descriptors, inheritance, contracts, privacy, and parent dispatch
- block-scoped classes/interfaces with captured method environments
- lazy block-scoped imports with idempotent module initialization
- named arguments across direct, first-class, member, and constructor calls
- source-aware VM tracebacks using the shared runtime diagnostic format

## Guiding rule

Each milestone should satisfy all of these:

1. Small enough to complete without destabilizing the whole backend.
2. Testable in isolation.
3. Add one real capability, not just scaffolding for scaffolding's sake.
4. Preserve the ability to stop after that phase and still have a coherent
   state of the project.

## Phase 0 — Architecture freeze

Goal:
- finalize the reviewable design before more backend work starts

Tasks:
- finalize [`docs/vm-design.md`](./vm-design.md)
- confirm the core LIR entities:
  - `Module`
  - `Function`
  - `Block`
  - `Instruction`
  - `Operand`
- confirm the initial LIR instruction families
- confirm the first supported language slice for the VM

Deliverable:
- reviewed VM design with no unresolved core architectural ambiguity

## Phase 1 — LIR scaffolding

Goal:
- introduce the LIR as a real data model without lowering anything yet

Tasks:
- add LIR container types
- add block and terminator model
- add operand model
- add source-location support in LIR
- add a textual LIR printer/dumper

Deliverable:
- LIR graphs can be constructed manually and printed for inspection

Suggested tests:
- unit tests for LIR construction invariants
- dump-format tests for stable human-readable output

## Phase 2 — Bytecode scaffolding

Goal:
- introduce the bytecode model as a real data model without AST lowering yet

Tasks:
- define bytecode opcode enum
- define bytecode function/module containers
- define bytecode constant-pool representation
- add bytecode disassembler / printer

Deliverable:
- bytecode programs can be constructed manually and printed/disassembled

Suggested tests:
- unit tests for constant pool insertion
- disassembler golden tests

## Phase 3 — Minimal VM shell

Goal:
- create the smallest bytecode executor that can run a trivial function

Tasks:
- define VM runtime state:
  - value stack
  - frame model
  - constant access
  - global table placeholder
- implement dispatch loop skeleton
- implement only:
  - `LOAD_CONST`
  - `NIL`
  - `RETURN`

Deliverable:
- VM can execute a trivial bytecode function that returns a literal

Suggested tests:
- run a bytecode function returning an integer
- run a bytecode function returning `rien`

## Phase 4 — LIR to bytecode for constants and returns

Goal:
- connect LIR to the bytecode model for the smallest meaningful subset

Tasks:
- lower:
  - `const`
  - `return`
  - `return_nil`
- add tests that start from LIR and verify emitted bytecode
- run emitted bytecode in the VM

Deliverable:
- `LIR -> Bytecode -> VM` works for literal-returning functions

Suggested tests:
- one LIR function returning an integer constant
- one LIR function returning a string constant

## Phase 5 — Locals and temporaries

Goal:
- support local variable storage and intermediate values

Tasks:
- add LIR support for:
  - `load_local`
  - `store_local`
  - `move`
- add bytecode support for:
  - `LOAD_LOCAL`
  - `STORE_LOCAL`
- extend frames with local storage
- decide how LIR temps flatten into the bytecode storage model

Deliverable:
- VM can execute simple functions that assign and read local variables

Suggested tests:
- simple `soit`
- local read/write
- chained temporary computations

## Phase 6 — Arithmetic and comparison

Goal:
- run nontrivial pure expressions through the VM

Tasks:
- add LIR lowering support for:
  - `add`
  - `sub`
  - `mul`
  - `div`
  - `mod`
  - `neg`
  - `eq`
  - `neq`
  - `lt`
  - `lte`
  - `gt`
  - `gte`
- add corresponding bytecode ops
- implement runtime semantics in the VM

Deliverable:
- arithmetic and comparison functions run end to end through the VM

Suggested tests:
- arithmetic expressions
- comparison expressions
- mixed local-and-constant expressions

## Phase 7 — Function lowering and entrypoint

Goal:
- compile a real parsed Lumiere function into the new backend pipeline

Tasks:
- lower AST function declarations into LIR functions
- define the startup convention:
  - `principal`
  - optional implicit module entry function if needed
- lower one AST function body into LIR
- execute a compiled `principal`

Deliverable:
- first real `AST -> LIR -> Bytecode -> VM` success path

Suggested tests:
- one simple `principal()` that returns a literal
- one simple `principal()` with local arithmetic

## Phase 8 — Builtins and uniform call support

Goal:
- support calling builtins through the new pipeline

Tasks:
- add LIR `call`
- add bytecode `CALL`
- implement call-frame conventions for builtin/native calls
- bridge at least:
  - `afficher`
  - optionally `afficher_inline`

Deliverable:
- builtin calls work through the VM pipeline

Suggested tests:
- `afficher(1)`
- `afficher("bonjour")`
- multiple positional args

## Phase 9 — Branching and block control flow

Goal:
- make the block graph meaningful at runtime

Tasks:
- lower `si / sinon` into LIR blocks with:
  - `branch`
  - `jump`
- lower blocks into bytecode jumps
- implement:
  - `JUMP`
  - `JUMP_IF_FALSE`

Deliverable:
- conditional control flow works end to end

Suggested tests:
- basic `si`
- `si / sinon`
- nested conditionals

## Phase 10 — Short-circuit boolean logic

Goal:
- preserve correct `et` / `ou` semantics through control-flow lowering

Tasks:
- lower `et` and `ou` using branches, not naive value ops
- ensure right-hand side is skipped when required

Deliverable:
- short-circuit boolean semantics match the language rules

Suggested tests:
- `et` skipping RHS
- `ou` skipping RHS
- nested boolean chains

## Phase 11 — User-defined function calls

Goal:
- support real Lumiere-to-Lumiere calls

Tasks:
- allow `call` to target compiled Lumiere functions
- implement argument passing into frames
- implement return propagation across frames
- support nested calls

Deliverable:
- user-defined function calls work through the VM

Suggested tests:
- one function calling another
- return-value chains
- simple recursion if desired

## Phase 12 — Loops

Goal:
- support repeated execution using block-based control flow

Tasks:
- lower `tant que`
- add loop tests
- decide whether `pour chaque` waits for iteration-specific runtime support

Deliverable:
- first loop support in the VM

Suggested tests:
- counting loop
- loop mutation
- loop condition re-evaluation

## Phase 13 — Module and global model

Goal:
- support programs that are more than a single function

Tasks:
- define how module-level bindings become globals
- lower global/function/module initialization logic
- add VM-side global lookup rules
- revisit import handling after the global model is stable

Deliverable:
- completed: linked module graphs with isolated globals, public exports,
  generated binding initializers, caching, and cycle detection

## Phase 14 — Objects and methods

Goal:
- begin representing the object model in VM terms

Tasks:
- decide runtime object layout
- add object-related LIR extensions
- add field/method bytecode lowering
- add runtime support incrementally

Deliverable:
- completed: descriptor-based classes and interfaces, typed/private/fixed
  fields, construction, inheritance, overrides, methods, and parent dispatch

## Phase 15 — Exceptions

Goal:
- support structured runtime error control flow

Tasks:
- design the runtime handler stack
- lower `essayer / attraper / finalement`
- implement throw propagation and handler restoration
- test control-flow interactions carefully

Deliverable:
- completed: frame-local handler stacks, cross-frame unwinding, typed catches,
  runtime errors as `Texte`, and cleanup on all non-local exits

## Recommended implementation order

The recommended order is:

1. Phase 0
2. Phase 1
3. Phase 2
4. Phase 3
5. Phase 4
6. Phase 5
7. Phase 6
8. Phase 7
9. Phase 8
10. Phase 9
11. Phase 10
12. Phase 11
13. Phase 12
14. Phase 13
15. Phase 14
16. Phase 15

This order favors:

- early vertical slices
- quick feedback
- low architectural risk
- visible progress

## Current verification focus

All planned backend phases are implemented. Ongoing work should preserve:

1. full unit and CLI integration coverage
2. tree-walker/VM fixture output and diagnostic parity
3. short/long table-reference boundary tests

## Milestone policy

Before starting a new phase:

- the previous phase should compile cleanly
- the new behavior should have focused tests
- debug printers/disassemblers should stay usable
- architecture drift from [`docs/vm-design.md`](./vm-design.md) should be
  reflected in docs immediately
