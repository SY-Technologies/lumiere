# Lumiere Tooling Architecture

> This document records the implemented first-generation architecture. The
> persistent workspace and language-service replacement is specified in
> [Lumiere Tooling v2 Specification](./tooling-v2-spec.md).

## Goals

Lumiere tooling must remain:

- free to build, distribute, and use
- local-first, with no hosted analysis service
- driven by the real Lumiere frontend
- independent of any specific editor
- simple enough for a solo project to maintain
- extensible as Lumiere gains syntax and semantic features

The primary cost is development time. The design avoids recurring financial
costs and unnecessary infrastructure.

## System Boundary

Language truth belongs in the Lumiere repository. Editor integration belongs in
a separate tooling repository.

```text
lumiere
    lexer, parser, source ranges, diagnostics, analysis API
    `lumiere check`

lumiere-tooling
    language server, TextMate grammar, VS Code extension
```

This split keeps editor dependencies out of the interpreter while preventing
the tooling from reimplementing Lumiere syntax or semantics.

## Lumiere Repository

The core repository owns:

- tokens and keywords
- source files and byte-offset ranges
- lexer and parser diagnostics
- AST and future semantic analysis
- name, type, visibility, and import rules
- stable diagnostic codes
- terminal and machine-readable diagnostic rendering

The long-term API should resemble:

```cpp
struct AnalysisRequest
{
    std::string source;
    std::string source_path;
};

struct AnalysisResult
{
    std::vector<Diagnostic> diagnostics;
};

AnalysisResult analyze(const AnalysisRequest &request);
```

Execution backends and tooling consume the same analysis result. Neither the VM
nor the tree-walker should define editor-facing language rules.

## Structured Diagnostics

Diagnostics must be data, not formatted stderr strings.

```cpp
enum class DiagnosticSeverity
{
    ERROR,
    WARNING,
    INFORMATION,
    HINT,
};

struct TextRange
{
    std::size_t start;
    std::size_t end;
};

struct Diagnostic
{
    std::string code;
    DiagnosticSeverity severity;
    std::string message;
    TextRange range;
};
```

Ranges use byte offsets internally. The language server converts them to LSP
UTF-16 positions using the exact editor buffer.

Diagnostic codes are stable API:

```text
LUM-L0012  invalid character
LUM-P0034  missing closing brace
LUM-N0007  unknown identifier
LUM-T0021  incompatible types
LUM-C0011  unreachable statement
```

Messages may improve without breaking tests, suppressions, documentation, or
editor integrations.

## CLI Analysis Interface

The first tooling boundary is a local compiler subprocess:

```bash
lumiere check programme.lum
lumiere check --format=json programme.lum
lumiere check --format=json --stdin --source-path /project/main.lum
```

Stdin support allows an editor to analyze unsaved text. `--source-path` supplies
the location needed for relative import resolution.

The human terminal renderer and JSON renderer consume the same structured
diagnostics.

## Versioned JSON Protocol

The language server must never parse human-readable stderr. Machine output uses
an explicitly versioned protocol:

```json
{
  "protocolVersion": 1,
  "source": "/project/main.lum",
  "diagnostics": [
    {
      "code": "LUM-P0004",
      "severity": "error",
      "message": "attendu ')' après les paramètres",
      "range": {
        "start": 21,
        "end": 22
      },
      "related": [],
      "fixes": []
    }
  ]
}
```

Protocol changes either remain backward compatible or increment
`protocolVersion`.

## Tooling Repository

The external repository can use a small monorepo layout:

```text
lumiere-tooling/
    packages/
        language-server/
        vscode-extension/
    syntaxes/
        lumiere.tmLanguage.json
    language-configuration.json
    tests/
```

The language server is responsible only for:

- document synchronization
- compiler subprocess management
- debouncing and cancellation
- byte-offset to UTF-16 conversion
- conversion between Lumiere diagnostics and LSP diagnostics
- locating the installed `lumiere` executable

It must not implement Lumiere parsing, name resolution, or type rules.

## Language Server Technology

