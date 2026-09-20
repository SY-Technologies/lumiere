# Tree-Walker Backlog

## Purpose

This note tracks the remaining work in the tree-walker interpreter at a level that is useful for implementation planning rather than vague roadmap talk.

The tree-walker already proves a meaningful subset of the language, but it is still missing large parts of the execution model. The list below is organized by dependency order rather than by “what would be nice someday.”

## 1. What Is Already Done

The tree-walker now executes a much larger subset than this backlog originally assumed.

Implemented:

- arithmetic, comparison, equality, logical operators, and assignment
- primitive casts for the current runtime types
- `SYMBOLE_LIT`, list literals, dictionary literals, and indexed access
- class declarations, object construction, field access, method calls, and receiver binding
- inheritance-aware lookup, `parent`, and runtime enforcement of `remplace`
- `agir selon` first-match execution with literal, typed-binding, and `rien` patterns
- `pour chaque` iteration for lists, sets, and text
- import execution from `.lum` files with module caching and cycle detection

That means the remaining work is no longer “make the interpreter basically usable.” It is now mostly semantic depth, protocol design, and polish.

## 2. Control flow and data model gaps

### 2.1 Iteration protocol depth

Needed:

- decide whether the language wants a first-class range / interval runtime protocol
- define whether dictionaries iterate over keys, entries, or require explicit helpers
- decide whether user-defined objects can become iterable later

### 2.2 `agir selon`

- exhaustiveness checking in a semantic phase
- richer pattern kinds if the language grows them
- clearer interaction between class hierarchies and typed patterns

### 2.3 Set semantics

The runtime has `ENSEMBLE` as a value kind, but the execution layer still does not treat sets as a first-class language feature with dedicated literal, uniqueness, or membership semantics.

## 3. Modules and packages

Needed:

- broader standard-library module coverage
- decide whether re-export syntax belongs in the language

## 4. Interfaces and semantic validation

### 4.1 Interfaces are still inert at runtime

Needed:

- check that classes claiming `réalise` actually provide the required methods
- decide whether interface conformance belongs entirely to semantic analysis or partly to runtime

### 4.2 Semantic phase handoff

Some interpreter complexity should eventually move out of the tree-walker once a semantic phase exists.

Likely candidates:

- type compatibility checks
- declaration validity
- `agir selon` exhaustiveness
- some cast validation

## Suggested implementation order

1. define real interface checking
2. deepen typed error semantics
3. improve module export/import surface design
4. move validity checks into a semantic pass

The reason for this order has changed. The interpreter is now usable enough to run meaningful programs, so the next gains come from reducing semantic ambiguity rather than only adding more surface constructs.
