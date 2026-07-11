#include "lumiere/interpreter/vm/lir.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace lumiere
{
namespace
{

[[nodiscard]] std::string format_source_location(const LirSourceLocation &source)
{
    if (!source.is_known())
    {
        return "";
    }

    std::ostringstream out;
    out << " @line:" << source.line << ",col:" << source.column;
    return out.str();
}

[[nodiscard]] std::string join_operands(const std::vector<LirOperand> &operands)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < operands.size(); ++i)
    {
        if (i != 0)
        {
            out << ", ";
        }
        out << to_string(operands[i]);
    }
    return out.str();
}

[[noreturn]] void throw_range_error(std::string_view entity, std::size_t index)
{
    std::ostringstream out;
    out << "LIR: invalid " << entity << " index " << index;
    throw std::out_of_range(out.str());
}

void validate_block_target(const LirOperand &operand)
{
    if (operand.kind != LirOperandKind::IR_OPERAND_BLOCK)
    {
        throw std::logic_error("LIR: block terminators must target blocks");
    }
}

} // namespace

LirOperand LirOperand::constant(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_CONSTANT, index};
}

LirOperand LirOperand::global(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_GLOBAL, index};
}

LirOperand LirOperand::local(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_LOCAL, index};
}

LirOperand LirOperand::temp(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_TEMP, index};
}

LirOperand LirOperand::function(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_FUNCTION, index};
}

LirOperand LirOperand::block(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_BLOCK, index};
}

LirOperand LirOperand::type(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_TYPE, index};
}

LirOperand LirOperand::member(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_MEMBER, index};
}

LirOperand LirOperand::capture(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_CAPTURE, index};
}

LirOperand LirOperand::klass(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_CLASS, index};
}

LirOperand LirOperand::interface(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_INTERFACE, index};
}

LirOperand LirOperand::argument_name(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_ARGUMENT_NAME, index};
}

LirOperand LirOperand::name_space(const std::size_t index) noexcept
{
    return {LirOperandKind::IR_OPERAND_NAMESPACE, index};
}

LirInstruction LirInstruction::make(const LirOpcode opcode,
                                    const LirOperand destination,
                                    std::vector<LirOperand> operands,
                                    const LirSourceLocation source)
{
    return {opcode, destination, std::move(operands), source};
}

LirTerminator LirTerminator::jump(const std::size_t target_block,
                                  const LirSourceLocation source)
{
    return {LirTerminatorKind::IR_TERM_JUMP, {LirOperand::block(target_block)}, source};
}

LirTerminator LirTerminator::branch(const LirOperand condition,
                                    const std::size_t true_block,
                                    const std::size_t false_block,
                                    const LirSourceLocation source)
{
    return {LirTerminatorKind::IR_TERM_BRANCH,
            {condition, LirOperand::block(true_block), LirOperand::block(false_block)},
            source};
}

LirTerminator LirTerminator::return_value(const LirOperand value,
                                          const LirSourceLocation source)
{
    return {LirTerminatorKind::IR_TERM_RETURN_VALUE, {value}, source};
}

LirTerminator LirTerminator::return_nil(const LirSourceLocation source)
{
    return {LirTerminatorKind::IR_TERM_RETURN_NIL, {}, source};
}

bool LirBlock::is_terminated() const noexcept
{
    return terminator != nullptr;
}

LirBlock &LirFunction::append_block()
{
    blocks.push_back({blocks.size(), {}, nullptr});
    return blocks.back();
}

LirInstruction &LirFunction::append_instruction(const std::size_t block_index,
                                                LirInstruction instruction)
{
    LirBlock &target_block = block(block_index);
    if (target_block.is_terminated())
    {
        throw std::logic_error("LIR: cannot append instruction after terminator");
    }

    target_block.instructions.push_back(std::move(instruction));
    return target_block.instructions.back();
}

void LirFunction::set_terminator(const std::size_t block_index, LirTerminator terminator_value)
{
    LirBlock &target_block = block(block_index);
    if (target_block.is_terminated())
    {
        throw std::logic_error("LIR: block already has a terminator");
    }

    if (terminator_value.kind == LirTerminatorKind::IR_TERM_JUMP)
    {
        validate_block_target(terminator_value.operands.at(0));
        static_cast<void>(block(terminator_value.operands[0].index));
    }
    else if (terminator_value.kind == LirTerminatorKind::IR_TERM_BRANCH)
    {
        validate_block_target(terminator_value.operands.at(1));
        validate_block_target(terminator_value.operands.at(2));
        static_cast<void>(block(terminator_value.operands[1].index));
        static_cast<void>(block(terminator_value.operands[2].index));
    }

    target_block.terminator = std::make_unique<LirTerminator>(std::move(terminator_value));
}

