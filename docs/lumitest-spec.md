# LumiTest Specification

Version 0.1

## Purpose

LumiTest is Lumiere's built-in test framework.

It should feel native to the language:

- no package installation
- a simple project-level runner
- French-first output
- explicit test declarations that are easy to read and evolve

This document replaces the earlier draft with a narrower and more coherent
surface for the first implementation.

## Design goals

LumiTest 0.1 aims to be:

- built in
- explicit in source code
- easy to scan in small files
- friendly to tooling and future evolution
- small enough to implement without special-case runtime magic

The canonical model is:

1. `lumiere tester` discovers test files
2. test files `importer LumiTest`
3. tests are declared with `LumiTest.test(...)`
4. related tests are organized with `LumiTest.groupe(...)`
5. assertions are called through `LumiTest.*`

## Non-goals for 0.1

These ideas are intentionally deferred:

- prelude-global assertions such as bare `vérifier(...)`
- mocking and spying
- coverage reporting
- parallel execution
- exact error-type assertion matching
- a very large assertion catalog

The goal of 0.1 is a durable core, not feature breadth.

## File discovery

Any file ending in `_test.lum` is considered a test file.

Recommended project layout:

```text
mon_projet/
├── src/
│   ├── calcul.lum
│   └── principal.lum
├── tests/
│   ├── calcul_test.lum
│   └── texte_test.lum
└── lumiere.toml
```

Discovery rules:

- `lumiere tester` scans the working tree or the provided path
- only files ending in `_test.lum` are considered test files
- `_test.lum` files are ignored by `lumiere lancer`
- a dedicated `tests/` directory is conventional, not required

## CLI

Phase-1 CLI surface:

```bash
lumiere tester
lumiere tester tests/calcul_test.lum
lumiere tester --filtre addition
lumiere tester --verbeux
lumiere tester --arrêter-sur-échec
```

Expected behavior:

- `lumiere tester` runs all discovered tests
- a file path limits execution to that file
- `--filtre` matches group names and test names by substring
- when a filter excludes every test in a group, that group's `avant_tout` and
  `après_tout` hooks do not run
- `--verbeux` prints each test as it starts and ends
- `--arrêter-sur-échec` stops at the first failed test

Deferred flags:

- `--couverture`
- `--parallèle`

## Canonical declaration style

The canonical beginner form is an explicit top-level test:

```lumiere
importer LumiTest

LumiTest.test("addition simple", fonction() {
    LumiTest.vérifier_égal(5, additionner(2, 3))
})
```

Grouped tests are the canonical organizational form:

```lumiere
importer LumiTest

LumiTest.groupe("Calculs", fonction() {
    LumiTest.test("addition", fonction() {
        LumiTest.vérifier_égal(5, additionner(2, 3))
    })

    LumiTest.test("soustraction", fonction() {
        LumiTest.vérifier_égal(7, soustraire(10, 3))
    })
})
```

Why this model is preferred:

- test names are human-facing
- source code clearly shows where test behavior comes from
- setup and teardown fit naturally
- tooling does not depend on function-name conventions

## Transitional compatibility

An implementation may choose to support `tester_*` function discovery
temporarily for migration or experiments.

If supported, it should be documented as compatibility behavior, not as the
recommended style.

The canonical documented API remains the explicit `LumiTest.*` DSL.

## Core API

## Module import

Test files import the built-in module explicitly:

```lumiere
importer LumiTest
```

## Test registration

### `LumiTest.test`

Registers a single test.

```lumiere
LumiTest.test(nom, bloc)
```

Example:

```lumiere
LumiTest.test("mention bien", fonction() {
    soit é = Étudiant(nom: "Bob", notes: [12, 13, 14])
    LumiTest.vérifier_égal("Bien", é.mention())
})
```

### `LumiTest.groupe`

Registers a named group of tests.

```lumiere
LumiTest.groupe(nom, bloc)
```

Example:

```lumiere
LumiTest.groupe("Étudiant", fonction() {
    LumiTest.test("identifiant", fonction() {
        soit é = Étudiant(nom: "Alice", notes: [])
        LumiTest.vérifier_contient(é.identifiant(), "Alice")
    })
})
```

## Assertions

The initial assertion set stays intentionally compact.

### `LumiTest.vérifier`

Fails if the condition is false.

```lumiere
LumiTest.vérifier(condition)
LumiTest.vérifier(condition, "message")
```

Example:

```lumiere
LumiTest.vérifier(2 + 2 == 4)
```

### `LumiTest.vérifier_égal`

Fails if two values are not equal.

```lumiere
LumiTest.vérifier_égal(attendu, reçu)
LumiTest.vérifier_égal(attendu, reçu, "message")
```

Example:

```lumiere
LumiTest.vérifier_égal("Très bien", é.mention())
```

### `LumiTest.vérifier_différent`

Fails if two values are equal.

```lumiere
LumiTest.vérifier_différent(a, b)
LumiTest.vérifier_différent(a, b, "message")
```

### `LumiTest.vérifier_lance`

Fails if the provided function does not raise an error.

