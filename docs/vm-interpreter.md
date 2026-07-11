# VM Interpreter

## Scope

This note describes the current bytecode backend:

- how the VM pipeline is structured
- what runtime state the VM carries
- what language slice is implemented end to end
- how modules, objects, calls, and exceptions map onto runtime state

Relevant code:

- `include/lumiere/interpreter/vm/lir.hpp`
- `include/lumiere/interpreter/vm/bytecode.hpp`
- `src/interpreter/vm/ast_to_lir.cpp`
- `src/interpreter/vm/lir_to_bytecode.cpp`
- `src/interpreter/vm/vm.cpp`

## Why the VM exists separately from the tree-walker

The tree-walker remains the direct reference implementation for language semantics.

The VM exists for a different reason:

- make execution structure explicit
- separate semantic lowering from runtime dispatch
- create a backend that can eventually optimize, serialize, and inspect code more cleanly

That means the VM is not just “the same interpreter but faster.” It has a different shape:

```text
AST -> LIR -> Bytecode -> VM
```

## Pipeline shape

The current backend has three stages after parsing:

1. `AstToLir` lowers AST into block-based LIR.
2. `LirToBytecode` flattens that LIR into compact bytecode.
3. `VM::run(...)` executes the bytecode.

This split matters because each layer owns a different problem:

- AST lowering owns source-language structure
- bytecode lowering owns encoding decisions
- the VM owns runtime stack and local-slot behavior

## State carried by the VM

The runtime execution state is intentionally small:

- one operand stack per active call
- one `locals` array per active call
- one instruction pointer per active call
- a module-global function table
- a module-global name table for global lookup

Calls use an explicit VM-owned frame stack. Each frame carries:

- its function bytecode
- its instruction pointer
- its local slots
- its base offset in the shared operand stack

The calling convention is:

- arguments are copied into local slots `0..arity-1`
- each function gets its own local storage
- return values come back as one `Value`

## Why LIR still matters even though execution is stack-based

The bytecode VM is stack-based.

The LIR is not trying to imitate the operand stack. It stays more explicit:

- locals are named as `L*`
- temporaries are named as `T*`
- blocks are named as `B*`
- globals are named as `G*`
- constants are named as `K*`

That is deliberate. The LIR exists to keep lowering readable, not to mirror final dispatch mechanics byte-for-byte.

## Current supported language slice

The VM currently supports:

- literals:
  - `Entier`
  - `Decimal`
  - `Logique`
  - `Texte`
  - `Symbole`
  - `rien`
- local declarations and reads
- local assignment
- arithmetic:
  - `+`
  - `-`
  - `*`
  - `/`
  - `%`
- comparisons:
  - `==`
  - `!=`
  - `<`
  - `<=`
  - `>`
  - `>=`
- unary operators:
  - `-`
  - `non`
- truthiness-based branching:
  - `si / sinon`
  - `tant que`
- short-circuit logic:
  - `et`
  - `ou`
- loop control:
  - `arreter`
  - `continuer`
- user-defined function calls
- recursive function calls
- default parameters evaluated in the callee
- named arguments with source-order evaluation
- runtime enforcement for parameter, local, and return annotations
- immutable local bindings declared with `fixe`
- persistent generic collection constraints across aliases and native results
- explicit casts to primitive types
- runtime type checks, including collection generic checks
- `agir selon` with literal, typed-binding, `rien`, and `sinon` patterns
- list literals
- dictionary literals
- indexed reads on:
  - `Liste`
  - `ListeFixe`
  - `Texte`
  - `Dictionnaire`
- indexed assignment on:
  - `Liste`
  - `ListeFixe`
  - `Dictionnaire`
- `pour chaque` over:
  - `Liste`
  - `ListeFixe`
  - `Texte`
- native global calls:
  - `afficher`
  - `afficher_inline`
- native member calls on:
  - `Texte`
  - `Liste`
  - `ListeFixe`
  - `Dictionnaire`
