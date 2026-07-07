#pragma once

#include "lumiere/interpreter/runtime/value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumiere
{

enum class Opcode : std::uint8_t
{
    CONSTANT,
    NIL,
    TRUE_VALUE,
    FALSE_VALUE,
    GET_GLOBAL,
    CALL,
    POP,
    RETURN,
};

struct SourceLocation
{
    std::size_t line = 0;
    std::size_t column = 0;
};

struct Chunk
{
    std::vector<std::uint8_t> code;
    std::vector<Value> constants;
    std::vector<SourceLocation> locations;

    // Appends a value to the function-local constant pool and returns the
    // single-byte index encoded by CONSTANT and similar operands.
    std::uint8_t add_constant(Value value);

    // Emits one opcode byte and records the source location for diagnostics
    // and disassembly tooling.
    void write_opcode(Opcode opcode, SourceLocation location);

    // Emits one raw byte into the instruction stream and stores the matching
    // source location at the same byte offset.
    void write_byte(std::uint8_t byte, SourceLocation location);
};

struct FunctionBytecode
{
    std::string name;
    std::size_t arity = 0;
    Chunk chunk;
};

struct ModuleBytecode
{
    std::vector<FunctionBytecode> functions;
    std::size_t entry_function_index = 0;
};

} // namespace lumiere
