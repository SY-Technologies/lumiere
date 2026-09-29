# Lumiere VM Design

**Version:** 4.0  
**Status:** Adopted design for review

This document defines the backend architecture Lumiere will implement next.

It is intentionally normative:

- it states adopted decisions
- it defines the full layer chain
- it defines the textual review syntax for each layer
- it avoids implementation code

It does not claim the design is already fully implemented.

## Executive summary

Lumiere adopts the following backend architecture:

```text
Source
-> CFG
-> Lexer / Tokenizer
-> Parser
-> AST
-> LIR
-> Bytecode
-> VM
```

The backend chain begins after parsing:

```text
AST -> LIR -> Bytecode -> VM
```

The adopted major decisions are:

- no HIR initially
- block-based LIR
- TAC-like LIR instructions
- non-SSA LIR initially
- stack-based bytecode VM
- explicit locals, temporaries, and block terminators

## Design goals

The VM design must satisfy all of the following:

1. Be fast enough to matter without becoming a research project.
2. Remain understandable enough that one engineer can maintain every layer.
3. Keep source-language concerns out of the VM where possible.
4. Preserve room for future optimization without requiring SSA or a large
   optimizer framework at the start.
5. Reuse the existing AST-based frontend rather than replacing it with a
   single-pass compiler architecture.

## Terminology

### CFG

`CFG` means **Context-Free Grammar**.

It refers only to the syntax definition of Lumiere.
It does not mean control-flow graph anywhere in this document.

### AST

`AST` means **Abstract Syntax Tree**.

It is the source-shaped tree produced by the parser.

### LIR

`LIR` means **Low-level Intermediate Representation**.

It is the first real compiler IR and sits between AST and bytecode.
It is execution-shaped, not source-shaped.

### Block graph

A **block graph** is the structure of a LIR function.

Nodes are basic blocks.
Edges are explicit control-flow relationships between blocks.

### Basic block

A **basic block** is a straight-line sequence of instructions with:

- one entry point
- one explicit exit terminator

There is no hidden fallthrough in the LIR model.

### Terminator

A **terminator** is the final operation of a basic block.

Initial terminators are:

- `jump`
- `branch`
- `return`
- `return_nil`

### TAC

`TAC` means **Three-Address Code**.

It describes the adopted instruction style for LIR:

```text
dst = op arg1, arg2
```

with at most one destination and a small fixed number of inputs.

### SSA

`SSA` means **Static Single Assignment**.

SSA is not the initial LIR form.
It remains a possible future optimization transform, not a foundational design
choice.

## Full chain and role of each layer

```text
Source    : human-written Lumiere program
CFG       : syntax definition for valid Lumiere source
AST       : source-shaped structured program
LIR       : execution-shaped structured program
Bytecode  : compact stack-machine instruction stream
VM        : runtime that executes bytecode
```

### CFG

The CFG defines:

- valid token sequences
- precedence and associativity
- statement and expression forms

It is not an IR and is not part of the backend execution model.

### AST

The AST preserves what the programmer wrote in structured form.

It remains:

- source-shaped
- language-facing
- suitable for frontend validation and the tree-walker

### LIR

The LIR makes explicit:

- computations
- locals
- temporaries
- calls
- returns
- control flow

Its internal structure is a block graph.

### Bytecode

The bytecode is:

- lower-level than LIR
- stack-oriented
- compact
- designed for VM dispatch

### VM

The VM executes bytecode and manages:

- instruction dispatch
- value stack
- call frames
- local slot access
- global lookup
- builtin/native bridging

The VM is not where source-language desugaring happens.

## Why HIR is omitted

HIR is intentionally omitted from the adopted architecture.

Reasons:

- one fewer representation to maintain
- one fewer lowering pass to debug
- simpler architecture for the first VM
- less conceptual overhead for a small team

If later experience shows that AST-to-LIR lowering becomes overloaded with:

- syntax sugar normalization
- semantic desugaring
- multiple surface forms with the same meaning
- object/call normalization that should not leak into LIR

then the pipeline may evolve to:

```text
AST -> HIR -> LIR -> Bytecode -> VM
```

That is a future extension, not part of the current adopted design.

## Why `AST -> Bytecode` is rejected

Direct `AST -> Bytecode` lowering would force one pass to handle:

- source-language structure
- control-flow normalization
- local and temporary management
- stack-effect planning
- bytecode instruction selection
- jump encoding details

That would make the compiler harder to maintain.

