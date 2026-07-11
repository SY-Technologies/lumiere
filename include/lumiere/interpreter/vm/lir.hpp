#pragma once

#include "lumiere/interpreter/runtime/value.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace lumiere
{

// Source coordinates carried by LIR nodes for diagnostics and IR dumps.
// A zero line and column means the origin is currently unknown.
struct LirSourceLocation
{
    std::size_t line = 0;
    std::size_t column = 0;

    [[nodiscard]] bool is_known() const noexcept
    {
        return line != 0 || column != 0;
    }
};

// Operands never embed AST or runtime values directly.
// They are stable references into module or function-owned tables.
enum class LirOperandKind : std::uint8_t
{
    IR_OPERAND_CONSTANT,
    IR_OPERAND_GLOBAL,
    IR_OPERAND_LOCAL,
    IR_OPERAND_TEMP,
    IR_OPERAND_FUNCTION,
    IR_OPERAND_BLOCK,
    IR_OPERAND_TYPE,
    IR_OPERAND_MEMBER,
    IR_OPERAND_CAPTURE,
    IR_OPERAND_CLASS,
    IR_OPERAND_INTERFACE,
    IR_OPERAND_ARGUMENT_NAME,
    IR_OPERAND_NAMESPACE,
};

// A typed reference to an already-lowered entity such as a constant,
// local, temporary, function, or block.
struct LirOperand
{
    LirOperandKind kind {};
    std::size_t index = 0;

    [[nodiscard]] static LirOperand constant(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand global(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand local(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand temp(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand function(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand block(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand type(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand member(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand capture(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand klass(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand interface(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand argument_name(std::size_t index) noexcept;
    [[nodiscard]] static LirOperand name_space(std::size_t index) noexcept;
};

// Non-terminating instruction kinds for the initial LIR.
// Control flow stays in LirTerminator so blocks always end explicitly.
enum class LirOpcode : std::uint8_t
{
    IR_OP_CONSTANT,
    IR_OP_LOAD_GLOBAL,
    IR_OP_STORE_GLOBAL,
    IR_OP_INIT_GLOBAL,
    IR_OP_LOAD_LOCAL,
    IR_OP_STORE_LOCAL,
    IR_OP_MOVE,
    IR_OP_ADD,
    IR_OP_SUBTRACT,
    IR_OP_MULTIPLY,
    IR_OP_DIVIDE,
    IR_OP_MODULO,
    IR_OP_NEGATE,
    IR_OP_NOT,
    IR_OP_EQUAL,
    IR_OP_NOT_EQUAL,
    IR_OP_LESS,
    IR_OP_LESS_EQUAL,
    IR_OP_GREATER,
    IR_OP_GREATER_EQUAL,
    IR_OP_CALL,
    IR_OP_CALL_GLOBAL,
    IR_OP_CALL_MEMBER,
    IR_OP_CALL_PARENT,
    IR_OP_GET_MEMBER,
    IR_OP_GET_PARENT,
    IR_OP_SET_MEMBER,
    IR_OP_CLASS,
    IR_OP_INTERFACE,
    IR_OP_NAMESPACE,
    IR_OP_LOAD_CAPTURE,
    IR_OP_STORE_CAPTURE,
    IR_OP_CLOSURE,
    IR_OP_TRY_BEGIN,
    IR_OP_TRY_END,
    IR_OP_EXCEPTION_VALUE,
    IR_OP_THROW,
    IR_OP_LIST,
    IR_OP_DICTIONARY,
    IR_OP_SEQUENCE_LENGTH,
    IR_OP_INDEX_GET,
    IR_OP_INDEX_SET,
    IR_OP_CAST,
    IR_OP_TYPE_CHECK,
    IR_OP_ASSERT_TYPE,
    IR_OP_MATCH_ERROR,
    IR_OP_DISCARD,
};

// Every block must end in exactly one terminator and there is no fallthrough.
enum class LirTerminatorKind : std::uint8_t
{
    IR_TERM_JUMP,
    IR_TERM_BRANCH,
    IR_TERM_RETURN_VALUE,
    IR_TERM_RETURN_NIL,
};

// A regular instruction may produce a destination value or perform an effect.
// For effect-only instructions, the destination field is ignored by the printer
// and by downstream lowering code.
struct LirInstruction
{
    LirOpcode opcode {};
    LirOperand destination = LirOperand::temp(0);
    std::vector<LirOperand> operands;
    LirSourceLocation source {};

    [[nodiscard]] static LirInstruction make(LirOpcode opcode,
                                             LirOperand destination,
                                             std::vector<LirOperand> operands = {},
                                             LirSourceLocation source = {});
};

// Terminators own the outgoing control-flow decision for a block.
// Jump and branch targets are encoded as block operands so validation and
// printing use the same representation as the rest of the IR.
struct LirTerminator
{
    LirTerminatorKind kind {};
    std::vector<LirOperand> operands;
    LirSourceLocation source {};

    [[nodiscard]] static LirTerminator jump(std::size_t target_block,
                                            LirSourceLocation source = {});
    [[nodiscard]] static LirTerminator branch(LirOperand condition,
                                              std::size_t true_block,
                                              std::size_t false_block,
                                              LirSourceLocation source = {});
    [[nodiscard]] static LirTerminator return_value(LirOperand value,
                                                    LirSourceLocation source = {});
    [[nodiscard]] static LirTerminator return_nil(LirSourceLocation source = {});
};

// Parameters and named locals both live in the local-slot space.
// Params are listed separately so dumps preserve the source-level function shape.
struct LirNamedValue
{
    std::size_t index = 0;
    std::string name;
};

struct LirCapture
{
    std::size_t index = 0;
    std::string name;
    LirOperand source;
};

// Constants store their textual review form for now.
// A later phase may replace this with a richer literal payload representation.
struct LirConstant
{
    std::size_t index = 0;
    Value value = Value::rien();
    std::string display;
};

// Globals represent names visible from a module-level environment.
struct LirGlobal
{
    std::size_t index = 0;
    std::string name;
};

struct LirType
{
    std::size_t index = 0;
    std::string name;
};

struct LirMember
{
    std::size_t index = 0;
    std::string name;
};

struct LirFieldDescriptor
{
    std::string name;
    std::string type;
    bool is_private = false;
    bool is_fixed = false;
};

struct LirMethodDescriptor
{
    std::string name;
    std::size_t function_index = 0;
    std::vector<std::string> parameter_types;
    std::string return_type;
    std::vector<LirOperand> capture_sources;
    bool is_private = false;
    bool is_override = false;
};

struct LirClassDescriptor
{
    std::string name;
    std::string parent;
    std::vector<std::string> interfaces;
    std::vector<LirFieldDescriptor> fields;
    std::vector<LirMethodDescriptor> methods;
};

struct LirInterfaceMethodDescriptor
{
    std::string name;
    std::vector<std::string> parameter_types;
    std::string return_type;
};

struct LirInterfaceDescriptor
{
    std::string name;
    std::vector<LirInterfaceMethodDescriptor> methods;
};

struct LirNamespaceMember
{
    std::string name;
    std::size_t global_index = 0;
};

struct LirNamespaceDescriptor
{
    std::vector<LirNamespaceMember> members;
};

// A block is a straight-line instruction sequence followed by one terminator.
// Instructions may be appended only until the block is terminated.
struct LirBlock
{
    std::size_t index = 0;
    std::vector<LirInstruction> instructions;
    std::unique_ptr<LirTerminator> terminator;

    [[nodiscard]] bool is_terminated() const noexcept;
};

// A function owns its blocks and the metadata needed to interpret operands.
// Parameters occupy the first local slots by convention; compiler temps stay
// separate so dumps remain easy for humans to read during bring-up.
struct LirFunction
{
    std::string name;
    std::string source_path;
    std::string source_text;
    std::vector<LirNamedValue> params;
    std::vector<LirNamedValue> locals;
    std::vector<LirCapture> captures;
    std::size_t source_arity = 0;
    std::vector<bool> optional_params;
    std::vector<std::size_t> temps;
    std::size_t entry_block = 0;
    std::vector<LirBlock> blocks;

    // Appends a block with the next stable index and returns it by reference.
    LirBlock &append_block();

    // Adds an instruction to an unterminated block.
    // Throws if the block does not exist or already has a terminator.
    LirInstruction &append_instruction(std::size_t block_index, LirInstruction instruction);

    // Installs the unique terminator for a block and validates referenced
    // successor blocks when the terminator encodes control-flow edges.
    void set_terminator(std::size_t block_index, LirTerminator terminator);

    // Indexed block access is the validation choke point for builder code.
    [[nodiscard]] const LirBlock &block(std::size_t block_index) const;
    [[nodiscard]] LirBlock &block(std::size_t block_index);
};

// A module is the top-level LIR container for one lowered source unit.
// It owns constant/global metadata and the function list that refers to them.
struct LirModule
{
    std::string name;
    std::vector<LirConstant> constants;
    std::vector<LirGlobal> globals;
    std::vector<LirType> types;
    std::vector<LirMember> members;
    std::vector<LirClassDescriptor> classes;
    std::vector<LirInterfaceDescriptor> interfaces;
    std::vector<std::string> argument_names;
    std::vector<LirNamespaceDescriptor> namespaces;
    std::vector<std::size_t> initializer_functions;
    std::deque<LirFunction> functions;

    // Appends a constant and returns its stable k-index.
    [[nodiscard]] std::size_t add_constant(Value value, std::string display);

    // Reuses an existing global binding when the name is already present;
    // otherwise appends a new one and returns its stable g-index.
    [[nodiscard]] std::size_t add_global(std::string name);

    [[nodiscard]] std::size_t add_type(std::string name);
    [[nodiscard]] std::size_t add_member(std::string name);
    [[nodiscard]] std::size_t add_argument_name(std::string name);

    // Appends a function shell ready to receive blocks and metadata.
    LirFunction &append_function(std::string name);
};

// Text printers emit the review syntax described in the VM design document.
[[nodiscard]] std::string to_string(const LirOperand &operand);
[[nodiscard]] std::string to_string(LirOpcode opcode);
[[nodiscard]] std::string to_string(const LirInstruction &instruction);
[[nodiscard]] std::string to_string(const LirTerminator &terminator);
[[nodiscard]] std::string to_string(const LirFunction &function);
[[nodiscard]] std::string to_string(const LirModule &module);

std::ostream &operator<<(std::ostream &stream, const LirModule &module);

} // namespace lumiere
