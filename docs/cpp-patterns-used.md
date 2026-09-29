# C++ Patterns Used Here

## Scope

This note is about the implementation style of the project, not the Lumiere language itself.

The goal is to make the C++ choices legible:

- what pattern is being used
- why it is a reasonable fit here
- what tradeoff it introduces
- what to watch out for while extending the code

## 1. Value semantics where possible, owning pointers where necessary

The codebase uses a fairly sensible default:

- plain values when the payload is naturally small or self-contained
- `std::unique_ptr` for tree ownership
- `std::shared_ptr` for shared runtime entities

This is a healthier mental model than memorizing “always prefer X.”

The real question is the ownership model:

- one owner: `unique_ptr`
- many owners: `shared_ptr`
- no ownership transfer needed: plain value or reference

## 2. `std::unique_ptr` for AST topology

The AST is a strict ownership tree. That makes `std::unique_ptr` the right primitive because it encodes the exact structure we want:

- a node owns its children
- no other object owns the same child
- subtree destruction is automatic

This removes a whole class of manual lifetime bugs.

## 3. `std::shared_ptr` for runtime aliasing

The runtime stores collections, functions, and objects behind `shared_ptr`.

That is not just convenience. These entities have semantics that naturally involve aliasing:

- the same function value can be referenced from multiple places
- the same object can be observed through different variables
- mutable aggregate values should not be accidentally deep-copied by default

The cost is reference-counted overhead and reduced lifetime explicitness. For this project stage, that tradeoff is acceptable.

## 4. `std::variant` instead of manual unions

`Value` uses `std::variant` to represent heterogeneous runtime payload.

That buys:

- type-safe access
- clear alternative list
- easier debugging than raw unions

The code still pairs it with a manual enum tag, which increases clarity at the language level but adds maintenance burden. That is a conscious “clarity over minimalism” choice.

## 5. RAII beyond memory

The most important C++ pattern in this codebase is probably RAII, but not for the usual beginner reason.

Here RAII protects interpreter state:

- `ScopeGuard` restores lexical scope
- `StackFrameGuard` restores stack-trace state

This is a strong lesson: RAII is about making scope exit correct, not merely about freeing heap memory.

## 6. Classic runtime polymorphism

The AST uses virtual base classes and visitors. `Backend` also uses a virtual interface.

This is not modern-C++ maximalism. It is classic runtime polymorphism, and it fits because:

- the set of operations over nodes changes
- the AST node hierarchy is explicit
- backend substitution is a real architectural need

You could model some of this with `std::variant` and `std::visit`, but the current design is defensible and easier to relate to traditional compiler architecture.

## 7. Exceptions used as structured control flow

This project uses exceptions for:

- actual failures (`ParseError`, `RuntimeError`)
- non-local interpreter control flow (`ReturnSignal`, `BreakSignal`, `ContinueSignal`)

This is one of the places where C++ can be elegant or disastrous depending on discipline.

Here it is mostly justified because:

- AST execution is deeply nested
- unwinding is exactly the behavior needed
- RAII guards make cleanup reliable

The rule is not “exceptions are always fine.” The rule is “exceptions are useful when the control-flow model truly is unwinding.”

## 8. Header / implementation split

The project follows a conventional split:

- headers define interfaces
- `.cpp` files define behavior

That matters pedagogically. In a compiler project, the public interface of the system is often as important as the behavior:

- token vocabulary
- AST surface
- runtime data layout
- interpreter entrypoints

Keeping those visible in headers helps.

## 9. Explicit helper methods over abstraction fever

The codebase often prefers helper methods like:

- `is_truthy`
- `is_equal`
- `assert_entier`
- `matches_type_name`

This is a good local style. It keeps complicated checks named and centralized instead of duplicated across visitors.

The danger is helper drift: if many helpers appear without a stronger abstraction boundary, they can become a bag of semi-related utilities. So the pattern is good, but should stay disciplined.

## 10. Sentinel values vs stronger types

The code sometimes uses empty-lexeme tokens as “missing” markers instead of `std::optional<Token>`.

This is a tradeoff:

- fewer type wrappers
- simpler immediate code
- weaker absence modeling

For a learning codebase this can be acceptable, but it is worth recognizing where stronger types would make invariants clearer.

## 11. What not to imitate blindly

Some current patterns are practical, but not automatically best practice forever:

- duplicating `Value::Type` and `Value::Data`
- string-driven runtime type checks
- partial semantic rules enforced only at runtime
- broad use of shared ownership

These are reasonable at the current stage. They are not permanent proof that the final architecture should look the same.

## Questions to ask before adding C++ complexity

Before introducing a more advanced pattern, ask:

1. Does this reduce a real maintenance burden?
2. Does it make ownership clearer?
3. Does it encode an invariant the compiler currently leaves implicit?
4. Will the next person reading this learn from it or fight it?

If the answer is “no,” simpler is usually better.

## Bottom line

The strongest C++ decisions in this repository are not the fanciest ones. They are the ones that make compiler structure explicit:

- clear ownership
- narrow state transitions
- localized invariants
- readable phase boundaries

That is the right priority for a serious learning language project.
