# Foundational Standard Library Design

## Status

This document defines the target v1 design for the standard-library pieces
Lumiere still needs before it can support ordinary command-line, data-processing,
and networked programs.

It covers:

- operating-system and process integration
- byte values, encodings, and binary files
- JSON
- collection algorithms
- HTTPS and TLS
- regular expressions
- complete Unicode text behavior
- clocks, calendars, and timezones

These are target APIs, not claims about the current implementation. Each module
should be implemented and documented independently, but the contracts below are
shared so the modules compose cleanly.

## Common rules

All of these libraries follow the same rules.

### Failures

- An expected operational failure returns `Résultat[T, E]`.
- A caller contract violation is a runtime error. Examples include the wrong
  argument type, a byte outside `0..255`, or a callback returning the wrong type.
- Error values record an operation and a human-readable cause. They also record
  the relevant path, variable, pattern, URL, or executable when one exists.
- Host exception text is diagnostic detail, not a stable value programs should
  branch on.

### Determinism

- Dictionary insertion order remains observable.
- Functions that enumerate host data return a deterministic order unless their
  contract explicitly says otherwise.
- Text encoding is explicit. APIs never silently use the host locale.
- The tree-walker and VM call the same native implementation and produce the
  same values and errors.

### Naming

- Each operation has one canonical exported name.
- Protocol and format names such as `JSON`, `HTTP`, `TLS`, `UTF-8`, and `Base64`
  keep their conventional spelling.
- Names describe the payload: `lire_octets`, not a vague binary overload of
  `lire_texte`.
- An API does not acquire accent-free aliases one function at a time. Any
  library-wide identifier policy change must be separate.

### Resource limits

Parsers and decoders must reject impossible sizes and nesting before integer
overflow or unbounded recursion. The implementation may impose documented
limits, but it must not silently truncate valid output.

## 1. `Système`, `Processus`, and console output

### Purpose

`Système` exposes facts about the current process. `Processus` executes another
program. Console output remains part of the core environment because every
program already receives core functions without an import.

Keeping these responsibilities separate prevents `Système` from becoming an
unstructured collection of platform calls.

### Core console additions

```lumiere
écrire(valeur: Universel) -> Rien
écrire_erreur(valeur: Universel) -> Rien
afficher_erreur(valeur: Universel) -> Rien
```

- `écrire` writes to standard output without appending a newline.
- `écrire_erreur` writes to standard error without appending a newline.
- `afficher_erreur` writes to standard error and appends one newline.
- All three use the same canonical value-to-text conversion as `afficher`.
- Output failures become runtime I/O errors. They are not ignored.

### `Système` surface

```lumiere
importer Système

Système.arguments() -> Résultat[Liste[Texte], ErreurSystème]
Système.variable(nom: Texte) -> Résultat[Texte | Rien, ErreurSystème]
Système.variables() -> Résultat[Dictionnaire[Texte, Texte], ErreurSystème]
Système.terminer(code: Entier) -> Rien
```

Semantics:

- The CLI form is `lumiere [options] programme.lum -- [arguments...]`. The
  delimiter is required when program arguments are present, so Lumiere options
  and program arguments can never be confused.
- `arguments` excludes the Lumiere executable and source-file path. Its first
  item is the first argument after the program path.
- Arguments are captured once at program startup and are identical in both
  execution backends. A host argument that is not valid Unicode returns
  `Échec(ErreurSystème)` rather than being replaced or exposed as malformed
  `Texte`.
- `variable` returns `rien` when the variable is absent. Absence is not an
  operational error. A host value that cannot be decoded as Unicode is an
  operational error.
- `variables` returns a snapshot ordered lexically by variable name so tests do
  not depend on host enumeration order.
- `terminer` accepts `0..255`. It flushes Lumiere-managed output and terminates
  without running further Lumiere statements.
- The test runner must execute tests of `terminer` in a child process.

The working directory stays in `Chemin.dossier_courant`; duplicating it in
`Système` would add two names for one operation.

### `Processus` surface

```lumiere
importer Processus

Processus.exécuter(programme: Texte, arguments: Liste[Texte])
    -> Résultat[RésultatProcessus, ErreurProcessus]
```

`RésultatProcessus` exposes:

```lumiere
résultat.code() -> Entier
résultat.sortie() -> Texte
résultat.erreur() -> Texte
résultat.réussi() -> Logique
```

Semantics:

- `programme` and each argument are passed directly to the host process API.
  They are never joined into a shell command.
- Standard output and standard error are captured separately as UTF-8.
- Invalid UTF-8 output produces `Échec(ErreurProcessus)`; binary subprocess I/O
  belongs in a later streaming API using `Octets`.
