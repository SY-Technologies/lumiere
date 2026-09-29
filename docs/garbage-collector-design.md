# Lumiere Garbage Collector Design

Status: superseded in part -- see the note below  
Collector: precise, tracing, non-moving mark-and-sweep  
Scope: runtime values shared by the tree walker and bytecode VM

> **What was actually built.** The runtime collects cycles today, but not with
> the tracing collector this document designs. It uses Bacon-Rajan synchronous
> cycle collection driven by the reference counts, because a tracing collector
> needs to enumerate its roots and the tree walker holds `Value`s in C++ locals
> everywhere, so there is no root set to enumerate. There is no `GcPtr`, no
> `Tracer` and no safe-point machinery; `Ref<T>` is the handle,
> `trace_references`/`clear_references` are the per-object hooks, and collection
> runs on loop back edges and function returns in both engines.
>
> Two of this document's conclusions were adopted as written, because they are
> about what C++ cannot show a collector rather than about which collector to
> build: section 13's rule that a native handler must not be the only holder of
> a managed value (implemented as `LumiereFunction::native_captures`, with the
> handler capturing a raw pointer), and section 14's typed `NativeState` base in
> place of `shared_ptr<void>` (implemented as a counted object, with both hooks
> left pure so a new state must answer). Both were live leaks when they were
> found. Read the rest of this document as the design it was, and
> `RUNTIME_HARDENING.md` under "Reference cycles" for the shipped behaviour.

## 1. Decision

Lumiere will use a tracing garbage-collected heap for language-level reference
values.

The first collector will be:

- precise rather than conservative;
- stop-the-world;
- non-moving;
- single-generation;
- mark-and-sweep;
- shared by the tree walker and VM;
- triggered only at explicit safe points.

This is intentionally the smallest collector that correctly supports Lumiere's
existing semantics. Moving collection, generations, parallel marking, and
incremental collection are possible later, but none belong in the first
implementation.

C++ RAII remains responsible for resources outside the Lumiere object graph,
including files, sockets, threads, and other operating-system handles. GC
determines when the containing Lumiere object is unreachable; destruction of
that object then releases its native RAII state.

## 2. Why reference counting is insufficient

Lumiere values form arbitrary directed graphs. They do not form an ownership
tree or DAG.

Cycles already arise through:

1. A list containing itself or two containers containing each other.
2. Object fields referring to the same object or mutually referring objects.
3. A bound function retaining its receiver while the receiver retains the
   function.
4. A result payload referring into a graph that refers back to the result.
5. A tree-walker environment containing a function whose closure owns that
   environment.
6. A VM capture cell containing a closure whose capture list contains that
   cell.
7. Module members and closure environments retaining one another.

The tree-walker closure cycle is automatic:

```text
Environment
  -> binding Value
  -> LumiereFunction
  -> TreeWalkerFunctionBody
  -> closure_owner
  -> Environment
```

Scattered `weak_ptr` substitutions cannot solve this generally. There is no
language-level edge that is always non-owning, and weakening closure references
would make valid escaping closures dangle.

## 3. Goals

The implementation must:

- reclaim unreachable cyclic and acyclic language objects;
- preserve object identity and mutable aliasing;
- support both execution backends with one heap representation;
- preserve escaping closures;
- preserve VM capture-cell semantics;
- make all traced edges explicit and reviewable;
- integrate safely with native functions and asynchronous native state;
- release all heap objects at runtime shutdown, even if no collection occurred;
- avoid relying on C++ stack scanning;
- avoid hidden owning references to managed objects;
- make collection deterministic in tests;
- make later collector improvements possible without redesigning `Value`.

## 4. Non-goals for the first implementation

The first implementation will not:

- move objects;
- compact the heap;
- provide generations;
- collect concurrently;
- mark concurrently;
- guarantee prompt finalization;
- use GC as the primary way to close files or network resources;
- expose weak references to Lumiere programs;
- preserve the current `shared_ptr` ABI for managed values;
- collect at arbitrary C++ allocation sites.

## 5. Core invariants

These invariants are non-negotiable.

### 5.1 Heap ownership

Every language-level reference object belongs to exactly one `RuntimeHeap`.
Managed objects never transfer between heaps.

### 5.2 Non-owning language references

