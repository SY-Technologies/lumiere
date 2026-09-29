# Lumiere Tooling v2 Specification

## Status

This document is the implementation contract for the second generation of
Lumiere editor tooling. It covers coordinated work in the `lumiere` and
`lumiere-tooling` repositories.

The existing tooling remains supported while v2 is built. Migration is
incremental; this is not authorization for a greenfield rewrite.

## Decision

Lumiere will add a persistent, compiler-owned language service:

```text
lumiere language-service --stdio
```

The TypeScript language server in `lumiere-tooling` remains the LSP adapter. It
starts the language service, synchronizes editor documents, converts UTF-16 LSP
positions to UTF-8 byte offsets, maps compiler results to LSP types, and handles
editor lifecycle concerns.

Language meaning remains entirely in the Lumiere compiler. The TypeScript
server does not parse Lumiere, bind names, infer types, resolve modules, or
guess semantic answers.

```text
Editor
  | Language Server Protocol
  v
TypeScript LSP adapter
  | versioned JSON-RPC over stdio
  v
Lumiere language service
  |-- source overlays
  |-- module graph
  |-- parse and semantic snapshots
  |-- scope and symbol index
  `-- compiler-owned language queries
```

## Why v2 is needed

The current architecture was appropriate for the first tooling release, but it
does not scale to a mature language:

- every diagnostic or hover request starts a new compiler process
- imported modules are reread and reanalyzed for each request
- diagnostics and hover use independent compiler executions
- an importing file sees an imported file's disk contents rather than its
  unsaved editor buffer
- inspection reconstructs declaration lookup separately from semantic binding
- the semantic model records many expression types but not every occurrence's
  resolved symbol
- protocol versions are split across unrelated one-shot commands
- the hand-maintained TextMate grammar can drift from compiler keywords and
  builtin types
- there is no foundation for definition, references, rename, completion,
  signature help, semantic tokens, or workspace symbols

Adding those features directly to the existing one-shot model would create a
second, less-correct compiler inside the inspection and LSP layers.

## Goals

Tooling v2 must provide:

1. Compiler-authoritative answers for every semantic editor feature.
2. Correct behavior across modules and unsaved buffers.
3. A responsive persistent service with bounded, cancellable requests.
4. Stable source identities, byte ranges, symbols, and document versions.
5. Graceful operation while a user is typing incomplete or invalid code.
6. Explicit compatibility negotiation between compiler and tooling.
7. Crash isolation and automatic recovery.
8. Editor independence through standard LSP at the outer boundary.
9. Deterministic, testable behavior with no hosted service.
10. A staged migration that preserves `lumiere check` and the released
    extension throughout development.

## Non-goals

Tooling v2 will not:

- move language semantics into TypeScript
- implement LSP directly inside the compiler
- expose C++ ABI objects to Node.js
- require a cloud service, account, or telemetry
- build an incremental parser before measurements justify one
- execute user Lumiere programs during analysis
- implement a formatter by printing the executable AST
- introduce a package manager or new module-resolution semantics
- support collaborative editing or remote shared analysis sessions
- promise semantic answers from stale source after an edit

## Repository ownership

### `lumiere`

The compiler repository owns:

- lexing, parsing, recovery, and semantic analysis
- module resolution and import visibility
- source files, snapshots, ranges, and version validation
- scope construction and symbol identity
- declaration, reference, member, type, and signature queries
- diagnostics, related locations, and safe fixes
- completion candidates and rename validation
- semantic token classification
- language-service protocol implementation
- compiler metadata used to validate editor grammars
- the one-shot `check` and compatibility `inspect` commands

### `lumiere-tooling`

The tooling repository owns:

- the editor-independent TypeScript LSP process
- language-service process discovery and supervision
- LSP document synchronization
- LSP UTF-16 to compiler UTF-8 position conversion
- mapping compiler query results into LSP response types
- debounce, request cancellation, stale-response rejection, and restart policy
- TextMate grammar, language configuration, and file icons
- VS Code extension activation, configuration, packaging, and publication
- LSP-level integration tests

### Boundary rule

If a feature needs to know what a Lumiere program means, its answer is computed
in `lumiere`. If it concerns an editor protocol, process, setting, or package,
it belongs in `lumiere-tooling`.

## Compiler component structure

The implementation separates workspace state, semantic facts, queries, and
transport. These are responsibilities, not a requirement to create one tiny
file per type.

```text
Workspace
  SourceStore
  ModuleGraph
  AnalysisDatabase

