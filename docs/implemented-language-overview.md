# Implemented Language Overview

## Purpose

This note is the practical companion to [`syntax_spec.md`](../syntax_spec.md).

The syntax spec describes the intended Lumiere surface. This document answers a different question:

- what the repository currently parses
- what the tree-walker currently executes
- what is implemented completely enough to rely on in tests and examples
- what is still partial, shallow, or intentionally deferred

If these two documents ever disagree, this file should be treated as the source of truth for the current implementation status.

## Current status

The repository already supports a meaningful end-to-end language slice:

- lexing, parsing, AST printing, and tree-walker execution
- top-level functions and variables
- classes, inheritance, interfaces, and method dispatch
- imports from files and built-in modules
- typed variables, typed functions, and generic collection annotations
- control flow including loops and `agir selon`
- Python-style runtime traceback formatting with source location context

The current implementation is no longer just a parser demo. It can run moderately rich multi-file programmes and enforce a growing amount of type and contract behavior at runtime.

## Syntax implemented today

### Declarations

Supported:

- `soit nom = valeur`
- `soit nom: Type = valeur`
- `soit fixe nom = valeur`
- `soit fixe nom: Type = valeur`
- `fonction nom(...) { ... }`
- `fonction nom(...) -> Type { ... }`
- `classe Nom { ... }`
- `classe Enfant : Parent { ... }`
- `classe Rapport réalise Presentable, Etiquetable { ... }`
- `interface Contrat { fonction faire() }`
- `public` on top-level `fonction`, `classe`, and `interface`
- `privé` on class fields
- `remplace fonction methode() { ... }`

Current restrictions:

- top-level `privé` declarations are rejected
- class-field initializers are not supported yet in the class body syntax
- interface members must be function signatures only

### Types and annotations

Supported type annotations:

- `Entier`
- `Décimal`
- `Logique`
- `Texte`
- `Rien`
- `Universel`
- class names
- interface names
- `Liste[T]`
- `ListeFixe[T, N]`
- `Dictionnaire[K, V]`
- `Ensemble[T]`

Current behavior:

- function parameters require type annotations
- return types are optional but enforced when present
- variable declarations may be annotated or inferred
- `Décimal` accepts `Entier` values where appropriate
- a `Décimal` prints as the shortest text that reads back as the same value and
  the same type, so `0.1 + 0.2` prints `0.30000000000000004` rather than `0.3`
  and `2.0` prints `2.0` rather than `2`; `infini` and `non_nombre` print under
  those names
- a decimal literal whose magnitude the type cannot hold is a lexical error
  rather than a silent zero or infinity; a subnormal such as `5e-324` is fine
- a file being run needs a `principal`; a file being imported as a module, or
  run by `lumiere tester`, does not
- generic collection annotations are parsed and enforced recursively at runtime

### Expressions

Supported:

- literals for integers, decimals, booleans, text, symbols, and `rien`

Current note on `Symbole`:
- `Symbole` is treated as exactly one Unicode character value
- this means literals such as `'é'`, `'ç'`, `'à'`, or `'œ'` are valid symbols
- this does not yet attempt full grapheme-cluster handling, so the rule is "one Unicode character value" rather than "one user-perceived letter in every Unicode edge case"
- arithmetic operators
- comparison and equality operators
- runtime type tests with `est`
- unary operators including logical negation
- assignment
- grouped expressions
- function calls
- named arguments in calls
- member access
- index access
- list literals
- dictionary literals
- casts with `en`
- `ici`
- `parent`

Notable runtime behavior:

- named arguments bind by parameter name in any order
- unknown or duplicate named arguments are rejected
- `et` and `ou` short-circuit
- `x est Type` uses the same runtime type-matching machinery as typed patterns and annotated values
- passing a local variable into a function now evaluates in the caller scope correctly

### Statements and control flow

Supported:

- expression statements
- blocks
- `si` / `sinon`
- `pour chaque x dans collection`
- `tant que (...)`
- `arrêter`
- `continuer`
- `retourne`
- `agir selon`