```lumiere
LumiTest.vérifier_lance(fonction() { ... })
LumiTest.vérifier_lance(fonction() { ... }, "message")
```

Example:

```lumiere
LumiTest.vérifier_lance(fonction() {
    diviser(10, 0)
})
```

### `LumiTest.vérifier_contient`

Fails if a collection or text does not contain the expected element.

```lumiere
LumiTest.vérifier_contient(collection, élément)
LumiTest.vérifier_contient(collection, élément, "message")
```

Supported targets should include:

- `Liste`
- `Ensemble`
- `Dictionnaire`
- `Texte`

### `LumiTest.vérifier_approx`

Fails if two numeric values differ by more than a tolerance.

```lumiere
LumiTest.vérifier_approx(attendu, reçu, tolérance)
LumiTest.vérifier_approx(attendu, reçu, tolérance, "message")
```

Example:

```lumiere
LumiTest.vérifier_approx(3.0, Maths.racine(9.0), 0.0001)
```

## Deferred assertions

These assertions are intentionally not part of the initial public surface:

- `vérifier_rien`
- `vérifier_pas_rien`
- `vérifier_taille`
- `vérifier_lance_type`

Rationale:

- `rien` checks can be expressed with `vérifier_égal(rien, valeur)`
- size checks can be expressed with `vérifier_égal(n, collection.taille())`
- exact error-type matching depends on a stronger error model than 0.1 needs

## Group context

Advanced features should grow through an explicit context object rather than
through many top-level static hooks.

Preferred direction:

```lumiere
importer LumiTest

LumiTest.groupe("Base", fonction(t: Universel) {
    t.avant_chaque(fonction() {
        réinitialiser_base()
    })

    t.test("insertion", fonction(t: Universel) {
        t.vérifier(insérer_utilisateur("Alice"))
    })
})
```

This design is preferred because it leaves room for:

- `avant_chaque`
- `après_chaque`
- `avant_tout`
- `après_tout`
- future helpers such as temporary directories or log capture

Implemented advanced lifecycle surface:

- `avant_tout`
- `avant_chaque`
- `après_chaque`
- `après_tout`

Current language note:

- callback parameters still require explicit type annotations, so context-style
  callbacks are written as `fonction(t: Universel) { ... }`

## Parametrized tests

Parametrized tests should begin as a simple table-driven form.

Recommended form:

```lumiere
importer LumiTest

soit cas = [
    [2, 3, 5],
    [0, 0, 0],
    [-1, 1, 0]
]

LumiTest.groupe("addition", fonction(t: Universel) {
    pour chaque ligne dans cas {
        soit a = ligne[0]
        soit b = ligne[1]
        soit attendu = ligne[2]

        t.test("cas: " + a.en_texte() + ", " + b.en_texte(), fonction() {
            t.vérifier_égal(attendu, additionner(a, b))
        })
    }
})
```

This keeps the first implementation simple and avoids introducing a special DSL
for destructuring or generated signatures too early.

## Output

LumiTest output should be compact, readable, and fully French-first.

### Success output

```text
--- LumiTest ---

Calculs
    ✓ addition
    ✓ soustraction

--- RÉUSSI — 2 tests ---
```

### Failure output

Failures should always include:

- test name
- file path
- line and column when available
- assertion kind
- expected and received values when meaningful

Recommended form:

```text
Échec — tests/calcul_test.lum:12:9
test: addition
assertion: vérifier_égal
attendu: 5
reçu: 4
```

This flatter structure is easier for humans and tools to parse than deeply
indented prose.

## Example

```lumiere
importer LumiTest

LumiTest.groupe("Étudiant", fonction() {
    LumiTest.test("moyenne normale", fonction() {
        soit é = Étudiant(nom: "Alice", notes: [14, 16, 18, 15])
        LumiTest.vérifier_approx(15.75, é.moyenne(), 0.01)
    })

    LumiTest.test("moyenne vide lance une erreur", fonction() {
        soit é = Étudiant(nom: "Bob", notes: [])
        LumiTest.vérifier_lance(fonction() {
            é.moyenne()
        })
    })

    LumiTest.test("mention très bien", fonction() {
        soit é = Étudiant(nom: "Alice", notes: [16, 17, 18])
        LumiTest.vérifier_égal("Très bien", é.mention())
    })

    LumiTest.test("identifiant contient le nom", fonction() {
        soit é = Étudiant(nom: "Alice", notes: [])
        LumiTest.vérifier_contient(é.identifiant(), "Alice")
    })
})
```

## Suggested implementation order

Recommended delivery sequence:

1. `lumiere tester` command with `_test.lum` discovery
2. built-in `LumiTest` module import
3. `LumiTest.test(...)`
4. `LumiTest.vérifier` and `LumiTest.vérifier_égal`
5. failure reporting with file and line metadata
6. `LumiTest.groupe(...)`
7. `vérifier_différent`, `vérifier_lance`, `vérifier_contient`, `vérifier_approx`
8. `avant_chaque` and `après_chaque`
9. simple table-driven patterns

This sequence delivers a usable framework quickly while keeping the runtime and
API design understandable.
