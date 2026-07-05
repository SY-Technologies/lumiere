# I am Creating a (Francophone) Programming Language

# Lumière — Devlog #1: The idea
>Yeah, when I grow up, you know what I wanna be? Take a seat, let me tell you my ridiculous dreams
— NF, When I Grow Up

That line comes to mind every time I sit down to think about this project.

Because yeah, creating a programming language, a sound one, is no small feat. It feels even more inconceivable to try doing it alone. But hey, even if the idea is a little nuts, it is what my heart desires.

“But why would you want to put yourself through this?” I hear you ask.

Fair question.

My goal here is twofold. First, creating a language is genuinely hard. It requires familiarity with some of the best software engineering practices and concepts, and I see this endeavour as a way to sharpen my technical skills. I am not starting completely from scratch, though. Back in university, I took a course on creating DSLs, or Domain Specific Languages, and I loved what I learned in that class. I intend to reuse some of those concepts here.

The second goal is a little more personal. As a francophone, I have often wondered why so many programming languages are built around English, and what it would take to create one that is not. A smaller goal along the way is to improve my C++ skills.

Maybe this project will never see the light of day. Maybe it will not work. But hey, at least I will have tried.

My first task is to design the syntax. I have gone back and forth on names more times than I would like to admit. But in the end, only one felt right:

**Lumière.**


# Lumière — Devlog #2: Designing the Syntax

> *"I admit the lyrics are weak, I been workin' on 'em, I'll be good eventually, I understand you gotta crawl before you get to your feet" — NF, When I Grow Up*


I have my first draft of keywords in. In a way, it was easy because this project has been brewing in my brain since last year. My mind tends to be fertile for this kind of thing and honestly I had a lot of fun with it. That said, the lyrics are weak right now and I know it. The lexer does not exist yet. The parser is a blank page. But hey, you gotta crawl before you get to your feet, and this is me crawling.

The guiding principle was simple: keep the keyword set small. Go, Python, and Java were my main inspirations here. The fewer reserved words a language has, the less a new learner has to memorize before they can be productive, everything else lives in the standard library. I also wanted every keyword to be semantically meaningful in French. Not cryptic, not abbreviated, not lazy. If you can read French and you see a keyword, you should immediately understand what it does.

I also went back and forth a lot on whether to include OOP constructs at all. It is a real design question because a full object model is not exactly a small keyword commitment. But I am biased toward OOP, I think in classes, I reach for interfaces naturally, and in the end I took my own bait. So Lumière has `classe`, `interface`, `réalise`, `remplace`, `public`, and `privé`. Familiar to anyone coming from Java, Kotlin, or C++.

Now, the two things that actually made me spin my wheels.

## The "this" Problem

In most languages this is trivial. Python calls it `self`, C++ and Java call it `this`, Kotlin too. In French the natural equivalent is `soi`, which means oneself. I went with it initially. Then I stared at it next to `soit`, the variable declaration keyword, and immediately felt uneasy. Two keywords, one letter apart, both common. That is a bug waiting to happen and a readability trap.

I landed on `ici`. It means "here", as in here, on this object. Is it perfect? No, and I will admit that. But it is unambiguous, it is short, and it does not collide with anything else. `ici` is only valid inside a bound method, at the top level or inside a free function it simply does not exist. It is the best I got for now, and I can live with that.

## Booleans

This one genuinely surprised me, I did not expect to spin my wheels here. The problem is that French has perfectly good words for true and false, `vrai` and `faux`, but I kept second guessing myself. Boolean keywords appear constantly, they sit in every condition, and I wanted them to feel native without throwing off a developer coming from another language.

In the end I just came back to `vrai` and `faux` and committed. They are clean, they are short, they mean exactly what they say. I love them. No regrets.

## The Full Keyword Table

| Mot-clé      | Signification                          | Équivalent              |
|--------------|----------------------------------------|-------------------------|
| `soit`       | Variable declaration                   | `let / var / auto`      |
| `fixe`       | Immutability modifier                  | `const / val`           |
| `fonction`   | Function or method definition          | `fn / fun / def`        |
| `retourne`   | Return a value                         | `return`                |
| `classe`     | Class definition                       | `class`                 |
| `interface`  | Interface definition                   | `interface`             |
| `réalise`    | Implement an interface                 | `implements`            |
| `remplace`   | Override a parent method               | `override`              |
| `public`     | Visible outside the class              | `public`                |
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
| `essayer`    | Try block                              | `try`                   |
| `attraper`   | Catch block                            | `catch`                 |
| `finalement` | Finally block                          | `finally`               |
| `lancer`     | Throw an error                         | `throw`                 |

# Lumière - Devlog #3: Designing the Syntax (Wednesday, June 10th)
-------------
>Anybody wanna hear me rap? (No)
>Come on, let me play a couple tracks (no)
>Come on, I can spit it really fast (no)
>You think I should throw this in the trash? (No)
--NF, When I Grow Up
---------------------

