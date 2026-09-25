# LumiDessin Design

## Status

This document is the implementation contract for `LumiDessin`. It defines the
complete public Lumière API, observable behavior, native architecture, error
policy, and acceptance tests — the whole module, not a staged first cut. There
is no planned "v2": what isn't in this contract is out of scope for the
module, on its own merits, not deferred.

The implementation now matches this contract through stage 8 of
"Implementation order" below: every public symbol is implemented and tested
under both execution engines, on Linux. Stage 9's cross-platform (macOS,
Windows) window smoke tests have not been run from this environment; see
that stage's notes for what is and is not yet verified.

## Design review — 2026-09-24

This revision resolves five gaps found in review before implementation began:

1. **Key mapping.** Canonical key names now explicitly name physical key
   position (scancode), not the character a layout produces — the previous
   revision left this undefined, which matters for a French-first language
   whose users are disproportionately on AZERTY keyboards.
2. **Image format scope.** The module reads and writes PNG only. JPEG input
   without any JPEG output would be a permanently asymmetric capability, for a
   decoder an order of magnitude more complex than PNG's, so it is a
   permanent scope decision, not a cut feature.
3. **Native dependencies.** `stb_image`/`stb_image_write`/`stb_truetype` and a
   named, licensed bundled font (Inter, OFL-1.1) are now pinned alongside
   SDL3 — the previous revision specified a window backend but never said how
   images or fonts would actually be decoded or rasterized.
4. **Weak references.** The "weak crayon overlay registration" the previous
   revision described has no supporting primitive in this codebase (there is
   no `WeakRef<T>`; see `include/lumiere/interpreter/runtime/ref.hpp`). The
   Native state section below now specifies the concrete mechanism instead:
   raw non-owning pointers, registered and deregistered in `CrayonState`'s
   constructor/destructor, kept safe by `CrayonState` owning its canvas
   strongly and never the reverse.
5. **Zero-thickness strokes.** Previously a runtime error, inconsistent with
   zero-area fills being a documented no-op for the same reason (a program
   computing a size or thickness from other values will eventually compute
   zero). Both are now no-ops; only a negative value is an error.