const LirBlock &LirFunction::block(const std::size_t block_index) const
{
    if (block_index >= blocks.size())
    {
        throw_range_error("block", block_index);
    }
    return blocks[block_index];
}

LirBlock &LirFunction::block(const std::size_t block_index)
{
    if (block_index >= blocks.size())
    {
        throw_range_error("block", block_index);
    }
    return blocks[block_index];
}

std::size_t LirModule::add_constant(Value value, std::string display)
{
    const std::size_t index = constants.size();
    constants.push_back({index, std::move(value), std::move(display)});
    return index;
}

std::size_t LirModule::add_global(std::string name)
{
    for (const LirGlobal &global : globals)
    {
        if (global.name == name)
        {
            return global.index;
        }
    }

    const std::size_t index = globals.size();
    globals.push_back({index, std::move(name)});
    return index;
}

std::size_t LirModule::add_type(std::string name)
{
    for (const LirType &type : types)
    {
        if (type.name == name)
        {
            return type.index;
        }
    }

    const std::size_t index = types.size();
    types.push_back({index, std::move(name)});
    return index;
}

std::size_t LirModule::add_member(std::string name)
{
    for (const LirMember &member : members)
    {
        if (member.name == name)
        {
            return member.index;
        }
    }
    const std::size_t index = members.size();
    members.push_back({index, std::move(name)});
    return index;
}

std::size_t LirModule::add_argument_name(std::string name)
{
    for (std::size_t i = 0; i < argument_names.size(); ++i)
    {
        if (argument_names[i] == name)
        {
            return i;
        }
    }
    argument_names.push_back(std::move(name));
    return argument_names.size() - 1;
}

LirFunction &LirModule::append_function(std::string function_name)
{
    functions.emplace_back();
    functions.back().name = std::move(function_name);
    return functions.back();
}

std::string to_string(const LirOperand &operand)
{
    std::ostringstream out;
    switch (operand.kind)
    {
    case LirOperandKind::IR_OPERAND_CONSTANT:
        out << 'K';
        break;
    case LirOperandKind::IR_OPERAND_GLOBAL:
        out << 'G';
        break;
    case LirOperandKind::IR_OPERAND_LOCAL:
        out << 'L';
        break;
    case LirOperandKind::IR_OPERAND_TEMP:
        out << 'T';
        break;
    case LirOperandKind::IR_OPERAND_FUNCTION:
        out << 'F';
        break;
    case LirOperandKind::IR_OPERAND_BLOCK:
        out << 'B';
        break;
    case LirOperandKind::IR_OPERAND_TYPE:
        out << "TYPE";
        break;
    case LirOperandKind::IR_OPERAND_MEMBER:
        out << "MEMBER";
        break;
    case LirOperandKind::IR_OPERAND_CAPTURE:
        out << 'C';
        break;
    case LirOperandKind::IR_OPERAND_CLASS:
        out << "CLASS";
        break;
    case LirOperandKind::IR_OPERAND_INTERFACE:
        out << "INTERFACE";
        break;
    case LirOperandKind::IR_OPERAND_ARGUMENT_NAME:
        out << "ARG";
        break;
    case LirOperandKind::IR_OPERAND_NAMESPACE:
        out << "NAMESPACE";
        break;
    }
    out << operand.index;
    return out.str();
}

