#pragma once

#include "lumiere/interpreter/runtime/value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lumiere
{

enum class Opcode : std::uint8_t
{
    CONSTANT,
    CONSTANT_LONG,
    NIL,
    TRUE_VALUE,
    FALSE_VALUE,
    GET_GLOBAL,
    GET_GLOBAL_LONG,
    SET_GLOBAL,
    SET_GLOBAL_LONG,
    INIT_GLOBAL,
    INIT_GLOBAL_LONG,
    GET_LOCAL,
    SET_LOCAL,
    GET_CAPTURE,
    SET_CAPTURE,
    CLOSURE,
    TRY_BEGIN,
    TRY_END,
    THROW,
    JUMP,
    JUMP_IF_FALSE,
    NEGATE,
    NOT,
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE,
    MODULO,
    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    CALL,
    CALL_GLOBAL,
    CALL_GLOBAL_LONG,
    CALL_MEMBER,
    CALL_MEMBER_LONG,
    CALL_PARENT,
    CALL_PARENT_LONG,
    GET_MEMBER,
    GET_MEMBER_LONG,
    GET_PARENT,
    GET_PARENT_LONG,
    SET_MEMBER,
    SET_MEMBER_LONG,
    CLASS,
    INTERFACE,
    NAMESPACE,
    LIST,
    DICTIONARY,
    SEQUENCE_LENGTH,
    INDEX_GET,
    INDEX_SET,
    CAST,
    CAST_LONG,
    TYPE_CHECK,
    TYPE_CHECK_LONG,
    ASSERT_TYPE,
    ASSERT_TYPE_LONG,
    MATCH_ERROR,
    POP,
    RETURN,
};

struct SourceLocation
{
    std::size_t line = 0;
    std::size_t column = 0;
};

// Represents a sequence of executable instructions
struct Chunk
{
    std::vector<std::uint8_t> code; // stream of instructions
    std::vector<Value> constants;
    std::vector<SourceLocation> locations;

    // Appends a value to the function-local constant pool and returns the
    // stable table index.
    std::size_t add_constant(Value value);

    // Emits one opcode byte and records the source location for diagnostics
    // and disassembly tooling.
    void write_opcode(Opcode opcode, SourceLocation location);

    // Emits one raw byte into the instruction stream and stores the matching
    // source location at the same byte offset.
    void write_byte(std::uint8_t byte, SourceLocation location);

    // Emits a 16-bit absolute code offset in big-endian order.
    void write_u16(std::uint16_t value, SourceLocation location);
    void write_u24(std::size_t value, SourceLocation location);

    // Emits either CONSTANT or CONSTANT_LONG depending on the width needed
    // by the encoded constant table index.
    void write_constant_ref(std::size_t index, SourceLocation location);

    // Emits either GET_GLOBAL or GET_GLOBAL_LONG depending on the width needed
    // by the encoded module-global index.
    void write_global_ref(std::size_t index, SourceLocation location);

    void write_cast_ref(std::size_t index, SourceLocation location);
    void write_type_check_ref(std::size_t index, SourceLocation location);
    void write_type_assertion(std::size_t index, SourceLocation location);
    void write_global_call(std::size_t index, std::uint8_t arity, SourceLocation location);
    void write_member_call(std::size_t index, std::uint8_t arity, SourceLocation location);
    void write_member_get(std::size_t index, SourceLocation location);
};

// Represents a Lumiere function compiled to bytecode.
struct FunctionBytecode
{
    std::string name;
    std::string source_path;
    std::string source_text;
    std::size_t arity = 0; // count of parameter
    std::size_t source_arity = 0;
    std::vector<bool> optional_params;
    std::size_t local_slot_count = 0;
    std::size_t capture_count = 0;
    Chunk chunk;
};

struct VmFieldDescriptor
{
    std::string name;
    std::string type;
    bool is_private = false;
    bool is_fixed = false;
};

struct VmMethodDescriptor
{
    std::string name;
    std::size_t function_index = 0;
    std::vector<std::string> parameter_types;
    std::string return_type;
    struct CaptureSource
    {
        bool from_capture = false;
        std::size_t index = 0;
    };
    std::vector<CaptureSource> capture_sources;
    bool is_private = false;
    bool is_override = false;
};

struct VmClassDescriptor
{
    std::string name;
    std::string parent;
    std::vector<std::string> interfaces;
    std::vector<VmFieldDescriptor> fields;
    std::vector<VmMethodDescriptor> methods;
};

struct VmInterfaceMethodDescriptor
{
    std::string name;
    std::vector<std::string> parameter_types;
    std::string return_type;
};

struct VmInterfaceDescriptor
{
    std::string name;
    std::vector<VmInterfaceMethodDescriptor> methods;
};

struct VmNamespaceMember
{
    std::string name;
    std::size_t global_index = 0;
};

struct VmNamespaceDescriptor
{
    std::vector<VmNamespaceMember> members;
};

struct ModuleBytecode
{
    std::vector<std::string> globals;
    std::vector<std::string> types;
    std::vector<std::string> members;
    std::vector<VmClassDescriptor> classes;
    std::vector<VmInterfaceDescriptor> interfaces;
    std::vector<std::string> argument_names;
    std::vector<VmNamespaceDescriptor> namespaces;
    std::vector<FunctionBytecode> functions;
    std::size_t entry_function_index = 0;
    std::size_t initializer_function_index = static_cast<std::size_t>(-1);
    std::vector<std::size_t> initializer_function_indices;
};

[[nodiscard]] std::string_view opcode_name(Opcode opcode);
[[nodiscard]] std::string disassemble(const ModuleBytecode &module);

} // namespace lumiere