LIR splits the responsibilities cleanly:

- `AST -> LIR` handles language-to-execution lowering
- `LIR -> Bytecode` handles execution-to-VM lowering

That separation is the reason LIR exists.

## Architectural contracts

### AST -> LIR

The AST-to-LIR pass is responsible for:

- converting source-shaped control flow into explicit blocks and terminators
- converting expression trees into explicit temporary-producing instructions
- assigning local identifiers
- resolving named arguments into positional order
- inserting implicit returns where language semantics require them
- preserving source locations

The AST-to-LIR pass is not responsible for:

- stack planning
- jump offset encoding
- bytecode compaction

### LIR -> Bytecode

The LIR-to-bytecode pass is responsible for:

- converting TAC-like instructions into stack-machine sequences
- flattening locals and temporaries into the VM storage model
- resolving block edges into concrete jump offsets
- encoding constants and globals into compact operands

The LIR-to-bytecode pass is not responsible for:

- language desugaring
- source-structure interpretation
- semantic normalization

### Bytecode -> VM

The VM is responsible for:

- consuming well-formed bytecode
- enforcing runtime semantics of stack ops, branches, calls, and returns
- managing runtime state such as frames, locals, and globals

The VM should not know about:

- AST nodes
- source syntax quirks
- LIR temporaries as named entities

## Mental model

Before reading the formal syntax, it helps to read the LIR as if it were a
slowed-down explanation of how the program runs.

At the source level, a Lumiere program is written in terms of:

- statements
- expressions
- variables
- conditions
- function calls

At the LIR level, that same program is rewritten into:

- named storage locations
- intermediate scratch values
- straight-line chunks of work
- explicit decisions about where execution goes next

So the mental model is:

- the **AST** still looks like what the programmer wrote
- the **LIR** looks like a step-by-step execution plan
- the **bytecode** looks like compact VM instructions

## How to read a LIR function

Read a LIR function in this order:

1. `params`
2. `locals`
3. `temps`
4. `entry`
5. blocks in control-flow order

Using:

```text
function <name>
params:
  ...
locals:
  ...
temps:
  ...
entry:
  b0

b0:
  <instruction>
  <instruction>
  <terminator>
```

the meaning is:

- `function <name>`:
  the function being compiled
- `params`:
  the function arguments
- `locals`:
  named storage the Lumiere program can observe directly
- `temps`:
  scratch values created by the compiler while breaking expressions into steps
- `entry`:
  the first block that executes when the function starts
- `b0`, `b1`, `b2`, ...:
  blocks of straight-line execution

## Plain-language definitions

### Parameter

A parameter is a function argument.

Example:

```lum
fonction addition(x, y) {
  retourne x + y
}
```

Here, `x` and `y` are parameters.

In the LIR, parameters are also locals.

### Local

A local is named storage that belongs to the function and is visible to the
program.

Example:

```lum
soit a = 1
```

If `a` exists in the source program, then `a` becomes a local in the LIR.

### Temp

A temp is a compiler-created scratch value used for intermediate results.

Temps are not user variables.

Example source:

```lum
soit c = a + b
```

Conceptual LIR:

```text
t0 = load_local l0
t1 = load_local l1
t2 = add t0, t1
store_local l2, t2
```

Here:

- `l0` and `l1` are locals for `a` and `b`
- `l2` is the local for `c`
- `t0`, `t1`, and `t2` are temps

The temps only exist so the compiler can express the computation in small,
explicit steps.

### Block

A block is one straight-line chunk of work.

Inside a block, instructions execute in order from top to bottom.
Control only changes direction at the end of the block.

### Terminator

A terminator is the last operation in a block.

Its job is to answer:

- where does execution go next?

Examples:

- `jump b3`
- `branch t7, b1, b2`
- `return t4`
- `return_nil`

Without terminators, the flow of execution would be implicit and harder to
reason about.

### Entry block

The entry block is the block where execution begins when a function starts.

Example:

```text
entry:
  b0
```

This means:

- when the function is called, execution begins at block `b0`

### Constant pool

The constant pool stores literal values separately from instructions.

Instead of embedding a literal payload everywhere, instructions refer to
constants by id.

Example:

```text
k0 = 1
k1 = "bonjour"
```

Then instructions can say:

```text
t0 = const k0
t1 = const k1
```

### Bytecode

Bytecode is the compact instruction stream the VM executes.

It is lower-level than LIR.

The LIR says:

- what computation is happening
- what blocks exist
- what values flow where