At this point, I've read a lot of blog articles and book chapters trying to develop my knowledge of language design as much as possible. I landed on [this book](https://craftinginterpreters.com/), which I've enjoyed a lot so far and have been drawing serious ideas from. One of the key things I've learned is the performance difference between a tree-walking interpreter and bytecode compilation. Because I'm primarily doing this project to learn, I decided to design my architecture in a way that supports both tree-walking interpretation and bytecode compilation via a bespoke VM. For now, though, I'm implementing the tree-walking approach first.

Another decision I made was to build the lexer and parser by hand. Back in university, we used ANTLR to get both for free after defining the grammar, but I think there's a lot of learning value in doing it from scratch. To keep things modular, I broke the lexer down into three parts: the Scanner, the Tokenizer, and the Lexer. The Scanner is responsible for taking the Lumière source code and moving through it character by character. The Tokenizer works hand-in-hand with the Scanner to detect and create tokens from the source code. The Lexer then composes these two to achieve its overall goal.


# Lumière — Devlog #4: The Visitor Pattern (and How I Had to Re-learn It (Thursday June 11th))



I sat down to work on AST traversal and found myself Googling the visitor pattern. Something I know I've understood and used before. I still remembered the overall idea, but the details were gone.

So I went back ...

## Why I Need It

A parser produces a tree of node types. Binary expressions, if statements, function declarations, and so on. Then you need to do many things with that tree: type-check it, lint it, eventually generate bytecode. The naive approach is to put all those operations directly on the node classes. That works until you want to add a new operation, at which point you're opening every single node class and bolting on another method. Ten compiler passes later, you are shaking your head in disappoint for not thinking of every possible methods upfront.

## The Flip

What visitor does is take all that logic out of the nodes entirely. The nodes become pure data. Each operation becomes its own class.

```cpp
class TypeChecker : public Visitor {
    void visitBinaryExpr(BinaryExpr* node) { ... }
    void visitIfStatement(IfStatement* node) { ... }
    void visitFunctionDecl(FunctionDecl* node) { ... }
};

class Linter : public Visitor {
    void visitBinaryExpr(BinaryExpr* node) { ... }
    void visitIfStatement(IfStatement* node) { ... }
    void visitFunctionDecl(FunctionDecl* node) { ... }
};
```

Adding a new pass means writing one new (visitor) class. The node classes never change again. It's just organizing code by operation instead of by type. Once that clicked everything else fell into place.

## The `accept` Method

Each node implements `accept`, and it looks almost insultingly (at least to me) simple:

```cpp
void BinaryExpr::accept(Visitor* v) {
    left->accept(v);
    right->accept(v);
    v->visitBinaryExpr(this);
}
```

There are actually two reasons for it. The obvious one: `this` is how the visitor gets access to the node's data. Without it the visitor has nothing to work with.

The less obvious one is dispatch. In C++, if you loop over a list of `Node*` pointers and call `visitor->visit(node)`, the compiler resolves overloads at compile time based on the static type of the argument, which is just `Node*`. It has no idea there's a `BinaryExpr` underneath. So it calls the base version every time.

`accept` sidesteps this because the node calls the visitor with itself. Inside `BinaryExpr::accept`, the compiler knows for certain that `this` is a `BinaryExpr*`, so `visitBinaryExpr` resolves correctly. You've used a virtual call on `accept` to get into the right class, and from there the second call is unambiguous. Two dispatches, hence double dispatch.

Without this I'd be writing `dynamic_cast` chains everywhere. Not fun.

## The Trade-off

Adding a new node type is the painful operation now. Add a WhileLoop node and every existing visitor is suddenly missing a case. You have to go touch all of them. Same hunt as before, just across visitor classes instead of node classes.

But the two lists are very different sizes. Lumière's grammar has a few dozen node types and that number is basically frozen once the language is designed. Compiler passes I'll keep adding forever. So visitor makes the short list painful and the long list easy

# Lumière - Devlog #4: A slip Into a Deep Rabbit Hole

>A parser really has two jobs:

>1. Given a valid sequence of tokens, produce a corresponding syntax tree.

>2. Given an invalid sequence of tokens, detect any errors and tell the user about their mistakes.

# ScopeGuard and RAII in the Lumière Tree Walker

## The problem

When the tree walker enters a block it pushes a new scope onto the
environment chain. When it leaves it must pop that scope. The naive
approach manages this manually:

```cpp
void TreeWalker::visit(BlockStmt &s)
{
    Environment *child = new Environment(m_env);
    m_env = child;                          // push

    for (auto &stmt : s.statements)
        execute(*stmt);                     // danger here

    Environment *old = m_env;
    m_env = m_env->parent();               // pop
    delete old;                            // free
}
```