`agir selon` currently supports:

- literal patterns
- `rien`
- typed binding patterns such as `n: Entier`
- typed `Succès` and `Échec` result patterns
- first-match execution in source order
- branch-local bindings for the winning branch
- expression-valued branches
- result exhaustiveness checking

Still partial:

- no destructuring patterns
- non-result matches remain intentionally open unless they have `sinon`

### Objects and dispatch

Supported:

- class declarations and object construction
- field storage on objects
- method lookup and invocation
- receiver binding through `ici`
- parent-method dispatch through `parent`
- inheritance-aware method lookup
- runtime checking of `remplace`
- interface declarations as runtime symbols
- runtime validation that a class claiming `réalise` provides required methods

Current limits:

- interface validation checks method presence, not deep signature equivalence
- object semantics are still tree-walker driven rather than backed by a separate semantic model

### Modules and imports

Supported:

- file-backed modules ending in `.lum`
- dotted module paths
- import aliases with `comme`
- selective imports with `module.{a, b comme c}`
- module caching
- cycle detection
- public-member export control

Built-in modules currently include:

- `Maths`
- `Fichier`
- `Chemin`
- `Texte`
- `Temps`
- `Aléatoire`

The current `Fichier` surface is intentionally tiny for now:

- `existe`
- `lire_texte`

The rest of the draft file-library API is still design-target material rather than fully implemented runtime surface.

The current `Maths` module includes:

