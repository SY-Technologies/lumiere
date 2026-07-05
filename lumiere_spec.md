# Lumiere Language Reference

This document describes the Lumiere language and standard library as implemented in this repository today.

It is intentionally implementation-oriented:

- it prefers current behavior over aspirational design
- it includes builtins, collection methods, module imports, and runtime conventions
- where the implementation is still partial, that is called out explicitly

## 1. At a glance

Lumiere is a French-keyword programming language with:

- static runtime-enforced type annotations
- brace-delimited blocks
- functions, classes, inheritance, interfaces, and modules
- list and dictionary literals
- exceptions and pattern-style branching with `agir selon`
- a built-in standard library for text, files, paths, math, time, randomness, networking, and tests

Minimal example:

```lumiere
fonction principal() {
  soit noms = ["Ada", "Grace"]
  noms.ajouter("Linus")

  pour chaque nom dans noms {
    afficher("Bonjour " + nom)
  }
}
```

## 2. Source format

- Source files use UTF-8 text.
- Blocks use `{ ... }`.
- Statements are newline-oriented. Semicolons are not used.
- Comments:

```lumiere
// commentaire sur une ligne

/* commentaire
   sur plusieurs lignes */
```

## 3. Reserved words and special names

### Keywords

`soit`, `fixe`, `fonction`, `retourne`, `classe`, `interface`, `réalise`, `remplace`, `public`, `privé`, `si`, `sinon`, `pour`, `chaque`, `dans`, `tant que`, `agir selon`, `vrai`, `faux`, `rien`, `ici`, `parent`, `en`, `et`, `ou`, `non`, `arrêter`, `continuer`, `importer`, `comme`, `essayer`, `attraper`, `finalement`, `lancer`

### Built-in entry names

`principal`, `afficher`, `afficher_inline`, `lire`, `lire_entier`, `lire_décimal`, `lire_decimal`, `lire_logique`

Notes:

- `tant que` is lexed as one token.
- `agir selon` is lexed as one token.
- `parent` is available in methods and must be used with member access such as `parent.presenter()`.

## 4. Literals

Supported literal forms:

- integers: `0`, `42`, `1_000`
- decimals: `3.14`, `2.0`, `1_000.25`
- text: `"bonjour"`
- symbols: `'A'`
- booleans: `vrai`, `faux`
- null-like value: `rien`
- lists: `[1, 2, 3]`
- dictionaries: `{"nom": "Ada", "age": 36}`

Escape sequences are supported in text and symbol literals.

## 5. Types

### Primitive types

- `Entier`
- `Décimal`
- `Decimal`
- `Logique`
- `Symbole`
- `Texte`
- `Rien`
- `Universel`

Notes:

- `Décimal` and `Decimal` are both accepted type names.
- `Universel` is the catch-all supertype.
- Function parameters require annotations.
- Variable annotations are optional when the initializer makes the type clear.

### Collection types

- `Liste[T]`
- `ListeFixe[T, N]`
- `Dictionnaire[K, V]`
- `Ensemble[T]`

Generic annotations are parsed and enforced recursively at runtime.

Examples:

```lumiere
soit age: Entier = 36
soit nom = "Ada"
soit notes: Liste[Entier] = [12, 14, 18]
soit trio: ListeFixe[Entier, 3] = [12, 14, 18].en_liste_fixe(3)
soit profils: Dictionnaire[Texte, Entier] = {"ada": 12}
```

`ListeFixe[T, N]` is a built-in ordered collection containing exactly `N` elements of type `T`.

Rules:

- `N` is part of the type annotation and must be an integer literal in type position.
- the size of a `ListeFixe` never changes after construction
- elements may be replaced by index
- out-of-bounds reads and writes throw `ErreurIndice`
- `ListeFixe` is iterable in index order, like `Liste`

Current construction surface:

- `liste.en_liste_fixe(n)` converts a `Liste[T]` to `ListeFixe[T, n]` when the source length is exactly `n`
- `ListeFixe.remplir(T, n, valeur)` creates a `ListeFixe[T, n]` filled with `valeur`

Examples:

```lumiere
soit notes: Liste[Entier] = [12, 14, 18]
soit fixe trio = notes.en_liste_fixe(3)
soit zeros = ListeFixe.remplir(Entier, 4, 0)
```

## 6. Variables and assignment

Variable declarations use `soit`:

```lumiere
soit message = "bonjour"
soit total: Entier = 0
soit fixe version: Entier = 1
```

Rules:

- `soit` declares a mutable variable.
- `soit fixe` declares an immutable variable.
- an initializer is expected in normal use
- assignment is a statement, not a value-producing expression

Assignment targets supported today:

- local variables
- object fields
- list indices
- dictionary indices

```lumiere
total = total + 1
ici.nom = "Rex"
notes[0] = 18
profil["ville"] = "Paris"
```

## 7. Expressions and operators

### Arithmetic and comparison

- `+`, `-`, `*`, `/`, `%`
- `==`, `!=`, `<`, `<=`, `>`, `>=`

### Logical operators

- `et`
- `ou`
- `non`

`et` and `ou` short-circuit.

### Other expression forms

- grouping: `(expr)`
- function call: `f(1, 2)`
- named arguments: `Point(x: 3, y: 4)`
- member access: `objet.methode()`
- indexing: `liste[0]`
- anonymous function: `fonction(x: Entier) { retourne x + 1 }`
- cast/conversion: `valeur en Entier`
- type test: `valeur est Type`
- receiver access: `ici.nom`
- parent dispatch: `parent.presenter()`

### String concatenation

If either operand of `+` is `Texte`, the other side is stringified.

```lumiere
afficher("Age: " + 36)
```

### Casts with `en`

Current explicit conversions include:

- `Texte -> Entier`
- `Texte -> Décimal`
- `Texte -> Logique`
- `Symbole -> Entier`
- `Entier -> Décimal`
- any value -> `Universel`

Invalid casts raise a runtime error.

### Type tests with `est`

`valeur est Type` evaluates to `vrai` when the runtime value matches the given Lumière type annotation.

Supported checks include:

- primitive types such as `Entier`, `Texte`, and `Logique`
- generic collection types such as `Liste[Entier]`, `ListeFixe[Texte, 2]`, `Dictionnaire[Texte, Entier]`, and `Ensemble[Texte]`
- classes
- interfaces implemented by an object
- parent classes of an object

Examples:

```lumiere
afficher(chien est Animal)
afficher(chien est Presentable)
afficher(notes est ListeFixe[Entier, 3])
```

### Runtime type query

`type_de(valeur)` returns a `Texte` describing the current Lumière runtime type.

Examples:

```lumiere
afficher(type_de(chien))                  // "Chien"
afficher(type_de(notes))                  // "Liste[Entier]"
afficher(type_de(trio))                   // "ListeFixe[Entier, 3]"
afficher(type_de({"ada": 1}))             // "Dictionnaire"
```

## 8. Control flow

### `si` / `sinon`

```lumiere
si condition {
  afficher("ok")
} sinon {
  afficher("non")
}
```

### `tant que`

```lumiere
tant que (i < 10) {
  i = i + 1
}
```

### `pour chaque`

```lumiere
pour chaque note dans notes {
  afficher(note)
}
```

### Loop control

- `arrêter`
- `continuer`

### `agir selon`

`agir selon` is Lumiere’s current pattern-style branch construct.

```lumiere
agir selon valeur {
  1 -> afficher("un")
  2, 3 -> afficher("petit")
  n: Entier -> afficher(n)
  rien -> afficher("vide")
  sinon -> afficher("autre")
}
```

Implemented pattern kinds:

- literal patterns
- `rien`
- typed binding patterns such as `n: Entier`
- multiple literal patterns on one branch

Behavior:

- the first matching branch runs
- typed bindings exist only inside the winning branch
- there is no exhaustiveness checking yet

## 9. Functions

### Named functions

```lumiere
fonction somme(a: Entier, b: Entier = 2) -> Entier {
  retourne a + b
}
```

### Anonymous functions

```lumiere
soit doubler = fonction(x: Entier) -> Entier {
  retourne x * 2
}
```

Rules:

- parameter types are required
- return types are optional
- default parameter values are supported
- named arguments are supported for normal functions and constructors
- unknown or duplicate named arguments are rejected
- a function without explicit `retourne` returns `rien`
- when a return type is declared, runtime checks enforce it

The interpreter automatically calls `principal()` after module evaluation if it exists and is a function.

## 10. Classes, objects, and interfaces

### Classes

```lumiere
classe Animal {
  nom: Texte

  fonction presenter() {
    retourne "Animal:" + ici.nom
  }
}
```

### Inheritance

```lumiere
classe Chien : Animal {
  remplace fonction presenter() {
    retourne parent.presenter() + ":Chien"
  }
}
```

### Interfaces

```lumiere
interface Presentable {
  fonction presenter()
}

classe Rapport réalise Presentable {
  fonction presenter() {
    retourne "ok"
  }
}
```

### Visibility

- class members are public by default
- `privé` is supported on fields and methods
- top-level `public` is supported on `soit`, `fonction`, `classe`, and `interface`
- top-level `privé` is rejected

### Construction

Classes are called like functions:

```lumiere
soit chien = Chien(nom: "Rex")
```

Current behavior:

- constructor arguments bind to declared fields by name
- inherited fields participate in construction
- field initializers inside class bodies are not supported yet
- `remplace` is runtime-validated against the parent class
- `réalise` checks that required interface methods exist

## 11. Exceptions

Lumiere currently throws ordinary runtime values. In practice, many examples catch `Texte`.

```lumiere
essayer {
  afficher(notes[99])
} attraper (e: Texte) {
  afficher(e.contient("indice hors limites"))
} finalement {
  afficher("fin")
}
```

Also supported:

```lumiere
lancer "erreur"
```

Behavior:

- there may be multiple `attraper` clauses
- `finalement` always runs
- uncaught errors print a traceback with source locations

## 12. Modules and imports

### Importing full modules

```lumiere
importer Texte
importer outils.reseau comme reseau
```

### Selective imports

```lumiere
importer Maths.{max, min}
importer outils.reseau.{client comme client_http, erreur}
```

Rules:

- dotted paths map to `.lum` files
- imported modules are cached
- cyclic imports are rejected
- selective imports can only access public exports
- imported members can be aliased with `comme`
- built-in modules resolve before filesystem modules

Built-in modules currently registered:

- `Chemin`
- `Fichier`
- `Texte`
- `Maths`
- `Temps`
- `Aléatoire`
- `Aleatoire`
- `LumiNet`
- `LumiTest`

## 13. Built-in I/O functions

### Output

- `afficher(...)`
- `afficher_inline(...)`

Behavior:

- accepts positional arguments only
- prints arguments separated by spaces
- `afficher` adds a newline
- `afficher_inline` does not

### Input

- `lire() -> Texte`
- `lire_entier() -> Entier`
- `lire_décimal() -> Décimal`
- `lire_decimal() -> Décimal`
- `lire_logique() -> Logique`

Behavior:

- all input builtins reject arguments
- `lire_logique` accepts only `vrai` or `faux`

## 14. Built-in collection methods

### `Liste`

- `taille() -> Entier`
- `vide() -> Logique`
- `contient(valeur) -> Logique`
- `ajouter(valeur) -> Entier`
- `inserer(index, valeur) -> Entier`
- `retirer_a(index) -> Universel`
- `joindre(séparateur: Texte) -> Texte`

Notes:

- `ajouter` returns the new size
- `inserer` returns the insertion index
- element-type constraints are enforced on mutation

