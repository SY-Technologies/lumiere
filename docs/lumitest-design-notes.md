# LumiTest Design Notes

This note revises the early LumiTest draft into a form that fits Lumiere's
existing language style and runtime architecture better.

The goal is not to freeze the final API, but to make the next design step more
coherent before implementation starts.

## Recommended direction

Keep these product goals:

- built-in test runner
- zero installation
- French-first output
- easy happy-path for simple tests

Change these design instincts:

- avoid too many magic prelude functions
- avoid mixing two unrelated test styles at the same level
- avoid a design that depends on reflection-like discovery tricks later
- keep the runtime bridge narrow, like the stdlib modules already do

The simplest durable model is:

- `lumiere tester` discovers test files
- test files explicitly import `LumiTest`
- tests are declared through a small DSL
- simple assertion helpers live in `LumiTest`
- advanced features are added through an explicit test context object

That keeps the feature visible in source code and easier to evolve.

## Why the original draft needs tightening

The current draft has four tensions.

### 1. It mixes two mental models

The draft supports both:

- implicit discovery through `tester_*` function names
- explicit grouped tests through `LumiTest.groupe(...)`

Those models pull in different directions.

If both exist equally, users will not know which one is canonical. Tooling and
documentation also become harder because there are two ways to express the same
thing.

Recommendation:

- choose explicit declaration as the primary model
- optionally support `tester_*` only as a transitional compatibility mode

### 2. Prelude assertions are too magical

Having `vérifier(...)` available everywhere sounds convenient, but it weakens
clarity:

- the source no longer shows where test behavior comes from
- future non-test code may want similar names
- editor tooling and static analysis get less obvious anchor points

Recommendation:

- require `importer LumiTest`
- expose assertions as `LumiTest.vérifier_*`
- optionally allow a short alias pattern later if the language grows one

That is one extra import, but much clearer.

### 3. `vérifier_lance_type` assumes an error model we do not fully have yet

The repository uses typed `Résultat` values for recoverable failure, but the testing
API should not over-promise rich error matching until the language-level error
story is more settled.

Recommendation:

- start with `vérifier_lance`
- add `vérifier_lance_avec(message_fragment, bloc)`
- defer exact-type matching until error values are more explicitly modeled

### 4. Verbose assertion variants can explode quickly

The draft already has many built-ins:

- `vérifier`
- `vérifier_égal`
- `vérifier_différent`
- `vérifier_rien`
- `vérifier_pas_rien`
- `vérifier_lance`
- `vérifier_lance_type`
- `vérifier_contient`
- `vérifier_taille`
- `vérifier_approx`

This is manageable, but it will sprawl if every special case gets its own
global helper.

Recommendation:

- keep a compact core
- put richer behavior on expectation objects or specialized helpers later

## Proposed surface

## CLI

Keep:

- `lumiere tester`
- `lumiere tester chemin`
- `lumiere tester --filtre motif`
- `lumiere tester --verbeux`
- `lumiere tester --arrêter-sur-échec`

Prefer adding later, not now:

- `--couverture`
- mocking/spying flags
- parallel execution flags

Coverage is important, but it should be a phase-2 concern.

## File discovery

Keep the `_test.lum` suffix convention.

Recommended discovery behavior:

- scan the working tree or explicit path
- include files ending in `_test.lum`
- ignore them for `lumiere lancer`
- do not require a dedicated `tests/` folder

That stays conventional without being restrictive.

## Declaration style

Prefer explicit test declarations:

```lumiere
importer LumiTest

LumiTest.groupe("Calculs", fonction() {
    LumiTest.test("addition simple", fonction() {
        LumiTest.vérifier_égal(5, additionner(2, 3))
    })
})
```

Why this is better:

- names are human-facing, not tied to function identifiers
- nested grouping is natural
- setup/teardown fits the model cleanly
- filtered execution can use labels directly

If we want a lightweight top-level shorthand, prefer this:

```lumiere
importer LumiTest

LumiTest.test("addition simple", fonction() {
    LumiTest.vérifier_égal(5, additionner(2, 3))
})
```

This should be the canonical beginner form.

## Core assertions

Recommended phase-1 core:

- `LumiTest.vérifier(condition, message: Texte = "")`
- `LumiTest.vérifier_égal(attendu, reçu, message: Texte = "")`
- `LumiTest.vérifier_différent(a, b, message: Texte = "")`
- `LumiTest.vérifier_lance(bloc, message: Texte = "")`
- `LumiTest.vérifier_contient(collection, élément, message: Texte = "")`
- `LumiTest.vérifier_approx(attendu, reçu, tolérance, message: Texte = "")`