TypeScript with the open-source `vscode-languageserver` package is the pragmatic
initial choice:

- protocol mechanics are already implemented
- VS Code integration is direct
- the same server works with other LSP clients
- it is easy to test and package
- it does not require a hosted service

The language server launches `lumiere check --format=json` locally. If measured
latency later justifies it, the subprocess boundary can become a persistent C++
analysis process without changing editor behavior.

## Highlighting

Use two layers:

1. A TextMate grammar for immediate lexical highlighting.
2. LSP semantic tokens for accurate symbol-aware highlighting.

TextMate handles keywords, comments, strings, numbers, operators, and basic
declaration patterns even while the language server is unavailable.

Semantic tokens later distinguish locals, parameters, globals, functions,
methods, classes, interfaces, properties, modules, types, and fixed bindings.

The TextMate grammar is a fallback, not a second semantic implementation.

## Distribution Without Paid Services

| Component | Distribution | Cost |
|---|---|---:|
| Lumiere compiler | GitHub Releases | Free |
| Language server | GitHub Releases or extension bundle | Free |
| VS Code extension | Visual Studio Marketplace | Free |
| VSCodium/Theia extension | Open VSX | Free |
| CI | GitHub Actions for the public repository | Free allowance |
| Documentation | Repository Markdown or GitHub Pages | Free |

No cloud analysis backend, telemetry service, proprietary parser generator, or
custom update server is required.

Other editors can launch the same language server directly. They do not require
new semantic implementations.

## Initial Scope

### Implementation Status

The Lumiere-owned initial scope is implemented:

- `analyze_source()` returns structured lexer and parser diagnostics
- tokens retain byte ranges without changing runtime traceback positions
- parser recovery reports independent syntax errors in one analysis pass
- `lumiere check` supports terminal output, JSON protocol version 1, and stdin
- tests lock diagnostic ranges, JSON escaping, recovery, and CLI behavior

The external `lumiere-tooling` sibling repository now contains:

- an editor-independent TypeScript language server
- per-document debounce, cancellation, and compiler process management
- UTF-8 byte-offset to LSP UTF-16 conversion
- protocol validation and LSP diagnostic publication
- a TextMate grammar and VS Code language configuration
- a bundled VS Code extension with reproducible VSIX packaging
- protocol, range, compiler integration, grammar, build, and packaging tests

The first release intentionally provides syntax diagnostics and lexical
highlighting. Semantic diagnostics and semantic tokens must be added only after
their authoritative analysis exists in Lumiere.

### Lumiere

1. Source IDs and byte-offset ranges
2. Structured diagnostic types
3. Lexer and parser diagnostics without direct stderr output
4. Basic parser recovery
5. `lumiere check`
6. JSON protocol version 1
7. Stdin analysis with a logical source path
8. Protocol and diagnostic stability tests

### Lumiere Tooling

1. Lumiere language registration
2. TextMate grammar
3. Language server process
4. Compiler executable discovery
5. Full-document synchronization
6. Debounced compiler analysis
7. LSP diagnostic publication
8. VS Code extension packaging
9. `.vsix`, Marketplace, and Open VSX release workflows

## Deferred Work

The architecture permits these features, but they are not required initially:

- incremental syntax trees
- semantic query caching
- cross-file incremental invalidation
- formatter
- hover and go-to-definition
- references and rename
- semantic completion
- code actions and automated fixes
- direct library integration

Full-file reanalysis is acceptable until measurements prove otherwise.

## Non-Negotiable Rules

- There is one authoritative Lumiere lexer and parser.
- The extension never parses compiler stderr.
- LSP protocol types do not enter the compiler frontend.
- Editor tooling does not implement language semantics.
- Diagnostics are backend-independent structured values.
- Source ranges are not stored only as line and column.
- Unsaved editor text takes precedence over disk contents.
- No hosted service is required for normal operation.
- Optimizations are introduced only after measuring a real bottleneck.

This gives Lumiere credible beta tooling without creating disposable beta
architecture. The initial implementation stays small, while the boundaries can
support future language growth.