References between managed objects are non-owning `GcPtr<T>` values. They must
never be `shared_ptr<T>`.

The heap is the sole owner of managed objects.

### 5.3 Explicit graph edges

Every managed edge must be visible to tracing through one of:

- a `Value`;
- a `GcPtr<T>`;
- a managed object's `trace(Tracer&)` method;
- a registered runtime root;
- a registered temporary or persistent root.

No managed pointer may be hidden only inside:

- `std::function` captures;
- `shared_ptr<void>`;
- an untraced native state object;
- an integer or `void *`;
- an asynchronous task capture;
- a backend-private structure without a trace method.

### 5.4 Safe collection

Collection occurs only at explicit safe points. At a safe point, every live
managed reference must be reachable from the root set.

Allocation may request a collection, but must not synchronously collect.
Deferring collection prevents an allocation deep in C++ code from unexpectedly
collecting an unregistered temporary.

### 5.5 Thread confinement

A `RuntimeHeap` is owned by one execution thread. Only that thread may:

- allocate managed objects;
- mutate managed objects;
- run the collector;
- trace the heap.

Worker threads may own native state. They may not directly traverse or mutate
managed objects. Work involving Lumiere values must be scheduled onto the heap
owner thread.

### 5.6 Destruction

A managed object's destructor must not:

- allocate another managed object;
- trigger collection;
- resurrect the object;
- call arbitrary Lumiere code;
- block waiting for a worker that needs the runtime thread.

## 6. Managed object model

Introduce a common base:

```cpp
class Tracer;

class GcObject
{
public:
    virtual ~GcObject() = default;
    virtual void trace(Tracer &tracer) = 0;

private:
    friend class RuntimeHeap;
    bool m_marked = false;
    std::size_t m_size = 0;
};
```

`m_size` is the allocation size used for collection thresholds. Exact retained
size accounting is not required initially; `sizeof(T)` plus obvious owned
buffer capacities is sufficient.

Use a small typed, non-owning pointer wrapper:

```cpp
template <typename T>
class GcPtr
{
public:
    GcPtr() = default;

    T *get() const { return m_pointer; }
    T &operator*() const { return *m_pointer; }
    T *operator->() const { return m_pointer; }
    explicit operator bool() const { return m_pointer != nullptr; }

    friend bool operator==(GcPtr, GcPtr) = default;

private:
    friend class RuntimeHeap;
    explicit GcPtr(T *pointer) : m_pointer(pointer) {}

    T *m_pointer = nullptr;
};
```

`GcPtr` must remain trivial or nearly trivial. It does not increment a count and
does not affect lifetime. Construction from arbitrary raw pointers should not
be public.

The heap owns objects:

```cpp
class RuntimeHeap
{
public:
    template <typename T, typename... Args>
    GcPtr<T> make(Args &&...args);

    void request_collection();
    void collect(RootProvider &roots);
    void collect_now_for_testing(RootProvider &roots);

    std::size_t live_object_count() const;
    std::size_t live_bytes() const;
    std::size_t collection_count() const;

private:
    std::vector<std::unique_ptr<GcObject>> m_objects;
    std::size_t m_live_bytes = 0;
    std::size_t m_next_threshold = 0;
    std::size_t m_collection_count = 0;
    bool m_collection_requested = false;
    bool m_collecting = false;
};
```

A `vector<unique_ptr<GcObject>>` is sufficient. Vector growth moves
`unique_ptr`s, not their pointees, so managed addresses remain stable. Sweep
compacts the vector by removing unmarked objects.

Do not optimize this into an intrusive allocation list until profiling shows a
reason.

## 7. Which types are managed

The following types must become `GcObject`s:

- `ListeData`;
- `ListeFixeData`;
- `DictData`;
- `EnsembleData`;
- `ResultData`;
- `LumiereObject`;
- `LumiereFunction`;
- `LumiereClass`;
- `LumiereInterface`;
- `Environment`;
- VM capture cells;
- `Module`, unless module ownership is retained externally with an explicit
  complete `trace` implementation;
- any backend function/class body that directly contains managed edges.

The preferred design is for backend bodies to remain polymorphic but become
uniquely owned implementation payloads:

```cpp
struct RuntimeFunctionBody
{
    virtual ~RuntimeFunctionBody() = default;
    virtual void trace(Tracer &tracer) = 0;
};
```