This looks correct. The problem is `execute(*stmt)`.

## How ReturnSignal breaks manual cleanup

Consider this Lumière code:

```
fonction test() -> Entier {
    soit x = 10
    si vrai {
        retourne x        -- throws ReturnSignal here
    }
}
```

The C++ call stack at the moment of the throw looks like this:

```
call_function()                   <-- ReturnSignal is caught here
  visit(BlockStmt)                <-- function body block
    visit(IfStmt)
      visit(BlockStmt)            <-- if-body block
        execute(*stmt)
          visit(ReturnStmt)
            throw ReturnSignal    <-- thrown here
```

C++ exception unwinding works upward through every frame until it
finds a matching catch. Nothing between `visit(ReturnStmt)` and
`call_function()` has a try/catch. So the unwind goes:

```
visit(ReturnStmt)      -- throws ReturnSignal
execute(*stmt)         -- no catch, keeps unwinding
visit(BlockStmt)       -- no catch, keeps unwinding
                          cleanup lines SKIPPED
visit(IfStmt)          -- no catch, keeps unwinding
visit(BlockStmt)       -- no catch, keeps unwinding
                          cleanup lines SKIPPED
call_function()        -- catch (ReturnSignal) { ... }
```

Every `visit(BlockStmt)` frame on the stack had a child scope pushed
and never popped. After the signal is caught:

- `m_env` still points at the innermost child scope
- both child scopes are leaked on the heap
- the next call pushes a new scope on top of corrupted state
- variable lookups now traverse a chain with dangling frames

## The RAII solution — ScopeGuard

The insight is that C++ *guarantees* local variable destructors run
when a function exits — whether that exit is a normal return or an
exception unwinding the stack. ScopeGuard ties the scope's lifetime
to a local variable's lifetime:

```cpp
class ScopeGuard
{
public:
    ScopeGuard(Environment *&current)
        : m_current(current),
          m_previous(current)
    {
        m_current = new Environment(m_previous);  // push in constructor
    }

    ~ScopeGuard()
    {
        delete m_current;        // free child scope
        m_current = m_previous;  // restore m_env to parent
    }

private:
    Environment *&m_current;   // reference to m_env — not a copy
    Environment  *m_previous;  // saved parent pointer
};
```

`m_current` is a reference to the interpreter's own `m_env` pointer.
When the destructor writes `m_current = m_previous` it is writing
directly into `m_env`, restoring it to the parent scope.

`visit(BlockStmt)` becomes:

```cpp
void TreeWalker::visit(BlockStmt &s)
{
    ScopeGuard guard(m_env);   // push — destructor will always run

    for (auto &stmt : s.statements)
        execute(*stmt);        // ReturnSignal may throw here
}                              // guard.~ScopeGuard() runs here
                               // whether we got here normally or via unwind
```

## The same stack trace with ScopeGuard

```
visit(ReturnStmt)      -- throws ReturnSignal
execute(*stmt)         -- no catch, unwinds
visit(BlockStmt)       -- no catch, unwinds
                          ~ScopeGuard() runs  <-- child scope freed, m_env restored
visit(IfStmt)          -- no catch, unwinds
visit(BlockStmt)       -- no catch, unwinds
                          ~ScopeGuard() runs  <-- child scope freed, m_env restored
call_function()        -- catch (ReturnSignal) { ... }
                          m_env now correctly points at the function scope
```

Every scope is cleaned up in reverse order of creation, automatically,
with no cleanup code at the bottom of any function.

## Why the destructor order matters

The destructor runs in the correct order because C++ always destroys
local variables in reverse order of construction as the stack unwinds.
If three scopes are pushed:

```
guard_1 pushed (function body)
  guard_2 pushed (for loop)
    guard_3 pushed (if body)
    guard_3 destroyed first   -- innermost scope freed first
  guard_2 destroyed second
guard_1 destroyed last
```

This mirrors the logical nesting of scopes exactly.

## The general principle — RAII

ScopeGuard is one instance of a broader C++ pattern called RAII
(Resource Acquisition Is Initialization). The idea is simple:

- acquire a resource in a constructor
- release it in a destructor
- let C++ guarantee the destructor always runs

The same pattern appears everywhere in C++:

| Class             | Acquires      | Releases           |
|-------------------|---------------|--------------------|
| `std::unique_ptr` | heap memory   | `delete`           |
| `std::lock_guard` | mutex lock    | `unlock()`         |
| `std::ifstream`   | file handle   | `close()`          |
| `ScopeGuard`      | child scope   | `delete` + restore |

Languages without deterministic destruction (Java, Python, Go) solve
the same problem with `finally` blocks, `defer` statements, or
`try-with-resources`. RAII is C++'s answer — and it requires no
special syntax because it falls out of the destructor guarantee that
already exists.