std::string to_string(const LirOpcode opcode)
{
    switch (opcode)
    {
    case LirOpcode::IR_OP_CONSTANT:
        return "IR_OP_CONSTANT";
    case LirOpcode::IR_OP_LOAD_GLOBAL:
        return "IR_OP_LOAD_GLOBAL";
    case LirOpcode::IR_OP_STORE_GLOBAL:
        return "IR_OP_STORE_GLOBAL";
    case LirOpcode::IR_OP_INIT_GLOBAL:
        return "IR_OP_INIT_GLOBAL";
    case LirOpcode::IR_OP_LOAD_LOCAL:
        return "IR_OP_LOAD_LOCAL";
    case LirOpcode::IR_OP_STORE_LOCAL:
        return "IR_OP_STORE_LOCAL";
    case LirOpcode::IR_OP_MOVE:
        return "IR_OP_MOVE";
    case LirOpcode::IR_OP_ADD:
        return "IR_OP_ADD";
    case LirOpcode::IR_OP_SUBTRACT:
        return "IR_OP_SUBTRACT";
    case LirOpcode::IR_OP_MULTIPLY:
        return "IR_OP_MULTIPLY";
    case LirOpcode::IR_OP_DIVIDE:
        return "IR_OP_DIVIDE";
    case LirOpcode::IR_OP_MODULO:
        return "IR_OP_MODULO";
    case LirOpcode::IR_OP_NEGATE:
        return "IR_OP_NEGATE";
    case LirOpcode::IR_OP_NOT:
        return "IR_OP_NOT";
    case LirOpcode::IR_OP_EQUAL:
        return "IR_OP_EQUAL";
    case LirOpcode::IR_OP_NOT_EQUAL:
        return "IR_OP_NOT_EQUAL";
    case LirOpcode::IR_OP_LESS:
        return "IR_OP_LESS";
    case LirOpcode::IR_OP_LESS_EQUAL:
        return "IR_OP_LESS_EQUAL";
    case LirOpcode::IR_OP_GREATER:
        return "IR_OP_GREATER";
    case LirOpcode::IR_OP_GREATER_EQUAL:
        return "IR_OP_GREATER_EQUAL";
    case LirOpcode::IR_OP_CALL:
        return "IR_OP_CALL";
    case LirOpcode::IR_OP_CALL_GLOBAL:
        return "IR_OP_CALL_GLOBAL";
    case LirOpcode::IR_OP_CALL_MEMBER:
        return "IR_OP_CALL_MEMBER";
    case LirOpcode::IR_OP_CALL_PARENT:
        return "IR_OP_CALL_PARENT";
    case LirOpcode::IR_OP_GET_MEMBER:
        return "IR_OP_GET_MEMBER";
    case LirOpcode::IR_OP_GET_PARENT:
        return "IR_OP_GET_PARENT";
    case LirOpcode::IR_OP_SET_MEMBER:
        return "IR_OP_SET_MEMBER";
    case LirOpcode::IR_OP_CLASS:
        return "IR_OP_CLASS";
    case LirOpcode::IR_OP_INTERFACE:
        return "IR_OP_INTERFACE";
    case LirOpcode::IR_OP_NAMESPACE:
        return "IR_OP_NAMESPACE";
    case LirOpcode::IR_OP_LOAD_CAPTURE:
        return "IR_OP_LOAD_CAPTURE";
    case LirOpcode::IR_OP_STORE_CAPTURE:
        return "IR_OP_STORE_CAPTURE";
    case LirOpcode::IR_OP_CLOSURE:
        return "IR_OP_CLOSURE";
    case LirOpcode::IR_OP_TRY_BEGIN:
        return "IR_OP_TRY_BEGIN";
    case LirOpcode::IR_OP_TRY_END:
        return "IR_OP_TRY_END";
    case LirOpcode::IR_OP_EXCEPTION_VALUE:
        return "IR_OP_EXCEPTION_VALUE";
    case LirOpcode::IR_OP_THROW:
        return "IR_OP_THROW";
    case LirOpcode::IR_OP_LIST:
        return "IR_OP_LIST";
    case LirOpcode::IR_OP_DICTIONARY:
        return "IR_OP_DICTIONARY";
    case LirOpcode::IR_OP_SEQUENCE_LENGTH:
        return "IR_OP_SEQUENCE_LENGTH";
    case LirOpcode::IR_OP_INDEX_GET:
        return "IR_OP_INDEX_GET";
    case LirOpcode::IR_OP_INDEX_SET:
        return "IR_OP_INDEX_SET";
    case LirOpcode::IR_OP_CAST:
        return "IR_OP_CAST";
    case LirOpcode::IR_OP_TYPE_CHECK:
        return "IR_OP_TYPE_CHECK";
    case LirOpcode::IR_OP_ASSERT_TYPE:
        return "IR_OP_ASSERT_TYPE";
    case LirOpcode::IR_OP_MATCH_ERROR:
        return "IR_OP_MATCH_ERROR";
    case LirOpcode::IR_OP_DISCARD:
        return "IR_OP_DISCARD";
    }

    throw std::logic_error("LIR: unknown opcode");
}