Defer these until the need is proven:

- `vérifier_rien`
- `vérifier_pas_rien`
- `vérifier_taille`
- `vérifier_lance_type`

Why defer them:

- `rien` checks can often be expressed with `vérifier_égal(rien, valeur)`
- size checks can often be `vérifier_égal(3, liste.taille())`
- type-specific failure matching uses `agir selon` and structured error values

This keeps the initial API tight.

## Failure reporting

Failures should always include:

- test name
- file path
- line/column if available
- assertion kind
- expected vs received when meaningful

Recommended tone:

```text
Échec — tests/calcul_test.lum:12:9
test: addition simple
assertion: vérifier_égal
attendu: 5
reçu: 4
```

That is flatter and easier to parse than deeply indented prose.

## Advanced API through a context object

Instead of growing many global `LumiTest.*` hooks, introduce a context object
for advanced scenarios.

Example direction:

```lumiere
importer LumiTest

LumiTest.groupe("Base", fonction(t) {
    t.avant_chaque(fonction() {
        réinitialiser_base()
    })

    t.test("insertion", fonction(t) {
        t.vérifier(insérer_utilisateur("Alice"))
    })
})
```

Benefits:

- setup/teardown naturally live on the group context
- assertions can be available on the test context too
- future APIs like temp dirs, logs, and skip/fail-now fit cleanly

This is a better long-term design than only static module functions.

## Parametrized tests

The draft idea is good, but the first version should stay simple.

Recommended phase-1 form:

```lumiere
importer LumiTest

soit cas = [
    [2, 3, 5],
    [0, 0, 0],
    [-1, 1, 0]
]

LumiTest.test_table("addition", cas, fonction(t, cas) {
    soit a = cas[0]
    soit b = cas[1]
    soit attendu = cas[2]
    t.vérifier_égal(attendu, additionner(a, b))
})
```

Why this version first:

- avoids needing special tuple/destructuring ergonomics
- works with the collection model already present in Lumiere
- still gives us useful table-driven tests

Later we can add prettier case labels and destructured parameters.

## Skip and pending tests

Prefer explicit runtime semantics:

- `t.ignorer("raison")`
- or `LumiTest.test_ignoré("nom", "raison", fonction() { ... })`

Avoid a separate registration-only concept that feels unlike the rest of the
language.

Recommended output:

```text
⊘ cache distant — ignoré: dépend d'une API non disponible localement
```

## Mocking and spying

Do not make mocks part of LumiTest 0.1.

Reasons:

- the language runtime is still evolving
- replacing globals or module members safely is a large semantic feature
- spies/mocks can distort the core framework design if introduced too early

Recommendation:

- keep mocks explicitly out of the first LumiTest milestone
- design test doubles later as a separate module or phase-2 extension

## Recommended first implementation slice

Build LumiTest in this order:

1. `lumiere tester` command with `_test.lum` discovery
2. explicit `importer LumiTest`
3. `LumiTest.test(...)`
4. core assertions: `vérifier`, `vérifier_égal`, `vérifier_différent`
5. French output with file/line metadata
6. `LumiTest.groupe(...)`
7. `vérifier_lance`, `vérifier_contient`, `vérifier_approx`
8. `avant_chaque` / `après_chaque`
9. simple table-driven tests

That gives real value quickly without committing too early to more complex
features.

## Concrete recommendation

If we were rewriting the spec today, the canonical beginner example should look
like this:

```lumiere
importer LumiTest

LumiTest.test("mention très bien", fonction() {
    soit é = Étudiant(nom: "Alice", notes: [16, 17, 18])
    LumiTest.vérifier_égal("Très bien", é.mention())
})
```

And the canonical structured example should look like this:

```lumiere
importer LumiTest

LumiTest.groupe("Étudiant", fonction(t) {
    t.test("moyenne normale", fonction(t) {
        soit é = Étudiant(nom: "Alice", notes: [14, 16, 18, 15])
        t.vérifier_approx(15.75, é.moyenne(), 0.01)
    })

    t.test("moyenne vide lance une erreur", fonction(t) {
        soit é = Étudiant(nom: "Bob", notes: [])
        t.vérifier_lance(fonction() {
            é.moyenne()
        })
    })
})
```

This feels more Lumiere-native:

- explicit
- readable
- French-first
- structured without reflection magic
- easy to extend with contexts later
