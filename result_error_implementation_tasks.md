# Typed result handling: implementation record

Status: implemented

Design authority: `error_handling_proposal.md`

## Completed work

### Source exception removal

- Removed the former exception tokens, grammar, AST nodes, visitors, CFG
  operations, LIR, bytecode, VM handlers, tree-walker handling, fixtures, and
  examples.
- The former keywords are ordinary identifiers.
- Added a repository test that rejects reintroduction of the removed
  production symbols.

### Type system

- Added structural parsed types for names, generic applications, integer
  arguments, and unions.
- Added interned semantic types and callable signatures.
- Added module-level `type` aliases, forward resolution, transparent
  expansion, cycle detection, visibility, imports, and collision diagnostics.
- Added normalized unions, result covariance, class/interface assignability,
  and closed-union versus open-interface exhaustiveness.
- Added native-module type and callable manifests.
- Semantic analysis runs before either execution backend and recursively
  analyzes imported source modules.
- Imported parse and semantic diagnostics retain their source file.
- Import cycles produce `LUM-S0043` with the complete ordered cycle.

### Result values and matching

- Added immutable `Succès(value)` and `Échec(error)` runtime variants.
- Added `Résultat[T,E]` typing with contextual variant inference.
- Added the built-in `Erreur` marker contract. `E` is rejected unless it is
  `Erreur`, an error class, an error subclass, or an all-error union after
  alias expansion.
- Restricted `Succès` and `Échec` construction to contexts with a complete
  expected `Résultat[T,E]`.
- Added expression-valued `agir selon`, single scrutinee evaluation, variant
  binding, branch typing, and exhaustiveness checks.
- Added tree-walker, LIR, bytecode, and VM execution.
- Failure values carry their construction origin. Forwarding preserves it;
  constructing a translated failure replaces it.

### Propagation

- Propagation uses the explicit postfix form:

  ```lumiere
  soit valeur = opération() ou propager
  ```

- A propagating failure branch is accepted only inside the nearest function
  whose explicit return annotation resolves to `Résultat[U,F]`.
- Transparent aliases, compatible unions, class/interface widening, nested
  functions, and explicit error translation are checked statically.
- `agir selon` also permits `Échec(...) -> propager`.
- Propagation is modeled as a possible function exit: pending Result
  obligations must already be consumed on that path, while an inner Result
  exposed by outer success retains its own obligation.
- Typed propagating match branches validate their precisely narrowed error
  union, including concrete selections from open `Erreur`.
- Both backends preserve exactly-once operand evaluation, error-object
  identity, concrete type, diagnostic origin, and nearest-frame exit.

### Mandatory consumption

- A call whose resolved return type is `Résultat[T,E]` cannot be discarded.
- Result constructions, result parameters, and locals create tracked
  obligations.
- Matching, returning, argument transfer, aggregate storage, and
  `ignorer expression` consume or transfer an obligation.
- Observation, casts, type tests, overwrites, scope exit, and live
  `continuer` edges do not silently consume it.
- `ignorer` accepts exactly one top-level result and returns `Rien`.
- A REPL result expression is consumed by displaying it.

### Function and process boundaries

- Every reachable normal exit of a result-returning function must explicitly
  return a compatible result.
- Interface and override methods preserve result-return contracts.
- `principal` accepts `Rien` or `Résultat[Rien,E]`.
- `Succès(rien)` exits successfully; `Échec(error)` reports the structured
  failure and its origin with a nonzero status in both backends.

### Standard library

- Migrated recoverable file, input, text-conversion, time-parsing, and
  networking failures to typed results.
- Added public structured error types and native signatures.
- LumiNet covers address, DNS, TCP, UDP, HTTP, and channel recoverable
  boundaries. Contract violations such as wrong arity, wrong types, and
  invalid port ranges remain traps.
- Added callable metadata for result-returning native object methods,
  including HTTP response writers and channel operations.

### Documentation and examples

- Updated the language and syntax specifications.
- Updated the implemented-language overview and affected standard-library
  references.
- Updated shipped examples and fixtures to handle or explicitly ignore
  results.

## Verification

- The no-legacy production-source test passes.
- Parser, semantic, runtime, fixture, CLI, LIR, and bytecode tests pass.
- Both execution backends are covered for result values, propagation,
  `principal`, origin preservation, native failures, aliases, and unions.
- Deterministic LumiNet failure tests pass. Socket integration tests remain
  platform-guarded because some sandboxes prohibit binding local sockets.
- `git diff --check` passes.
