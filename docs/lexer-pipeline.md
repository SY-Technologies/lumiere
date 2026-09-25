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
- `decoded`

Keeping the lexeme is a deliberate memory-for-clarity trade. It supports:

- human diagnostics
- AST printing
- preserving source spelling for later semantic work

`decoded` is narrower: only `TEXTE_LIT` and `SYMBOLE_LIT` tokens populate it, with
quotes stripped and escape sequences resolved (see "Escape sequences" below).
Every other token type leaves it empty and unused. The two execution engines
build their runtime `Texte`/`Symbole` values from `decoded`, never from
`lexeme` — `lexeme` stays the raw, still-escaped source slice, kept only for
diagnostics and round-tripping. This mirrors the project's `lexeme`-for-clarity
trade-off above: a second string per literal token costs a small, one-time
allocation in exchange for keeping "what the source said" and "what it means"
unambiguously separate.

If the implementation later needs lower allocation pressure, token storage is one of the places to revisit, but it is not the right early optimization target for this project.

## Escape sequences

`scan_string()` and `scan_symbol()` (`src/lexer/tokenizer.cpp`) decode
backslash escapes into `Token::decoded` as they scan, rather than deferring
to the two execution engines. A malformed escape is therefore a lexer-time
`ERREUR` token, consistent with how a malformed number literal is already
rejected here rather than downstream.

Recognised escapes: `\n`, `\t`, `\r`, `\\`, `\"`, `\'`, `\0`, and
`\u{XXXXXX}` (1 to 6 hexadecimal digits, any Unicode scalar value, encoded to
UTF-8 via `utf8::encode_character`). Any other character after a backslash is
a lexer error, not a silent pass-through — a language string is either what
its author wrote or a clear diagnostic, never a guess. One consequence worth
knowing: a literal backslash that must survive unresolved (a Windows path, a
regular-expression pattern handed to another module) is written `\\` in
Lumière source, exactly as in most other languages with escape sequences —
there is no separate "raw string" literal form.

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

- separators
- malformed edge cases
- numeric suffixes if ever introduced

Escape sequences are implemented (see "Escape sequences" above); the items
above remain open. Tokenizer complexity tends to grow here first.

### Error recovery

Right now the lexer can emit `ERREUR`, but the broader frontend does not yet implement rich recovery. That means lexical error handling is still a partial story.

## C++ notes

- Splitting movement from recognition is a design win.
- Returning tokens by value is fine here; the code gains clarity.
- Backtracking state as a small value object (`Scanner::State`) is a clean C++ technique.
- This subsystem benefits more from crisp responsibilities than from clever templating or parser-framework indirection.