The bytecode says:

- what the VM pushes, pops, jumps to, calls, and returns

## Why locals and temps are separate

Separating locals and temps in the LIR makes the IR easier to read.

Locals answer:

- what named storage does the Lumiere program have?

Temps answer:

- what scratch values did the compiler need while lowering the program?

If they were merged too early, the IR would still work, but it would be harder
for a human reader to distinguish:

- user-visible state
- compiler-generated intermediate state

That distinction may disappear later in bytecode lowering, but it is valuable
in the LIR.

## Why blocks exist at all

Blocks exist because execution is not always linear.

A function with:

- `si`
- loops
- short-circuit boolean logic
- returns in the middle of execution

cannot be described as just one flat list of high-level operations unless the
compiler also hides control flow somewhere else.

Blocks make control flow visible.

That is why the LIR uses:

- blocks
- terminators
- explicit edges

instead of trying to stay tree-shaped like the AST.

## Tiny worked example

Source:

```lum
fonction principal() {
  soit a = 1
  soit b = 2
  soit c = a + b
  afficher(c)
}
```

Conceptual LIR:

```text
function principal
params:
locals:
  l0 = a
  l1 = b
  l2 = c
temps:
  t0
  t1
  t2
entry:
  b0

b0:
  t0 = const k0
  store_local l0, t0
  t1 = const k1
  store_local l1, t1
  t2 = add l0, l1
  store_local l2, t2
  t3 = load_global g0
  t4 = call t3, [l2]
  return_nil
```

How to read it:

- create `a`
- create `b`
- compute `a + b`
- store the result in `c`
- call `afficher(c)`
- return

That is the whole point of the LIR: it turns the source program into an
explicit execution story.

## Syntax summary

This section defines the review notation used throughout this document.

### Source notation

Source examples use normal Lumiere syntax.

Example:

```lum
fonction principal() {
  soit a = 1
  soit b = 2
  soit c = a + b
  afficher(c)
}
```

### AST notation

AST notation is tree-shaped.

General form:

```text
NodeType(field = value, ...)
```

Nested form:

```text
ParentNode
  child_a: ChildNode(...)
  child_b: OtherNode(...)
```

### LIR notation

LIR notation is block-graph oriented.

General function form:

```text
function <name>
params:
  ...
locals:
  ...
temps:
  ...
entry:
  b0

b0:
  <instruction>
  <instruction>
  <terminator>
```

General instruction form:

```text
<dst> = <op> <arg1>, <arg2>, ...
<op> <arg1>, <arg2>, ...
```

General terminator form:

```text
jump <block>
branch <cond>, <then_block>, <else_block>
return <src>
return_nil
```

### Bytecode notation

Bytecode notation is linear and stack-oriented.

Symbolic form:

```text
OPCODE operand operand ...
```

Resolved-offset form:

```text
12: OPCODE operand operand ...
```

## Adopted LIR form

The adopted LIR is:

- block-based
- TAC-like
- non-SSA
- explicit about locals and temporaries
- explicit about control flow
- explicit about source locations

## LIR syntax specification

This is the adopted textual review syntax for LIR.

### Top-level form

```text
Module           ::= "module" ModuleName ModuleBody
ModuleBody       ::= ConstantsSection GlobalsSection FunctionsSection

ConstantsSection ::= "constants:" ConstantEntry*
GlobalsSection   ::= "globals:" GlobalEntry*
FunctionsSection ::= Function+

Function         ::= "function" FunctionName FunctionHeader FunctionBody
FunctionHeader   ::= ParamsSection LocalsSection TempsSection EntrySection
FunctionBody     ::= Block+

ParamsSection    ::= "params:" ParamEntry*
LocalsSection    ::= "locals:" LocalEntry*
TempsSection     ::= "temps:" TempEntry*
EntrySection     ::= "entry:" BlockId

Block            ::= BlockId ":" Instruction* Terminator
```

This is descriptive notation for architecture review, not a promise of a
shipped parser for this exact text format.

### Identifier syntax

```text
ModuleName    ::= identifier
FunctionName  ::= identifier
BlockId       ::= "b" integer
LocalId       ::= "l" integer
TempId        ::= "t" integer
GlobalId      ::= "g" integer | identifier
ConstId       ::= "k" integer
FunctionId    ::= "f" integer | identifier
```

### Metadata entry syntax