### `Dictionnaire`

- `taille() -> Entier`
- `vide() -> Logique`
- `contient(cle) -> Logique`
- `cles() -> Liste[K]`
- `valeurs() -> Liste[V]`
- `paires() -> Liste[ListeFixe[Universel, 2]]`
- `retirer(cle) -> V`

Notes:

- missing keys in `retirer` raise an error
- dictionary index assignment also enforces key/value annotations
- `paires()` returns ordered two-element fixed lists `[clé, valeur]`
- when key and value types are identical, pair elements preserve that shared type; otherwise pair elements are currently typed as `Universel`

### `Ensemble`

`Ensemble[T]` is a recognized type and is supported in runtime values, but this repository currently documents less surface behavior for it than for lists and dictionaries. Treat it as implemented but less mature.

## 15. `Texte` methods and module

### Methods on text values

- `taille()`
- `est_vide()`
- `contient(texte)`
- `index_de(texte)`
- `commence_par(prefixe)`
- `finit_par(suffixe)`
- `majuscules()`
- `minuscules()`
- `inverser()`
- `repeter(n)`
- `elaguer()`
- `elaguer_gauche()`
- `elaguer_droite()`
- `sous_texte(debut)`
- `sous_texte(debut, longueur)`
- `separer(separateur)`
- `separer_lignes()`
- `remplacer(cible, remplacement)`
- `remplacer_tout(cible, remplacement)`
- `inserer(position, texte)`
- `supprimer(debut, longueur)`
- `en_entier()`
- `en_decimal()`
- `en_logique()`

### `Texte` module functions

- `Texte.taille(texte)`
- `Texte.est_vide(texte)`
- `Texte.contient(texte, morceau)`
- `Texte.index_de(texte, morceau)`
- `Texte.commence_par(texte, prefixe)`
- `Texte.finit_par(texte, suffixe)`
- `Texte.separer(texte, separateur)`
- `Texte.separer_lignes(texte)`
- `Texte.remplacer(texte, cible, remplacement)`
- `Texte.joindre(valeurs, separateur)`
- `Texte.elaguer(texte)`
- `Texte.elaguer_gauche(texte)`
- `Texte.elaguer_droite(texte)`
- `Texte.minuscules(texte)`
- `Texte.majuscules(texte)`
- `Texte.convertir_entier(valeur)`
- `Texte.convertir_decimal(valeur)`
- `Texte.convertir_logique(valeur)`

Important current behavior:

- text operations are byte-oriented because runtime strings are `std::string`
- `separer` rejects an empty separator
- `remplacer` and `remplacer_tout` currently share the same replace-all behavior

## 16. `Maths`

### Constants

- `Maths.pi`
- `Maths.e`
- `Maths.infini`
- `Maths.non_nombre`

### Functions

- `absolu`
- `abs`
- `min`
- `max`
- `arrondir`
- `arrondi`
- `plancher`
- `plafond`
- `tronquer`
- `racine`
- `racine_n`
- `puissance`
- `log`
- `log10`
- `log2`
- `sin`
- `sinus`
- `cos`
- `cosinus`
- `tan`
- `tangente`
- `asin`
- `acos`
- `atan`
- `atan2`
- `degres_vers_radians`
- `radians_vers_degres`
- `est_non_nombre`
- `est_infini`
- `est_pair`
- `est_impair`

Examples:

```lumiere
importer Maths.{max, puissance}

afficher(max(3, 9))
afficher(puissance(2, 3))
```

## 17. `Chemin`

Exports:

- `Chemin.separateur`
- `Chemin.dossier_courant()`
- `Chemin.joindre(...parties)`
- `Chemin.absolu(chemin)`
- `Chemin.nom(chemin)`
- `Chemin.nom_sans_extension(chemin)`
- `Chemin.extension(chemin)`
- `Chemin.dossier(chemin)`
- `Chemin.parties(chemin)`
- `Chemin.est_absolu(chemin)`
- `Chemin.est_relatif(chemin)`
- `Chemin.normaliser(chemin)`

