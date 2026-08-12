# Full Syntax Specification for Lumière

## 1. Design Philosophy

Lumière is a strongly typed, object-oriented language with a minimal and consistent syntax. Its keywords are French, but its operators, delimiters, and structure are deliberately familiar to developers coming from C++, Java, Kotlin, or Swift. The goal is a language that feels natural to read in French while presenting zero structural surprises to a mainstream programmer.

## 2. Core Design Principles:**

- Strongly typed with full type inference — explicit like Java and C++, concise Go
- Curly-brace scoping — immediately familiar to anyone coming from C++, Java, or Rust
- Single keyword for functions and methods — like Kotlin's `fun`
- Primitives and objects coexist — value types like C++/Java without forced heap allocation for integers and booleans
- Minimal keyword set — the standard library does the heavy lifting
- Keywords are French words with clear real-world meaning — readable like Python/Go, typed like C++


## 2. Reserved Keywords

The full set of reserved keywords. Everything else is a library identifier.

| Mot-clé      | Signification                          | Équivalent              |
|--------------|----------------------------------------|-------------------------|
| `soit`       | Variable declaration                   | `let / var / auto`      |
| `fixe`       | Immutability modifier (used with soit) | `const / val`           |
| `type`       | Module-level transparent type alias    | `type alias`            |
| `fonction`   | Function or method definition          | `fn / fun / def`        |
| `retourne`   | Return a value                         | `return`                |
| `classe`     | Class definition                       | `class`                 |
| `interface`  | Interface definition                   | `interface`             |
| `réalise`    | Implement an interface                 | `implements`            |
| `remplace`   | Override a parent method               | `override`              |
| `public`     | Visible outside the class (default)    | `public`                |
| `privé`      | Visible only inside the class          | `private`               |
| `si`         | Conditional — if                       | `if`                    |
| `sinon`      | Conditional — else                     | `else`                  |
| `pour`       | For loop                               | `for`                   |
| `chaque`     | Each — used with pour                  | `each`                  |
| `dans`       | In — iteration keyword                 | `in`                    |
| `tant que`   | While loop                             | `while`                 |
| `agir selon` | Pattern matching expression            | `match`                 |
| `propager`   | Propagate a result failure              | `propagate`             |
| `ignorer`    | Explicitly discard a result             | `discard`               |
| `vrai`       | Boolean true                           | `true`                  |
| `faux`       | Boolean false                          | `false`                 |
| `rien`       | Null / absence of value                | `null / nil / None`     |
| `ici`        | Current receiver inside a class method | `self / this`           |
| `parent`     | Parent receiver inside a method        | `super`                 |
| `en`         | Type cast operator                     | `as / cast`             |
| `et`         | Logical AND                            | `&& / and`              |
| `ou`         | Logical OR                             | `\|\| / or`             |
| `non`        | Logical NOT                            | `! / not`               |
| `arrêter`    | Break out of a loop                    | `break`                 |
| `continuer`  | Continue to next iteration             | `continue`              |
| `importer`   | Import a module                        | `import / use`          |
| `comme`      | Import alias                           | `as`                    |

> `tant que` is two words but treated as a single token by the lexer.
> `ici` means “here, on this object.” It is only available inside a bound method call. At top level, inside ordinary functions, or before a method is bound to an object, `ici` is not defined.

---

## 3. Comments

Lumière supports the same comment forms as Java and JavaScript:

- `// comment` for single-line comments
- `/* comment */` for block comments

`--` and `---` are not comments. They are lexed as minus operators.

---

## 4. Result control

Propagation is postfix syntax:

```lumiere
soit valeur = opération() ou propager
```

The `ou propager` suffix has no right-hand expression and evaluates only its
left operand. It terminates that `ou` chain. A repeated suffix and `propager`
without `ou` are syntax errors.

Explicit discard is statement syntax:

```lumiere
ignorer opération()
```

`ignorer(opération())` is not an alternative spelling. In an `agir selon`
branch, `-> propager` is restricted to `Échec` patterns and `-> ignorer` is
restricted to result-variant patterns.

The second parameter of `Résultat[T,E]` must resolve to `Erreur`, a class
realizing `Erreur`, a subclass of an error class, or a union exclusively of
error types. `Succès(...)` and `Échec(...)` require a surrounding expected
`Résultat[T,E]`; they are not independently constructible wrapper values.
