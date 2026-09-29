# Architecture Overview

## Scope

This note describes the current implementation architecture of Lumiere as it exists in this repository, not the idealized future architecture. The goal is to make explicit:

- what each subsystem owns
- what data crosses subsystem boundaries
- which invariants are already relied on
- which design choices are deliberate simplifications for a learning compiler

## High-level pipeline

The current pipeline is:

1. raw source text
2. `Lexer`
3. `std::vector<Token>`
4. `Parser`
5. AST (`StmtPtr` / `ExprPtr`)
6. `Program`
7. `Backend`
8. `TreeWalker` execution

There is no semantic analysis pass yet. That matters because several responsibilities that would normally live in a binder or type checker are currently distributed between:

- parser structural restrictions
- runtime checks in the tree-walker
- conventions documented in the language spec

This is acceptable for now, but it is the main architectural pressure point as the language grows.

## Repository structure

### Frontend

- `include/lumiere/lexer/`
- `include/lumiere/parser/`
- `src/lexer/`
- `src/parser/`

The frontend transforms text into structure. It should not know anything about runtime environments, stack traces, or object layout.

### Runtime / backend

- `include/lumiere/interpreter/`
- `include/lumiere/interpreter/tree_walker/`
- `src/interpreter/tree_walker/`
- `include/lumiere/interpreter/vm/`
- `src/interpreter/vm/`

The runtime consumes AST and executes it. It should not care how the source was tokenized as long as the AST is valid.

### CLI

- `src/lumiere.cpp`

The CLI is currently also part of the integration story: it creates the `Program`, selects the backend, and reports diagnostics.

## Current abstraction boundaries

### `Token`

`Token` is the frontend’s stable currency. The parser depends on token type, lexeme, and source position, but not on any scanner internals.

### AST

The AST is the contract between frontend and backend. Once the parser produces AST nodes, the backend does not need to re-interpret token adjacency or source spelling rules.

### `Program`

`Program` is the unit of execution. It packages:

- statements
- source path
- source text

This is a small but important design choice. It keeps execution context alongside the AST, which is useful for diagnostics and future tooling.

### `Backend`

`Backend` is the main seam for alternative execution strategies. Right now it is small:

```cpp
class Backend {
public:
    virtual ~Backend() = default;
    virtual void execute(Program &program) = 0;
};
```

That simplicity is good. It means the frontend is not yet biased toward the tree-walker.

## Main architectural simplifications

### 1. No separate semantic phase

The project does not yet have:

- symbol resolution pass
- type inference pass
- exhaustiveness checker
- lowering pass

Consequences:

- parser builds some nodes that are only partially validated
- runtime may reject cases a semantic phase would normally catch earlier
- some language rules live in the spec but are not fully enforced yet

This is the single biggest gap between “learning interpreter” and “robust implementation.”

### 2. Tree-walker first, VM later

The tree-walker backend exists because it is the fastest route to executable semantics. The VM directory exists because direct AST execution is unlikely to remain the long-term execution model.

This is a good staged design:

- Stage 1: prove language semantics
- Stage 2: stabilize AST and runtime model
- Stage 3: lower to bytecode or another IR

Trying to jump directly to a VM would have mixed execution design questions with language design questions too early.

### 3. Headers emphasize structure

The codebase uses a classic C++ layout:

- headers define data and interfaces
- `.cpp` files define behavior

That is not just conventional. For a compiler project, it keeps the structural model visible:

- token vocabulary
- AST node set
- runtime value model
- interpreter entrypoints

## Invariants already relied on

These are not all documented in code comments, but the implementation assumes them.

### Frontend invariants

- Token streams end with `FIN_FICHIER`.
- `AGIR_SELON` and `TANT_QUE` are single tokens even though they are spelled with spaces.
- `Token.lexeme` preserves the original source spelling for diagnostics and AST printing.

### AST invariants

- AST nodes own their children exclusively via `std::unique_ptr`.
- Many statement bodies are expected to be blocks by convention even when the type is `StmtPtr`.
- Some optional tokens use “empty sentinel” tokens instead of `std::optional<Token>`.

### Runtime invariants

- `m_env` in `TreeWalker` is non-null during execution.
- `Value::Type` and `Value::Data` must stay in lockstep.
- `ScopeGuard` and `StackFrameGuard` are relied on for exception-safe state restoration.

## Where the design will likely evolve

### Likely next architectural additions

- a semantic analysis pass between parser and backend
- a module/package loader that resolves imports before or during program assembly
- clearer separation between parse-time syntax nodes and semantically-resolved nodes
- a bytecode IR for the VM backend

### Code that will feel pressure first

- `parser.cpp`: because more syntax is being added without a corresponding semantic layer
- `value.hpp`: because more runtime types or protocols will make the variant larger
- `tree_walker_stmt.cpp` and `tree_walker_expr.cpp`: because direct execution tends to accumulate many special cases

## Why this architecture is still a good learning architecture

It is not minimal in the toy sense, but it is still small enough to hold in one head. That is the sweet spot:

- large enough to expose real compiler structure
- small enough that design decisions are still visible
- simple enough to refactor without a giant migration tax

## C++ notes

The main C++ lesson here is not syntax. It is separation of concerns.

The project is strongest where it has clear seams:

- scanner vs tokenizer
- parser vs AST
- frontend vs backend
- runtime values vs interpreter logic

For a language implementation, those seams matter more than advanced metaprogramming.