This module is lexical and path-oriented, not filesystem-state-aware.

## 18. `Fichier`

Exports:

- `Fichier.existe(chemin)`
- `Fichier.est_fichier(chemin)`
- `Fichier.est_dossier(chemin)`
- `Fichier.taille(chemin)`
- `Fichier.modifie_le(chemin)`
- `Fichier.lire_texte(chemin)`
- `Fichier.lire_lignes(chemin)`
- `Fichier.ecrire_texte(chemin, contenu)`
- `Fichier.ajouter_texte(chemin, contenu)`
- `Fichier.ecrire_lignes(chemin, lignes)`
- `Fichier.creer_dossiers(chemin)`
- `Fichier.lister(chemin)`
- `Fichier.lister_recursif(chemin)`
- `Fichier.copier(source, destination)`
- `Fichier.deplacer(source, destination)`
- `Fichier.supprimer(chemin)`
- `Fichier.supprimer_dossier(chemin)`
- `Fichier.supprimer_arbre(chemin)`

## 19. `Temps`

### Module functions

- `Temps.horodatage()`
- `Temps.maintenant()`
- `Temps.depuis_horodatage(ms)`
- `Temps.analyser(texte, format)`
- `Temps.entre(debut, fin)`
- `Temps.attendre(durée)`
- `Temps.millisecondes(n)`
- `Temps.secondes(n)`
- `Temps.minutes(n)`
- `Temps.heures(n)`
- `Temps.jours(n)`

### `Instant` methods

- `année()`
- `mois()`
- `jour()`
- `heure()`
- `minute()`
- `seconde()`
- `milliseconde()`
- `formater(format)`
- `en_horodatage()`
- `ajouter(durée)`
- `soustraire(durée)`

### `Durée` methods

- `en_millisecondes()`
- `en_secondes()`
- `en_minutes()`
- `en_heures()`

Formatting tokens currently implemented:

- `AAAA`
- `MM`
- `JJ`
- `HH`
- `mm`
- `ss`
- `SSS`

## 20. `Aléatoire`

Exports:

- `Aléatoire.graine(graine)`
- `Aléatoire.entier(min, max)`
- `Aléatoire.décimal()`
- `Aléatoire.décimal_entre(min, max)`
- `Aléatoire.choisir(valeurs)`
- `Aléatoire.mélanger(valeurs)`
- `Aléatoire.échantillon(valeurs, n)`

Notes:

- `entier` uses inclusive bounds
- `mélanger` mutates and returns the same list
- `Aleatoire` is also accepted as a module name alias

## 21. `LumiNet`

`LumiNet` is the built-in networking umbrella module.

### Root submodules

- `LumiNet.HTTP`
- `LumiNet.Canal`
- `LumiNet.TCP`
- `LumiNet.UDP`
- `LumiNet.DNS`
- `LumiNet.Adresse`

### HTTP

- `LumiNet.HTTP.requête(...)`
- `LumiNet.HTTP.obtenir(...)`
- `LumiNet.HTTP.créer(...)`
- `LumiNet.HTTP.modifier(...)`
- `LumiNet.HTTP.supprimer(...)`
- `LumiNet.HTTP.Serveur()`

HTTP-related object methods currently include names such as:

- `RéponseHTTP.entête`
- `RéponseHTTP.entêtes`
- `RequêteHTTP.paramètre`
- `RequêteHTTP.requête`
- `RequêteHTTP.entête`
- `RequêteHTTP.entêtes`
- `RéponseServeurHTTP.définir_entête`
- `RéponseServeurHTTP.envoyer`
- `RéponseServeurHTTP.envoyer_json`
- `RéponseServeurHTTP.envoyer_fichier`
- `RéponseServeurHTTP.rediriger`
- `ServeurHTTP.OBTENIR`
- `ServeurHTTP.CRÉER`
- `ServeurHTTP.MODIFIER`
- `ServeurHTTP.SUPPRIMER`
- `ServeurHTTP.avant`
- `ServeurHTTP.canal`
- `ServeurHTTP.écouter`
- `ServeurHTTP.arrêter`

