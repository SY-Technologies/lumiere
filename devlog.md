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