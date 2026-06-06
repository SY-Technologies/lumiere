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
| `vrai`       | Boolean true                           | `true`                  |
| `faux`       | Boolean false                          | `false`                 |
| `rien`       | Null / absence of value                | `null / nil / None`     |
| `ici`        | Current receiver inside a class method | `self / this`           |
| `en`         | Type cast operator                     | `as / cast`             |
| `et`         | Logical AND                            | `&& / and`              |
| `ou`         | Logical OR                             | `\|\| / or`             |
| `non`        | Logical NOT                            | `! / not`               |
| `arrêter`    | Break out of a loop                    | `break`                 |
| `continuer`  | Continue to next iteration             | `continue`              |
| `importer`   | Import a module                        | `import / use`          |
| `comme`      | Import alias                           | `as`                    |
| `essayer`    | Try block — error handling             | `try`                   |
| `attraper`   | Catch block — error handling           | `catch`                 |
| `finalement` | Finally block — always executes        | `finally`               |
| `lancer`     | Throw an error                         | `throw`                 |

> `tant que` is two words but treated as a single token by the lexer.
> `ici` means “here, on this object.” It is only available inside a bound method call. At top level, inside ordinary functions, or before a method is bound to an object, `ici` is not defined.

---