### TCP

- `LumiNet.TCP.connecter(...)`
- `LumiNet.TCP.Serveur()`

TCP object methods include:

- `ConnexionTCP.est_connecté`
- `ConnexionTCP.fermer`
- `ConnexionTCP.définir_délai`
- `ConnexionTCP.écrire`
- `ConnexionTCP.écrire_octets`
- `ConnexionTCP.lire`
- `ConnexionTCP.lire_ligne`
- `ConnexionTCP.lire_octets`
- `ServeurTCP.quand_connexion`
- `ServeurTCP.écouter`
- `ServeurTCP.arrêter`

### UDP

- `LumiNet.UDP.ouvrir(...)`

UDP object methods include:

- `SocketUDP.définir_délai`
- `SocketUDP.envoyer`
- `SocketUDP.envoyer_octets`

### DNS

- `LumiNet.DNS.résoudre`
- `LumiNet.DNS.résoudre_tous`
- `LumiNet.DNS.résoudre_inverse`

### Canal

- `LumiNet.Canal.connecter(...)`
- `LumiNet.Canal.Serveur()`

Canal-related object methods include:

- `envoyer`
- `envoyer_octets`
- `est_connecté`
- `attendre`
- `quand_ouvert`
- `quand_message`
- `quand_fermé`
- `quand_erreur`
- `quand_connexion`
- `quand_déconnexion`
- `écouter`
- `arrêter`

This module is substantial but still evolving. For the exact implementation split, see [docs/stdlib-luminet.md](./docs/stdlib-luminet.md).

## 22. `LumiTest`

`LumiTest` is the built-in testing module used by the `lumiere tester` CLI.

Exports:

- `LumiTest.test`
- `LumiTest.groupe`
- `LumiTest.avant_tout`
- `LumiTest.avant_chaque`
- `LumiTest.après_chaque`
- `LumiTest.après_tout`
- `LumiTest.vérifier`
- `LumiTest.vérifier_égal`
- `LumiTest.vérifier_différent`
- `LumiTest.vérifier_lance`
- `LumiTest.vérifier_contient`
- `LumiTest.vérifier_approx`

Example:

```lumiere
importer LumiTest

LumiTest.groupe("Maths", fonction() {
  LumiTest.test("addition", fonction() {
    LumiTest.vérifier_égal(4, 2 + 2)
  })
})
```

See [docs/lumitest-spec.md](./docs/lumitest-spec.md) for the fuller runner model.

## 23. Current limitations and sharp edges

- The interpreter is runtime-checked rather than statically compiled.
- Text indexing and slicing are currently byte-oriented, not full Unicode-scalar aware.
- `Ensemble` exists but is less fully documented than `Liste` and `Dictionnaire`.
- Interface validation checks method presence, not deep signature equivalence.
- Class field initializers inside class bodies are not implemented yet.
- `remplacer` and `remplacer_tout` currently behave the same.
- Some library surfaces accept accented and unaccented variants only in specific places, not uniformly.

## 24. Practical reading order

If you are learning the language from this repository, read next:

1. [docs/implemented-language-overview.md](/Users/sylvainyabre/programming/tech/lumiere/docs/implemented-language-overview.md)
2. [examples/bonjour.lum](/Users/sylvainyabre/programming/tech/lumiere/examples/bonjour.lum)
3. [tests/fixtures/interpreter/expansive_language_tour/main.lum](/Users/sylvainyabre/programming/tech/lumiere/tests/fixtures/interpreter/expansive_language_tour/main.lum)
4. [docs/README.md](/Users/sylvainyabre/programming/tech/lumiere/docs/README.md)
