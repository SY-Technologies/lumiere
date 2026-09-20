# Aléatoire Stdlib

## Purpose

This note documents the intended direction of Lumiere random APIs.

The design choice is explicit:

- one clear operation name per behavior
- deterministic replay through `graine`
- explicit failure behavior for empty collections and invalid sample sizes

This should keep random operations convenient without becoming vague.

## Preferred style

```lumiere
importer Aléatoire

Aléatoire.graine(42)

afficher(Aléatoire.entier(1, 6))
afficher(Aléatoire.décimal())
afficher(Aléatoire.décimal_entre(10.0, 20.0))

soit fruits = ["pomme", "banane", "cerise"]
afficher(Aléatoire.choisir(fruits))
Aléatoire.mélanger(fruits)
afficher(Aléatoire.échantillon(fruits, 2))
```

## Canonical surface

- `Aléatoire.graine(graine)`
- `Aléatoire.entier(min, max)`
- `Aléatoire.décimal()`
- `Aléatoire.décimal_entre(min, max)`
- `Aléatoire.choisir(valeurs)`
- `Aléatoire.mélanger(valeurs)`
- `Aléatoire.échantillon(valeurs, n)`

## Current implementation status

The current interpreter runtime already implements:

- `graine`
- `entier`
- `décimal`
- `choisir`
- `mélanger`
- `échantillon`

`décimal_entre` is the adopted canonical API direction and should be treated as the target naming choice when aligning the runtime surface.

## Semantic rules worth fixing now

### Integer and decimal ranges

- `Aléatoire.entier(min, max)` uses inclusive bounds
- `Aléatoire.décimal()` returns a value in `0.0..1.0`
- `Aléatoire.décimal_entre(min, max)` returns a value within the requested decimal interval

Inclusive-versus-exclusive behavior should be stated everywhere this API is documented.

### Collection operations

- `Aléatoire.choisir([])` should raise a runtime error
- `Aléatoire.échantillon(valeurs, n)` should raise when `n < 0` or `n > taille`
- `Aléatoire.mélanger(valeurs)` should document whether it returns `Rien` or the mutated list

Recommendation:

- mutate the list in place
- return the same list for ergonomic chaining

Either choice is fine, but it should be deliberate.

### Reproducibility

- `Aléatoire.graine` should fully determine subsequent results for the same runtime implementation
- tests should use `graine` whenever stable expectations matter

This is especially helpful for fixture-based interpreter coverage.

## Naming recommendations

- keep `décimal_entre`
- avoid overloading `décimal(...)` with ranged and non-ranged forms

Separate names are clearer and easier to document than overload-heavy random APIs.