`LumiereFunction` uniquely owns its body:

```cpp
std::unique_ptr<RuntimeFunctionBody> body;
```

The body is not independently visible to Lumiere and does not need its own heap
identity. Its `trace` method exposes its edges through the containing function.
Apply the same rule to class and interface bodies.

Keep these values outside the GC heap:

- integers, decimals, booleans, symbols, and text stored inline in `Value`;
- AST nodes owned by parser/program structures;
- semantic-analysis types;
- bytecode and immutable compiler metadata;
- native resource state owned by C++ RAII, subject to the native-state rules
  below.

Text can remain inline initially. Moving large strings to managed storage is an
independent optimization.

## 8. `Value` representation

Replace managed `shared_ptr` alternatives in `Value::Data` with `GcPtr`:

```cpp
using Data = std::variant<
    int64_t,
    double,
    bool,
    char32_t,
    std::string,
    GcPtr<ListeData>,
    GcPtr<ListeFixeData>,
    GcPtr<DictData>,
    GcPtr<EnsembleData>,
    GcPtr<LumiereObject>,
    GcPtr<LumiereFunction>,
    GcPtr<LumiereClass>,
    GcPtr<LumiereInterface>,
    GcPtr<ResultData>>;
```

Factories accept `GcPtr<T>`. Accessors return `GcPtr<T>`.

`Value` remains cheaply copyable. Copying a `Value` copies a non-owning managed
pointer and does not allocate.

Do not retain two parallel ownership systems for the same runtime type. A type
must not sometimes appear as `shared_ptr<T>` and sometimes as `GcPtr<T>`.

The existing explicit `Type` tag may remain during this work. Removing the
duplicated tag is unrelated and would enlarge the migration unnecessarily.

## 9. Tracing

Tracing uses an explicit worklist rather than recursive C++ calls:

```cpp
class Tracer
{
public:
    void mark(const Value &value);

    template <typename T>
    void mark(GcPtr<T> pointer);

private:
    friend class RuntimeHeap;
    std::vector<GcObject *> m_worklist;
};
```

`mark(pointer)`:

1. Returns for null.
2. Asserts in debug builds that the object belongs to this heap.
3. Returns if already marked.
4. Sets the mark bit.
5. Pushes the object onto the worklist.

The collector drains the worklist iteratively:

```cpp
while (!worklist.empty())
{
    GcObject *object = worklist.back();
    worklist.pop_back();
    object->trace(tracer);
}
```

This prevents stack overflow on deep or cyclic graphs.

### 9.1 Edge table

Each type traces exactly these edges:

| Type | Outgoing managed edges |
| --- | --- |
| `ListeData` | every element |
| `ListeFixeData` | every element |
| `DictData` | every key and value |
| `EnsembleData` | every element |
| `ResultData` | payload |
| `LumiereObject` | class and every field value |
| `LumiereFunction` | receiver, native captures, body edges |
| `LumiereClass` | parent, interfaces, body edges |
| `LumiereInterface` | body edges |
| `Environment` | parent and every binding value |
| VM capture cell | contained value |
| `Module` | state edges and every member value |

If runtime type annotations contain only strings or immutable semantic
descriptors, they are not managed edges.

## 10. Mark-and-sweep algorithm

Collection is:

1. Assert execution is on the heap owner thread.
2. Assert the heap is not already collecting.
3. Clear `m_collection_requested`.
4. Create a `Tracer`.
5. Ask all root providers to mark roots.
6. Mark temporary roots.
7. Mark persistent roots.
8. Drain the tracing worklist.
9. Sweep:
   - retain marked objects and clear their mark bits;
   - destroy and remove unmarked objects.
10. Recalculate live-byte accounting.
11. Set the next threshold.
12. Increment the collection count.

Never destroy objects while tracing. Sweep only after the mark worklist is
empty.

A reasonable initial threshold policy:

```text
initial threshold = 1 MiB
next threshold = max(1 MiB, live_bytes * 2)
```

Also permit a test/configuration mode based on allocation count so tests do not
depend on byte estimates.

## 11. Roots

Use a common root-provider interface:

```cpp
class RootProvider
{
public:
    virtual ~RootProvider() = default;
    virtual void trace_roots(Tracer &tracer) = 0;
};
```

