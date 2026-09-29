# LumiNet Stdlib

## Purpose

This note describes Lumiere's networking standard library.

The goal is ambitious surface area with simple usage:

- one import for networking
- one coherent naming style
- layered access from high-level HTTP down to raw sockets
- an implementation that remains backend-neutral and readable

Recoverable address, DNS, connection, timeout, protocol, HTTP, and socket I/O
failures are returned as `Résultat`. Wrong arity, wrong argument types, invalid
port ranges, and violated object invariants remain contract errors.

## Core direction

The right mental model is:

- `LumiNet` is the networking umbrella module
- users import it once
- capabilities are grouped into small submodules
- higher layers are convenience built on lower layers, not separate worlds

That means:

- `importer LumiNet`
- then `LumiNet.HTTP`, `LumiNet.Canal`, `LumiNet.TCP`, `LumiNet.UDP`, `LumiNet.DNS`, `LumiNet.Adresse`

This preserves the "one module" philosophy without flattening every operation
into the root namespace.

## Error types

- `LumiNet.ErreurAdresse`
- `LumiNet.ErreurDNS`
- `LumiNet.ErreurConnexion`
- `LumiNet.ErreurDélai`
- `LumiNet.ErreurIO`
- `LumiNet.ErreurProtocole`
- `LumiNet.ErreurHTTP`

Each value records the failed operation and a stable public category while
retaining the platform cause as diagnostic text. Platform-specific socket
codes do not become public type identities.

## Strong design decisions

These are the recommended final decisions.

### 1. Keep one import, but avoid root-level alias clutter

Recommended form:

```lumiere
importer LumiNet

soit réponse = agir selon LumiNet.HTTP.obtenir("http://example.com") {
    Succès(valeur) -> valeur
    Échec(erreur) -> retourne Échec(erreur)
}
soit serveur = LumiNet.HTTP.Serveur()
soit socket = agir selon LumiNet.UDP.ouvrir(9001) {
    Succès(valeur) -> valeur
    Échec(erreur) -> retourne Échec(erreur)
}
```

Avoid:

- `LumiNet.obtenir(...)`
- `LumiNet.créer(...)`
- `LumiNet.Serveur()`
- multiple spellings of the same operation

Why:

- it scales much better as the module grows
- it keeps HTTP, TCP, UDP, DNS, and Canal visually separated
- it matches our no-alias preference elsewhere in stdlib design

The import is still only one line, so the ergonomic goal remains intact.

### 2. Keep French verbs, preserve protocol acronyms

Good examples:

- `obtenir`
- `créer`
- `modifier`
- `supprimer`
- `écouter`
- `résoudre`
- `définir_délai`

Keep acronyms as proper nouns:

- `HTTP`
- `TCP`
- `UDP`
- `DNS`
- `TLS`

This preserves clarity and avoids awkward fake translations.

### 3. Use native objects with methods, not backend-specific magic

Networking values should be native runtime objects:

- `RéponseHTTP`
- `ServeurHTTP`
- `RequêteHTTP`
- `RéponseServeurHTTP`
- `CanalClient`
- `ServeurCanal`
- `ConnexionTCP`
- `ServeurTCP`
- `SocketUDP`
- `PaquetUDP`
- `AdresseRéseau`

Each should be represented like the current `Temps` objects:

- a shared `Value::objet(...)`
- fields for state
- native methods bound onto the object

This is compatible with both the tree walker and the future VM.

### 4. Runtime bridge must stay narrow

The stdlib implementation must not depend directly on:

- AST nodes
- tokens
- environments
- VM stacks
- opcodes

Networking stdlib code may depend on:

- shared runtime values
- `IRuntime`
- `NativeArgs`
- `RuntimeSite`

If the networking library ever needs backend-specific branches, that is an
architectural bug.

### 5. Start synchronous, then layer events deliberately

Networking grows complexity very fast.

So the implementation order should be:

1. address parsing and validation
2. DNS resolution
3. blocking TCP client/server
4. blocking UDP sockets
5. blocking HTTP client
6. HTTP server
7. Canal / WebSocket

This order matters.

It gives us useful networking early without forcing a large event runtime
before we are ready.

## Canonical surface

## `LumiNet.HTTP`

### Client

- `LumiNet.HTTP.obtenir(url, ...)`
- `LumiNet.HTTP.créer(url, ...)`
- `LumiNet.HTTP.modifier(url, ...)`
- `LumiNet.HTTP.supprimer(url, ...)`
- `LumiNet.HTTP.requête(méthode, url, ...)`

The generic `requête(...)` should exist even if docs lead with convenience
verbs. It keeps the design complete and avoids painting us into a corner for
future methods such as `PATCH`.

Recommended request options:

- `entêtes: Dictionnaire[Texte, Texte]`
- `corps: Texte`
- `délai: Durée`