- constants such as `pi`, `e`, `infini`, and `non_nombre`
- core numeric functions such as `absolu`, `arrondir`, `plancher`, `plafond`, `tronquer`, `racine`, `racine_n`, `puissance`, `min`, and `max`
- logarithms: `log`, `log10`, `log2`
- trigonometry: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`
- conversions: `degres_vers_radians`, `radians_vers_degres`
- predicates: `est_non_nombre`, `est_infini`, `est_pair`, `est_impair`

The current `Texte` surface is method-first. String values expose methods such as:

- `taille`, `est_vide`, `contient`, `index_de`, `commence_par`, `finit_par`
- `majuscules`, `minuscules`, `inverser`, `repeter`
- `elaguer`, `elaguer_gauche`, `elaguer_droite`
- `sous_texte`, `separer`, `separer_lignes`
- `remplacer`, `remplacer_tout`, `inserer`, `supprimer`
- `en_entier`, `en_decimal`, `en_logique`

The built-in `Texte` module exists for static helpers such as:

- `joindre`
- `convertir_entier`
- `convertir_decimal`
- `convertir_logique`

The current `Chemin` module is lexical and string-based. It currently includes:

- `dossier_courant`
- `joindre`
- `absolu`
- `nom`
- `nom_sans_extension`
- `extension`
- `dossier`
- `parties`
- `est_absolu`
- `est_relatif`
- `normaliser`

Path existence and filesystem state remain the responsibility of `Fichier`, not `Chemin`.

The current `Temps` module provides runtime-backed `Instant` and `Durée` values. Its surface currently includes:

- instant creation and conversion: `maintenant`, `horodatage`, `depuis_horodatage`
- parsing and formatting support through `analyser` and `Instant.formater(...)`
- `Instant` accessors such as `annee`, `mois`, `jour`, `heure`, `minute`, `seconde`, `milliseconde`
- duration constructors such as `millisecondes`, `secondes`, `minutes`, `heures`, `jours`
- elapsed-time and waiting helpers such as `entre` and `attendre`

The current `Aléatoire` module includes:

- `graine`
- `entier`
- `décimal`
- `décimal_entre`
- `choisir`
- `mélanger`
- `échantillon`

### Collections and standard operations

Supported runtime collection families:

- `Liste`
- `ListeFixe`
- `Dictionnaire`
- `Ensemble`

Internally, the current tree-walker now distinguishes shared capabilities such as:

- iterable values: `Liste`, `ListeFixe`, `Ensemble`, `Dictionnaire` (by key), and `Texte`
- indexed reads: `Liste`, `ListeFixe`, `Dictionnaire`, and `Texte`
- mutable indexed sequences: `Liste` only; a `ListeFixe` is immutable

The current tree-walker enforces generic constraints across mutation paths, including:

- `Liste.ajouter`
- `Liste.inserer`
- list index assignment
- dictionary index assignment
- nested alias mutation of already-typed collections

Type metadata also propagates through derived values in important cases:

- `Texte.separer` and `"texte".separer(...)` produce an effectively typed `Liste[Texte]`
- `Liste.en_liste_fixe(n)` preserves the source element type and produces `ListeFixe[T, n]`
- `Liste.en_ensemble()` preserves the element type and produces `Ensemble[T]`
- `ListeFixe.en_liste()` preserves the element type and produces `Liste[T]`
- `Dictionnaire.cles()` produces `Liste[key_type]`
- `Dictionnaire.valeurs()` produces `Liste[value_type]`
- `Dictionnaire.paires()` produces `Liste[ListeFixe[Universel, 2]]` in the general case

The current `ListeFixe` surface includes:

- `taille`
- `vide`
- `contient`
- `joindre`
- `en_liste`

Creation currently happens through:

- `liste.en_liste_fixe(n)`
- `ListeFixe.remplir(T, n, valeur)`

The current runtime introspection surface includes:

- `type_de(valeur) -> Texte`
- `valeur est Type -> Logique`

### Error reporting

Implemented:

- parse errors with source position
- runtime errors with file, line, and column
- stack traces for runtime failures
- source snippets with caret positioning

This area is much stronger than a basic interpreter now. Diagnostics are intended to make it obvious where execution failed, not just that it failed.

## Runtime semantics already enforced

The tree-walker currently enforces all of the following at runtime:

- variable declaration type compatibility
- reassignment type compatibility
- function parameter type compatibility
- function return type compatibility
- implicit-return compatibility for typed functions
- constructor-time field initialization compatibility
- field assignment compatibility
- inheritance/interface-aware type acceptance
- interface contract presence checks for `réalise`
- collection element and key/value compatibility for typed generic containers

This enforcement is significant because the project does not yet have a dedicated semantic analysis pass. Some rules that would normally be compile-time checks are still runtime checks here.

## Examples that exercise the current surface

Repository examples worth using as documentation and smoke tests:

- [`examples/bonjour.lum`](../examples/bonjour.lum)
- [`examples/analyse_collections.lum`](../examples/analyse_collections.lum)
- [`examples/objets.lum`](../examples/objets.lum)
- [`examples/modules/main.lum`](../examples/modules/main.lum)
- [`examples/contrats/main.lum`](../examples/contrats/main.lum)

Stronger interpreter fixtures that cover more combined syntax:

- [`tests/fixtures/interpreter/expansive_language_tour/main.lum`](../tests/fixtures/interpreter/expansive_language_tour/main.lum)
- [`tests/fixtures/interpreter/interface_contracts/main.lum`](../tests/fixtures/interpreter/interface_contracts/main.lum)
- [`tests/fixtures/interpreter/nested_module_graph/main.lum`](../tests/fixtures/interpreter/nested_module_graph/main.lum)

## Gaps and partial areas

Important remaining gaps:

- semantic name/type/result analysis is mandatory before execution
- interface checking is presence-based rather than full signature conformance
- set semantics exist in the runtime value model but are less developed than lists and dictionaries
- some runtime annotations still use textual metadata at native boundaries

These are the places where new implementation work is most likely to change behavior significantly.

## Suggested use of this document

Use this file for three jobs:

- tracking what the implementation can actually do today
- deciding what belongs in comprehensive fixtures and examples
- documenting syntax that users can rely on right now

When a feature lands, this page should be updated together with tests and examples. That keeps the repository honest about both capability and ambition.
