# Chemin Stdlib

## Purpose

This note documents the intended direction of Lumiere path APIs.

The design choice is explicit:

- `Chemin` is lexical, not filesystem-aware
- path operations stay string-based
- filesystem state belongs in `Fichier`

That keeps path manipulation predictable, portable, and safe to use without hidden disk access.

## Preferred style

Prefer `Chemin` for building and decomposing paths lexically:

```lumiere
importer Chemin

soit base = Chemin.dossier_courant()
soit config = Chemin.joindre(base, "config", "app.txt")

afficher(Chemin.nom(config))
afficher(Chemin.nom_sans_extension(config))
afficher(Chemin.extension(config))
afficher(Chemin.dossier(config))
```

Use `Fichier` only when asking questions about the actual filesystem:

```lumiere
importer Fichier

si Fichier.existe(config) {
  afficher("present")
}
```

## Canonical surface

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

## Current implementation status

The built-in `Chemin` module currently implements:

- `Chemin.dossier_courant( )`
- `Chemin.joindre(...)`
- `Chemin.absolu(chemin)`
- `Chemin.nom(chemin)`
- `Chemin.nom_sans_extension(chemin)`
- `Chemin.extension(chemin)`
- `Chemin.dossier(chemin)`
- `Chemin.parties(chemin)`
- `Chemin.est_absolu(chemin)`
- `Chemin.est_relatif(chemin)`
- `Chemin.normaliser(chemin)`

Those names should be treated as the current source of truth for the interpreter.

## Semantic rules worth fixing now

### Lexical only

- `Chemin.normaliser` performs lexical normalization only
- `Chemin.absolu` resolves relative paths against `Chemin.dossier_courant()`
- neither operation requires the path to exist

This is important. `Chemin` should not quietly canonicalize through the filesystem.

### `nom_sans_extension`

- removes only the final extension component
- `archive.tar.gz` becomes `archive.tar`

That follows common path-library behavior and should be documented clearly.

### `parties`

- returns `Liste[Texte]`
- uses normalized path segments
- should avoid platform-specific surprises where possible

Cross-platform exact root handling should be tested carefully as the implementation hardens.

## Naming recommendations

- keep `nom_sans_extension`
- do not reintroduce `base`
- keep `dossier`, not `parent`

These names are clearer in French and reduce ambiguity.

## What should stay out of `Chemin`

Do not move these into `Chemin`:

- existence checks
- file type checks
- reading or writing
- directory listing

Those belong in `Fichier`.