```text
ConstantEntry ::= ConstId "=" ConstantValue
GlobalEntry   ::= GlobalId "=" GlobalName
ParamEntry    ::= LocalId "=" ParamName
LocalEntry    ::= LocalId "=" LocalName
TempEntry     ::= TempId
```

Example:

```text
constants:
  k0 = 1
  k1 = 2
  k2 = "afficher"

globals:
  g0 = afficher

params:
  l0 = x
  l1 = y

locals:
  l2 = a
  l3 = c

temps:
  t0
  t1
  t2

entry:
  b0
```

## LIR core entities

The LIR has five core entities:

1. `Module`
2. `Function`
3. `Block`
4. `Instruction`
5. `Operand`

### 1. Module

A `Module` represents one lowered Lumiere source unit.

Shape:

```text
Module
  source_path
  constants
  globals
  functions
```

Role:

- top-level compilation container
- owner of function list
- owner of constant and global metadata

Rule:

- top-level executable statements lower into an implicit module entry function
  named `__module_init__`
- VM startup invokes `__module_init__` before entering `principal`

This keeps execution uniformly function-based.

### 2. Function

A `Function` is the primary compilation unit.

Shape:

```text
Function
  name
  arity
  visibility
  params
  locals
  temps
  entry_block
  blocks
```

Meaning:

- parameters are the first locals
- locals are user-visible durable storage
- temporaries are compiler-generated intermediate values

### 3. Block

A `Block` is a straight-line sequence of instructions with one terminator.

Shape:

```text
Block
  id
  label
  instructions
  terminator
  predecessors
  successors
```

Required invariants:

- every block ends with exactly one terminator
- there is no hidden fallthrough
- predecessor and successor edges are explicit

Review/debug syntax:

```text
b3:
  ; predecessors: [b1, b2]
  ; successors:   [b4, b5]
  t0 = load_local l1
  t1 = const k2
  t2 = gt t0, t1
  branch t2, b4, b5
```

### 4. Instruction

Instructions describe computation or side effects.

Categories:

- value-producing instructions
- effect-only instructions

Terminators remain separate from ordinary instructions.

Examples:

```text
t0 = const k0
t1 = load_local l0
t2 = add t0, t1
store_local l2, t2
```

Every instruction carries a source location.

Source location syntax:

```text
t0 = const k0   @line:4,col:10
```

Formal syntax:

```text
Instruction ::= ValueInstruction [SourceLoc]
              | EffectInstruction [SourceLoc]

SourceLoc   ::= "@line:" integer ",col:" integer
```

### 5. Operand

Operands are references to already-lowered entities.
They are never embedded AST nodes.

Operand kinds:

```text
ConstId
LocalId
TempId
GlobalId
FunctionId
BlockId
```

Operand syntax:

```text
Operand ::= ConstId
          | LocalId
          | TempId
          | GlobalId
          | FunctionId
```

Terminators additionally reference block ids:

```text
ControlOperand ::= BlockId
```

Naming convention:

- `lN` for locals
- `tN` for temporaries
- `bN` for blocks

## Constants pool

The module constants pool holds literal values referenced by LIR.

Constant kinds:

```text
TAG_INT
TAG_FLOAT
TAG_BOOL
TAG_NIL
TAG_STRING
TAG_FUNCTION
TAG_CLASS
```

This defines the abstract design, not the concrete binary layout.

Rules:

- literals are referenced through constant ids
- LIR does not duplicate literal payloads blindly throughout instructions

## TAC instruction style

Most value instructions take one of these forms:

```text
dst = op arg1, arg2
dst = op arg1
dst = op
```

Examples:

```text
t0 = const k0
t1 = load_local l0
t2 = add t0, t1
t3 = gt t2, t1
```

Why TAC is chosen:

- explicit and easy to read
- easy to print and debug
- easy to lower into stack bytecode
- simpler than SSA
- cleaner than implicit stack effects at the IR layer

### Value instruction syntax

```text
ValueInstruction ::= TempId "=" NullaryOp
                   | TempId "=" UnaryOp Operand
                   | TempId "=" BinaryOp Operand "," Operand
                   | TempId "=" VariadicOp OperandList
```

### Effect instruction syntax

```text
EffectInstruction ::= StoreOp Operand "," Operand
                    | SideEffectOp OperandList
```

### Terminator syntax

```text
Terminator ::= "jump" BlockId
             | "branch" Operand "," BlockId "," BlockId
             | "return" Operand
             | "return_nil"
```

### Operand list syntax