- A program that starts and exits nonzero still returns
  `Succès(RésultatProcessus)`. Failure to locate, start, wait for, or decode the
  program returns `Échec(ErreurProcessus)`.
- `réussi` is exactly `code() == 0`.

Shell interpretation, interactive child processes, pipelines, detached
processes, and streaming handles are deliberately outside v1.

## 2. `Octets`, `Encodage`, and binary files

### Purpose

Binary data needs its own compact value. `Liste[Entier]` does not encode the
`0..255` invariant and is too expensive for files and network payloads.

### Runtime value

`Octets` is an immutable, insertion-ordered sequence of bytes.

Immutability gives it content equality, a stable hash, and safe sharing across
native calls. Implementations may share backing storage between slices.

```lumiere
importer Octets

Octets.vide() -> Octets
Octets.depuis_liste(valeurs: Liste[Entier]) -> Octets
Octets.concaténer(valeurs: Liste[Octets]) -> Octets
```

Methods:

```lumiere
octets.taille() -> Entier
octets.est_vide() -> Logique
octets.sous_octets(début: Entier, longueur: Entier) -> Octets
octets.en_liste() -> Liste[Entier]
```

Semantics:

- `depuis_liste` validates every item and rejects values outside `0..255`.
- `octets[position]` returns an `Entier` in `0..255`; iteration yields the same
  values. Indexing and slicing use zero-based byte positions.
- `sous_octets` takes the same `(début, longueur)` shape as `Texte.sous_texte`,
  the existing slicing convention, rather than introducing a second name for
  the same operation. It accepts the position immediately after the final
  byte only when `longueur` is zero. Other out-of-range inputs are contract
  errors.
- The usual equality operator compares content.
- `Octets` may be used as a dictionary key.
- There is no byte literal syntax in v1.

### `Encodage` surface

```lumiere
importer Encodage

Encodage.encoder_utf8(texte: Texte) -> Octets
Encodage.decoder_utf8(octets: Octets) -> Résultat[Texte, ErreurEncodage]
Encodage.encoder_hexadécimal(octets: Octets) -> Texte
Encodage.decoder_hexadécimal(texte: Texte) -> Résultat[Octets, ErreurEncodage]
Encodage.encoder_base64(octets: Octets) -> Texte
Encodage.decoder_base64(texte: Texte) -> Résultat[Octets, ErreurEncodage]
```

- UTF-8 decoding is strict: malformed, overlong, surrogate, and truncated
  sequences fail.
- Hexadecimal encoding uses lowercase ASCII. Decoding accepts upper- and
  lowercase ASCII but no whitespace or prefix.
- Base64 uses the RFC 4648 standard alphabet with padding. Whitespace and
  noncanonical padding are rejected.
- URL-safe Base64 is a later, separately named operation.

### `Fichier` additions

```lumiere
Fichier.lire_octets(chemin: Texte) -> Résultat[Octets, ErreurFichier]
Fichier.ecrire_octets(chemin: Texte, contenu: Octets)
    -> Résultat[Rien, ErreurFichier]
Fichier.ajouter_octets(chemin: Texte, contenu: Octets)
    -> Résultat[Rien, ErreurFichier]
```

These operations perform no text conversion. Existing text operations remain
strict UTF-8 wrappers around byte I/O.

All LumiNet byte fields and methods should migrate atomically from
`Liste[Entier]` to `Octets`. Keeping both payload types indefinitely would make
every networking API ambiguous.

## 3. `JSON`

### Purpose

`JSON` converts between JSON text and the ordinary Lumiere value graph.

### Surface

```lumiere
importer JSON

JSON.analyser(texte: Texte) -> Résultat[Universel, ErreurJSON]
JSON.encoder(valeur: Universel) -> Résultat[Texte, ErreurJSON]
JSON.encoder_indenté(valeur: Universel, espaces: Entier)
    -> Résultat[Texte, ErreurJSON]
```

`ErreurJSON` records:

- `opération`: `analyser` or `encoder`
- `cause`
- `ligne` and `colonne` for parsing errors
- a value path such as `$.utilisateur.adresses[2]` for encoding errors

### Value mapping

| JSON | Lumiere |
|---|---|
| `null` | `rien` |
| boolean | `Logique` |
| integral number in range | `Entier` |
| other finite number | `Décimal` |
| string | `Texte` |
| array | `Liste[Universel]` |
| object | `Dictionnaire[Texte, Universel]` |

Rules:

- Parsing rejects duplicate object keys. Silently choosing one value hides bad
  input and differs between parsers.