Each backend implements it. The heap also owns registries for scoped temporary
roots and persistent external roots.

### 11.1 Tree-walker roots

The tree walker must trace:

- `m_env_owner`, replaced by `GcPtr<Environment>`;
- `m_result`;
- `m_self`;
- every module in `m_modules`;
- any loaded module state containing managed references;
- active temporary roots registered on the shadow root stack;
- LumiTest or standard-library runtime state that explicitly exposes managed
  references.

`m_env` may remain a raw non-owning convenience pointer if `m_env_owner` is the
traced authoritative reference. Prefer replacing both with one
`GcPtr<Environment>` to eliminate disagreement.

Environment parent links become `GcPtr<Environment>`. The separate raw parent
pointer and owning parent pointer are no longer needed.

### 11.2 VM roots

The VM must trace:

- all defined globals;
- every operand-stack `Value`;
- every active call frame's local capture cells;
- every active call frame's captured cells;
- the currently invoked closure if it is not otherwise on the operand stack;
- loaded module namespace values;
- callback values held by runtime services;
- active temporary roots registered on the shadow root stack.

`CallFrame::locals` and `CallFrame::captures` become vectors of
`GcPtr<VmCell>`, where:

```cpp
struct VmCell final : GcObject
{
    Value value;
    void trace(Tracer &tracer) override { tracer.mark(value); }
};
```

`VmClosureBody::captures` and method-capture tables use the same `GcPtr<VmCell>`.
This directly supports recursive closures without reference-count cycles.

### 11.3 AST and bytecode are not roots

AST and bytecode contain descriptions of execution, not live runtime values.
They are retained by their existing C++ owners and are not traced.

Tree-walker function bodies contain raw AST pointers. Their owning `Program` or
loaded module statement list must continue to outlive every function that can
refer to those AST nodes. GC does not change that pre-existing requirement.

## 12. Temporary roots and safe points

Precise GC cannot discover a `Value` held only in an arbitrary C++ local.

Provide scoped roots:

```cpp
class RootedValue
{
public:
    RootedValue(RuntimeHeap &heap, Value value);
    ~RootedValue();

    RootedValue(const RootedValue &) = delete;
    RootedValue &operator=(const RootedValue &) = delete;

    Value &get();
    const Value &get() const;

private:
    RuntimeHeap *m_heap;
    Value m_value;
};
```

Construction registers the stable address of `m_value` in a LIFO shadow root
stack. Destruction unregisters it. Moving should be disabled initially because
the registry stores its address.

Provide an equivalent `RootedGcPtr<T>` only if needed. Prefer wrapping a pointer
in a `Value` when that remains clear.

Use a temporary root when a C++ local managed value:

- survives a call that may allocate;
- survives a call that may reach a GC safe point;
- is stored in native code before being attached to another traced object;
- is removed from one traced container before being installed in another.

### 12.1 Allocation does not collect

`RuntimeHeap::make` only:

- allocates;
- records ownership;
- updates allocation accounting;
- sets `m_collection_requested` when the threshold is crossed.

It does not call `collect`.

### 12.2 Explicit safe point

Backends call:

```cpp
heap.safe_point(*this);
```

`safe_point` collects only when requested. A forced test API collects regardless
of the threshold.

Initial safe points:

- VM: at the top of the opcode dispatch loop, before decoding the next opcode;
- VM: after returning from a native call, once the result is on the operand
  stack;
- tree walker: between top-level statements;
- tree walker: at loop backedges after loop-carried values are rooted;
- tree walker: after a function call result has been placed in a traced or
  temporary-rooted location;
- REPL: after each submission result has been rooted for presentation;
- runtime shutdown: no tracing is required; destroy the whole heap.

Before enabling a tree-walker safe point inside a visitor, audit every caller
above it for unregistered locals. If uncertain, omit that safe point. Delayed
collection is safe; collecting with an incomplete root set is not.

### 12.3 No-GC scope

Provide a narrowly used `NoGcScope` that suppresses safe-point collection while
active. Allocation remains allowed and can request a later collection.

Use it only when adapting legacy code that has not yet been root-audited.
Permanent broad `NoGcScope`s would turn GC into an arena and are not an
acceptable final state.

## 13. Native function boundary

