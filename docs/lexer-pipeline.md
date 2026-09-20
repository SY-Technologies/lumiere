# Lexer Pipeline

## Scope

This note explains how source text becomes tokens in the current implementation and what design constraints that imposes on the rest of the frontend.

Relevant code:

- `include/lumiere/lexer/scanner.hpp`
- `include/lumiere/lexer/tokenizer.hpp`
- `include/lumiere/lexer/lexer.hpp`
- `include/lumiere/lexer/token.hpp`
- `src/lexer/scanner.cpp`
- `src/lexer/tokenizer.cpp`
- `src/lexer/lexer.cpp`

## Why three classes instead of one

The lexer is intentionally split into:

- `Scanner`: cursor mechanics over source bytes
- `Tokenizer`: token recognition rules
- `Lexer`: “scan until EOF” orchestration

This is not abstraction for its own sake. It enforces a separation between:

- movement
- recognition
- control flow

That separation makes it easier to debug lexical bugs because each class can be blamed for a narrower class of failure.

## `TokenType` as parser-facing vocabulary

`TokenType` is not just a lexer detail. It is the language’s parser-facing vocabulary.

That means adding a keyword or operator is never a lexer-only change. It usually implies coordinated updates to:

- `TokenType`
- keyword recognition
- parser dispatch
- AST if new syntax is introduced
- runtime if semantics are executable

The recent `AGIR_SELON` work is a good example of this pipeline coupling.

## `Token` payload design

Each token stores:

- `type`
- `lexeme`
- `line`
- `column`

Keeping the lexeme is a deliberate memory-for-clarity trade. It supports:

- human diagnostics
- AST printing
- preserving source spelling for later semantic work

If the implementation later needs lower allocation pressure, token storage is one of the places to revisit, but it is not the right early optimization target for this project.

## `Scanner`

`Scanner` owns the full source string and tracks:

- `m_start`
- `m_current`
- `m_line`
- `m_column`

### Design role

`Scanner` does not know language syntax. It only knows how to move through text and expose the current character window.

That is important. Once `Scanner` starts making language decisions, the tokenizer becomes much harder to reason about.

### Source-position model

The scanner records 1-based line and column positions. That is a user-facing choice for diagnostics.

### `save()` / `restore()`

This is the most nontrivial scanner feature. It allows controlled backtracking.

Current use cases:

- `tant que`
- `agir selon`

This tells us something important about the language design: Lumiere includes multi-word keywords, so the scanner/tokenizer boundary must support speculative recognition.

## `Tokenizer`

`Tokenizer` is where syntax-aware lexical decisions live.

### Responsibilities

- skip whitespace/comments
- recognize punctuation
- recognize operators
- scan literals
- scan identifiers
- classify reserved words
- produce error tokens for malformed lexemes

### Comment handling

Whitespace and comment skipping happen outside `scan_token()`. This is a useful design because it keeps token recognition focused on real token starts.

### Error token strategy

Malformed input produces `ERREUR` tokens rather than immediately throwing.

That is a notable design decision. It means the lexer can remain a producer of token objects even when input is malformed.

Whether the parser should later recover from such tokens is a separate question.

### Identifier-then-keyword classification

The tokenizer first scans an identifier-like word, then calls `keyword_type(word)`.

That is preferable to hard-coding every keyword directly in the character switch because:

- the code stays simpler
- keyword addition is localized
- “identifier but maybe keyword” remains one conceptual operation

### Multi-word keywords

The tokenizer has special-case logic for `tant` and `agir` because they can begin multi-word keywords.

This is a pragmatic implementation, but it introduces a maintenance rule:

If Lumiere gains more spaced keywords, the tokenizer will accumulate more special-case lookahead unless the keyword model is generalized.

That is not a problem yet, but it is worth recognizing.

## `Lexer`

`Lexer` is intentionally thin. It composes scanner + tokenizer and emits a full token stream with `FIN_FICHIER`.

This is exactly where orchestration should live. `Lexer` should be boring.

If `Lexer` ever becomes complicated, it is usually a sign that logic has leaked out of `Tokenizer` or the token model is underdesigned.

## Lexical invariants

The parser relies on these invariants:

- `FIN_FICHIER` is always present
- comments are gone before parsing
- combined spaced keywords are already fused into single token kinds
- token positions are stable enough for diagnostics

If any of these break, parser code becomes more complex immediately.

## Current limitations and likely future work

### UTF-8 handling

The code currently supports accented identifiers pragmatically through byte-level rules. It is not yet a full Unicode-lexing subsystem with grapheme-aware behavior.

That is probably acceptable for the current language stage, but it should be treated as “good enough for now,” not “done forever.”

### Numeric and string literal growth

As the language grows, literals are a common pressure point:

- escapes
- separators
- malformed edge cases
- numeric suffixes if ever introduced

Tokenizer complexity tends to grow here first.

### Error recovery

Right now the lexer can emit `ERREUR`, but the broader frontend does not yet implement rich recovery. That means lexical error handling is still a partial story.

## C++ notes

- Splitting movement from recognition is a design win.
- Returning tokens by value is fine here; the code gains clarity.
- Backtracking state as a small value object (`Scanner::State`) is a clean C++ technique.
- This subsystem benefits more from crisp responsibilities than from clever templating or parser-framework indirection.