Not yet:

- multipart forms
- streaming request bodies
- binary request bodies
- cookie jars

Those are valid later additions, but not a good first target.

### `RéponseHTTP`

Recommended fields:

- `statut: Entier`
- `corps: Texte`
- `succès: Logique`

Recommended methods:

- `entête(nom: Texte) -> Texte`
- `entêtes() -> Dictionnaire[Texte, Texte]`

Recommendation:

- treat missing headers as `""`
- expose headers case-insensitively

### Server

Recommended constructor:

- `LumiNet.HTTP.Serveur()`

Recommended route methods:

- `OBTENIR(chemin, gestionnaire)`
- `CRÉER(chemin, gestionnaire)`
- `MODIFIER(chemin, gestionnaire)`
- `SUPPRIMER(chemin, gestionnaire)`

Recommended lifecycle methods:

- `avant(middleware)`
- `écouter(hôte: Texte, port: Entier)`
- `arrêter()`

Future-friendly addition worth reserving:

- `écouter_sur(adresse: AdresseRéseau)`

### `RequêteHTTP`

Recommended fields:

- `méthode: Texte`
- `chemin: Texte`
- `corps: Texte`

Recommended methods:

- `paramètre(nom: Texte) -> Texte`
- `requête(nom: Texte, défaut: Texte = "") -> Texte`
- `entête(nom: Texte) -> Texte`
- `entêtes() -> Dictionnaire[Texte, Texte]`

Recommendation:

- keep route params and query params separate
- do not make `requête(...)` throw when missing if a default is supplied

### `RéponseServeurHTTP`

Recommended methods:

- `envoyer(statut: Entier, corps: Texte)`
- `envoyer_json(statut: Entier, corps: Texte)`
- `envoyer_fichier(statut: Entier, chemin: Texte)`
- `définir_entête(nom: Texte, valeur: Texte)`
- `rediriger(url: Texte, statut: Entier = 302)`

Important semantic rule:

- once a response is sent, sending again should raise a runtime error

That avoids silent double-write bugs.

## `LumiNet.Canal`

`Canal` is a good name.

It is short, French, and conceptually accurate without forcing users to think
about WebSocket first.

Recommended surface:

- `LumiNet.Canal.connecter(url, ...)`
- `LumiNet.Canal.Serveur()`

### `CanalClient`

Recommended methods:

- `envoyer(message: Texte)`
- `envoyer_octets(données: Liste[Entier])`
- `fermer(code: Entier = 1000, raison: Texte = "")`
- `est_connecté() -> Logique`
- `attendre()`

Recommended event hooks:

- `quand_ouvert(callback)`
- `quand_message(callback)`
- `quand_fermé(callback)`
- `quand_erreur(callback)`

Recommendation:

- standardize on `octets`, not `binaire`

That matches the rest of stdlib naming better.

### `ServeurCanal`

Recommended methods:

- `quand_connexion(callback)`
- `quand_message(callback)`
- `quand_déconnexion(callback)`
- `quand_erreur(callback)`
- `écouter(hôte: Texte, port: Entier)`
- `arrêter()`

### Mixed HTTP + Canal

Good feature, worth keeping:

- `serveur.canal("/chat", gestionnaire)`

But this should be implemented only after plain HTTP server support is solid.

## `LumiNet.TCP`

Recommended surface:

- `LumiNet.TCP.connecter(hôte, port, ...)`
- `LumiNet.TCP.Serveur()`

Recommended connect options:

- `délai: Durée`

### `ConnexionTCP`

Recommended fields:

- `adresse: Texte`
- `port: Entier`

Recommended methods:

- `lire() -> Texte`
- `lire_octets(n: Entier) -> Liste[Entier]`
- `lire_ligne() -> Texte`
- `écrire(données: Texte)`
- `écrire_octets(données: Liste[Entier])`
- `fermer()`
- `est_connecté() -> Logique`
- `définir_délai(durée: Durée)`

Important rule:

- `lire()` should be clearly documented

Recommendation:

- make `lire()` mean "read whatever is currently available or until peer close"
- encourage `lire_ligne()` or `lire_octets(n)` when boundaries matter

Without that clarification, users will project incompatible expectations onto
the API.

### `ServeurTCP`

Recommended methods:

- `quand_connexion(callback)`
- `écouter(hôte: Texte, port: Entier)`
- `arrêter()`

The callback should receive one `ConnexionTCP`.

## `LumiNet.UDP`

Recommended surface:

- `LumiNet.UDP.ouvrir(port: Entier = 0)`

### `SocketUDP`

Recommended fields:

- `port: Entier`

Recommended methods:

- `envoyer(données: Texte, adresse: Texte, port: Entier)`
- `envoyer_octets(données: Liste[Entier], adresse: Texte, port: Entier)`
- `recevoir() -> PaquetUDP`
- `recevoir_octets() -> PaquetUDP`
- `diffuser(données: Texte, port: Entier)`
- `diffuser_octets(données: Liste[Entier], port: Entier)`
- `fermer()`
- `définir_délai(durée: Durée)`

### `PaquetUDP`

Recommended fields:

- `données: Texte`
- `adresse: Texte`
- `port: Entier`

Recommendation:

- if `recevoir_octets()` is used, return octets in a dedicated `octets` field
  instead of overloading `données`

Preferred clean form:

- `PaquetUDP.texte`
- `PaquetUDP.octets`
- `PaquetUDP.adresse`
- `PaquetUDP.port`

But if we want to keep the object smaller, then document that only one payload
field is meaningful depending on the receive mode.

## `LumiNet.DNS`

Recommended surface:

- `LumiNet.DNS.résoudre(hôte)`
- `LumiNet.DNS.résoudre_tous(hôte)`
- `LumiNet.DNS.résoudre_inverse(ip)`

Do not include:

- `joignable(hôte)`

Why:

- "reachable" is not DNS
- it leaks transport semantics into the naming
- users will argue endlessly about what "joignable" really means

If we ever want that feature, it belongs somewhere else with explicit protocol
semantics such as:

- `LumiNet.TCP.peut_connecter(...)`
- or a future diagnostics module

## `LumiNet.Adresse`

Recommended surface:

- `LumiNet.Adresse.analyser(texte)`
- `LumiNet.Adresse.locale()`
- `LumiNet.Adresse.est_valide(texte)`
- `LumiNet.Adresse.est_ipv4(texte)`
- `LumiNet.Adresse.est_ipv6(texte)`
- `LumiNet.Adresse.est_locale(texte)`

### `AdresseRéseau`

Recommended fields:

- `hôte: Texte`
- `port: Entier`

Recommended method:

- `en_texte() -> Texte`

Avoid relying only on `adresse en Texte` magic in the docs as the primary form.
An explicit method is easier to specify and simpler to keep consistent across
backends.

## Error model

Recommended error families:

- `ErreurRéseau`
- `ErreurDélai`
- `ErreurTLS`
- `ErreurProtocole`
- `ErreurCanal`
- `ErreurDNS`
- `ErreurAdresse`

These should ultimately be proper Lumiere error values or classes.

Until that full error hierarchy exists, diagnostics must still clearly include:

- which `LumiNet.*` operation failed
- the host, URL, or port when relevant
- whether the failure was timeout, address, protocol, DNS, or connection related

## What not to do in v1

To keep this library readable and implementable, v1 should not try to do all
of the following:

- HTTP streaming bodies
- chunked manual streaming APIs
- TLS certificate inspection APIs
- cookie jar management
- proxy support
- HTTP/2 or HTTP/3 surface
- concurrent multiplexing abstractions
- generic poll/select/epoll-style primitives

Those are valid future areas.

They are not good first-shipping scope.

## Implementation architecture

The shared stdlib architecture still applies here.

The design should be:

1. stdlib semantic entrypoints in one place
2. opaque per-object native state stored behind shared object fields or module state
3. runtime callback invocation through `IRuntime::call(...)`
4. zero direct dependency on tree-walker internals

That implies two useful implementation patterns.

### Pattern 1: native state wrappers

Each networking object can hold:

- visible Lumiere fields and methods
- hidden native state keys such as `__handle`, `__closed`, or `__kind`

The hidden state should stay behind helper functions in `src/interpreter/stdlib/luminet.cpp`,
not spread across the runtime.

### Pattern 2: callback registry on objects

Server and channel objects will need stored callbacks:

- `quand_connexion`
- `quand_message`
- `quand_erreur`

Those should be stored as ordinary `Value` fields or in a small native state
bundle associated with the object.

When an event occurs, the backend-neutral stdlib implementation should invoke
the registered callback through `runtime.call(...)`.

## Recommended delivery phases

### Phase 1

- `LumiNet.Adresse`
- `LumiNet.DNS`

### Phase 2

- blocking `LumiNet.TCP.connecter`
- `ConnexionTCP`

### Phase 3

- `ServeurTCP`
- `LumiNet.UDP`

### Phase 4

- HTTP client via `LumiNet.HTTP`

### Phase 5

- `LumiNet.HTTP.Serveur`
- routing
- middleware

### Phase 6

- `LumiNet.Canal`
- HTTP upgrade support

This sequence gives us useful wins early and keeps risk controlled.

## Final recommendation

The right target is:

- one imported umbrella module
- submodule-based organization
- no alias-heavy flat root API
- synchronous core first
- evented server features layered afterward
- strict backend-neutral native implementation

That keeps the library smart and simple in exactly the way Go's networking
packages feel smart and simple: not because they are tiny, but because the
surface stays organized while the power grows.