```text
OperandList ::= Operand
              | Operand "," OperandList
              | "[" "]"
              | "[" OperandSeq "]"

OperandSeq  ::= Operand
              | Operand "," OperandSeq
```

## Locals and temporaries

### Locals

Locals represent:

- parameters
- `soit` bindings
- durable user-visible storage

Examples:

```text
l0 = parameter x
l1 = local a
l2 = local c
```

### Temporaries

Temporaries represent:

- intermediate expression results
- compiler-generated scratch values

Examples:

```text
t0 = load_local l1
t1 = const k1
t2 = add t0, t1
```

Rule:

- locals and temps remain logically separate in LIR
- bytecode lowering flattens them into the VM storage model

## Terminators

Initial terminator family:

- `jump target_block`
- `branch cond, then_block, else_block`
- `return src`
- `return_nil`

No implicit control flow is allowed.

## Adopted initial LIR instruction families

### Constants and moves

```text
dst = const ConstId
dst = move src
```

Formal form:

```text
NullaryOp ::= "const" ConstId
UnaryOp   ::= "move"
```

### Local and global access

```text
dst = load_local LocalId
store_local LocalId, src
dst = load_global GlobalId
store_global GlobalId, src      -- future
```

Formal form:

```text
UnaryOp ::= "load_local" | "load_global"
StoreOp ::= "store_local" | "store_global"
```

### Arithmetic

```text
dst = add src1, src2
dst = sub src1, src2
dst = mul src1, src2
dst = div src1, src2
dst = mod src1, src2
dst = neg src
```

Formal form:

```text
BinaryOp ::= "add" | "sub" | "mul" | "div" | "mod"
UnaryOp  ::= "neg"
```

### Comparison

```text
dst = eq  src1, src2
dst = neq src1, src2
dst = lt  src1, src2
dst = lte src1, src2
dst = gt  src1, src2
dst = gte src1, src2
```

Formal form:

```text
BinaryOp ::= "eq" | "neq" | "lt" | "lte" | "gt" | "gte"
```

### Calls

```text
dst = call callee, [arg0, arg1, ...]
```

Rules:

- named arguments are resolved before LIR
- LIR has one uniform call form initially
- call specialization, if ever needed, happens in later lowering or
  optimization, not in the base LIR design

Formal form:

```text
VariadicOp ::= "call"
```

### Control flow

```text
jump   bN
branch cond, b_then, b_else
```

### Return

```text
return src
return_nil
```

## Short-circuit logic

Short-circuit boolean behavior lowers through control flow, not through naive
binary value instructions.

Example source:

```lum
si a et b {
  afficher(1)
}
```

Conceptual LIR:

```text
b0:
  t0 = load_local l0
  branch t0, b1, b3

b1:
  t1 = load_local l1
  branch t1, b2, b3

b2:
  t2 = load_global g0
  t3 = const k0
  t4 = call t2, [t3]
  jump b3

b3:
  return_nil
```

This makes short-circuit behavior explicit in the block graph.

## Bytecode syntax specification

This section defines the adopted textual review syntax for bytecode.

### Bytecode stream structure

```text
BytecodeModule   ::= BytecodeFunction+
BytecodeFunction ::= "function" FunctionName BytecodeBody
BytecodeBody     ::= BytecodeInstr*
```

### Bytecode instruction forms

```text
BytecodeInstr ::= StackOp
                | LoadStoreOp
                | ArithmeticOp
                | CompareOp
                | CallOp
                | JumpOp
                | ReturnOp
                | Label
```

Instruction families:

```text
StackOp      ::= "LOAD_CONST" integer
               | "NIL"

LoadStoreOp  ::= "LOAD_LOCAL" integer
               | "STORE_LOCAL" integer
               | "LOAD_GLOBAL" integer

ArithmeticOp ::= "ADD" | "SUB" | "MUL" | "DIV" | "MOD" | "NEG"
CompareOp    ::= "EQ" | "NEQ" | "LT" | "LTE" | "GT" | "GTE"

CallOp       ::= "CALL" integer

JumpOp       ::= "JUMP" integer
               | "JUMP_IF_FALSE" integer

ReturnOp     ::= "RETURN"

Label        ::= "LABEL" identifier
```

This defines the textual review notation, not the concrete binary encoding.

## Mapping rules: LIR to bytecode

### Rule 1

LIR temporaries do not survive into bytecode as named values.

They lower into stack effects and, where necessary, VM storage slots.

### Rule 2

