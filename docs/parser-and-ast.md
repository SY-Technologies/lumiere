# Parser and AST

## Scope

This note explains:

- how the parser is structured
- what the AST currently represents
- why visitors were chosen
- what invariants and compromises the current parser relies on

Relevant code:

- `include/lumiere/parser/ast.hpp`
- `include/lumiere/parser/parser.hpp`
- `include/lumiere/parser/printer.hpp`
- `src/parser/parser.cpp`

## Why recursive descent is a good fit here

The parser is hand-written recursive descent. That means the grammar structure is encoded directly in control flow.

This is a strong fit for the project because:

- the language grammar is still moving
- syntax experiments are common
- debugging needs to stay transparent
- learning value is high

The cost is manual upkeep. Every grammar feature is hand-implemented, and precedence has to be maintained carefully.

## Parser responsibilities

The current parser does all of the following:

- statement dispatch
- expression precedence parsing
- AST construction
- some syntax-form validation
- early syntax error reporting

It does not yet do full semantic validation. That means some constructs can be syntactically accepted before later layers truly know what they mean.

## Expression precedence structure

The parser encodes precedence as a function stack:

- `parse_assignment`
- `parse_or`
- `parse_and`
- `parse_not`
- `parse_equality`
- `parse_comparison`
- `parse_addition`
- `parse_multiplication`
- `parse_unary`
- `parse_cast`
- `parse_call`
- `parse_primary`

This is one of the most important parser patterns to learn. The code is not arbitrary; each layer represents one precedence band.

If precedence bugs appear, the first question should be: which parse layer should own this operator?

## AST ownership model

The AST is built out of explicit node structs, with child ownership expressed via `std::unique_ptr`.

That gives:

- single ownership of subtrees
- no manual delete logic
- explicit tree structure in the type system

This is a strong educational choice because the ownership model is visible and difficult to misuse accidentally.

## Why explicit node structs instead of tagged enums

There are two broad ways to model ASTs in C++:

1. inheritance + virtual dispatch + visitors
2. tagged unions / variants for nodes

This project uses the first style.

Reasons that make sense here:

- it matches classic compiler teaching material
- node-specific fields are easy to attach
- backend operations can be expressed as visitors

Costs:

- more boilerplate
- every new node requires many coordinated edits

That is acceptable in a learning compiler as long as the node set is still manageable.

## Visitor pattern tradeoff

The AST uses:

- `ExprVisitor`
- `StmtVisitor`

This keeps AST operations separated from AST data, which is useful because the same tree is walked by:

- the printer
- the tree-walker
- future analysis passes

The downside is that adding a node means touching:

- the forward declarations
- the visitor interfaces
- the node definitions
- every visitor implementation

That friction is real. It is the price of this design.

## Current parser conventions worth knowing

### Sentinel tokens instead of `std::optional<Token>`

Several parse results use a token with empty lexeme as an “absent” marker.

This is simple, but it is also slightly fragile because the absence is conventional rather than type-enforced.

It works well enough for now, but longer-term some places may become clearer with `std::optional<Token>`.

### Expression statements are broad

Anything not claimed by a more specific statement parser often falls back to expression parsing.

This is normal in recursive descent parsers, but it also means syntax errors can sometimes surface “later than expected” because the parser first tries to interpret the input as an expression.

### Block bodies are stored as `StmtPtr`

Many constructs semantically expect a block but store it as a generic statement pointer. That gives flexibility, but it also means some “must be a block” constraints are conventional rather than encoded in the type system.

## `AgirSelonStmt` as a case study

`agir selon` is a useful example because it crosses multiple parser concerns:

- statement dispatch
- branch parsing
- pattern form parsing
- optional else-like branch

It also shows the boundary between syntax and semantics:

- parser knows how to read literal / typed / `rien` patterns
- parser does not perform exhaustiveness analysis
- runtime currently interprets type names pragmatically

That split is okay, but it should be documented as such.

## The AST printer is more important than it looks

`AstPrinter` is effectively a parse-debugging tool.

It helps answer:

- did the parser consume the right tokens?
- did precedence associate the way we expected?
- did the syntax extension build the right node type?

For a hand-written parser, a printer like this is one of the highest-leverage support tools you can have.

## Missing layer: semantic analysis

The biggest architectural absence after parsing is a semantic pass.

That future layer would likely own:

- symbol resolution
- type checks
- declaration validity checks
- pattern exhaustiveness checks
- import/package resolution checks

Once that exists, the parser can stay more purely syntactic instead of slowly absorbing semantic responsibilities.

## C++ notes

- `std::unique_ptr` is the correct default for AST ownership.
- The visitor pattern is verbose but mechanically honest.
- Recursive descent rewards readable control flow more than abstraction tricks.
- In this subsystem, clarity of grammar-to-code mapping is more valuable than reducing every line of boilerplate.
