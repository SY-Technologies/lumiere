# Texte Stdlib

## Purpose

This note documents the current direction of Lumiere string APIs.

The design choice is now explicit:

- string behavior should be method-first
- the `Texte` module should stay small and hold only static helpers

That means ordinary string work should read like language syntax rather than like a utility library call.

## Preferred style

Prefer:

```lumiere
soit t = "Bonjour, monde!"

t.taille()
t.est_vide()
t.contient("monde")
t.commence_par("Bon")
t.finit_par("!")
t.index_de("monde")

t.majuscules()
t.minuscules()
t.inverser()
t.repeter(3)

t.elaguer()
t.elaguer_gauche()
t.elaguer_droite()

t.sous_texte(0, 7)
t.separer(",")
t.separer_lignes()

t.remplacer("monde", "lumiere")
t.remplacer_tout("a", "b")
t.inserer(7, " monde")
t.supprimer(0, 7)

"42".en_entier()
"3.14".en_decimal()
"vrai".en_logique()
```

Use `Texte` only for helpers that are naturally static:

```lumiere
importer Texte

Texte.joindre(["bonjour", "monde"], ", ")
Texte.convertir_entier(42)
Texte.convertir_decimal(3.14)
Texte.convertir_logique(vrai)
```

## Current implementation status

### Implemented string methods

Preferred method names currently implemented:

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

### Implemented `Texte` module helpers

The built-in `Texte` module currently exposes:

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

## Notes

- `Texte.joindre` currently expects a `Liste` whose elements are all `Texte`.
- `separer` and `Texte.separer` reject an empty separator.
- conversion methods return `Résultat` and report malformed input with
  `Texte.ErreurConversion`; wrong arity and argument types remain contract
  errors.
- Text is stored as UTF-8. Indexing, iteration, `taille`, `index_de`,
  `inverser`, `inserer`, `supprimer`, and `sous_texte` use Unicode scalar
  positions, not byte offsets. Module helpers and methods follow the same rule.
- Scalar positions are not grapheme positions: `"é"` (e + combining acute)
  has length 2, while `"é"` has length 1. Reversal reverses scalars, not
  user-perceived characters. No implicit normalization is performed.
- Insertion and slicing accept the position immediately after the last scalar.
  Deletion clamps its length to the remaining text; slicing rejects an
  excessive length. Negative positions and lengths are rejected.
- Case conversion and trimming still use byte-oriented C character functions;
  they are not full Unicode case mapping or Unicode whitespace handling.

## Why this split is worth keeping

This split keeps the language readable:

- methods for operations on one string
- module helpers for factory-style and collection-style helpers

If Lumiere keeps growing in this direction, the standard library will feel designed instead of accumulated.

## How To Make The Static Module More Useful

If we want `Texte` to be genuinely valuable even after going method-first, it should focus on helpers that are awkward as instance methods or naturally multi-input.

Strong candidates:

- `Texte.joindre(valeurs, separateur)`
- `Texte.formater(modele, valeurs...)` once formatting exists
- `Texte.lignes(texte)` and `Texte.mots(texte)` as parsing helpers
- `Texte.concatener(valeurs)` for list-based bulk concatenation
- `Texte.elargir_gauche(texte, largeur, remplissage = " ")`
- `Texte.elargir_droite(texte, largeur, remplissage = " ")`
- `Texte.centrer(texte, largeur, remplissage = " ")`
- `Texte.compter(texte, morceau)` for occurrence counting
- `Texte.commun_prefixe(valeurs)`
- `Texte.commun_suffixe(valeurs)`
- `Texte.normaliser_espaces(texte)`
- `Texte.echapper_json(texte)` if serialization helpers arrive
- `Texte.convertir_entier`, `Texte.convertir_decimal`, `Texte.convertir_logique`

My recommendation is to keep the module biased toward:

- many-to-one helpers
- parsing helpers
- formatting helpers
- conversion helpers

and avoid re-adding every ordinary single-string operation there if the method form already reads better.