LIR locals survive conceptually into bytecode as local slot references.

Example:

```text
LIR:      t0 = load_local l2
Bytecode: LOAD_LOCAL 2
```

### Rule 3

LIR block terminators become jumps and returns.

Example:

```text
LIR:      branch t7, b1, b2
Bytecode: JUMP_IF_FALSE <b2_offset>
          ...then-body...
          JUMP <end_offset>
```

### Rule 4

LIR `call` becomes:

- bytecode to place the callee on the stack
- bytecode to place arguments on the stack
- one `CALL argc`

### Rule 5

LIR constants become constant-pool indices used by bytecode load operations.

## End-to-end example

### Source

```lum
fonction principal() {
  soit a = 1
  soit b = 2
  soit c = a + b
  si c > 2 {
    afficher(c)
  } sinon {
    afficher(0)
  }
}
```

### AST

```text
FunctionDeclStmt("principal")
  body: BlockStmt
    VarDeclStmt("a")
      initializer: LiteralExpr(1)
    VarDeclStmt("b")
      initializer: LiteralExpr(2)
    VarDeclStmt("c")
      initializer: BinaryExpr(
        left = IdentifierExpr("a"),
        op = PLUS,
        right = IdentifierExpr("b")
      )
    IfStmt
      condition: BinaryExpr(
        left = IdentifierExpr("c"),
        op = SUPERIEUR,
        right = LiteralExpr(2)
      )
      then_branch: BlockStmt(...)
      else_branch: BlockStmt(...)
```

### LIR

```text
module main
constants:
  k0 = 1
  k1 = 2
  k2 = 0

globals:
  g0 = afficher

function principal
params:
locals:
  l0 = a
  l1 = b
  l2 = c
temps:
  t0
  t1
  t2
  t3
  t4
  t5
  t6
  t7
  t8
  t9
  t10
  t11
  t12
  t13
entry:
  b0

b0:
  t0 = const k0              @line:2,col:12
  store_local l0, t0         @line:2,col:3
  t1 = const k1              @line:3,col:12
  store_local l1, t1         @line:3,col:3
  t2 = load_local l0         @line:4,col:12
  t3 = load_local l1         @line:4,col:16
  t4 = add t2, t3            @line:4,col:14
  store_local l2, t4         @line:4,col:3
  t5 = load_local l2         @line:5,col:6
  t6 = const k1              @line:5,col:10
  t7 = gt t5, t6             @line:5,col:8
  branch t7, b1, b2          @line:5,col:3

b1:
  t8 = load_global g0        @line:6,col:5
  t9 = load_local l2         @line:6,col:14
  t10 = call t8, [t9]        @line:6,col:5
  jump b3                    @line:6,col:5

b2:
  t11 = load_global g0       @line:8,col:5
  t12 = const k2             @line:8,col:14
  t13 = call t11, [t12]      @line:8,col:5
  jump b3                    @line:8,col:5

b3:
  return_nil                 @line:1,col:1
```

### Bytecode

```text
function principal
0:  LOAD_CONST 0
1:  STORE_LOCAL 0
2:  LOAD_CONST 1
3:  STORE_LOCAL 1
4:  LOAD_LOCAL 0
5:  LOAD_LOCAL 1
6:  ADD
7:  STORE_LOCAL 2
8:  LOAD_LOCAL 2
9:  LOAD_CONST 1
10: GT
11: JUMP_IF_FALSE 16
12: LOAD_GLOBAL 0
13: LOAD_LOCAL 2
14: CALL 1
15: JUMP 19
16: LOAD_GLOBAL 0
17: LOAD_CONST 2
18: CALL 1
19: NIL
20: RETURN
```

## Additional syntax domains

This section completes the design for the remaining language domains that are
expected to matter for a production-capable VM.

These domains are not part of the smallest implementation slice, but they are
part of the adopted architecture and must fit the same `AST -> LIR -> Bytecode
-> VM` chain.

### Design principle for all extended domains

Every additional domain must obey the same rules as the core LIR:

- source sugar is removed before bytecode generation
- control flow is explicit in LIR
- runtime-visible storage is explicit
- hidden behavior becomes first-class LIR instructions or metadata

## Object model

The VM object model supports:

- class declarations
- instance construction
- field storage
- method lookup
- inheritance
- interface conformance checks
- receiver-aware dispatch

### Runtime object entities

The runtime model contains:

- `ClassObject`
- `InstanceObject`
- `BoundMethod`
- `InterfaceObject`

