# Fichier Stdlib

## Purpose

This note documents the intended direction of Lumiere file APIs.

Filesystem APIs benefit from being explicit, narrow, and unsurprising.

The design choice here is therefore:

- one canonical name per operation
- no aliases
- payload-aware names such as `lire_texte` rather than vague verbs such as `lire`
- destructive operations named as clearly as possible

This should keep the standard library readable and safer to use.

## Preferred style

Prefer precise operations whose names already tell the caller what is being read, written, or removed.

```lumiere
importer Fichier

soit contenu = Fichier.lire_texte("notes.txt")
soit lignes = Fichier.lire_lignes("notes.txt")

Fichier.ecrire_texte("sortie.txt", "bonjour")
Fichier.ajouter_texte("journal.txt", "suite\n")
Fichier.ecrire_lignes("sortie.txt", ["a", "b", "c"])

si Fichier.existe("config.json") {
  afficher("present")
}
```

Avoid a broader but blurrier surface such as:

- `lire`
- `ecrire`
- `ajouter`
- multiple aliases for the same behavior

## Proposed surface

### Metadata and predicates

- `Fichier.existe(chemin)`
- `Fichier.est_fichier(chemin)`
- `Fichier.est_dossier(chemin)`
- `Fichier.taille(chemin)`
- `Fichier.modifie_le(chemin)`

### Read operations

- `Fichier.lire_texte(chemin)`
- `Fichier.lire_lignes(chemin)`

### Write operations

- `Fichier.ecrire_texte(chemin, contenu)`
- `Fichier.ajouter_texte(chemin, contenu)`
- `Fichier.ecrire_lignes(chemin, lignes)`

### File and directory operations

- `Fichier.copier(source, destination)`
- `Fichier.deplacer(source, destination)`
- `Fichier.supprimer(chemin)`
- `Fichier.creer_dossiers(chemin)`
- `Fichier.supprimer_dossier(chemin)`
- `Fichier.supprimer_arbre(chemin)`
- `Fichier.lister(chemin)`
- `Fichier.lister_recursif(chemin)`

## Current implementation status

The built-in `Fichier` module currently implements:

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

Those names should be treated as the current source of truth for the interpreter.

Binary I/O remains intentionally deferred for now.

## Semantic rules worth fixing now

These rules should be part of the contract before implementation expands.

### Text encoding

- text reads and writes use UTF-8
- invalid text decoding returns `Échec(ErreurFichier(...))`

### `lire_lignes`

- returns `Liste[Texte]`
- line terminators are not preserved
- empty final lines are preserved only if the underlying split semantics explicitly keep them

This last point should be decided deliberately during implementation and then documented exactly.

### `ecrire_lignes`

- expects `Liste[Texte]`
- joins lines using `\n`
- overwrites the file if it already exists

### Listing behavior

- `lister` returns direct children only
- `lister_recursif` returns the full recursive listing
- results should be sorted deterministically
- returned values should be paths as `Texte`

Deterministic ordering matters a lot for tests, examples, and cross-platform predictability.

### Directory deletion semantics

- `supprimer_dossier` should remove an empty directory only
- `supprimer_arbre` should be the explicit recursive destructive operation

This naming split is much safer than making `supprimer_dossier` context-dependent.

### `deplacer` versus `renommer`

Recommendation:

- keep `deplacer`
- do not add `renommer`

Renaming is already a move operation, and carrying both adds surface without adding much expressive power.

## Error model

Filesystem operations return `Résultat[T, Fichier.ErreurFichier]`.
`ErreurFichier` records the operation, path, and host cause. Programmer errors
such as wrong arity or argument types remain runtime contract errors.

## Naming recommendations

These choices are recommended for consistency with the current codebase:

- prefer ASCII-friendly exported names in code: `ecrire`, `modifie`, `recursif`
- keep human-facing documentation free to show accented French where helpful
- prefer operation names that encode payload form: `lire_texte`, `ecrire_octets`
- do not introduce aliases for alternate spellings

If the language later standardizes accented identifiers in stdlib exports, that should be a deliberate library-wide move rather than an isolated exception here.

## Deferred area

Binary file support is still intentionally postponed:

- `Fichier.lire_octets(chemin)`
- `Fichier.ecrire_octets(chemin, octets)`

If Lumiere adds those later, they should arrive with a very strict contract around `0..255` byte values and explicit validation errors.