- bound native member values stored and called later
- anonymous functions and nested function declarations
- mutable, transitive lexical closures backed by shared cells
- typed thrown values, ordered catch clauses, and `finalement`
- VM runtime errors caught as `Texte`
- typed mutable module globals initialized by compiled bytecode
- first-class references to compiled global functions
- named arguments for direct, first-class, member, and constructor calls
- classes, interfaces, inheritance, private/fixed/typed fields, and methods
- `ici` and explicit `parent` dispatch
- namespace and selective imports with aliases, visibility, caching, and cycle detection
- registered built-in modules through the same import surface
- standard input helpers and `type_de`
- structured runtime tracebacks with source snippets

## Current lowering model for loops

The VM does not have a dedicated `foreach` opcode.

That is good.

`pour chaque` lowers into ordinary control flow plus sequence helpers:

- store iterable in a hidden local
- store loop index in a hidden local
- compute `length`
- branch on `index < length`
- fetch current item with `INDEX_GET`
- jump to increment block on `continuer`
- jump to exit block on `arreter`

This keeps the bytecode small and the runtime model honest.

## Sequence support

The current VM collection surface is intentionally minimal:

- `LIST` constructs a dynamic list value
- `DICTIONARY` constructs an ordered dictionary value
- `SEQUENCE_LENGTH` returns the logical length
- `INDEX_GET` reads a sequence element or dictionary entry
- `INDEX_SET` mutates a list element or dictionary entry and returns the assigned value

Type names use their own module table and their own short/long bytecode
references. They are not stored in either the value constant pool or the
global-name table.

For `Texte`, sequence behavior is Unicode-character based rather than byte based.

That matters because `"é"` must count as one element, not two bytes.

## Truthiness model

The VM now follows the same broad truthiness rule as the tree-walker:

- `rien` is false
- `Logique(false)` is false
- everything else is true

That rule is used for:

- `si`
- `tant que`
- `non`
- branch-based lowering of short-circuit logic

## Exception model

Each call frame owns a stack of exception handlers. `TRY_BEGIN Bhandler`
records two facts: the bytecode offset of `Bhandler` and the current operand
stack depth. `TRY_END` removes that record on normal exit.

`THROW` removes the thrown value from the operand stack and walks VM frames
from innermost to outermost. At the first handler it:

- removes the handler so the catch body cannot catch itself
- restores the operand stack to the recorded depth
- pushes the thrown value
- resumes execution at the handler block

The handler block begins with `IR_OP_EXCEPTION_VALUE`. This is a zero-byte LIR
pseudo-instruction: it gives the exceptional stack value an explicit name such
as `T4`, after which ordinary `IR_OP_STORE_LOCAL` saves it in a hidden local.
Catch selection is then normal CFG using `IR_OP_TYPE_CHECK` in source order.

`finalement` is not a runtime opcode. The AST-to-LIR pass copies its control
flow onto every exit path: normal completion, handled or unhandled throw,
`retourne`, `arreter`, and `continuer`. This keeps bytecode exception handling
small while preserving the source-language rule that cleanup always runs.

VM runtime failures use the same unwinder. They are converted to `Texte` only
when a language handler exists; otherwise the original `VmRuntimeError`
continues to the CLI unchanged.

## Module initialization and globals

Top-level variable initializers lower into an ordinary synthetic function named
`__module_init__`. The VM allocates one module-global value array, installs
first-class values for compiled and native functions, executes the initializer
once, and then invokes `principal` against that same array.

Global reads and writes use `GET_GLOBAL` / `SET_GLOBAL` or their 24-bit `LONG`
forms. A parallel definition bitmap distinguishes a declared global containing
`rien` from an undeclared name. Type assertions run before stores, and fixed
global bindings are rejected during AST-to-LIR lowering.

## Call argument ABI

Argument names have their own module table. Every call opcode carries an arity
and one argument-name reference per value; an empty name means positional.
Values remain on the operand stack, while names stay bytecode metadata.

Statically known global calls are normalized during AST lowering so defaults
execute in the callee without changing source evaluation order. First-class
functions, imported aliases, methods, and constructors normalize at runtime.
Native APIs receive the original `RuntimeArgument` values and therefore enforce
the same named-argument rules as the tree-walker.

## Objects and interfaces

Class and interface declarations become module-owned descriptors. Class
descriptors contain field contracts, method-to-function indices, parent names,
and implemented interfaces. Method bytecode is ordinary function bytecode with
an explicit `ici` local at slot zero.