Conceptually:

```text
ClassObject
  name
  parent_class
  method_table
  field_layout
  implemented_interfaces

InstanceObject
  class_ref
  field_slots

BoundMethod
  receiver
  function_ref

InterfaceObject
  name
  required_methods
```

### LIR object instructions

The LIR includes these object-related instructions:

```text
dst = make_class ClassDescriptorId
dst = make_interface InterfaceDescriptorId
dst = new_instance ClassId, [arg0, arg1, ...]

dst = get_field obj, FieldId
set_field obj, FieldId, src

dst = get_method obj, MethodId
dst = bind_method obj, FunctionId

dst = get_parent_method obj, MethodId
dst = check_interface obj, InterfaceId
```

### Meaning of object instructions

- `make_class`:
  constructs the runtime class object from compiled class metadata
- `new_instance`:
  allocates a new instance and runs constructor logic if applicable
- `get_field` / `set_field`:
  access instance field storage
- `get_method`:
  resolve a method through the class hierarchy
- `bind_method`:
  package a receiver plus function into a callable value
- `get_parent_method`:
  resolve a method through the direct parent chain for `parent`
- `check_interface`:
  runtime helper used where interface conformance must be asserted dynamically

### Lowering source `ici`

`ici` never survives as source syntax beyond AST lowering.

Rule:

- every method body receives an implicit first local for the receiver
- `ici` lowers to that local

Conceptually:

```lum
fonction presenter() {
  retourne ici.nom
}
```

lowers to:

```text
params:
  l0 = __self

b0:
  t0 = get_field l0, nom
  return t0
```

### Lowering source `parent`

`parent` also does not survive as source syntax.

Rule:

- `parent.methode()` lowers to a parent-method resolution plus an explicit call
  with the same receiver

Conceptual LIR:

```text
t0 = load_local l0                ; receiver
t1 = get_parent_method t0, presenter
t2 = call t1, [t0]
```

### Field identity

Field names should not remain raw strings after LIR construction.

The compiler should assign each declared field a stable `FieldId` within the
class layout.

That gives:

- predictable field-slot lookup
- better performance than repeated string lookup
- simpler bytecode operands

## Methods and dispatch

Method dispatch is an extension of the object model, but it is important enough
to define separately.

### Source forms

The relevant source-level call forms are:

- free-function call
- method call
- parent call
- constructor-like class call if the language keeps that surface form

### Unified LIR call policy

LIR keeps one general `call` instruction, but the compiler is allowed to use
pre-call helper instructions to make the callee explicit.

Examples:

```text
dst = call callee, [arg0, arg1, ...]
```

and before it:

```text
t0 = get_method obj, presenter
t1 = call t0, [obj]
```

That means:

- call mechanics stay uniform
- dispatch complexity stays out of the base call instruction

### Receiver passing rule

The VM does not infer receivers implicitly.

Rule:

- receiver-aware calls are lowered to explicit receiver arguments or bound
  method values before `call`

This keeps runtime call semantics simpler.

## Indexing and collection mutation

Indexing applies to:

- lists
- fixed lists
- dictionaries
- text
- potentially objects exposing indexing behavior later

### LIR indexing instructions

```text
dst = get_index obj, index
set_index obj, index, src
dst = get_length obj
```

### Semantics

- `get_index`:
  reads an indexed value
- `set_index`:
  writes an indexed value where the language allows mutation
- `get_length`:
  retrieves the size/length of an indexable collection

### Why indexing is separate from fields

Field access and indexed access are different semantic operations:

- field access uses named slots
- indexed access uses runtime keys or integer offsets

They remain distinct in LIR and bytecode.

## Iteration

Iteration must support the current language semantics of `pour chaque`.

### Iteration protocol model

The VM iteration model should use an explicit iterator protocol, even if some
types internally optimize it later.

Conceptual protocol:

- `get_iter value` returns an iterator object/value
- `iter_has_next iterator` checks whether iteration can continue
- `iter_next iterator` yields the next value

### LIR iteration instructions

```text
dst = get_iter src
dst = iter_has_next iterator
dst = iter_next iterator
```

Loop lowering pattern:

```text
b0:
  t0 = get_iter l0
  jump b1

b1:
  t1 = iter_has_next t0
  branch t1, b2, b3

b2:
  t2 = iter_next t0
  store_local l1, t2
  ...
  jump b1

b3:
  return_nil
```

### Why not a single magic foreach instruction

