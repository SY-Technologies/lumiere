# Codebase Reading Guide

## Why this guide exists

Lumiere is still small enough to understand fully, but it is already large enough that reading files in arbitrary order is exhausting.

The goal of this guide is to give you:

- a mental map of the repository
- a recommended reading order
- the main questions each file answers
- the invariants worth holding in your head while you read

This is not a full design spec. It is a practical guide for reducing the feeling of "I am looking at many files but not building one coherent model."

## The mental model first

Before reading details, hold this pipeline in your head:

1. `src/lumiere.cpp` reads source and chooses a backend.
2. `Lexer` turns source text into tokens.
3. `Parser` turns tokens into AST nodes.
4. `Program` packages AST plus source context.
5. `Backend` executes the `Program`.
6. Today, the real backend is the tree-walker.
7. The VM exists as the future backend seam, not yet the main execution engine.

If you keep only one picture in your head, keep that one.

## The repository map

### Entry points

- [`src/lumiere.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/lumiere.cpp)
- [`CMakeLists.txt`](/Users/sylvainyabre/programming/tech/lumiere/CMakeLists.txt)

These tell you how the project is assembled and where execution starts.

### Frontend: text to structure

- [`include/lumiere/lexer/`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/lexer)
- [`src/lexer/`](/Users/sylvainyabre/programming/tech/lumiere/src/lexer)
- [`include/lumiere/parser/`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/parser)
- [`src/parser/`](/Users/sylvainyabre/programming/tech/lumiere/src/parser)

This layer answers: "What does the source code mean syntactically?"

### Runtime model

- [`include/lumiere/interpreter/runtime/value.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/runtime/value.hpp)
- [`src/interpreter/runtime/value.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/runtime/value.cpp)
- [`include/lumiere/interpreter/runtime/iruntime.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/runtime/iruntime.hpp)

This layer answers: "What kinds of values and runtime services exist?"

### Current execution engine

- [`include/lumiere/interpreter/tree_walker/tree_walker.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/tree_walker/tree_walker.hpp)
- [`src/interpreter/tree_walker/`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker)

This layer answers: "How does the language actually run today?"

### Built-in modules

- [`src/interpreter/stdlib/`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib)

This layer answers: "What capabilities does the language expose beyond pure core semantics?"

### Future backend seam

- [`include/lumiere/interpreter/vm/vm.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/vm/vm.hpp)
- [`src/interpreter/vm/vm.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/vm/vm.cpp)

This layer answers: "Where will the next execution strategy plug in?"

### Tests

- [`tests/`](/Users/sylvainyabre/programming/tech/lumiere/tests)

This layer answers: "What behavior is considered real and already relied on?"

## Recommended reading order

Do not read the codebase in alphabetical order. Read it in dependency order.

### Pass 1: learn the shape of the system

Read these first:

1. [`README.md`](/Users/sylvainyabre/programming/tech/lumiere/README.md)
2. [`docs/architecture-overview.md`](/Users/sylvainyabre/programming/tech/lumiere/docs/architecture-overview.md)
3. [`src/lumiere.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/lumiere.cpp)
4. [`CMakeLists.txt`](/Users/sylvainyabre/programming/tech/lumiere/CMakeLists.txt)

What you should learn from this pass:

- Lumiere has one main pipeline.
- The CLI builds a `Program` and dispatches to a `Backend`.
- The VM is not yet the semantic source of truth.
- The tree-walker is the current executable meaning of the language.

If you do not feel oriented after this pass, do not continue deeper yet.

### Pass 2: learn the language data structures

Read these next:

1. [`include/lumiere/lexer/token.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/lexer/token.hpp)
2. [`include/lumiere/parser/ast.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/parser/ast.hpp)
3. [`include/lumiere/interpreter/runtime/value.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/runtime/value.hpp)
4. [`include/lumiere/interpreter/runtime/iruntime.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/runtime/iruntime.hpp)
5. [`include/lumiere/interpreter/tree_walker/environment.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/tree_walker/environment.hpp)
6. [`include/lumiere/interpreter/tree_walker/tree_walker.hpp`](/Users/sylvainyabre/programming/tech/lumiere/include/lumiere/interpreter/tree_walker/tree_walker.hpp)

What you should learn from this pass:

- what the token vocabulary looks like
- what AST node families exist
- what runtime values exist
- what the interpreter has to keep track of while executing

This pass is important because headers are the best place to understand shape without getting lost in control flow.

### Pass 3: learn the frontend pipeline

Read these next:

1. [`docs/lexer-pipeline.md`](/Users/sylvainyabre/programming/tech/lumiere/docs/lexer-pipeline.md)
2. [`src/lexer/scanner.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/lexer/scanner.cpp)
3. [`src/lexer/tokenizer.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/lexer/tokenizer.cpp)
4. [`src/lexer/lexer.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/lexer/lexer.cpp)
5. [`docs/parser-and-ast.md`](/Users/sylvainyabre/programming/tech/lumiere/docs/parser-and-ast.md)
6. [`src/parser/parser.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/parser/parser.cpp)

Read this pass with one question:

"How does raw source become a trustworthy AST?"

Main frontend invariants:

- token streams end with `FIN_FICHIER`
- composite keywords like `AGIR_SELON` are treated as single syntax concepts
- the parser owns structure, not runtime meaning

Do not try to understand every parser production on the first pass. Focus on the pattern of the parser, not every branch.

### Pass 4: learn execution through the tree-walker

Read these next:

1. [`docs/tree-walker-interpreter.md`](/Users/sylvainyabre/programming/tech/lumiere/docs/tree-walker-interpreter.md)
2. [`src/interpreter/tree_walker/tree_walker.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker.cpp)
3. [`src/interpreter/tree_walker/tree_walker_core.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker_core.cpp)
4. [`src/interpreter/tree_walker/tree_walker_stmt.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker_stmt.cpp)
5. [`src/interpreter/tree_walker/tree_walker_expr.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker_expr.cpp)
6. [`src/interpreter/tree_walker/tree_walker_objects.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker_objects.cpp)
7. [`src/interpreter/tree_walker/tree_walker_types.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker_types.cpp)
8. [`src/interpreter/tree_walker/tree_walker_modules.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker_modules.cpp)
9. [`src/interpreter/tree_walker/tree_walker_runtime.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker_runtime.cpp)
10. [`src/interpreter/tree_walker/tree_walker_sequences.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/tree_walker/tree_walker_sequences.cpp)

Read this pass with a narrower question:

"Given a valid AST, how does Lumiere produce runtime behavior?"

Main runtime invariants:

- `TreeWalker` is the current semantic truth
- `m_env` must be valid during execution
- `m_result` is the visitor scratch slot for expression results
- control flow like `retourne`, `arrêter`, and `continuer` is modeled with unwind signals
- modules execute in their own environment, then export selected members

This pass is where Lumiere stops being "a parser project" and becomes "a language runtime."

### Pass 5: learn the stdlib and integration surface

Read these next:

1. [`docs/stdlib-backend-architecture.md`](/Users/sylvainyabre/programming/tech/lumiere/docs/stdlib-backend-architecture.md)
2. [`src/interpreter/stdlib_helpers.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib_helpers.cpp)
3. [`src/interpreter/stdlib/texte.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/texte.cpp)
4. [`src/interpreter/stdlib/fichier.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/fichier.cpp)
5. [`src/interpreter/stdlib/chemin.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/chemin.cpp)
6. [`src/interpreter/stdlib/temps.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/temps.cpp)
7. [`src/interpreter/stdlib/aleatoire.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/aleatoire.cpp)
8. [`src/interpreter/stdlib/lumitest.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/lumitest.cpp)
9. [`src/interpreter/stdlib/luminet.cpp`](/Users/sylvainyabre/programming/tech/lumiere/src/interpreter/stdlib/luminet.cpp)
10. [`docs/stdlib-luminet.md`](/Users/sylvainyabre/programming/tech/lumiere/docs/stdlib-luminet.md)

Read this pass with the question:

"How do builtins fit into the same runtime model as user code?"

The important idea here is that builtins are not a separate world. They plug into the same runtime through `IRuntime`, `Value`, and native function wrappers.

### Pass 6: read tests as executable documentation

Read these after you understand the main runtime:

1. [`tests/test_lexer.cpp`](/Users/sylvainyabre/programming/tech/lumiere/tests/test_lexer.cpp)
2. [`tests/test_parser.cpp`](/Users/sylvainyabre/programming/tech/lumiere/tests/test_parser.cpp)
3. [`tests/test_parser_fixtures.cpp`](/Users/sylvainyabre/programming/tech/lumiere/tests/test_parser_fixtures.cpp)
4. [`tests/test_interpreter_fixtures.cpp`](/Users/sylvainyabre/programming/tech/lumiere/tests/test_interpreter_fixtures.cpp)
5. [`tests/test_cli_integration.cpp`](/Users/sylvainyabre/programming/tech/lumiere/tests/test_cli_integration.cpp)

Tests are where "I think this is how it works" becomes "this is what the project actually promises."

## What each major file family is for

### `src/lumiere.cpp`

This is the operational entry point.

Read it to answer:

- how CLI modes are parsed
- how a file becomes a `Program`
- how the backend is selected
- how tests are discovered from the CLI side

### `include/lumiere/parser/ast.hpp`

This is the language shape file.

Read it to answer:

- what expressions exist
- what statements exist
- what data each node stores
- how visitors traverse the tree

If you understand this file well, many later files become far less mysterious.

### `include/lumiere/interpreter/runtime/value.hpp`

This is the runtime universe file.

Read it to answer:

- what counts as a runtime value
- how primitive and compound values are represented
- which runtime object types already exist

This file is one of the most important "center of gravity" files in the repo.

### `include/lumiere/interpreter/tree_walker/tree_walker.hpp`

This is the execution contract file.

Read it to answer:

- what state the interpreter carries
- what helper responsibilities exist
- where modules, types, calls, and errors fit into execution

It is the best single header for understanding what the current backend really does.

### `src/interpreter/tree_walker/tree_walker_core.cpp`

This is where execution begins to feel real.

Read it to answer:

- how `Program` execution is initialized
- how closures are represented
- how function calls are framed
- what runtime state gets reset or preserved

### `src/interpreter/tree_walker/tree_walker_modules.cpp`

This is the import and builtins bridge.

Read it to answer:

- how built-in modules are registered
- how filesystem modules are resolved
- how module caching works
- how imports avoid leaking raw top-level environments into callers

### `src/interpreter/stdlib/*`

These files are concrete examples of native integration.

Read them when you want to understand:

- how C++ code exposes Lumiere-level functions
- how native code validates arguments
- how native code creates correctly annotated Lumiere values

### `src/interpreter/stdlib/luminet/*`

This is a sub-project inside the project.

Read it only after the core runtime feels comfortable. Otherwise it is too much surface area too early.

## The most important invariants to keep in your head

These are the ones worth memorizing.

### 1. The parser owns syntax, not semantics

The parser is responsible for building structure, not for proving every program is semantically valid.

That means some rules are enforced later by the tree-walker.

### 2. The tree-walker is the current language definition in executable form

If you want to know what Lumiere means today, the tree-walker is the answer.

The VM is not yet the main semantic authority.

### 3. `Value` is the runtime currency

Almost everything important crosses subsystem boundaries as tokens, AST nodes, or `Value`.

Those are the three currencies of the implementation.

### 4. Builtins are not outside the runtime model

They use the same call machinery and the same runtime value model as user code.

### 5. Tests are part of the spec

When in doubt, check the tests. They often define project truth more precisely than comments do.

## What not to do while reading

### Do not start with `tests/test_interpreter_fixtures.cpp`

It is valuable, but it is enormous. It is better as a confirmation pass than as a first pass.

### Do not start with `LumiNet`

Networking code is dense even when well written. It is the wrong first door into the project.

### Do not try to memorize every parser branch

Understand parser structure before parser completeness.

### Do not treat the VM as the current implementation

It is the future seam. The tree-walker is the present.

## A practical study plan

If you want a concrete approach, use this:

### Session 1

- `README.md`
- `docs/architecture-overview.md`
- `src/lumiere.cpp`
- `include/lumiere/parser/ast.hpp`

Goal:

Know the top-level pipeline and the AST shape.

### Session 2

- lexer docs and lexer files
- parser docs and parser file

Goal:

Know how source becomes structure.

### Session 3

- `value.hpp`
- `tree_walker.hpp`
- `tree_walker.cpp`
- `tree_walker_core.cpp`

Goal:

Know what the runtime stores and how execution begins.

### Session 4

- tree-walker statement and expression files
- module loading file

Goal:

Know how the language actually runs.

### Session 5

- stdlib helpers
- one small builtin module
- one larger builtin module
- tests for that area

Goal:

Know how native integration works in practice.

## The best order for strict understanding

If your goal is strict understanding rather than shallow familiarity, the right order is:

1. structure
2. data model
3. execution model
4. extensions
5. tests

Not:

1. random file
2. another random file
3. panic

## If you want to go even deeper after this

Once this guide is no longer enough, the best next step is not more broad reading. It is a traced walkthrough.

The most useful traced walkthroughs would be:

- "what happens when `principal()` runs"
- "what happens during an import"
- "what happens during a function call"
- "what happens when a builtin module returns a typed value"
- "what happens from HTTP request to Lumiere handler response"

Those are the right next layers once the map is in place.
