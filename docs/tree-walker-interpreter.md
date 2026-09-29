# Tree-Walker Interpreter

## Scope

This note describes the current execution backend:

- how it stores state
- how it evaluates expressions and statements
- where it deliberately cheats for now
- what that implies for future VM or semantic work

Relevant code:

- `include/lumiere/interpreter/tree_walker/tree_walker.hpp`
- `src/interpreter/tree_walker/tree_walker_core.cpp`
- `src/interpreter/tree_walker/tree_walker_expr.cpp`
- `src/interpreter/tree_walker/tree_walker_stmt.cpp`
- `src/interpreter/tree_walker/tree_walker_runtime.cpp`

## Why a tree-walker exists first

The tree-walker is the fastest path from “grammar parses” to “language runs.”

That makes it the right first backend because it answers the most important early questions:

- what does each AST node mean?
- what runtime state is required?
- where do scope and control-flow semantics become tricky?

Performance is not the current goal. Semantic clarity is.

## State carried by `TreeWalker`

Key members:

- `m_result`: scratch slot for expression evaluation
- `m_env`: current lexical environment
- `m_self`: current receiver
- `m_modules`: loaded modules
- `m_loading_modules`: cycle-detection support
- `m_import_paths`: configured import roots
- `m_stack_trace`: runtime stack frames
- `m_current_source_path`: diagnostic context

This state tells you what the interpreter currently thinks execution requires. It is also a preview of what a future VM would need in more explicit machine form.

At this point the module state is doing real work, not just reserving space for later:

- filesystem package resolution
- built-in module lookup
- module caching by canonical path
- selective import support

## Why expression evaluation writes into `m_result`

The visitor interface returns `void`, so expression visitors communicate results through interpreter state.

This is a classic visitor compromise:

- simple visitor signatures
- centralized runtime scratch state
- less templating complexity

The cost is that expression evaluation is stateful. That means nested evaluation must be carefully structured so `m_result` is always overwritten intentionally, not read accidentally.

The current helper `evaluate(Expr&)` keeps that manageable.

## Control-flow signals

The interpreter uses dedicated exception-like types:

- `ReturnSignal`
- `BreakSignal`
- `ContinueSignal`

This is not “abusing exceptions.” It is using stack unwinding to model non-local control flow.

Why this is reasonable:

- returns need to jump out of nested blocks
- breaks and continues need to escape loop body execution
- RAII already protects environment restoration

Trying to encode this with flags would quickly pollute every visitor with bookkeeping noise.

## Scope management

Lexical scope is implemented with `Environment` plus `ScopeGuard`.

Patterns:

- entering a block creates a child environment
- leaving a block restores the parent automatically
- branch-local bindings for `agir selon` are scoped by the same mechanism

This is one of the cleanest parts of the current runtime design.

## Function call model

`call_function` does several important things:

1. validates argument count / defaults
2. creates a stack frame for diagnostics
3. creates a local environment rooted at the closure
4. binds parameters
5. installs `m_self`
6. executes the body
7. catches `ReturnSignal`
8. restores previous runtime state even on failure

This is effectively the interpreter’s calling convention.

## Current runtime philosophy

The tree-walker currently mixes three kinds of logic:

- pure execution of already-well-formed AST
- pragmatic dynamic checks
- “not implemented yet” runtime barriers

That mixture is normal in an early interpreter. The important thing is to know which category a behavior belongs to.

## `AgirSelonStmt`

Current semantics are intentionally narrow:

- branches are checked in source order
- first match wins
- supported patterns are:
  - literal pattern
  - `rien`
  - typed binding such as `n: Entier`
- typed branch variables are bound only in the winning branch scope

What is still missing:

- full exhaustiveness enforcement
- richer type-pattern semantics
- collection/object destructuring patterns
- a separate semantic notion of pattern compatibility

This is a good example of a correct partial implementation: runtime behavior exists, but the language feature is not yet “finished” in the formal sense.

## Error handling model

Runtime failures use `RuntimeError`, which carries:

- message
- source location
- stack trace

This is an important design strength. Even in a learning interpreter, it is worth attaching runtime failures to source-level context early.

## Limitations that matter architecturally

### 1. No pre-execution semantic normalization

The tree-walker often has to interpret raw syntax directly because there is no lowered IR.

### 2. No optimized value model

Everything goes through high-level `Value` objects, which is simple but heavy.

### 3. Runtime type-name matching is still string-driven in places

That is pragmatic, but a real semantic layer should eventually own more of this.

### 4. Some features are syntax-first and runtime-later

The project already has examples of syntax arriving before full runtime semantics. That is okay as long as unsupported runtime behavior fails explicitly, not silently.

## What a future VM would likely take from this backend

The tree-walker is already defining semantics the VM can inherit:

- function call behavior
- lexical scoping rules
- return/break/continue behavior
- runtime error structure
- pattern-match execution order

That is why this backend matters even if it is not the eventual performance backend.

## C++ notes

- This subsystem gets most of its safety from RAII and narrow helper methods, not from fancy type machinery.
- Exception-based control flow is justified here because interpreter execution naturally unwinds nested AST visits.
- File-level separation inside the interpreter is important; otherwise statement and expression semantics become unreadable fast.