The design deliberately avoids a monolithic `foreach` VM instruction because:

- it hides control flow
- it hides iteration state
- it becomes harder to adapt to multiple iterable types

Explicit iterator operations keep the model teachable and optimizable later.

## Imports and module initialization

The source language supports `importer`, including dotted modules and selective
imports.

### Module runtime model

Each imported module is compiled as a module unit with:

- module globals
- module public export table
- one `__module_init__` function

### Import lowering model

Importing a module lowers conceptually into:

1. resolve module identity
2. ensure the target module is loaded/compiled
3. ensure its `__module_init__` has run exactly once
4. read exports from the module export table
5. bind those exports into the importer's environment/global table

### LIR import instructions

```text
dst = load_module ModuleId
dst = get_export module, ExportId
bind_global GlobalId, src
```

### Selective imports

Example source:

```lum
importer outils.maths.{tripler, base comme origine}
```

Conceptual LIR:

```text
t0 = load_module outils.maths
t1 = get_export t0, tripler
bind_global tripler, t1
t2 = get_export t0, base
bind_global origine, t2
```

### Module initialization invariant

Every module is initialized at most once per process/runtime instance.

This matches the current tree-walker direction and keeps import behavior
predictable.

## Closures and captured variables

Closures are the most important future domain to design before implementation,
even if they are not in the first VM milestone.

### Source model

Anonymous functions and nested functions may capture outer variables.

That means stack-only locals are not always sufficient.

### Runtime model

The closure-capable runtime needs:

- `FunctionObject`
- `ClosureObject`
- `UpvalueObject`

Conceptually:

```text
FunctionObject
  bytecode_ref
  arity
  metadata

ClosureObject
  function_ref
  captured_upvalues

UpvalueObject
  storage_ref
  is_closed
  closed_value
```

### Capture rule

A local that is referenced by an inner function becomes capture-eligible.

The lowering pipeline must identify:

- which locals are captured
- from which lexical depth
- in what order they are stored in the closure

### LIR closure instructions

```text
dst = make_closure FunctionId, [CaptureSpec...]
dst = get_upvalue UpvalueId
set_upvalue UpvalueId, src
close_upvalues ScopeMarker
```

### Why closures need explicit design

Closures change function and local semantics:

- some locals outlive their declaring frame
- functions are no longer just code pointers
- return from a frame may require closing captured values

So closures must not be improvised after the rest of the VM is already fixed.

## Interfaces

Interfaces matter enough in Lumiere that the VM design should account for them
explicitly, even if most conformance checking may happen before runtime.

### Interface runtime role

Interfaces serve two possible roles:

1. compile-time / semantic conformance checking
2. runtime type tests and dispatch support where needed

The design should support both, even if runtime checking is minimized later.

### Interface LIR instructions

```text
dst = make_interface InterfaceDescriptorId
dst = check_interface obj, InterfaceId
dst = implements_interface ClassId, InterfaceId
```

### Preferred division of responsibility

The frontend or semantic stage should do as much interface conformance checking
as possible.

The VM should only carry the runtime pieces needed for:

- `est`-style runtime checks
- preserving interface identity in runtime values

## Full domain list after this expansion

After this design expansion, the VM architecture now covers:

- arithmetic and comparison
- locals and temporaries
- branching and returns
- calls and builtins
- objects and fields
- methods and receiver dispatch
- indexing and mutation
- iteration
- imports and module initialization
- interfaces
- closures and captured variables

That means the design is intentionally exhaustive at the architecture level,
even though implementation will still proceed incrementally.

## Non-goals for the initial LIR

The initial LIR explicitly avoids:

- SSA
- phi nodes
- heavy optimizer framework
- complicated alias analysis
- speculative inlining
- object-model optimization
- backend-specific special cases embedded in high-level lowering

## Future evolution

The design intentionally leaves room for:

- an optional future HIR
- optional SSA transforms
- dead code elimination
- constant propagation
- copy propagation
- call specialization
- richer object support

These are future layers on top of the base design, not part of the adopted core
today.

## Final adopted design

Lumiere adopts:

```text
Source
-> CFG
-> Lexer / Tokenizer
-> Parser
-> AST
-> LIR
-> Bytecode
-> VM
```

with:

- no HIR initially
- block-based TAC-like LIR
- explicit locals, temps, and terminators
- stack-oriented bytecode
- a stack-based VM
- SSA postponed until optimization pressure justifies it

This is the architecture to review and implement.