`CLASS` resolves parent and interface globals and validates override/interface
contracts. Construction allocates a `LumiereObject`, binds positional or named
field arguments, and enforces field types. `GET_MEMBER`, `SET_MEMBER`, and
`CALL_MEMBER` handle fields, bound methods, privacy, and fixed fields.
`GET_PARENT` and `CALL_PARENT` start lookup at the runtime parent class without
disabling ordinary virtual dispatch elsewhere.

Block-scoped classes and interfaces use the same descriptors but are stored in
lexical locals. Method descriptors retain capture-cell recipes, allowing local
class methods to close over mutable bindings from their declaration scope.

## Import linking

Each source module first lowers independently to LIR. The linker then:

- resolves canonical files and detects active-load cycles
- compiles each dependency once
- prefixes private module globals and function names
- remaps every table-backed LIR operand
- generates selective-alias or namespace binding initializers
- schedules dependency initialization before importer initialization

Public exports alone enter namespace objects or selective bindings. Built-in
modules are registered as backend-neutral native `Value`s and materialized by
the same initializer sequence, so their implementation is not duplicated in
the VM. When native code invokes `IRuntime::call` with a compiled closure, the
runtime re-enters the bytecode dispatcher with that closure's captures while
sharing module globals and initializer state. This is the callback path used by
LumiTest hooks and LumiNet handlers.

Builtin module-name dispatch and native callable allocation live behind the
single `register_builtin_module` stdlib entry point. Neither backend supplies a
callable factory. The complete cross-backend boundary is documented in
`runtime-boundaries.md`.

Imports inside blocks create fixed lexical bindings. Their linked initializer
chain is invoked by `INIT_GLOBAL` only when execution reaches the import;
initializer function indices are tracked idempotently, so conditional imports
remain lazy and repeated imports execute module top-level code once.

## Runtime diagnostics

Every function carries its source path and source text into bytecode. Call
frames retain their call site. An unhandled runtime error or thrown value is
converted to the shared `RuntimeError` representation, producing the same
traceback, source line, caret, and language-level message as the tree-walker.
Errors intercepted by `attraper` remain ordinary thrown `Texte` values.

## Architectural notes

### 1. Direct global calls are still name-driven

`CALL_GLOBAL` resolves a module-global name and dispatches it either to:

- a native function
- a compiled Lumiere function

Global names no longer masquerade as `Texte` values on the operand stack.
Compiled and native global functions also exist as first-class callable values;
the direct call opcode remains as a compact fast path when the callee is known.

### 2. Collection lowering stays compact

The VM can construct, index, and call native methods on lists and dictionaries,
and it can iterate lists and text. Text methods reuse the same backend-neutral
`IRuntime` operation executor as the tree-walker, so their behavior is not
duplicated in the VM.

## What this backend already proves

The current VM is no longer just scaffolding.

It proves that Lumiere semantics survive the full lowering chain:

- AST control flow can become explicit CFG
- explicit CFG can become bytecode
- bytecode can preserve:
  - locals
  - calls
  - loops
  - short-circuiting
  - sequence iteration
  - closures and mutable captures
  - structured exceptions
  - object dispatch
  - linked module initialization

Repository success and error fixtures are executed against both backends to
keep this proof behavioral rather than structural.

## Inspecting compiled programs

The CLI exposes both compiler representations without executing the program:

```bash
lumiere ir programme.lum
lumiere bytecode programme.lum
```

`lumiere ir` prints the linked LIR, including imported functions, module
initializers, constants, globals, blocks, temporaries, and source locations.
This is the exact LIR consumed by bytecode emission.

`lumiere bytecode` compiles the same linked module and decodes each instruction.
The output includes function metadata, byte offsets, opcode names, operands,
resolved table names or constants, and source coordinates. The disassembler is
also available to C++ tooling through `disassemble(const ModuleBytecode&)`.

## C++ notes

- The LIR layer is valuable because it keeps source-level structure readable during lowering, even though the runtime is stack-based.
- The current VM runtime stays simple by reusing `Value` directly instead of inventing an optimized tagged machine value too early.
- Ordinary bytecode calls do not consume the C++ call stack; recursive Lumiere programs grow the explicit VM frame stack instead. A synchronous native-to-bytecode callback creates a nested dispatcher invocation because the native API must receive its return value immediately.
