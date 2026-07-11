# Lumiere Docs

This folder contains short design notes for two goals:

1. Explain how Lumiere is built, subsystem by subsystem.
2. Explain the C++ techniques used in those subsystems and why they were chosen.

These notes are intentionally small. Each file focuses on one area so that the docs can grow with the codebase instead of turning into one large stale overview.

## Suggested reading order

- [Codebase Reading Guide](./codebase-reading-guide.md)
- [Language Reference](../lumiere_spec.md)
- [Command-Line Reference](./cli.md)
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
- [Value and Runtime Data](./value-and-runtime-data.md)
- [Lexer Pipeline](./lexer-pipeline.md)
- [Parser and AST](./parser-and-ast.md)
- [Tree-Walker Interpreter](./tree-walker-interpreter.md)
- [Tree-Walker Backlog](./tree-walker-backlog.md)
- [VM Design](./vm-design.md)
- [VM Interpreter](./vm-interpreter.md)
- [VM Roadmap](./vm-roadmap.md)
- [C++ Patterns Used Here](./cpp-patterns-used.md)

## Doc style

Each micro doc tries to answer four questions:

- What problem does this subsystem solve?
- How is it structured in this repository?
- What are the key tradeoffs?
- What C++ ideas are worth learning from it?

When the implementation changes, these docs should change with it.