`std::function` captures are opaque to the tracer. A native handler must never
be the only owner/reference holder for a managed value.

Change native functions to store traceable captures explicitly:

```cpp
using NativeHandler = std::function<Value(
    IRuntime &,
    const NativeArgs &,
    std::span<Value>)>;

struct LumiereFunction final : GcObject
{
    NativeHandler native_handler;
    std::vector<Value> native_captures;
    // ...
};
```

`LumiereFunction::trace` marks `native_captures`.

Native lambdas may capture:

- immutable C++ data;
- `shared_ptr` native state that contains no unmanaged `GcPtr`;
- integer indexes into `native_captures`;
- non-owning service pointers whose lifetime is guaranteed by the active
  runtime call.

Native lambdas must not capture `Value`, `GcPtr`, or raw managed-object pointers.
Managed captures belong in `native_captures` and are accessed through the span
passed to the handler.

Add debug-oriented helper APIs so creating a native function with managed
captures is explicit:

```cpp
GcPtr<LumiereFunction> make_native_function(
    NativeHandler handler,
    std::vector<Value> captures = {});
```

Bound native methods trace their `receiver` through `LumiereFunction`.

## 14. Native object state

The current `shared_ptr<void> native_state` is opaque. Use a typed base:

```cpp
struct NativeState
{
    virtual ~NativeState() = default;
    virtual void trace(Tracer &) {}
};
```

`LumiereObject` may retain `shared_ptr<NativeState>` because native state has
C++ ownership and destruction semantics. The default trace method is empty.

If a native state stores Lumiere values, it must override `trace`. Such state
must only be traced and mutated on the runtime thread.

Prefer storing long-lived callbacks as explicit persistent roots rather than
raw `Value` fields in worker-owned state.

Do not use `shared_ptr<NativeState>` to own a `GcObject`. Native and managed
ownership domains must remain separate.

## 15. Persistent roots and asynchronous work

Some native systems retain callbacks after the native function returns.
Introduce a move-only persistent root:

```cpp
class PersistentValue
{
public:
    PersistentValue(RuntimeHeap &heap, Value value);
    ~PersistentValue();

    PersistentValue(PersistentValue &&);
    PersistentValue &operator=(PersistentValue &&);

    Value get_on_runtime_thread() const;
    void reset();
};
```

The heap keeps a synchronized registry of persistent-root slots. Registration
and removal may be requested from worker threads, but reading or invoking the
managed value occurs only on the runtime thread.

The preferred asynchronous flow is:

```text
Lumiere callback
  -> PersistentValue registered
  -> worker retains native data and persistent-root token
  -> worker posts completion to runtime event queue
  -> runtime thread obtains callback value
  -> runtime invokes callback
  -> token reset when subscription/server is closed
```

Audit LumiNet and LumiTest for:

- callback `Value`s stored in native state;
- callbacks captured in lambdas;
- objects captured by worker tasks;
- native state that points back to its containing Lumiere object;
- shutdown paths that must release persistent roots.

The collector must not run concurrently with a worker traversing managed
memory. Persistent rooting solves lifetime, not data races.

## 16. Runtime type metadata

The existing type-constraint maps are keyed by raw container addresses:

```cpp
unordered_map<const ListeData *, ListConstraint>
```

They retain stale entries after a container dies. If an allocator reuses an
address, a new container can inherit unrelated constraints.

Move constraint metadata onto its corresponding managed object:

```cpp
struct ListeData final : GcObject
{
    std::vector<Value> elements;
    std::optional<TypeConstraint> element_constraint;
    // ...
};
```

Do the same for fixed lists, dictionaries, and sets. This makes metadata lifetime
identical to object lifetime and removes backend-side pointer maps.

Use one shared constraint representation where possible so tree walker and VM
do not implement different heap-object layouts.

## 17. Cycle-safe value operations

GC makes cyclic graphs legitimate, so runtime operations must terminate on
cycles.

### 17.1 String conversion

`Value::to_string()` currently recursively prints list, dictionary, result, and
selected object fields. Add a formatting context containing an active-path set:

```cpp
struct FormatContext
{
    std::unordered_set<const GcObject *> active;
};
```

When formatting reaches an object already on the active recursion path, print a
stable placeholder such as:

```text
<cycle>
```

Use an active-path set, not a global visited set. Repeated aliases that are not
recursive should still print normally.

### 17.2 Equality

Identity-based equality needs no recursion. Structural equality must track
already-compared object pairs:

```cpp
unordered_set<ObjectPair, ObjectPairHash>
```

Define the language semantics before expanding structural equality. Do not let
backend-specific recursion produce different answers or stack overflow.

### 17.3 Type checking and traversal

Any recursive traversal of nested type-constrained containers must use a
visited set. This includes:

- annotation registration;
- runtime generic validation;
- conversion;
- deep formatting;
- any future serialization.

## 18. Backend integration

Both backends receive the same heap abstraction:

```cpp
class Backend
{
protected:
    RuntimeHeap m_heap;
};
```

Alternatively inject a heap into each backend constructor. Do not create a
separate object representation or collector per backend.

`IRuntime` should expose only the minimum heap operations needed by standard
library code:

```cpp
virtual RuntimeHeap &heap() = 0;
```

Prefer higher-level factories over allowing stdlib code to manipulate collector
internals.

All creation sites must migrate from:

```cpp
std::make_shared<ListeData>()
```

to:

```cpp
runtime.heap().make<ListeData>()
```

This includes runtime code, both backends, builtin modules, LumiNet, LumiTest,
and tests that construct runtime values directly.

## 19. Migration plan

Implement in small compilable stages. Do not attempt a repository-wide blind
replacement of `shared_ptr`.

### Phase 0: Baseline and instrumentation

1. Add focused tests that construct known cycles in both backends.
2. Add test-only live-object counters to current runtime entities if necessary.
3. Add LeakSanitizer coverage where supported.
4. Record current behavior for closures, recursion, bound methods, imports,
   LumiTest callbacks, and LumiNet callbacks.
5. Add cycle-safe formatting tests before enabling arbitrary cyclic values in
   diagnostic paths.

This phase proves the leak and protects semantics. It does not try to fix cycles
with destructors or `weak_ptr`.

### Phase 1: Heap primitives

1. Add `GcObject`, `GcPtr`, `Tracer`, `RootProvider`, and `RuntimeHeap`.
2. Add allocation accounting and forced collection for tests.
3. Add heap-membership debug assertions.
4. Add shadow-stack support with `RootedValue`.
5. Unit-test isolated object graphs:
   - unreachable object reclaimed;
   - rooted object retained;
   - rooted parent retains child;
   - unreachable cycle reclaimed;
   - reachable cycle retained;
   - deep graph marking does not recurse on the C++ stack.

Do not connect production `Value` yet.

### Phase 2: Containers and results

1. Convert list, fixed-list, dictionary, set, and result storage to managed
   objects.
2. Move type constraints onto container objects.
3. Update factories, accessors, stdlib helpers, and both backends.
4. Implement tracing for these types.
5. Keep automatic collection disabled except in targeted forced-collection
   tests until roots are complete.
6. Add self-cycle and mutual-container-cycle tests.

At no point may a converted type still be created through `make_shared`.

### Phase 3: Objects, classes, and interfaces

1. Convert objects, classes, and interfaces.
2. Convert body ownership to `unique_ptr` plus explicit tracing.
3. Introduce typed `NativeState`.
4. Trace class, interfaces, and fields.
5. Test:
   - self field;
   - mutual object fields;
   - inheritance retention;
   - unreachable class/object graph collection;
   - native state destruction after sweep.

### Phase 4: Functions and native captures

1. Convert `LumiereFunction`.
2. Add explicit `native_captures`.
3. Remove managed `Value`/pointer captures from native lambdas.
4. Convert bound receivers.
5. Make backend function bodies trace their edges.
6. Test bound-method cycles and native-capture retention.

This is the phase where a strict audit of every native lambda is mandatory.

### Phase 5: Tree-walker environments

1. Convert `Environment` to a managed object.
2. Replace parent ownership/raw-pointer pair with `GcPtr<Environment>`.
3. Change closure bodies to retain `GcPtr<Environment>`.
4. Trace all environment bindings and parents.
5. Implement tree-walker roots.
6. Add temporary roots at audited allocation boundaries.
7. Enable safe points first between top-level statements.
8. Add loop-backedge and call-boundary safe points only after root audits.
9. Test:
   - ordinary global function no longer leaks its environment;
   - escaping closure survives collection;
   - mutually recursive local functions survive while reachable;
   - unreachable closure/environment cycles are reclaimed;
   - module closure cycles are reclaimed after module roots disappear;
   - REPL state remains reachable across submissions.