AnalysisSnapshot
  parsed documents
  semantic models
  SemanticIndex

LanguageQueries
  diagnostics, hover, definition, references, completion, ...

LanguageService
  framed JSON-RPC
  request validation and cancellation
  calls Workspace and LanguageQueries
```

`Workspace` is the only mutable language state. Installing an overlay or disk
change produces a new workspace revision and invalidates affected cached data.

`AnalysisSnapshot` owns the trees and semantic data its queries reference. A
snapshot is immutable and reference-counted internally so a cancelled or slow
query cannot observe freed AST storage.

`LanguageQueries` contains no JSON, LSP, process, or terminal logic. Its inputs
are a snapshot plus source positions; its outputs are compiler data structures.
The one-shot CLI and persistent service serialize those same structures.

`LanguageService` contains no language rules. It validates protocol messages,
selects snapshots, invokes queries, serializes results, and manages request
cancellation.

The initial source layout should remain compact:

```text
include/lumiere/tooling/
    workspace.hpp
    semantic_index.hpp
    language_queries.hpp
    service_protocol.hpp
src/tooling/
    workspace.cpp
    semantic_index.cpp
    language_queries.cpp
    service_protocol.cpp
```

Additional files are justified only when a component acquires a distinct
responsibility. None of these components may depend on the tree-walker, VM,
runtime values, or LSP libraries.

## Terminology and core identities

### `SourceId`

An opaque service-session identifier for one source document. A canonical file
path has one `SourceId` even when its contents come from an editor overlay.
Untitled documents have synthetic source identities and no disk path.

`SourceId` values are valid only for the language-service process that issued
them. They are never persisted by the LSP adapter.

### `DocumentVersion`

A monotonically increasing signed 64-bit integer supplied by the client for an
open document. Requests naming an older or unknown version fail with
`StaleDocument` rather than returning an answer for different text.

Disk-backed closed documents use an internal content version derived from file
identity, modification metadata, and a content hash when necessary. Correctness
must not depend on timestamp resolution alone.

### `ContentVersion`

An opaque version attached to exact source bytes. For an open document it
incorporates `DocumentVersion`; for a closed file it incorporates the verified
disk identity. It is used in cross-source locations and edits so consumers can
prove that offsets belong to the text they are converting or changing.

### `SourceSpan`

```text
SourceSpan {
    source: SourceId
    start:  zero-based UTF-8 byte offset
    end:    zero-based exclusive UTF-8 byte offset
}
```

Offsets must fall on UTF-8 scalar boundaries. Empty spans are allowed at EOF or
an insertion point. The compiler protocol never uses LSP line/column positions.

Protocol locations wrap a span with its URI and `ContentVersion`:

```text
SourceLocation {
    uri
    contentVersion
    span
}
```

### `SymbolId`

An opaque identifier for one semantic declaration in an analysis snapshot.
Every declaration and resolved occurrence uses a `SymbolId`.

`SymbolId` is stable for the lifetime of the snapshot that produced it. It is
not promised to survive edits. Clients request durable operations such as
rename using a document version and source position, not a cached `SymbolId`.

### Analysis snapshot

An immutable, internally owned view of:

- exact source versions
- tokens and recovered syntax trees
- diagnostics
- module dependencies and exports
- semantic models
- scopes, symbols, and occurrences

A query observes one snapshot. It cannot see half of an update or mix document
versions.

## Workspace model

### Service lifetime

One language-service process serves one LSP connection and may contain multiple
workspace roots. It initializes compiler metadata once and remains alive until
`shutdown` followed by `exit`, stdin closure, or a fatal internal failure.

The initial implementation processes state mutations serially. Read queries may
also be serialized until profiling proves that parallel snapshots are needed.
Correct cancellation and deterministic results take priority over parallelism.

### Workspace roots

The client supplies normalized absolute workspace roots during initialization
and may add or remove roots later. A file belongs to the longest matching root.
A file outside every root is still analyzable as a standalone document.

Version 2 does not invent a project manifest. Module resolution follows the
compiler's existing rules, using the importing source's logical path.

### Workspace discovery

The service discovers `.lum` files beneath workspace roots for workspace
symbols, references, and rename. Discovery runs in the background, does not
follow directory symlinks by default, and skips version-control metadata and
dependency directories (`.git`, `.hg`, `.svn`, and `node_modules`). Open files
and resolved imports are indexed immediately rather than waiting for discovery.

Discovery has configurable file-count and total-byte limits, with conservative
defaults of 50,000 files and 512 MiB of Lumiere source. Reaching a limit marks
the workspace index incomplete and emits a service status event. Reference and
workspace-symbol results then report `isIncomplete: true`. Rename of a symbol
visible outside its declaring source is rejected while the relevant workspace
index is incomplete; the service never returns a knowingly partial rename.

### Source store and overlays

The workspace source store has two layers:

1. open-document overlays received from the editor
2. disk contents for closed files

An overlay always wins. If `a.lum` imports `b.lum` and `b.lum` is open with
unsaved changes, analysis of `a.lum` must use that overlay.

Opening a document creates or replaces its overlay. A change replaces the full
text in the first implementation. Closing a document removes the overlay and
reloads disk state if the source is still reachable from the module graph.

The adapter reports created, changed, and deleted `.lum` files received through
editor file-watching facilities. A change to a closed file invalidates that
source and its affected importers. Before reusing a closed disk source, the
service also validates its recorded file identity so correctness does not depend
on a client delivering every filesystem notification.

LSP incremental text synchronization may be added later, but it must produce
the same exact source buffer before analysis. It is a transport optimization,
not an incremental parser requirement.

### Untitled documents

Untitled buffers may be lexed, parsed, and analyzed with builtin modules. They
cannot resolve relative file imports until the client supplies a logical file
path. A failed relative import produces an ordinary structured diagnostic, not
a service failure.

### Module graph

The workspace owns a directed graph from each source to its direct imports and
a reverse graph from each source to its importers.

Changing a source invalidates:

- its tokens, tree, semantic model, and query indexes
- its public export summary
- the semantic results of transitive importers when the export summary changes

Parsing is always invalidated for the changed source. Importers need not be
reanalyzed when the new export summary is semantically identical. The first
implementation may conservatively invalidate all transitive importers; export
fingerprinting is an allowed measured optimization.

Import cycles use the compiler's normal diagnostic rules. The graph must remain
usable after reporting a cycle.

### Cache policy

The service caches only compiler-owned immutable data:

- source contents and line indexes
- tokens and recovered syntax trees
- semantic export summaries
- semantic models and symbol indexes
- completed query-independent diagnostics

Cache keys include all source and dependency versions that affect an answer.
There is no time-based correctness cache. Memory may be reclaimed for closed,
unreachable documents using least-recently-used eviction, but open documents
and their import closure remain resident.

The first implementation performs full-file lexing and parsing after an edit.
It must not introduce incremental syntax trees merely to satisfy this design.

## Error-tolerant analysis

Editor buffers are invalid during ordinary typing. The service must preserve as
much current-source information as possible without pretending stale code is
current.

The analysis pipeline is:

1. tokenize the current buffer and retain all valid tokens
2. parse with recovery and retain every recovered statement
3. build scopes and bind recoverable declarations and expressions
4. skip malformed nodes whose required structure is absent
5. produce lexical, syntax, and semantic diagnostics together
6. mark incomplete semantic facts as unavailable rather than manufacturing a
   type or declaration

Semantic analysis must no longer be globally disabled because one parser
diagnostic exists. Individual malformed subtrees are skipped.

The service never answers from a last-known-good semantic model after the
document changes. Lexical fallback features, such as keyword hover, may still
answer from current tokens.

## Semantic model requirements

The existing executable semantic analysis remains authoritative, but its model
must expose a tooling index rather than forcing inspection code to walk the AST
and guess.

### Symbol record

Each symbol records:

```text
Symbol {
    id
    name
    kind
    declarationSpan
    selectionSpan
    containingSymbol
    declaredType
    callableSignature
    visibility
    isFixed
    documentation
}
```

Supported kinds include:

```text
module, variable, parameter, function, method, class, interface,
field, typeAlias, loopVariable, patternBinding, builtin
```

The declaration span covers the declaration; the selection span covers its
name. Documentation remains plain source text in the compiler model. The LSP
adapter decides how to render Markdown.

### Occurrence record

Every successfully bound identifier, type name, imported name, and member name
records:

```text
Occurrence {
    span
    symbol
    role
}
```

Roles include:

```text
declaration, read, write, call, typeReference, import, override
```

One occurrence may have more than one role, such as `read` and `call`.

### Scope record

Every lexical scope records its source extent, parent scope, declarations, and
the ordering rules that control visibility. Scope lookup must correctly handle:

- nested blocks
- parameters and locals
- shadowing
- forward-visible top-level declarations
- class fields and methods
- `ici` and `parent`
- loop variables
- pattern bindings
- import aliases and selected imports
- public/private visibility

Tooling may not approximate these rules by selecting the nearest declaration
with the same spelling.

### Member resolution

Member lookup uses the same class, inheritance, interface, builtin, and native
module rules as semantic checking. Its result records the resolved member
symbol and declaring type. Hover, definition, completion, references, and
rename consume that result.

### AST lifetime

Compiler internals may temporarily map AST addresses to semantic facts, but no
protocol result and no cache entry surviving its syntax tree may contain a raw
AST pointer. Cross-query state uses `SourceId`, spans, and semantic IDs.

## Language-service transport

### Framing

The service uses JSON-RPC 2.0 messages over stdin/stdout with `Content-Length`
framing, identical in framing principle to LSP. UTF-8 is mandatory.

Stdout is protocol-only. Logs, assertions, and diagnostics about the service
itself go to stderr. A single non-protocol byte on stdout is a defect.

Each inbound message is limited to 32 MiB by default. The process rejects an
oversized message before allocating its declared body. JSON nesting and string
lengths are validated. All serialization uses one shared, fully tested JSON
implementation; feature-local escaping functions are forbidden.

### Initialization

The first request must be `initialize`:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "protocolVersion": 1,
    "clientVersion": "0.4.0",
    "workspaceRoots": ["/workspace"],
    "capabilities": {}
  }
}
```