std::string to_string(const LirInstruction &instruction)
{
    std::ostringstream out;

    if (instruction.opcode == LirOpcode::IR_OP_STORE_LOCAL ||
        instruction.opcode == LirOpcode::IR_OP_STORE_GLOBAL ||
        instruction.opcode == LirOpcode::IR_OP_INIT_GLOBAL ||
        instruction.opcode == LirOpcode::IR_OP_SET_MEMBER ||
        instruction.opcode == LirOpcode::IR_OP_MATCH_ERROR ||
        instruction.opcode == LirOpcode::IR_OP_TRY_BEGIN ||
        instruction.opcode == LirOpcode::IR_OP_TRY_END ||
        instruction.opcode == LirOpcode::IR_OP_THROW ||
        instruction.opcode == LirOpcode::IR_OP_DISCARD)
    {
        out << to_string(instruction.opcode);
        if (!instruction.operands.empty())
        {
            out << ' ' << join_operands(instruction.operands);
        }
    }
    else
    {
        out << to_string(instruction.destination) << " = " << to_string(instruction.opcode);
        if (!instruction.operands.empty())
        {
            out << ' ' << join_operands(instruction.operands);
        }
    }

    out << format_source_location(instruction.source);
    return out.str();
}

std::string to_string(const LirTerminator &terminator)
{
    std::ostringstream out;

    switch (terminator.kind)
    {
    case LirTerminatorKind::IR_TERM_JUMP:
        out << "IR_TERM_JUMP " << to_string(terminator.operands.at(0));
        break;
    case LirTerminatorKind::IR_TERM_BRANCH:
        out << "IR_TERM_BRANCH "
            << to_string(terminator.operands.at(0)) << ", "
            << to_string(terminator.operands.at(1)) << ", "
            << to_string(terminator.operands.at(2));
        break;
    case LirTerminatorKind::IR_TERM_RETURN_VALUE:
        out << "IR_TERM_RETURN_VALUE " << to_string(terminator.operands.at(0));
        break;
    case LirTerminatorKind::IR_TERM_RETURN_NIL:
        out << "IR_TERM_RETURN_NIL";
        break;
    }

    out << format_source_location(terminator.source);
    return out.str();
}

std::string to_string(const LirFunction &function)
{
    std::ostringstream out;
    out << "function " << function.name << '\n';
    out << "params:\n";
    for (const LirNamedValue &param : function.params)
    {
        out << "  L" << param.index << " = " << param.name << '\n';
    }
    out << "locals:\n";
    for (const LirNamedValue &local : function.locals)
    {
        out << "  L" << local.index << " = " << local.name << '\n';
    }
    out << "captures:\n";
    for (const LirCapture &capture : function.captures)
    {
        out << "  C" << capture.index << " = " << capture.name
            << " from " << to_string(capture.source) << '\n';
    }
    out << "temps:\n";
    for (const std::size_t temp : function.temps)
    {
        out << "  T" << temp << '\n';
    }
    out << "entry:\n";
    out << "  B" << function.entry_block << '\n';

    for (const LirBlock &block_value : function.blocks)
    {
        out << '\n' << "B" << block_value.index << ":\n";
        for (const LirInstruction &instruction : block_value.instructions)
        {
            out << "  " << to_string(instruction) << '\n';
        }
        if (block_value.terminator != nullptr)
        {
            out << "  " << to_string(*block_value.terminator) << '\n';
        }
    }

    return out.str();
}

std::string to_string(const LirModule &module)
{
    std::ostringstream out;
    out << "module " << module.name << '\n';
    out << "constants:\n";
    for (const LirConstant &constant : module.constants)
    {
        out << "  K" << constant.index << " = " << constant.display << '\n';
    }
    out << "globals:\n";
    for (const LirGlobal &global : module.globals)
    {
        out << "  G" << global.index << " = " << global.name << '\n';
    }
    out << "types:\n";
    for (const LirType &type : module.types)
    {
        out << "  TYPE" << type.index << " = " << type.name << '\n';
    }
    out << "members:\n";
    for (const LirMember &member : module.members)
    {
        out << "  MEMBER" << member.index << " = " << member.name << '\n';
    }

    for (std::size_t i = 0; i < module.functions.size(); ++i)
    {
        out << '\n' << to_string(module.functions[i]);
        if (i + 1 != module.functions.size())
        {
            out << '\n';
        }
    }

    return out.str();
}

std::ostream &operator<<(std::ostream &stream, const LirModule &module)
{
    stream << to_string(module);
    return stream;
}

} // namespace lumiere