- An integral token outside the `Entier` range fails instead of silently losing
  precision in `Décimal`.
- Encoding preserves dictionary insertion order.
- `infini`, `-infini`, and `non_nombre` are not JSON numbers and fail encoding.
- Only `rien`, booleans, numbers, text, lists, fixed lists, and dictionaries
  with text keys are encodable in v1.
- Sets, functions, results, modules, classes, and objects fail with a path to
  the unsupported value.
- Cyclic lists or dictionaries fail rather than recursing forever.
- `encoder_indenté` accepts `0..8`; zero still emits line breaks but no leading
  indentation. `encoder` emits the compact form.
- Both encoders terminate their output without an extra newline.

LumiNet should continue accepting an already encoded body. A later convenience
method may call `JSON.encoder`, but networking must not acquire a second JSON
implementation.

## 4. `Collections`

### Purpose

Container methods remain small and structural. Cross-cutting algorithms live in
`Collections`, implemented in Lumiere where possible.

### Surface

```lumiere
importer Collections

Collections.étendue(début: Entier, fin: Entier, pas: Entier) -> Liste[Entier]
Collections.transformer(valeurs: Universel, transformation: Universel) -> Liste
Collections.filtrer(valeurs: Universel, prédicat: Universel) -> Liste
Collections.réduire(valeurs: Universel, initial: Universel, réduction: Universel)
    -> Universel
Collections.trouver(valeurs: Universel, prédicat: Universel) -> Universel | Rien
Collections.position(valeurs: Universel, prédicat: Universel) -> Entier | Rien
Collections.tout(valeurs: Universel, prédicat: Universel) -> Logique
Collections.au_moins_un(valeurs: Universel, prédicat: Universel) -> Logique
Collections.trier(valeurs: Universel) -> Liste
Collections.trier_par(valeurs: Universel, clé: Universel) -> Liste
Collections.inverser(valeurs: Universel) -> Liste
```

Semantics:

- Algorithms accept every existing iterable: `Liste`, `ListeFixe`, `Ensemble`,
  `Dictionnaire`, and `Texte`. Dictionary traversal yields keys, matching the
  existing iteration contract.
- All returned lists are new values. Algorithms do not mutate the input.
- `étendue` excludes `fin`. `pas` cannot be zero, and its sign must move toward
  `fin`. An already exhausted interval returns an empty list.
- `transformer` calls its function once per value in traversal order.
- Predicates must return `Logique`; implicit truth conversion is not used.
- `réduire` passes `(accumulateur, valeur)` and returns `initial` for empty
  input.
- `trouver` and `position` stop at the first match.
- `tout` returns true for empty input; `au_moins_un` returns false.
- `trier` accepts homogeneous `Entier`/`Décimal`, `Texte`, or `Symbole` values.
  Mixed integers and decimals compare numerically. Other mixtures are contract
  errors.
- Sorting is stable. Text and symbols sort by Unicode scalar value, independent
  of the host locale.
- `trier_par` evaluates the key function exactly once per element and applies
  the same key rules as `trier`.

### Type-safety compromise

`Collections` has no way to express "the same element type in and out" or
"a function from T to U" -- Lumiere has generic collection types (`Liste[T]`)
but not generic *functions* or a function value type yet. That is why every
container and callback parameter above is `Universel` rather than something
like `transformer[T, U](valeurs: Liste[T], transformation: Fonction[T, U])
-> Liste[U]`.

The practical consequence: nothing about a `Collections` call is checked until
it runs. `Collections.transformer(mes_entiers, fonction(x) { retourne
x.majuscules() })` compiles without complaint and only fails, per the
"Common rules" above, as a runtime contract error the first time the
callback actually receives an `Entier`. Every other typed API in this
document rejects that kind of mistake at the call site; `Collections` cannot,
by construction, until generic functions exist.

This is a real cost, not a footnote, and it is worth an explicit choice
rather than shipping it by default sequencing: either accept untyped
`Collections` now because the alternative is no `Collections` at all, or move
it later in the dependency order so it ships once generic function types
exist and it can be given a real signature. The "Dependency and delivery
order" section below currently does the former; revisit that if generic
function types turn out to be closer than the rest of this sequence.

## 5. HTTPS and TLS

### Purpose

HTTPS becomes a transparent secure transport for the existing LumiNet HTTP
client. TLS is not exposed as a bag of cryptographic knobs.

### HTTP behavior

The existing `LumiNet.HTTP` request functions accept both `http://` and
`https://` URLs.

For HTTPS:

- certificates are validated against the host trust store
- the requested hostname is verified
- expired and not-yet-valid certificates are rejected
- SNI uses the URL hostname
- TLS 1.2 is the minimum accepted protocol
- TLS 1.3 is preferred when available
- certificate and protocol failures return `Échec(ErreurTLS)` through the
  existing result channel

`ErreurTLS` realizes the existing HTTP error contract, so HTTPS does not force
every `LumiNet.HTTP` return type to become a new error union. It additionally
records the host and the failed TLS phase; it does not expose platform-specific
certificate objects.

There is deliberately no `ignorer_certificat`, `non_sécurisé`, or equivalent
switch in v1. Such a switch inevitably escapes tests and reaches production.

### Platform seam

The public semantics are shared, while a narrow platform adapter supplies:

- TLS connection setup
- host trust-store access
- certificate and hostname verification
- encrypted reads and writes

LumiNet owns URL, HTTP, timeout, and response semantics. The TLS adapter must
not become a second HTTP implementation.

Client HTTPS ships before server certificates. Custom trust roots, mutual TLS,
certificate inspection, ALPN controls, and HTTP/2 are deferred.

## 6. `Regex`

### Purpose

`Regex` provides reusable, Unicode-aware patterns with deterministic semantics.

### Surface

```lumiere
importer Regex

Regex.analyser(source: Texte) -> Résultat[Motif, ErreurRegex]
Regex.correspond(motif: Motif, texte: Texte) -> Logique
Regex.chercher(motif: Motif, texte: Texte) -> Correspondance | Rien
Regex.trouver_tous(motif: Motif, texte: Texte) -> Liste[Correspondance]
Regex.remplacer(motif: Motif, texte: Texte, remplacement: Texte) -> Texte
Regex.remplacer_tout(motif: Motif, texte: Texte, remplacement: Texte) -> Texte
```

`Correspondance` exposes:

```lumiere
correspondance.texte() -> Texte
correspondance.début() -> Entier
correspondance.fin() -> Entier
correspondance.groupe(index: Entier) -> Texte | Rien
correspondance.groupes() -> Liste[Texte | Rien]
```

Semantics:

- `analyser` is named to match the existing "parse text into a structured
  value, with a Résultat outcome" convention already used by `JSON.analyser`
  and `Temps.analyser_iso8601`, rather than `compiler`, which reads as a
  compile-time operation this is not and would crowd out that word if
  Lumiere ever gets real compile-time reflection or macros.
- `correspond` requires the entire text to match. `chercher` finds the first
  leftmost match.
- Positions are Unicode scalar positions, matching the existing `Texte` index
  contract. `fin` is exclusive.
- Patterns operate on valid UTF-8 text, not raw bytes.
- Supported v1 syntax includes literals, `.`, character classes, negated
  classes, grouping, alternation, `?`, `*`, `+`, bounded repetition, anchors,
  escapes, and numbered captures.
- Backreferences, lookbehind, conditionals, embedded code, and recursion are
  rejected. The supported language must admit a linear-time implementation.
- `trouver_tous` advances by one Unicode scalar after an empty match, so it
  always terminates.
- Replacement recognizes `$0` for the whole match and `$1` through `$99` for
  captures. `$$` emits a literal dollar sign. An unavailable capture is an
  empty replacement; an invalid capture number is a contract error.
- Parsed patterns are immutable and safe to reuse.

The exact pattern grammar belongs in a dedicated reference before
implementation. The C++ standard library's `std::regex` behavior must not define
the Lumiere language contract because it varies across implementations and does
not provide the required Unicode semantics.

## 7. Unicode completion

### Purpose

Lumiere text remains UTF-8 and scalar-indexed. Unicode completion fixes case,
whitespace, equivalence, and user-perceived-character operations without
changing ordinary indexing semantics.

### Existing `Texte` behavior to strengthen

- `majuscules` uses Unicode default uppercase mapping.
- `minuscules` uses Unicode default lowercase mapping.
- `elaguer`, `elaguer_gauche`, and `elaguer_droite` recognize Unicode White_Space.
- Results never depend on the host locale.

Some case mappings change length, such as one scalar becoming multiple scalars.
That is correct and must not be truncated to preserve the original length.

### `Unicode` surface

```lumiere
importer Unicode

Unicode.normaliser_nfc(texte: Texte) -> Texte
Unicode.normaliser_nfd(texte: Texte) -> Texte
Unicode.normaliser_nfkc(texte: Texte) -> Texte
Unicode.normaliser_nfkd(texte: Texte) -> Texte
Unicode.plier_casse(texte: Texte) -> Texte
Unicode.grappèmes(texte: Texte) -> Liste[Texte]
```