### Phase 6: VM capture cells and roots

1. Introduce managed `VmCell`.
2. Convert frame locals, captures, closure captures, and method captures.
3. Implement complete VM root tracing.
4. Root the active callee during callback execution.
5. Enable the opcode-loop safe point.
6. Test:
   - escaping capture;
   - mutable capture;
   - recursive nested closure cycle;
   - method capture;
   - callback from native code into bytecode;
   - forced collection at every opcode safe point.

### Phase 7: Modules and native asynchronous state

1. Convert modules or give externally owned modules complete tracing.
2. Audit module states.
3. Add `PersistentValue`.
4. Migrate LumiTest callback retention.
5. Migrate LumiNet callback/subscription retention.
6. Enforce runtime-thread access to managed values.
7. Test collection while network/native state remains alive and after shutdown.

### Phase 8: Enable normal collection

1. Enable threshold-triggered collection in both backends.
2. Add command-line or test configuration for:
   - collection disabled;
   - collect on every safe point;
   - custom threshold;
   - GC statistics.
3. Run the complete test suite in normal mode.
4. Run the complete runtime suite with collection forced at every safe point.
5. Run ASan/LSan and UBSan configurations.
6. Benchmark representative scripts and server workloads.

### Phase 9: Remove transitional ownership

1. Search for `shared_ptr` to every managed runtime type.
2. Search for `make_shared` creation of every managed runtime type.
3. Search native lambdas for captured `Value` or managed pointers.
4. Remove obsolete owner fields and raw-pointer metadata maps.
5. Remove temporary compatibility adapters.
6. Update architecture documentation.

## 20. Unsafe partial states

The implementation must not ship in any of these states:

### 20.1 Collecting before roots are complete

This causes use-after-free and is worse than leaking. Keep automatic collection
disabled during early migration.

### 20.2 Mixed ownership for one type

If both the heap and a `shared_ptr` believe they own the same object, double
destruction is possible. If only `shared_ptr` owns some instances, the tracer
cannot reason about them.

### 20.3 Hidden native captures

A native lambda holding an untraced `Value` can point to a swept object.

### 20.4 Raw managed pointers across safe points

A raw pointer or `GcPtr` in an unregistered C++ local is not a root.

### 20.5 Worker-thread heap access

Even a persistently rooted object is not safe to mutate concurrently with the
runtime or collector.

### 20.6 Finalizers that invoke Lumiere

Sweep order is unspecified. Calling back into partially swept graphs is invalid.

## 21. Verification strategy

### 21.1 Heap unit tests

Expose deterministic counters and forced collection. Tests should verify exact
live-object counts where object construction is controlled.

Required graph shapes:

```text
unrooted: A                         -> collect A
rooted:   root -> A                 -> retain A
chain:    root -> A -> B -> C       -> retain all
cycle:    A <-> B                   -> collect both
cycle:    root -> A <-> B           -> retain both
diamond:  root -> A -> B,D; A -> C -> D
deep:     root -> 100,000 nodes     -> no C++ recursion overflow
```

### 21.2 Language-level tests

Cover both backends wherever both implement the feature:

```lumiere
soit xs = [rien]
xs[0] = xs
```

Also cover:

- two mutually linked lists;
- self and mutual object fields;
- a bound method stored on its receiver;
- escaping closures;
- recursive nested closures;
- module-exported closures;
- cyclic data passed through `Succès`/`Échec`;
- printing cyclic structures;
- equality involving cyclic structures;
- type-constrained cyclic containers where allowed by the type system.

Tests must make cycles unreachable and then force collection. Merely completing
the program does not prove collection during a long-lived runtime.

### 21.3 Stress modes

Add a mode that requests collection at every safe point. Run:

- all interpreter fixture tests;
- all CLI integration tests;
- REPL incremental-execution tests;
- module import tests;
- LumiTest callback tests;
- LumiNet tests that do not depend on flaky external services.

This mode is the primary way to expose missing roots.