Also lowered: the canvas/image pixel ceiling (67,108,864 → 16,777,216, sized
for real displays rather than only overflow safety) and the maximum cadence
(1000 → 240 fps, since this interpreter cannot usefully target four-figure
frame rates — see the project's `performance-target` decision). Also newly
specified: crayon overlay draw order (creation order), `Point`/`Dimensions`
immutability, and `ErreurCouleur`'s field shape to match the rest of the
stdlib's error types.

## Purpose

LumiDessin is a small two-dimensional graphics module for learning, diagrams,
generative art, and simple interactive programs. It combines two APIs:

- a direct canvas API for shapes, text, images, animation, and input
- a `Crayon` API for Turtle-style relative drawing

The canvas is the foundation. A crayon is state layered over a canvas; canvas
operations never need an invisible turtle.

The module name is `LumiDessin`. Public identifiers are French and use accents
where French requires them.

## Design goals

LumiDessin must provide:

1. A first drawing in a few lines.
2. Explicit state: no process-global current canvas, color, or crayon.
3. The same semantics under the tree-walker and bytecode VM.
4. Static drawings, frame-based animation, keyboard input, and mouse input.
5. Off-screen rendering and PNG export without a display server.
6. Deterministic ordering, clipping, alpha blending, and resource cleanup.
7. Useful French diagnostics with the Lumière call site.
8. One bounded 2D library rather than the beginning of a UI or game engine.

## Non-goals

LumiDessin does not provide:

- GUI widgets or layout
- 3D rendering, shaders, lighting, or a physics engine
- audio
- networking
- SVG or PDF export
- arbitrary affine transforms or a scene graph
- user-loaded fonts
- loading JPEG or any non-PNG image format
- asynchronous callbacks or background rendering
- multiple visible windows
- a compatibility clone of Python's `turtle` module

Every omission above is a permanent scope decision for what a small,
bounded 2D module is for, not a placeholder for a future release. If one of
them turns out to be genuinely needed, that is a new design document and a new
module contract, not an addition to this one.

## First programs

### Turtle-style drawing

```lum
importer LumiDessin

fonction principal()
{
    soit dessin = LumiDessin.fenêtre(800, 600, "Carré")
    soit crayon = dessin.crayon()

    soit côté = 0
    tant que (côté < 4)
    {
        crayon.avancer(140)
        crayon.tourner_gauche(90)
        côté = côté + 1
    }

    dessin.présenter()
    dessin.attendre_fermeture()
}
```

### Direct drawing

```lum
importer LumiDessin

fonction principal()
{
    soit dessin = LumiDessin.fenêtre(800, 600, "Formes")
    dessin.effacer(LumiDessin.Couleurs.blanc)
    dessin.remplir_cercle(400, 300, 90, LumiDessin.Couleurs.bleu)
    dessin.tracer_cercle(400, 300, 90, LumiDessin.Couleurs.noir, 4)
    dessin.dessiner_texte("Bonjour !", 330, 420, 28, LumiDessin.Couleurs.noir)
    dessin.présenter()
    dessin.attendre_fermeture()
}
```

### Animation

```lum
importer LumiDessin

fonction principal()
{
    soit dessin = LumiDessin.fenêtre(800, 600, "Animation")
    dessin.régler_cadence(60)
    soit x = 100.0

    tant que (dessin.prochaine_image())
    {
        soit vitesse = 220.0
        si dessin.touche_enfoncée("droite")
        {
            x = x + vitesse * dessin.écart_image()
        }
        si dessin.touche_enfoncée("gauche")
        {
            x = x - vitesse * dessin.écart_image()
        }

        dessin.effacer(LumiDessin.Couleurs.blanc)
        dessin.remplir_cercle(x, 300, 30, LumiDessin.Couleurs.orange)
        dessin.présenter()
    }
}
```

### Headless rendering

```lum
importer LumiDessin

fonction principal()
{
    soit dessin = LumiDessin.canevas(640, 480)
    dessin.effacer(LumiDessin.Couleurs.blanc)
    dessin.tracer_ligne(40, 40, 600, 440, LumiDessin.Couleurs.rouge, 6)

    agir selon dessin.enregistrer_png("image.png")
    {
        Succès(_) -> afficher("image.png créée")
        Échec(erreur) -> afficher(erreur.cause)
    }
}
```

## Public types

### `Canevas`

A mutable RGBA drawing surface. A canvas is either visible or off-screen. Both
kinds use the same drawing implementation.

A visible canvas owns the sole LumiDessin window in the process. An off-screen
canvas owns no operating-system window and works in headless environments.

### `Crayon`

A mutable Turtle-style cursor bound to one canvas. It stores a position,
heading, line color, line width, pen state, and visibility state.

### `Couleur`

An immutable color with 8-bit red, green, blue, and alpha components. Colors
use straight alpha at the public boundary. The renderer may use premultiplied
alpha internally.

### `Image`

An immutable decoded RGBA image. Loading an image copies its pixels into memory;
the file is not kept open. Capturing a canvas also produces an `Image`.

### `Point`

An immutable value with readable decimal fields `x` and `y`. Every `Point` the
module returns is a fresh, independent object — holding onto one and reading
it later never reflects a subsequent change to mouse or crayon state.

### `Dimensions`

An immutable value with readable integer fields `largeur` and `hauteur`.

### `ErreurImage`

A recoverable image I/O or decoding failure. It realizes the core `Erreur`
interface and exposes these text fields:

```text
opération
chemin
cause
```

### `ErreurCouleur`

A recoverable textual color parsing failure. It realizes the core `Erreur`
interface and exposes these text fields, matching the `opération`/`cause`
shape every other stdlib error type in this codebase uses (`ErreurJSON`,
`ErreurRegex`, `ErreurTemps`), plus the field the parse itself needs:

```text
opération
valeur
cause
```

`opération` is always `"couleur_hex"` today; it is carried for the same reason
the other error types carry it — so a second color-parsing function added
later does not force a breaking field-shape change. Because the shared
`stdlib_error_value` helper names its fourth field `chemin`, which does not
fit a color string, `ErreurCouleur` builds its error value with a small local
helper mirroring `stdlib_error_value`'s shape instead — the same choice the
JSON module made for its `ligne`/`colonne` fields. `ErreurImage` fits
`stdlib_error_value`'s existing shape exactly (`opération`/`chemin`/`cause`)
and uses it directly.

## Module surface

The following signatures are normative. They are written in compact reference
form; `stdlib/lumidessin.lum`, written as part of this implementation, must
carry the equivalent class and function declarations used by documentation
and tooling.

### Canvas creation

```text
fenêtre(largeur: Entier, hauteur: Entier, titre: Texte) -> Canevas
canevas(largeur: Entier, hauteur: Entier) -> Canevas
```

`fenêtre` creates the single visible window. It raises a runtime error if a
visible canvas is already open, the window system is unavailable, or creation
fails. A program generally cannot recover meaningfully from those conditions,
so window creation does not return `Résultat`.

The title is UTF-8 text and may be empty. The visible window's client area has
the requested logical size; window decorations are not included in that size.

`canevas` never initializes the window system. It may therefore be
used in tests, CI, containers, and servers.

Both functions validate dimensions before multiplying or allocating. Each
dimension must be in `1..16384`, and the product must not exceed 16,777,216
pixels (comfortably above a 4K display's pixel count). Allocation failure
becomes a Lumière runtime error rather than escaping as a C++ exception.

This ceiling exists for memory and overflow safety, not as a performance
guarantee. The rasterizer is a scalar CPU implementation (see Rendering core
below); a canvas anywhere near this size will not sustain `prochaine_image`'s
cadence. LumiDessin is sized for windowed programs at ordinary display
resolutions, not for large-canvas real-time rendering, consistent with its own
non-goals.

A new canvas is initialized to opaque white. Visible and off-screen canvases
therefore have the same initial pixels. Call `effacer(Couleurs.transparent)`
when a transparent exported image is wanted.

### Values and colors

```text
point(x: Décimal, y: Décimal) -> Point
couleur(rouge: Entier, vert: Entier, bleu: Entier, alpha: Entier = 255) -> Couleur
couleur_hex(valeur: Texte) -> Résultat[Couleur, ErreurCouleur]
```

Every color component must be in `0..255`. `alpha` defaults to `255`, so
the common case is `couleur(rouge, vert, bleu)`. `couleur_hex` accepts exactly
`#RRGGBB` or `#RRGGBBAA`, case-insensitively, and accepts no surrounding
whitespace. Six-digit input has alpha 255.

`LumiDessin.Couleurs` exposes immutable `Couleur` values:

```text
transparent, noir, blanc, gris, rouge, vert, bleu, jaune,
cyan, magenta, orange, violet, rose, brun
```

The named values are part of the API and have these exact RGBA components:

| Name | RGBA |
|---|---:|
| `transparent` | `(0, 0, 0, 0)` |
| `noir` | `(0, 0, 0, 255)` |
| `blanc` | `(255, 255, 255, 255)` |
| `gris` | `(128, 128, 128, 255)` |
| `rouge` | `(255, 0, 0, 255)` |
| `vert` | `(0, 255, 0, 255)` |
| `bleu` | `(0, 0, 255, 255)` |
| `jaune` | `(255, 255, 0, 255)` |
| `cyan` | `(0, 255, 255, 255)` |
| `magenta` | `(255, 0, 255, 255)` |
| `orange` | `(255, 165, 0, 255)` |
| `violet` | `(128, 0, 128, 255)` |
| `rose` | `(255, 192, 203, 255)` |
| `brun` | `(165, 42, 42, 255)` |

`Couleur` exposes read-only component methods:

```text
rouge() -> Entier
vert() -> Entier
bleu() -> Entier
alpha() -> Entier
```

### Images

```text
charger_image(chemin: Texte) -> Résultat[Image, ErreurImage]
```

LumiDessin reads and writes exactly one image format, PNG. That symmetry is
deliberate: every image a program loads was, realistically, produced by this
same module or by an ordinary export from a paint/diagram tool, and PNG covers
both without a second decoder (JPEG's DCT, Huffman tables, and chroma
subsampling) whose only output-side counterpart would be a decode path nothing
in the module ever exercises by writing.

Format detection uses file contents rather than the filename extension, so a
non-PNG file — including JPEG — is recognized and rejected as
`Échec(ErreurImage)` with a clear "format non pris en charge" cause, rather
than being misread or crashing. Images are converted to 8-bit RGBA in sRGB
order. Malformed data, oversized images, missing files, and read failures also
return `Échec(ErreurImage)`.

Relative paths are resolved from the process working directory. Image reads and
writes apply the same path-validation policy as the `Fichier` module and use
native Unicode-capable path APIs on each platform.

`Image` exposes:

```text
largeur() -> Entier
hauteur() -> Entier
```

Decoded images use the same dimension and pixel-count limits as canvases.

## `Canevas` API

### Identity and lifetime

```text
largeur() -> Entier
hauteur() -> Entier
est_visible() -> Logique
est_ouvert() -> Logique
fermer() -> Rien
```

`fermer` is idempotent. It destroys the native window, if any, and releases the
pixel buffer. `est_ouvert` is then false. Every other method raises
`Canevas.<méthode> ne peut pas utiliser un canevas fermé` when called after
closure.

Native destruction also closes an open canvas. A crayon does not keep a closed
canvas usable.

### Frame lifecycle

```text
régler_cadence(images_par_seconde: Entier) -> Rien
prochaine_image() -> Logique
écart_image() -> Décimal
présenter() -> Rien
attendre_fermeture() -> Rien
```

The default cadence is 60 frames per second. A cadence must be in `1..240` — generous headroom over every common display refresh rate, without pretending an interpreted animation loop can usefully target a four-figure frame rate.

For a visible canvas, `prochaine_image`:

1. waits until the next cadence deadline when necessary
2. clears transient input state
3. pumps all pending native window events
4. updates keyboard, mouse, text, wheel, and close state
5. records the elapsed monotonic time in seconds
6. returns false if closure was requested, otherwise true

When a native close request is observed, `prochaine_image` closes the canvas
before returning false. Escape is an ordinary key and does not close the window
implicitly.

The first call does not wait and reports an elapsed time of zero. If a frame
misses its deadline, the next deadline advances from the current monotonic time;
the function never performs catch-up frames. System clock changes do not affect
frame timing.

For an open off-screen canvas, `prochaine_image` performs cadence timing and
returns true. It has no input events. After `fermer`, the ordinary closed-canvas
rule applies and calling it is an error.

`écart_image` returns the value recorded by the most recent
`prochaine_image`. It is never negative.

`présenter` uploads the current framebuffer to the visible window. On an
off-screen canvas it is a valid no-op, allowing the same rendering loop to run
with or without a window. Presentation never clears the framebuffer.

`attendre_fermeture` presents the current framebuffer, then blocks while pumping
events until the user closes the visible window. Calling it on an off-screen
canvas is a runtime error. It is intended for static programs; animated programs
must use `prochaine_image`. It closes the canvas before returning.

No Lumière callback is invoked by the frame lifecycle. All calls occur on the
interpreter thread, and no rendering worker thread is created.

### Framebuffer operations

```text
effacer(couleur: Couleur) -> Rien
lire_pixel(x: Entier, y: Entier) -> Couleur
capturer() -> Image
enregistrer_png(chemin: Texte) -> Résultat[Rien, ErreurImage]
```

`effacer` replaces every pixel, including alpha; it does not alpha-blend.
`lire_pixel` uses integer pixel coordinates and rejects coordinates outside the
canvas. `capturer` returns a deep immutable copy. `enregistrer_png` writes the
current framebuffer as lossless RGBA PNG and never requires a visible window.

File creation, encoding, and write failures return `Échec(ErreurImage)`. The
function must not leave a successfully reported partial file. The implementation
writes beside the destination and atomically replaces it when the platform
supports that operation. If replacement is not atomic on a platform, the
platform limitation must be documented and failure must still be reported.

### Drawing primitives

```text
dessiner_pixel(x: Entier, y: Entier, couleur: Couleur) -> Rien

tracer_ligne(
    x1: Décimal, y1: Décimal,
    x2: Décimal, y2: Décimal,
    couleur: Couleur, épaisseur: Décimal
) -> Rien

tracer_rectangle(
    x: Décimal, y: Décimal,
    largeur: Décimal, hauteur: Décimal,
    couleur: Couleur, épaisseur: Décimal
) -> Rien

remplir_rectangle(
    x: Décimal, y: Décimal,
    largeur: Décimal, hauteur: Décimal,
    couleur: Couleur
) -> Rien

tracer_cercle(
    centre_x: Décimal, centre_y: Décimal,
    rayon: Décimal, couleur: Couleur, épaisseur: Décimal
) -> Rien

remplir_cercle(
    centre_x: Décimal, centre_y: Décimal,
    rayon: Décimal, couleur: Couleur
) -> Rien

tracer_ellipse(
    centre_x: Décimal, centre_y: Décimal,
    rayon_x: Décimal, rayon_y: Décimal,
    couleur: Couleur, épaisseur: Décimal
) -> Rien

remplir_ellipse(
    centre_x: Décimal, centre_y: Décimal,
    rayon_x: Décimal, rayon_y: Décimal,
    couleur: Couleur
) -> Rien

tracer_arc(
    centre_x: Décimal, centre_y: Décimal,
    rayon: Décimal, angle_début: Décimal, amplitude: Décimal,
    couleur: Couleur, épaisseur: Décimal
) -> Rien

tracer_polyligne(
    points: Liste[Point], fermée: Logique,
    couleur: Couleur, épaisseur: Décimal
) -> Rien

remplir_polygone(points: Liste[Point], couleur: Couleur) -> Rien
```

All primitive operations modify the canvas immediately. Later operations draw
over earlier ones. Drawing is clipped to the canvas; geometry wholly or partly
outside it is not an error.

Polylines require at least two points. Filled polygons require at least three.
Polygon fill uses the even-odd rule and accepts concave and self-intersecting
paths. When `fermée` is true, the polyline includes a final segment from its
last point to its first. Lines and outlines have round caps and round joins.

Every decimal argument must be finite. Widths, heights, and radii must be
non-negative, and stroke thickness (`épaisseur`) must be non-negative too.
Zero-area filled shapes and zero-thickness strokes both do nothing — the two
cases are symmetric on purpose, since a program computing a size or thickness
from other values will eventually compute zero, and that should never be a
crash. A *negative* value, for a fill dimension or for stroke thickness, is
what raises a runtime error at the Lumière call site.

### Text

```text
dessiner_texte(
    texte: Texte, x: Décimal, y: Décimal,
    taille: Entier, couleur: Couleur
) -> Rien

mesurer_texte(texte: Texte, taille: Entier) -> Dimensions
```

`x` and `y` identify the upper-left corner of the text box. `taille` is the
font's em height in logical pixels and must be in `1..1024`. Newline characters
start new lines. Tabs advance to the next four-space tab stop.

LumiDessin embeds one redistributable font (Inter-Regular.ttf). It must
contain the French alphabet, ASCII, and common punctuation. A missing glyph
renders as the font's own `.notdef` glyph — a deliberately drawn hollow box in
Inter, not an empty shape — rather than a lookup of U+FFFD in the font's
character map: most fonts, Inter included, have no cmap entry for the
replacement character itself, so "missing glyphs render as the replacement
glyph" means rendering whichever glyph the rasterizer already resolves an
unmapped codepoint to, unconditionally, not a second lookup that would just as
often fail. Text measurement and rendering use the same shaping and
line-spacing path, so the returned dimensions enclose the rendered pixels.
Advanced script shaping — complex-script reordering, ligatures beyond what the bundled font's own glyph table provides — is out of scope for a module whose text drawing exists for labels and diagrams, not document layout.

### Images

```text
dessiner_image(image: Image, x: Décimal, y: Décimal) -> Rien

dessiner_image_redimensionnée(
    image: Image, x: Décimal, y: Décimal,
    largeur: Décimal, hauteur: Décimal,
    opacité: Décimal
) -> Rien

dessiner_image_nette(
    image: Image, x: Décimal, y: Décimal,
    largeur: Décimal, hauteur: Décimal,
    opacité: Décimal
) -> Rien
```

The unscaled operation draws one source pixel per logical canvas pixel. The
redimensioned operation uses bilinear sampling. The `nette` variant uses
nearest-neighbor sampling for pixel art. `opacité` must be finite and in
`0.0..1.0`. Width and height follow the geometry validation rules above.

### Input

```text
touche_enfoncée(touche: Texte) -> Logique
touche_pressée(touche: Texte) -> Logique
touche_relâchée(touche: Texte) -> Logique
texte_saisi() -> Texte

position_souris() -> Point
souris_présente() -> Logique
bouton_enfoncé(bouton: Texte) -> Logique
bouton_pressé(bouton: Texte) -> Logique
bouton_relâché(bouton: Texte) -> Logique
défilement() -> Point
```

Input is a snapshot produced by the most recent `prochaine_image` call. Before
the first call, all state is neutral: no keys or buttons, empty text, zero
wheel movement, and mouse position `(0, 0)`.

`enfoncée`/`enfoncé` describe current state. `pressée`/`pressé` and
`relâchée`/`relâché` describe transitions observed during the latest event
pump. A press and release received in the same pump make both transition
queries true while current state is released.

Canonical key names identify a **physical key position**, not the character it
produces — the same convention as SDL's `SDL_Scancode`. `touche_enfoncée("a")`
is true when the key in the QWERTY "A" position is held down, regardless of
keyboard layout. On an AZERTY keyboard — the layout most Lumière programs will
actually run under, since the language's own syntax and diagnostics are French
— that is the key labelled "Q". This is the right default for movement keys (a
WASD-style game binds the same four physical keys under any layout), and it is
also the cheapest to implement correctly: SDL3 reports scancodes directly, so
no per-layout translation table is needed.

`texte_saisi()` is the deliberate exception to this rule: it reports the
characters the operating system actually produced, honoring layout and IME
composition, because its purpose is capturing what the user typed, not which
physical keys they pressed.

```text
a..z, 0..9, espace, entrée, échappement, tabulation, retour_arrière,
supprimer, gauche, droite, haut, bas, début, fin, page_haut, page_bas,
f1..f12, majuscule, contrôle, option, commande
```

`option` maps to Alt on Windows and Linux. `commande` maps to Command on macOS
and the Windows/Super key elsewhere. Unknown names are runtime errors rather
than silently returning false.

Canonical mouse button names are `gauche`, `milieu`, `droite`, `x1`, and `x2`.

Mouse and wheel coordinates are logical canvas pixels with the same origin and
axes as direct drawing. Mouse coordinates may be outside canvas bounds;
`souris_présente` reports whether the pointer is currently inside. Positive
wheel `y` means upward, and positive wheel `x` means rightward.

Input methods on an off-screen canvas return neutral state. They do not raise.

### Crayon creation

```text
crayon() -> Crayon
```

Each call creates independent crayon state bound to the receiver canvas.

## `Crayon` API

```text
avancer(distance: Décimal) -> Rien
reculer(distance: Décimal) -> Rien
tourner_gauche(angle: Décimal) -> Rien
tourner_droite(angle: Décimal) -> Rien
aller_à(x: Décimal, y: Décimal) -> Rien
recentrer() -> Rien

lever() -> Rien
baisser() -> Rien
est_baissé() -> Logique

régler_couleur(couleur: Couleur) -> Rien
régler_épaisseur(épaisseur: Décimal) -> Rien
régler_cap(angle: Décimal) -> Rien

position() -> Point
cap() -> Décimal

montrer() -> Rien
cacher() -> Rien
est_visible() -> Logique
```

A new crayon starts at `(0, 0)`, points right at 0 degrees, is lowered, is
black, has thickness 1, and is visible.

Crayon coordinates differ intentionally from direct canvas coordinates:

- origin at the canvas center
- positive x to the right
- positive y upward
- positive headings counterclockwise
- 0 degrees points right

The exact mapping to a canvas point is:

```text
canvas_x = canvas_width / 2 + crayon_x
canvas_y = canvas_height / 2 - crayon_y
```

This preserves the conventional mathematical coordinate system used in Turtle
teaching while leaving the direct API in ordinary image coordinates.

`avancer` accepts negative distances; `reculer(d)` is exactly
`avancer(-d)`. Movement draws one line with the current style when the pen is
lowered. `aller_à` and `recentrer` also draw when lowered. Angles and distances
must be finite. Reported headings are normalized to `0 <= cap < 360`.
`recentrer` resets both position and heading -- (0, 0) and 0 degrees -- the
same "home" semantics as the Turtle/Logo traditions this API follows;
`régler_cap`/`tourner_gauche`/`tourner_droite` are the only way to change
heading without also moving.

The visible crayon cursor is an overlay drawn only when presenting a visible
canvas. It is not written into the framebuffer and therefore does not appear in
`capturer` or `enregistrer_png`. Multiple visible crayons are all overlaid, in
the order their `crayon()` calls were made — later crayons draw over earlier
ones. Draw order is part of the contract, not an implementation detail: it is
observable, so it must be identical under both engines.

A crayon operation after its canvas is closed raises a runtime error at the
crayon call site.

## Coordinate and pixel semantics

Direct drawing uses logical pixel coordinates:

- origin `(0, 0)` at the upper-left corner
- positive x to the right
- positive y downward
- pixel `(x, y)` occupies `[x, x + 1) × [y, y + 1)`
- integer pixel centers are at `(x + 0.5, y + 0.5)`

Arc angle zero points right. Positive arc amplitude proceeds clockwise, matching
the direct coordinate system. Negative amplitude proceeds counterclockwise.
Amplitudes whose magnitude exceeds 360 draw repeated coverage only once; they
are clamped to one complete revolution.

The logical canvas size never changes. A visible backend may scale the canvas
for high-density displays, but it must preserve aspect ratio, use letterboxing
when necessary, and map input back to logical coordinates. LumiDessin windows
are not user-resizable.

Shapes are antialiased. Exact edge coverage is an implementation detail, but
the same software rasterizer must serve visible and off-screen canvases so they
cannot disagree semantically.

## Compositing

Drawing uses source-over alpha compositing in call order. The public color and
image format is straight RGBA8. An implementation may store premultiplied
channels internally, provided round trips through `lire_pixel`, `capturer`, and
PNG preserve the defined result.

The reference channel equation, with values normalized to `0..1`, is:

```text
out_a   = src_a + dst_a * (1 - src_a)
out_rgb = (src_rgb * src_a + dst_rgb * dst_a * (1 - src_a)) / out_a
```

When `out_a` is zero, output RGB is zero. Conversion back to bytes rounds to
the nearest integer, with halves away from zero. Blending is performed in sRGB component space; this is the one compositing
rule that is explicitly load-bearing rather than an implementation detail,
because it changes the value of every pixel. A linear-light mode is out of
scope: it would need to be a separately named function, never a change in
what this one computes.

## Failure policy

The API distinguishes programmer errors from recoverable external failures.

Runtime errors are used for:

- invalid dimensions, geometry, colors, opacity, or cadence
- non-finite numeric values
- too few polygon points
- unknown key or mouse button names
- use after close
- opening a second visible window
- attempting a visible window where window support is unavailable
- native allocation failure

`Résultat` is used for:

- parsing a color string
- reading or decoding an image
- encoding or writing a PNG

Every runtime error names the operation and violated value. Native code uses
the incoming `RuntimeSite`; it never prints directly, terminates the process,
or exposes a backend exception.

No C++ exception from allocation, codecs, the window backend, or the standard
library may cross the native ABI. It must become either the documented
`Résultat` failure or a Lumière runtime error.

## Native architecture

LumiDessin follows the shared stdlib architecture in
[`stdlib-backend-architecture.md`](./stdlib-backend-architecture.md).

### Shared runtime entry point

Both execution engines call one `register_lumidessin_module` implementation.
The module receives normalized `NativeArgs`, reports errors through `IRuntime`,
and contains no AST, environment, opcode, or VM dependency.

The initial file layout is:

```text
stdlib/lumidessin.lum
src/interpreter/stdlib/lumidessin.cpp
src/interpreter/stdlib/lumidessin/
    state.hpp
    values.cpp
    raster.cpp
    text.cpp
    image.cpp
    input.cpp
    window.cpp
tests/test_lumidessin.cpp
tests/fixtures/interpreter/lumidessin/
```

Split a source file only when it has a distinct responsibility. The layout is
not permission to create wrappers containing no logic.

### Native state

The implementation uses these native states:

```text
LumiDessinModuleState
  owns window subsystem lifetime and the identity of the visible canvas,
  as a raw, non-owning CanvasState*

CanvasState : NativeState
  owns dimensions, RGBA framebuffer, frame clock, input snapshot,
  optional platform window, open flag, and a list of raw, non-owning
  CrayonState* for overlay drawing, in crayon-creation order

ImageState : NativeState
  owns immutable dimensions and RGBA pixels

ColorState : NativeState
  owns four immutable bytes

CrayonState : NativeState
  owns a strong Ref<CanvasState> and its independent cursor state
```

The module and native object states must obey Lumière's intrusive reference
counting and cycle collector contracts. None stores a Lumière `Value` callback,
so its `trace_references` implementation is empty on every one of them.

Native-to-native ownership must be acyclic: a `CrayonState` strongly owns the
`CanvasState` it draws on (`Ref<CanvasState>`), so the canvas cannot be
destroyed while a crayon referencing it still exists. (This is orthogonal to
`fermer()`: closing a canvas sets its open flag and releases the window and
framebuffer while it may still be kept alive by a crayon's `Ref`; every method
on it, crayon included, then raises the documented use-after-close error.) The
reverse reference — a canvas's list of crayons to overlay at present time — is
never a `Ref`: this codebase has no weak-pointer type, and a strong reference
in both directions would be an uncollectable native-to-native cycle, since the
cycle collector traces Lumière `Value`s reachable through `trace_references`,
not raw C++ pointers between native states. So the mechanism is concrete, not
"weak" in name only:

- `CrayonState`'s constructor registers `this` into its canvas's overlay list.
- `CrayonState`'s destructor removes `this` from that list.
- A `CrayonState*` in that list is only ever dereferenced from a present-time
  overlay draw driven by its owning `CanvasState`, and a `CanvasState` is only
  reachable there while at least one live `Ref<CanvasState>` — held by one of
  its own registered crayons — keeps it alive. There is consequently no window
  in which the list can hold a dangling pointer.

The same shape, and the same reasoning, applies to `LumiDessinModuleState`'s
non-owning pointer to the current visible `CanvasState`: the canvas is what a
Lumière `Value` keeps alive; the module state only remembers which one it is,
and `CanvasState`'s destructor clears that pointer if it is still the one
recorded.

All nominal types use `native_nominal_type_identity("LumiDessin", type_name)`.
Native methods obtain state through a checked helper equivalent to LumiNet's
`require_native_state`; forged or corrupt objects become Lumière runtime errors.

### Rendering core

One CPU rasterizer writes every canvas framebuffer. The visible backend only
creates a window, translates input, and presents that buffer. It must not use a
second geometry implementation supplied by a GPU API, because visible and
headless output would then diverge.

The raster core operates on clipped spans, performs checked indexing, and never
allocates per pixel. Geometry is clipped before conversion to integer bounds so
extreme finite coordinates cannot overflow.

Image decoding and font parsing treat their input as hostile. They check
dimensions, lengths, multiplication, allocation sizes, and decoder results.

### Platform backend

The platform layer is private. Its types must not appear in public Lumière
headers or in the language API. All native dependencies below are pinned,
license-compatible (each permissive — zlib, MIT/public-domain, OFL-1.1 for the
font — none copyleft), vendored via CMake `FetchContent` (the pattern already
used for googletest), and included in source and binary release notices:

- **SDL3** — window creation, input, and presentation. The one window/input
  adapter; its types must not appear in public Lumière headers or in the
  language API.
- **`stb_image.h`** / **`stb_image_write.h`** (public domain / MIT-0) — PNG
  decode and encode. Single-header, no build-system footprint beyond vendoring
  the file, and the library a large fraction of small game and graphics tools
  already use for exactly this job.
- **`stb_truetype.h`** (public domain / MIT-0) — rasterizes the bundled font's
  glyphs into coverage bitmaps that the CPU rasterizer composites like any
  other drawing.
- **Inter, static Regular weight, OFL-1.1** — the bundled font. Chosen for full
  Latin Extended coverage (every accented character French text needs), a
  permissive license that allows embedding and redistribution, and wide
  existing use, so its metrics and rendering are well exercised elsewhere. Its
  license text ships in the release's font-licenses notice alongside SDL3's
  and the stb libraries'.

The CPU rasterizer, PNG encoder/decoder, and bundled font remain usable when
window support is disabled. CMake exposes:

```text
LUMIERE_ENABLE_LUMIDESSIN=ON
LUMIERE_ENABLE_LUMIDESSIN_WINDOW=ON
```

Disabling the first option removes the module. Disabling only the second keeps
off-screen drawing and image I/O; `fenêtre` then raises a clear availability
error. Official desktop releases enable both.

Visible canvas operations must run on the process main thread, satisfying the
strictest supported window platform. The CLI already runs `principal` there;
LumiDessin must not move interpretation to another thread.

### Semantic signatures and documentation

Until native-module metadata has one source of truth, every API addition must
update both:

- `stdlib/lumidessin.lum`, for public declarations and documentation
- `native_module_exports`, for semantic analysis

A parity test compares exported names, arity, parameter names, optionality, and
types. This prevents the two descriptions from drifting. LumiDessin must not
introduce a general metadata generator merely to avoid this small duplication.

## Testing contract

### Pure unit tests

Tests cover:

- checked canvas and image size calculations
- color parsing and component bounds
- alpha blending, including transparent destination and zero alpha
- clipping for every primitive
- degenerate and non-finite geometry
- line caps and joins
- concave and self-intersecting polygon fills
- nearest and bilinear image sampling
- Turtle-to-canvas coordinate conversion
- heading normalization and negative movement
- frame-deadline calculation without real sleeps
- key and mouse name mapping
- malformed and oversized image input
- PNG round trips
- use after close and idempotent close

Time and native events enter through small injectable seams. Unit tests do not
sleep and do not require a real display.

### Golden rendering tests

Small off-screen fixtures render each primitive, overlapping alpha, clipping,
text with accented French characters, images, and crayon paths. Tests compare
the RGBA buffer and encoded PNG against versioned fixtures.

Fixture changes require visual review. A renderer change may not rewrite all
goldens without an explanation of the intended semantic difference.

### Language conformance tests

Every public function and method is exercised from Lumière source under both
the tree-walker and VM. The engines must agree on:

- returned values
- visible object types
- named-argument binding
- runtime error text and source coordinates
- `Résultat` success and failure types
- framebuffer hashes for headless examples

The conformance suite always uses off-screen canvases and therefore runs in
ordinary headless CI.

### Platform tests

Linux, macOS, and Windows builds test module registration and off-screen
rendering. A bounded visible-window smoke test creates, presents, injects or
receives a close event, and exits. Linux may run that smoke test under a virtual
display. Platform tests must never wait for human input.

Sanitizer runs cover repeated creation and destruction, image decode failures,
canvas/crayon lifetime ordering, and forced errors. The test suite verifies that
window resources, native states, and temporary output files are released.

## Implementation order

This is a build sequence for one implementation, not a phased release plan:
every stage below is a checkpoint inside a single effort that only ends at
Definition of done, and nothing is usable or shippable at an earlier stage.
Each stage ends with tests and works under both execution engines.

1. Register the module, native nominal types, semantic signatures, and build
   options. -- DONE
2. Implement colors, points, checked dimensions, off-screen canvas lifetime,
   clear, pixel read/write, capture, and alpha composition. -- DONE
3. Implement clipped lines, rectangles, circles, ellipses, arcs, polylines,
   and polygons. -- DONE
4. Implement the bundled font, text measurement, and text drawing. -- DONE
5. Implement PNG loading, image drawing, and atomic PNG export. -- DONE
6. Implement `Crayon` over the completed canvas primitives. -- DONE
7. Add the SDL3 visible backend, presentation, frame pacing, and close
   handling. -- DONE. `fenêtre`, `régler_cadence`,
   `prochaine_image`, `écart_image`, `présenter`, and
   `attendre_fermeture` are implemented in `window.cpp`, the module's only
   translation unit that includes an SDL3 header (pImpl'd behind
   `PlatformWindow` in `state.hpp`, so nothing else in the module or its
   public headers depends on SDL). Frame-deadline arithmetic
   (`compute_frame_wait`) is a pure function, unit-tested without a real
   clock or sleep; window creation, presentation, and both the
   Lumière-side (`fermer()`) and native (an injected `SDL_EVENT_QUIT`)
   close paths are exercised under SDL's dummy video driver in ordinary
   headless CI (`tests/test_interpreter_fixtures.cpp`'s `LumiDessinFenetre*`
   tests). `LUMIERE_ENABLE_LUMIDESSIN_WINDOW=OFF` still builds and raises the
   documented availability error from `fenêtre()`.
8. Add keyboard, text, mouse, and wheel snapshots. -- DONE, in `input.cpp`
   (reads only the plain `InputSnapshot` in `state.hpp`; `window.cpp`'s event
   pump is the only place a platform code becomes one of the canonical
   French names). Off-screen canvases return the documented neutral state
   without raising; unknown key/button names raise.
9. Complete cross-platform, sanitizer, leak, documentation, and example
   checks. -- Linux only so far: AddressSanitizer/UndefinedBehaviorSanitizer
   pass with no defect (including a 25-cycle window open/close/crayon
   stress run and a forced already-open error path), and
   `scripts/conformance`/`scripts/fuzz` are clean against both engines.
   `examples/lumidessin_window_demo.lum` is the module's one interactive,
   non-headless example. macOS and Windows window smoke tests are not yet
   run from this environment -- left for a machine that can build and
   exercise those backends.

Window work comes late on purpose. Most semantics can be made correct and fully
testable before platform event loops enter the system.

## Definition of done

LumiDessin is complete only when:

- every public symbol in this document exists and is documented
- examples run unchanged with both execution engines
- all drawing is available without a display
- visible and off-screen canvases share one rasterizer
- all errors follow the documented runtime-error or `Résultat` policy
- the full native test suite and language conformance suite pass
- macOS, Linux, and Windows builds pass their window smoke test
- AddressSanitizer and UndefinedBehaviorSanitizer find no defect
- closing windows and collecting canvases, images, colors, and crayons leaks no
  native resource
- release packages contain the window dependency and font licenses
- the implemented API has no undocumented global state

Anything beyond this contract is out of scope for the module, not unfinished work still to come.