Semantics:

- Normalization follows the Unicode version bundled with the Lumiere release.
- `plier_casse` is locale-independent default case folding for caseless
  comparison and may change text length.
- `grappèmes` follows Unicode extended grapheme-cluster boundaries.
- Existing `taille`, indexing, slicing, and iteration remain scalar-based.
  Silently changing them to graphemes would break established programs and
  make indexing substantially more expensive.
- Lumiere release notes record Unicode-data version upgrades because they can
  change classifications and boundaries.

Locale-sensitive collation, transliteration, word breaking, and language-
specific casing are deferred.

## 8. Clocks, calendars, and timezones

### Purpose

`Instant` remains an absolute UTC timeline value. Calendar fields are a view of
an instant in a named timezone. Elapsed-time measurement uses a separate
monotonic clock that cannot jump when the wall clock changes.

### Strengthened `Temps` surface

```lumiere
importer Temps

Temps.analyser_iso8601(texte: Texte) -> Résultat[Instant, ErreurTemps]
Temps.formater_iso8601(instant: Instant) -> Texte
Temps.fuseau(nom: Texte) -> Résultat[Fuseau, ErreurTemps]
Temps.fuseau_local() -> Résultat[Fuseau, ErreurTemps]
Temps.dans_fuseau(instant: Instant, fuseau: Fuseau) -> DateHeure
Temps.repère() -> RepèreMonotone
Temps.écoulé(depuis: RepèreMonotone) -> Durée
```

`Fuseau` exposes `nom()`. `DateHeure` exposes:

```lumiere
date.année() -> Entier
date.mois() -> Entier
date.jour() -> Entier
date.heure() -> Entier
date.minute() -> Entier
date.seconde() -> Entier
date.milliseconde() -> Entier
date.décalage_utc_secondes() -> Entier
date.fuseau() -> Fuseau
date.instant() -> Instant
```

Semantics:

- `Instant` is stored as signed Unix milliseconds in UTC.
- Existing `Instant` calendar accessors are defined in UTC. Code that needs
  local or regional fields first creates a `DateHeure` with `dans_fuseau`.
- `analyser_iso8601` requires an explicit `Z` or numeric UTC offset. A local
  timestamp with no zone is rejected as ambiguous.
- `formater_iso8601` emits UTC with `Z`, includes milliseconds, and is
  round-trippable through `analyser_iso8601`.
- `fuseau` accepts canonical IANA timezone names such as
  `America/Edmonton`. Unknown names return `Échec`.
- `fuseau_local` snapshots the host timezone as a named `Fuseau`; failure to
  identify it is explicit.
- `DateHeure` is immutable. Its fields are derived using the timezone rules in
  the bundled timezone-data version.
- `RepèreMonotone` is opaque, cannot be serialized or compared with `Instant`,
  and is meaningful only inside the current process.
- `écoulé` never returns a negative duration.
- Network timeouts and benchmarks use monotonic time, not `maintenant`.

Constructing local calendar times and calendar arithmetic are deferred because
daylight-saving gaps and overlaps require a caller-visible ambiguity policy.
They should not be added until that policy is designed.

## Dependency and delivery order

The implementation order is constrained by value dependencies, not just user
visibility.

1. Add `Système` and the three console functions. They require no new value
   representation and immediately make CLI programs useful.
2. Add the `Octets` runtime value, equality, hashing, and backend support.
3. Add `Encodage`, binary `Fichier`, and migrate LumiNet byte APIs together.
4. Add `JSON`; it then has stable UTF-8 and binary boundaries.
5. Add `Collections`, primarily in Lumiere, after callback behavior is covered
   equally by both engines. This is the step to reconsider first if generic
   function types land sooner than expected: see the type-safety compromise
   noted in section 4.
6. Add client HTTPS using the `Octets` transport boundary.
7. Add `Regex` with a defined portable pattern grammar.
8. Complete Unicode casing and whitespace, then add normalization, folding,
   and grapheme segmentation.
9. Add monotonic time, ISO 8601, and timezone views to `Temps`.

Steps 4, 5, and the design work for 7 through 9 can proceed independently once
`Octets` semantics are fixed. Each shipped step must include tree-walker/VM
conformance tests and platform tests where host behavior is involved.

## Explicit non-goals for this sequence

Do not expand this work into:

- a general stream hierarchy
- asynchronous I/O or an event loop
- shell-language parsing
- arbitrary object-to-JSON reflection
- a cryptography API
- a locale framework
- HTTP/2 or HTTP/3
- calendar recurrence rules

Those may be useful later. None is required to make these foundational APIs
coherent and safe.
