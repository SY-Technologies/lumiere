# Temps Stdlib

## Purpose

This note documents the intended direction of Lumiere time APIs.

The design choice is explicit:

- `Temps` exposes runtime-backed value types
- `Instant` and `Durée` are first-class values with methods
- time semantics should be fixed deliberately instead of drifting

This should keep the API readable and make later backend sharing much easier.

## Preferred style

```lumiere
importer Temps

soit debut = Temps.maintenant()
soit pause = Temps.secondes(2)

Temps.attendre(pause)

soit fin = Temps.maintenant()
soit durée = Temps.entre(debut, fin)

afficher(fin.formater("AAAA-MM-JJ HH:mm:ss"))
afficher(durée.en_millisecondes())
```

## Canonical surface

### Module functions

- `Temps.maintenant()`
- `Temps.horodatage()`
- `Temps.depuis_horodatage(ms)`
- `Temps.analyser(texte, format)`
- `Temps.entre(debut, fin)`
- `Temps.attendre(durée)`
- `Temps.millisecondes(n)`
- `Temps.secondes(n)`
- `Temps.minutes(n)`
- `Temps.heures(n)`
- `Temps.jours(n)`

### `Instant`

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

### `Durée`

- `en_millisecondes()`
- `en_secondes()`
- `en_minutes()`
- `en_heures()`

## Current implementation status

The current interpreter runtime already implements:

- `horodatage`
- `maintenant`
- `depuis_horodatage`
- the current `Instant` accessor and formatting surface
- duration constructors
- `entre`
- `analyser`
- `attendre`

The arithmetic surface should be treated as the intended direction even where implementation details are still evolving.

## Semantic rules worth fixing now

### Internal time basis

Recommendation:

- use Unix timestamps in milliseconds
- store instants internally in UTC

That gives a stable cross-backend representation even if formatting policy evolves later.

### Parsing and formatting

- `Temps.analyser` returns `Résultat[Instant, ErreurTemps]`; invalid input or
  format produces `Échec`
- `Instant.formater` should use a documented token set
- timezone behavior should be documented explicitly rather than implied

If formatting and parsing are local-time based for now, say so clearly.

### `Durée`

- duration constructors return a `Durée` value
- conversion accessors expose integer milliseconds and decimal larger units
- negative durations should either be supported deliberately or rejected deliberately

This should be decided explicitly before the surface expands.

## Naming recommendations

- keep `Instant` and `Durée`
- keep `horodatage` for Unix timestamp values
- keep constructor-style functions such as `secondes(...)`

This surface reads naturally in Lumiere and avoids overloading the module with vague names.

## Strong next additions

Good near-term additions once the current surface stabilizes:

- `Instant.ajouter(durée)`
- `Instant.soustraire(durée)`
- `Durée.ajouter(durée2)`
- `Durée.soustraire(durée2)`

Time APIs become much more useful once arithmetic is available directly on the value types.
