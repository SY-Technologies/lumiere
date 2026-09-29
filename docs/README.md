# Lumiere Docs

This folder holds short design notes: how Lumiere is built, subsystem by
subsystem, and the C++ techniques used to build it and why they were chosen.
Each file sticks to one area, so the docs can grow alongside the codebase
instead of calcifying into one big stale overview.

## Suggested reading order

- [Codebase Reading Guide](./codebase-reading-guide.md)
- [Language Reference](../lumiere_spec.md)
- [Command-Line Reference](./cli.md)
- [Tooling Architecture](./tooling-architecture.md)
- [Release Scaffolding](./release-scaffolding.md)
- [Architecture Overview](./architecture-overview.md)
- [Stdlib / Backend Architecture](./stdlib-backend-architecture.md)
- [Implemented Language Overview](./implemented-language-overview.md)
- [Texte Stdlib](./stdlib-texte.md)
- [Chemin Stdlib](./stdlib-chemin.md)
- [Fichier Stdlib](./stdlib-fichier.md)
- [Temps Stdlib](./stdlib-temps.md)
- [Aléatoire Stdlib](./stdlib-aleatoire.md)
- [LumiNet Stdlib](./stdlib-luminet.md)
- [LumiDessin Design](./stdlib-lumidessin.md)
- [Foundational Standard Library Design](./stdlib-foundations.md)
- [Value and Runtime Data](./value-and-runtime-data.md)
- [Lexer Pipeline](./lexer-pipeline.md)
- [Parser and AST](./parser-and-ast.md)
- [Tree-Walker Interpreter](./tree-walker-interpreter.md)
- [Tree-Walker Backlog](./tree-walker-backlog.md)
- [VM Design](./vm-design.md)
- [VM Interpreter](./vm-interpreter.md)
- [Runtime Diagnostic Locations](./runtime-diagnostic-locations.md)
- [VM Roadmap](./vm-roadmap.md)
- [C++ Patterns Used Here](./cpp-patterns-used.md)

## Doc style

Each doc covers what problem the subsystem solves, how it's structured here,
the key tradeoffs, and any C++ ideas worth learning from it. Keep them in sync
with the implementation as it changes.