The result is:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": 1,
    "serverVersion": "0.2.0",
    "languageVersion": "0.2.0",
    "capabilities": {
      "diagnostics": true,
      "hover": true,
      "definition": true,
      "references": true,
      "completion": true,
      "signatureHelp": true,
      "documentSymbols": true,
      "workspaceSymbols": true,
      "semanticTokens": true,
      "rename": true,
      "codeActions": true,
      "formatting": false
    }
  }
}
```

An unsupported protocol version returns `ProtocolMismatch` and the service
accepts only `shutdown` afterward. Capabilities are negotiated; the LSP adapter
must not advertise an LSP feature the service reports as unavailable.

### Document synchronization

```text
workspace/addRoots          request
workspace/removeRoots       request
workspace/filesChanged      request
textDocument/open           request
textDocument/change         request
textDocument/close          request
```

Mutations are requests rather than notifications so the adapter knows when the
new state is installed. Every mutation response returns the URIs whose semantic
state was invalidated. Document mutations additionally return the accepted
`SourceId`, document version, and content version.

Before acknowledging an open or change, the service performs enough current
lexing and recovered parsing to update that source's direct-import edges. This
makes the returned invalidation set accurate even when an edit adds or removes
an import.

`workspace/filesChanged` carries canonical created, changed, and deleted file
paths. It never carries open-document contents: overlays remain authoritative.

`open` and `change` carry the complete UTF-8 source text in v2. A change is
accepted only when its version is greater than the current open version.

### Query envelope

Every document query carries:

```json
{
  "uri": "file:///workspace/main.lum",
  "version": 17,
  "byteOffset": 142
}
```

Range queries carry `start` and `end` byte offsets. Workspace queries carry no
document version but operate on a snapshot selected after all preceding
mutations on the serial protocol stream.

Responses include the source version or workspace revision they observed. The
adapter discards a response if the editor has advanced past it.

### Cancellation

The adapter cancels work using JSON-RPC `$/cancelRequest`. The service checks
cancellation at least between lexing, parsing, module loading, semantic passes,
index construction, and result serialization.

The protocol reader remains responsive while the serialized analysis worker is
busy and records cancellation in thread-safe request state. A single blocking
read/analyze loop that cannot receive cancellation until analysis finishes does
not satisfy this contract.

Cancellation returns the JSON-RPC cancellation error and publishes no partial
result. It does not roll back an already accepted document change.

### Shutdown

`shutdown` stops accepting new analysis requests, completes or cancels active
work, and responds. `exit` then terminates the process with status zero. EOF or
parent-process death also triggers bounded cleanup without waiting for editor
input.

### Service errors

Structured service error data includes a stable code:

```text
ProtocolMismatch
NotInitialized
InvalidRequest
InvalidDocumentVersion
StaleDocument
SourceNotFound
SourceVersionUnavailable
RequestCancelled
FeatureUnavailable
InternalError
```

Language errors are never service errors. Invalid Lumiere source produces
ordinary diagnostics and partial or empty query results.

`InvalidDocumentVersion` applies to a non-monotonic open/change mutation.
`StaleDocument` applies to a query naming a version that is no longer current.

An `InternalError` includes a safe summary and correlation identifier. Native
stack traces and local source contents remain in stderr logs, not protocol data.

## Compiler query contracts

### Diagnostics

```text
analysis/diagnostics(uri, version)
```

Returns diagnostics whose primary span belongs to the requested source. Its
analysis may update cached diagnostics for dependencies and importers; the
mutation response's invalidated URI list tells the adapter which open documents
to request again. Related locations may point into other sources. Each
diagnostic includes:

```text
code, severity, message, primarySpan, related[], tags[], fixes[]
```

Diagnostic codes are stable. Messages may improve. Related locations name the
other declaration, import, or constraint involved.

A fix contains a title, applicability (`always` or `maybe`), and versioned text
edits. Edits are non-overlapping and sorted by source then descending byte
offset. The compiler emits a fix only when applying it cannot silently change
unrelated semantics.

### Hover

```text
analysis/hover(uri, version, byteOffset)
```

Returns either `null` or:

```text
selectionSpan, symbolKind, name, signature, type, documentation,
declarationSpan, containingSymbol
```

The compiler returns structured plain text, not Markdown. Hover uses resolved
symbols and member lookup. Keyword hover may use current tokens without a
semantic model.

### Definition

```text
analysis/definition(uri, version, byteOffset)
```

Returns zero or more declaration locations. Most symbols have one location;
interface implementations and ambiguous recoverable states may have several.
Builtin declarations point to a stable virtual `lumiere-stdlib:` URI whose text
the service can return through `source/content`.

### Source content

```text
source/content(source, contentVersion)
```

Returns the exact immutable text, URI, content version, and line-start byte
index for a source in the current snapshot. The adapter uses it to translate
locations in closed imported files and read-only virtual standard-library
documents. Open editor overlays are already available locally and must have the
same content version.

The adapter caches source content by `SourceId` and content version. The service
does not return source text unsolicited in every location result. Recent
snapshot contents remain available through a bounded cache. If the requested
version has been evicted, the service returns `SourceVersionUnavailable`; the
adapter discards the original location result and repeats its semantic query on
a current snapshot.

### References

```text
analysis/references(uri, version, byteOffset, includeDeclaration)
```

References are resolved by `SymbolId`, never textual matching. Results include
open overlays and discovered disk modules in the symbol's workspace. Results
are deterministically sorted by URI and byte offset and report whether workspace
discovery was complete.

### Completion

```text
analysis/completion(uri, version, byteOffset, trigger)
```

Completion is scope- and type-aware. It returns visible locals, parameters,
fields, methods, types, module members, keywords, and named parameters as
appropriate to the syntactic context.

Each item includes:

```text
label, kind, detail, documentation, replacementSpan, insertText,
filterText, sortGroup, deprecated
```

The compiler chooses the replacement span and semantic candidates. The adapter
maps them to LSP completion items and snippets only when the client supports
them. Results must remain useful without snippets.

The initial implementation may return the complete candidate set for a scope.
Prefix filtering can remain in the editor until profiling shows a protocol-size
problem.

### Signature help

```text
analysis/signatureHelp(uri, version, byteOffset)
```

Returns the resolved callable signatures, active signature, active parameter,
parameter documentation, and the call span. Named arguments select their
declared parameter independent of source order.

### Document and workspace symbols

```text
analysis/documentSymbols(uri, version)
analysis/workspaceSymbols(query, limit)
```

Document symbols preserve declaration nesting. Workspace symbols search public
and private declarations in the discovered workspace index and report whether
discovery was complete.

Workspace results have a deterministic relevance order and a hard default
limit of 200.

### Semantic tokens

```text
analysis/semanticTokens(uri, version)
```

The compiler returns absolute byte spans and stable token classifications. The
adapter converts them to the relative integer encoding required by LSP.

Initial token kinds are:

```text
namespace, type, class, interface, typeParameter, parameter, variable,
property, function, method, keyword, string, number, operator
```

Initial modifiers are:

```text
declaration, definition, readonly, static, deprecated, defaultLibrary
```

Tokens are sorted, non-overlapping, and never split a UTF-8 scalar. Lexical
tokens remain available in syntactically broken regions; semantic
classifications replace lexical classifications where binding succeeded.

### Rename

Rename is a two-step operation:

```text
analysis/prepareRename(uri, version, byteOffset)
analysis/rename(uri, version, byteOffset, newName)
```

Preparation rejects keywords, builtins, synthetic values, inaccessible
external declarations, and occurrences without a unique symbol.

Rename validates identifier spelling and checks every affected scope for
capture, collision, changed overload selection, visibility changes, and named
argument breakage. It is all-or-nothing. On success it returns versioned edits
for declarations, references, imports, aliases, and named arguments that refer
to the same symbol.

Each edit names the expected `ContentVersion`. Before submitting the workspace
edit, the adapter verifies versions of open documents and content identities of
closed files. Any mismatch discards the entire rename and asks the user to
retry; it never applies a partial set.

The adapter must not perform textual fallback rename.

### Code actions

```text
analysis/codeActions(uri, version, start, end, diagnosticCodes)
```

Initially this exposes compiler-provided diagnostic fixes. Refactorings are
added only when their semantic preconditions and edits are computed by the
compiler.

### Formatting

The v2 protocol reserves document and range formatting methods, but the service
advertises `formatting: false` until Lumiere has a lossless syntax
representation retaining comments and trivia.

Formatting by serializing the executable AST is forbidden because it would
discard comments and may alter incomplete programs.

## LSP adapter requirements

The TypeScript server remains deliberately thin.

### Process supervision

It must:

- spawn without a shell
- complete the version/capability handshake before advertising dynamic
  capabilities
- keep one service process per LSP connection
- capture bounded stderr logs
- detect exit, malformed frames, timeout, and protocol mismatch
- fail outstanding requests exactly once
- restart after unexpected failure with bounded exponential backoff
- reopen current editor overlays after restart
- register `.lum` file watching and forward disk changes
- stop restarting after three failures in 60 seconds and show one actionable
  user-facing error
- terminate the child during LSP shutdown

An unavailable service clears stale diagnostics and reports the failure. It
must not leave old diagnostics displayed as if they describe current source.

### Executable discovery

Resolution order is:

1. explicit `lumiere.executablePath`
2. a compiler bundled with the official platform-specific extension package
3. `lumiere` on `PATH`

Every candidate must complete the protocol handshake. Finding an executable is
not sufficient if its language-service protocol is incompatible.

Official extension releases bundle the compiler version they were tested
against for supported platforms. The explicit setting remains available for
compiler development and nonstandard installations. Generic packages that
cannot bundle a platform binary fail with installation guidance rather than
silently downloading executable code.

### Document synchronization

The adapter sends the exact editor buffer and monotonically increasing version.
It tracks accepted service versions separately from editor versions. Queries
wait for the corresponding change acknowledgement or are cancelled.

On service restart, roots are restored first, then all open documents are
reopened in deterministic URI order before queries resume.

### Position conversion

All LSP UTF-16 positions are converted against the exact document version sent
to the service. Results referring to another source are converted using that
source's matching overlay or a version-matched `source/content` response.

Invalid byte offsets, offsets inside UTF-8 scalars, and ranges beyond EOF are
protocol errors from the service. The adapter does not silently clamp compiler
results. User positions received from an editor may be clamped to the current
buffer before conversion.

### LSP feature mapping

The adapter may format hover Markdown, encode semantic-token deltas, construct
snippets, and map symbol/token kinds. It may not add semantic candidates,
perform textual rename, infer a declaration, or suppress a compiler diagnostic
because it disagrees with its meaning.

### Security

Workspace source is untrusted. The adapter and service:

- never execute source during analysis
- never construct shell command strings from paths or source text
- never enable command links or raw HTML in source documentation
- bound protocol messages, captured logs, query result counts, and restart loops
- restrict virtual-document schemes to compiler-provided read-only content
- do not load workspace-native plugins into the language-service process

## TextMate grammar and compiler metadata

TextMate remains a lexical fallback. It is not expected to understand scopes or
types, but it must not advertise nonexistent language syntax.

The compiler adds:

```text
lumiere metadata --format=json
```

The versioned metadata contains canonical keywords, word operators, primitive
types, literal names, builtin modules, and file extensions. A tooling script
generates or validates the corresponding TextMate alternatives. Structural
patterns for declarations remain hand-authored and tested with fixtures.

CI fails when compiler metadata and grammar vocabulary drift. Compatibility
aliases are included only when the compiler actually accepts them.

## Compatibility commands

`lumiere check` remains a stable CLI and reuses the same workspace analysis
engine in one-shot mode. Human and JSON diagnostic renderers consume the same
diagnostic data as the service.

`lumiere inspect` remains during migration and becomes a one-shot adapter over
`analysis/hover`. It is deprecated only after a released extension has used the
persistent service for at least one release cycle.

The old diagnostic and inspection JSON formats remain versioned independently;
they are not silently changed to the language-service protocol.

## Reliability and performance

### Correctness requirements

- One document version produces one immutable answer snapshot.
- No response may combine versions of the same source.
- Unsaved overlays always beat disk contents.
- Stale requests never publish diagnostics or edits.
- Definition, references, and rename operate on symbol identity.
- Both interpreters and tooling consume the same semantic rules.
- Service failure cannot crash the editor extension host.
- Restart cannot lose or reorder open-document state.
- Diagnostics and edits use validated UTF-8 boundaries.

### Performance policy

Performance is measured with checked-in small, medium, and large workspace
fixtures. Benchmarks record:

- service startup and initialization
- first diagnostics after opening a workspace
- warm diagnostics after a one-line edit
- hover, definition, completion, references, and rename latency
- bytes and processes per request
- peak resident memory
- invalidated and reanalyzed files per edit

The required architectural property is one persistent service process, not a
specific microbenchmark number. Release CI records baselines and rejects an
unexplained regression above 20% in a stable benchmark environment.

Interactive targets on the reference development machine are:

- warm hover and definition: 50 ms p95
- warm completion: 100 ms p95
- diagnostics for a typical edited file and its affected importers: 200 ms p95
- cancellation acknowledgement: 50 ms p95 between defined cancellation points

These are engineering targets, not reasons to return incomplete or stale data.

## Testing strategy

### Compiler unit tests

Tests cover:

- source identity and canonicalization
- overlay precedence and close-to-disk transition
- document version rejection
- module and reverse-dependency graph updates
- cache invalidation
- recovered syntax and partial binding
- scopes, shadowing, and forward declarations
- occurrence-to-symbol binding for every expression and type form
- member lookup through inheritance and interfaces
- imported aliases and selected imports
- deterministic references and symbols
- rename collision and capture rejection
- completion contexts and named arguments
- semantic token ordering and non-overlap
- diagnostic related locations and edit validation
- virtual stdlib documents
- Unicode byte spans

### Protocol tests

Transcript tests send framed messages to a real service process and verify:

- initialization and capability negotiation
- malformed headers, JSON, oversized bodies, and unsupported versions
- ordered mutations and snapshot revisions
- cancellation
- shutdown and EOF behavior
- stdout purity
- JSON escaping for every control character
- recovery after language errors without service errors

Protocol fuzzing mutates framing, JSON values, offsets, versions, and request
order. A malformed client message may fail a request or close the protocol, but
must not trigger undefined behavior.

### Cross-file conformance tests

Fixtures open several modules, modify an imported module without saving, and
verify diagnostics, hover, definition, completion, references, semantic tokens,
and rename from importers. The same fixture is rerun after closing the overlay
to prove that disk contents become authoritative again.

### LSP adapter tests

Tests use a fake framed service and a real service to verify:

- UTF-8/UTF-16 conversion, including accents and supplementary characters
- stale response rejection
- capability mapping
- restart, backoff, and overlay replay
- missing and incompatible executable messages
- diagnostic clearing on service failure
- cancellation propagation
- bounded logs and results
- no shell invocation

### Extension tests

Tests verify language registration, configuration, grammar fixtures, packaging,
bundled executable selection, activation, and a real VS Code smoke workflow for
diagnostics, hover, definition, completion, and rename.

### Sanitizers and leak checks

The compiler suite repeatedly opens, changes, closes, invalidates, and evicts
documents under AddressSanitizer and UndefinedBehaviorSanitizer. Shutdown tests
prove that syntax trees, semantic types, source buffers, and protocol requests
are released.

## Observability

The language service supports an opt-in trace level sent during initialization:

```text
off, messages, verbose
```

Trace output goes to stderr and never contains full source text by default.
Verbose mode may include paths, request IDs, cache hits, invalidation counts,
durations, and memory totals. It still redacts source contents and environment
variables.

The LSP adapter exposes one output channel containing its own lifecycle logs and
the bounded service stderr stream. No telemetry leaves the machine.

## Implementation plan

### Stage 0: repair current correctness gaps

- replace inspection's handwritten JSON escaping with the shared serializer
- pass source path and import context through inspection
- reuse `AnalysisResult.model` rather than running an unscoped second semantic
  analysis
- synchronize the TextMate grammar with actual tokens and primitive types
- add regression tests for those fixes

These repairs ship independently and are not thrown away by v2.

### Stage 1: semantic tooling index

- add `SourceId`, `SourceSpan`, and snapshot-local `SymbolId`
- record scopes, symbols, occurrences, documentation, and declaration spans
- replace heuristic hover resolution with symbol and member queries
- implement compiler-unit tests for binding and recovery

### Stage 2: workspace engine

- add roots, source store, overlays, versions, module graph, reverse graph, and
  caches
- make import loading read through the source store
- support conservative transitive invalidation
- refactor `lumiere check` to use the workspace engine in one-shot mode

### Stage 3: persistent service

- implement framed JSON-RPC, initialization, document synchronization,
  diagnostics, hover, cancellation, shutdown, and protocol tests
- keep current subprocess commands operational

### Stage 4: tooling migration

- add service supervision and handshake to the TypeScript server
- replay documents after restart
- move diagnostics and hover to the persistent service
- retain the one-shot adapter behind a development-only fallback during one
  release cycle

### Stage 5: core navigation and intelligence

- definition
- document and workspace symbols
- signature help
- completion
- semantic tokens
- references
- prepare-rename and rename
- compiler-provided code actions

Each feature ships only after compiler, protocol, LSP, Unicode, and cross-file
tests pass.

### Stage 6: distribution hardening

- produce official platform-specific packages with a matching compiler
- implement executable selection and compatibility errors
- complete VS Code smoke tests and release compatibility tests
- remove the development fallback after the migration window
- deprecate one-shot `inspect` on the published schedule

### Stage 7: formatter prerequisite

- design a lossless syntax representation with comments and trivia
- specify formatting stability and idempotence separately
- advertise formatting only after that specification and implementation pass
  round-trip tests

## Rejected alternatives

### Rewrite the LSP server in C++

Rejected. It duplicates mature protocol plumbing, complicates packaging, and
does not fix the missing workspace or semantic index.

### Load the compiler directly into Node.js

Rejected. A native ABI binding couples releases, risks crashing the extension
host, and makes sanitization and process recovery harder. A persistent process
removes startup overhead while preserving isolation.

### Continue spawning one process per query

Rejected for v2. It cannot correctly model multiple unsaved modules and makes
workspace features repeatedly rebuild the same state.

### Implement semantic features in TypeScript

Rejected. It creates two definitions of Lumiere and guarantees drift.

### Build an incremental parser first

Rejected until profiling demonstrates that full-file parsing inside a
persistent cached workspace misses latency targets. Module reuse and semantic
invalidation are the first-order improvements.

### Use stale last-known-good semantics while typing

Rejected. It can navigate or rename a symbol that no longer exists in the text
the user sees. Current partial facts are preferable to confident stale answers.

### Implement formatting from the executable AST

Rejected. The AST does not retain enough concrete syntax to preserve comments
and malformed input.

## Definition of done

Tooling v2 is complete when:

- the released extension uses one persistent language-service process
- protocol compatibility is negotiated before features are advertised
- all open buffers participate in cross-file analysis before they are saved
- diagnostics and hover no longer spawn per-request compiler processes
- hover, definition, references, completion, signature help, symbols, semantic
  tokens, rename, and compiler fixes are compiler-authoritative
- occurrence resolution uses symbols rather than textual declaration search
- invalid code receives current partial analysis without stale semantic answers
- cancellation and stale versions cannot publish results
- the service automatically recovers from a single crash and faithfully
  restores open documents
- grammar vocabulary is validated against compiler metadata
- official supported-platform packages include a tested compatible compiler
- compiler, protocol, LSP, extension, cross-file, Unicode, fuzz, sanitizer, and
  packaging tests pass
- measured warm interactions meet the stated targets or carry a documented,
  accepted exception
- `lumiere check` still behaves as a reliable standalone command
- no language rule is duplicated in the tooling repository

Formatting is explicitly outside this completion gate until the lossless syntax
prerequisite is complete.
