# Runtime boundaries

The tree-walker and bytecode VM share standard-library implementations without
sharing an execution engine. The boundary has four parts.

## Module registration

Both backends call:

```text
register_builtin_module(Module, optional LumiTest state)
```

The stdlib owns module-name dispatch and native callable construction. A backend
does not pass factories or select individual registration functions.

## Native invocation

A native `LumiereFunction` stores one `NativeHandler`. The active backend calls
that handler with its `IRuntime` services and a `NativeArgs` call bundle. Most
stdlib functions only validate arguments and calculate a `Value`.

```text
language call -> backend dispatch -> NativeHandler -> result
```

## Runtime services

`IRuntime` contains only operations whose semantics belong to the active
backend:

- call a Lumiere value
- raise a source-aware runtime error
- compare values
- convert a value to language text
- retain runtime type metadata

The VM implementation is named `VmRuntimeServices`. It is an adapter for these
operations, not another VM.

## Callback re-entry

Some native modules call Lumiere closures, notably LumiTest hooks and LumiNet
handlers. The VM configures one explicit callback executor on
`VmRuntimeServices`:

```text
NativeHandler
    -> IRuntime::call
    -> configured callback executor
    -> run_frames(shared VmExecutionState, closure)
```

`VmExecutionState` owns the references shared by normal execution, module
initializers, and callback re-entry: bytecode, global values, global-definition
flags, function lookup, native lookup, and initializer state. Re-entry therefore
cannot accidentally create a second global environment.

## File responsibilities

- `stdlib_helpers.cpp`: shared registration and argument helpers
- `stdlib/*.cpp`: module behavior
- `tree_walker/tree_walker_runtime.cpp`: tree-walker runtime services
- `vm/native_globals.cpp`: VM-only global builtins such as input and output
- `vm/vm.cpp`: stack, frames, opcode dispatch, and VM runtime services

Keep backend selection out of stdlib module implementations. Add a runtime
service only when behavior truly depends on the active execution engine.