### 21.4 Sanitizers

Use:

- AddressSanitizer for invalid accesses;
- LeakSanitizer for missed external/native ownership;
- UndefinedBehaviorSanitizer;
- ThreadSanitizer for event-loop/native-thread integration where practical.

LSan may report intentionally retained process-global native registries. Add
specific suppressions only after proving the ownership is intentional.

## 22. Debugging support

Provide optional GC statistics:

```text
collections
objects allocated
objects live
bytes allocated
bytes live
bytes reclaimed
largest pause
```

In debug builds:

- assign each managed object a monotonically increasing ID;
- record its runtime kind;
- assert heap membership when tracing a pointer;
- assert that marking occurs only during collection;
- assert safe-point execution on the owner thread;
- poison or tag swept objects where sanitizers make this useful;
- optionally dump the retained graph from roots.

Do not expose raw addresses as stable object identities in language behavior.

## 23. Performance expectations

The first implementation favors correctness and clarity.

Expected costs:

- one heap allocation per reference object;
- virtual dispatch once per marked object;
- linear marking in the reachable graph;
- linear sweeping in the allocated-object list;
- stop-the-world pauses proportional to heap size.

Expected benefits over `shared_ptr`:

- cyclic garbage is reclaimed;
- copying `Value` no longer performs atomic reference-count operations;
- graph lifetime is centralized;
- language semantics no longer depend on guessed weak edges.

If profiling later shows pause problems, improve in this order:

1. better allocation thresholds;
2. segregated/free-list allocation;
3. incremental marking;
4. generations;
5. optional compaction or a moving young generation.

Do not introduce these before the basic collector and root model are proven.

## 24. Resource-lifetime semantics

GC finalization is nondeterministic from the program's perspective.

Native APIs representing scarce resources should provide explicit operations:

- `fermer`;
- `arrêter`;
- `annuler`;
- scoped APIs where appropriate.

The native-state destructor remains a fallback for exceptional paths and
runtime shutdown. Programs must not depend on a collection happening promptly
to release a port, flush output, or stop a thread.

If Lumiere later adds user-defined destructors, they must not be implemented as
ordinary sweep-time C++ destructors. They require a separate finalization queue
with carefully defined resurrection and ordering semantics. That is outside the
first collector.

## 25. API and code-style guidance

Keep the collector small and explicit:

- one heap owner;
- one pointer wrapper;
- one tracer;
- one root-provider interface;
- one scoped temporary-root type;
- one persistent-root type.

Avoid:

- template-heavy generic graph frameworks;
- visitor registries keyed by runtime type;
- macros for declaring traced fields;
- implicit thread-local heaps;
- global collectors;
- reference-count fallback inside `GcPtr`;
- clever pointer tagging in the first version.

Each `trace` implementation should be a short direct list of outgoing edges.
That repetition is useful: it makes correctness auditable.

## 26. Definition of done

The GC migration is complete when all of the following are true:

- no managed runtime type is owned by `shared_ptr`;
- every managed allocation goes through `RuntimeHeap`;
- every managed type has a complete trace implementation;
- both backends expose complete roots;
- all native managed captures are explicit;
- asynchronous callback retention uses persistent roots;
- container constraint metadata dies with its container;
- cyclic formatting and traversal terminate;
- unreachable container, object, closure, environment, module, and capture-cell
  cycles are reclaimed during execution;
- reachable cycles survive forced collection;
- the full suite passes with collection forced at every safe point;
- sanitizer runs show no use-after-free, double-free, or unexplained retained
  language graphs;
- runtime shutdown releases the entire heap and native state;
- documentation no longer describes `shared_ptr` as the language-runtime
  lifetime mechanism.

## 27. Implementation review checklist

For every pull request in this migration, review:

1. What new managed objects are allocated?
2. Which heap owns them?
3. What outgoing managed edges were added?
4. Where are those edges traced?
5. Can a C++ temporary holding them survive a safe point?
6. If yes, where is the temporary rooted?
7. Does a native lambda hide a managed reference?
8. Can a worker thread access the reference?
9. Is native resource destruction non-reentrant and nonblocking?
10. Is forced collection exercised by a test at the relevant boundary?

If any answer is unclear, collection must remain disabled at that boundary
until the ownership and root story is explicit.
