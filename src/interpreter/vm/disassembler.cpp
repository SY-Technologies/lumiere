#include "lumiere/interpreter/vm/bytecode.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace lumiere
{
namespace
{

std::size_t read_byte(const Chunk &chunk, std::size_t &offset)
{
    if (offset >= chunk.code.size())
    {
        throw std::runtime_error("bytecode tronque pendant le desassemblage");
    }
    return chunk.code[offset++];
}

std::size_t read_u16(const Chunk &chunk, std::size_t &offset)
{
    return (read_byte(chunk, offset) << 8) | read_byte(chunk, offset);
}

std::size_t read_u24(const Chunk &chunk, std::size_t &offset)
{
    return (read_byte(chunk, offset) << 16) | (read_byte(chunk, offset) << 8) | read_byte(chunk, offset);
}

std::string table_value(const std::vector<std::string> &table, const std::size_t index)
{
    return index < table.size() ? table[index] : "<index invalide>";
}

void print_index(std::ostringstream &out, const std::size_t index, const std::vector<std::string> &table)
{
    out << ' ' << index << " (" << table_value(table, index) << ')';
}

void print_argument_names(std::ostringstream &out, const ModuleBytecode &module, const Chunk &chunk,
                          std::size_t &offset, const std::size_t arity)
{
    if (arity == 0)
    {
        return;
    }
    out << " names=[";
    for (std::size_t i = 0; i < arity; ++i)
    {
        if (i > 0)
        {
            out << ", ";
        }
        const std::size_t name = read_u16(chunk, offset);
        const std::string value = table_value(module.argument_names, name);
        out << (value.empty() ? "_" : value);
    }
    out << ']';
}

void disassemble_instruction(std::ostringstream &out, const ModuleBytecode &module, const FunctionBytecode &function,
                             std::size_t &offset)
{
    const Chunk &chunk = function.chunk;
    const std::size_t instruction_offset = offset;
    const Opcode opcode = static_cast<Opcode>(read_byte(chunk, offset));
    out << "  " << std::right << std::setw(4) << std::setfill('0') << instruction_offset << "  " << std::left
        << std::setw(22) << std::setfill(' ') << opcode_name(opcode);

    switch (opcode)
    {
    case Opcode::CONSTANT:
    case Opcode::CONSTANT_LONG:
    {
        const std::size_t index = opcode == Opcode::CONSTANT ? read_byte(chunk, offset) : read_u24(chunk, offset);
        out << ' ' << index;
        if (index < chunk.constants.size())
        {
            out << " (" << chunk.constants[index].to_string() << ')';
        }
        break;
    }
    case Opcode::GET_GLOBAL:
    case Opcode::SET_GLOBAL:
    case Opcode::INIT_GLOBAL:
        print_index(out, read_byte(chunk, offset), module.globals);
        break;
    case Opcode::GET_GLOBAL_LONG:
    case Opcode::SET_GLOBAL_LONG:
    case Opcode::INIT_GLOBAL_LONG:
        print_index(out, read_u24(chunk, offset), module.globals);
        break;
    case Opcode::GET_LOCAL:
    case Opcode::SET_LOCAL:
    case Opcode::GET_CAPTURE:
    case Opcode::SET_CAPTURE:
    case Opcode::LIST:
    case Opcode::DICTIONARY:
        out << ' ' << read_byte(chunk, offset);
        break;
    case Opcode::CLOSURE:
    {
        const std::size_t target = read_u16(chunk, offset);
        const std::size_t captures = read_byte(chunk, offset);
        out << ' ' << target;
        if (target < module.functions.size())
        {
            out << " (" << module.functions[target].name << ')';
        }
        out << " captures=[";
        for (std::size_t i = 0; i < captures; ++i)
        {
            if (i > 0)
            {
                out << ", ";
            }
            out << (read_byte(chunk, offset) == 0 ? 'L' : 'C') << read_byte(chunk, offset);
        }
        out << ']';
        break;
    }
    case Opcode::TRY_BEGIN:
    case Opcode::JUMP:
        out << ' ' << read_u16(chunk, offset);
        break;
    case Opcode::JUMP_IF_FALSE:
        out << " true=" << read_u16(chunk, offset) << " false=" << read_u16(chunk, offset);
        break;
    case Opcode::CALL:
    {
        const std::size_t arity = read_byte(chunk, offset);
        out << " arity=" << arity;
        print_argument_names(out, module, chunk, offset, arity);
        break;
    }
    case Opcode::CALL_GLOBAL:
    case Opcode::CALL_GLOBAL_LONG:
    {
        const std::size_t index = opcode == Opcode::CALL_GLOBAL ? read_byte(chunk, offset) : read_u24(chunk, offset);
        print_index(out, index, module.globals);
        const std::size_t arity = read_byte(chunk, offset);
        out << " arity=" << arity;
        print_argument_names(out, module, chunk, offset, arity);
        break;
    }
    case Opcode::CALL_MEMBER:
    case Opcode::CALL_MEMBER_LONG:
    case Opcode::CALL_PARENT:
    case Opcode::CALL_PARENT_LONG:
    {
        const bool is_long = opcode == Opcode::CALL_MEMBER_LONG || opcode == Opcode::CALL_PARENT_LONG;
        print_index(out, is_long ? read_u24(chunk, offset) : read_byte(chunk, offset), module.members);
        const std::size_t arity = read_byte(chunk, offset);
        out << " arity=" << arity;
        print_argument_names(out, module, chunk, offset, arity);
        break;
    }
    case Opcode::GET_MEMBER:
    case Opcode::GET_PARENT:
    case Opcode::SET_MEMBER:
        print_index(out, read_byte(chunk, offset), module.members);
        break;
    case Opcode::GET_MEMBER_LONG:
    case Opcode::GET_PARENT_LONG:
    case Opcode::SET_MEMBER_LONG:
        print_index(out, read_u24(chunk, offset), module.members);
        break;
    case Opcode::CLASS:
        print_index(out, read_u16(chunk, offset),
                    [&]()
                    {
                        std::vector<std::string> names;
                        for (const VmClassDescriptor &klass : module.classes)
                        {
                            names.push_back(klass.name);
                        }
                        return names;
                    }());
        break;
    case Opcode::INTERFACE:
        print_index(out, read_u16(chunk, offset),
                    [&]()
                    {
                        std::vector<std::string> names;
                        for (const VmInterfaceDescriptor &interface : module.interfaces)
                        {
                            names.push_back(interface.name);
                        }
                        return names;
                    }());
        break;
    case Opcode::NAMESPACE:
        out << ' ' << read_u16(chunk, offset);
        break;
    case Opcode::CAST:
    case Opcode::TYPE_CHECK:
    case Opcode::ASSERT_TYPE:
        print_index(out, read_byte(chunk, offset), module.types);
        break;
    case Opcode::CAST_LONG:
    case Opcode::TYPE_CHECK_LONG:
    case Opcode::ASSERT_TYPE_LONG:
        print_index(out, read_u24(chunk, offset), module.types);
        break;
    default:
        break;
    }

    if (instruction_offset < chunk.locations.size())
    {
        const SourceLocation location = chunk.locations[instruction_offset];
        if (location.line != 0 || location.column != 0)
        {
            out << "  @line:" << location.line << ",col:" << location.column;
        }
    }
    out << '\n';
}

} // namespace

std::string_view opcode_name(const Opcode opcode)
{
#define LUMIERE_OPCODE_NAME(name)                                                                                      \
    case Opcode::name:                                                                                                 \
        return #name
    switch (opcode)
    {
        LUMIERE_OPCODE_NAME(CONSTANT);
        LUMIERE_OPCODE_NAME(CONSTANT_LONG);
        LUMIERE_OPCODE_NAME(NIL);
        LUMIERE_OPCODE_NAME(TRUE_VALUE);
        LUMIERE_OPCODE_NAME(FALSE_VALUE);
        LUMIERE_OPCODE_NAME(GET_GLOBAL);
        LUMIERE_OPCODE_NAME(GET_GLOBAL_LONG);
        LUMIERE_OPCODE_NAME(SET_GLOBAL);
        LUMIERE_OPCODE_NAME(SET_GLOBAL_LONG);
        LUMIERE_OPCODE_NAME(INIT_GLOBAL);
        LUMIERE_OPCODE_NAME(INIT_GLOBAL_LONG);
        LUMIERE_OPCODE_NAME(GET_LOCAL);
        LUMIERE_OPCODE_NAME(SET_LOCAL);
        LUMIERE_OPCODE_NAME(GET_CAPTURE);
        LUMIERE_OPCODE_NAME(SET_CAPTURE);
        LUMIERE_OPCODE_NAME(CLOSURE);
        LUMIERE_OPCODE_NAME(TRY_BEGIN);
        LUMIERE_OPCODE_NAME(TRY_END);
        LUMIERE_OPCODE_NAME(THROW);
        LUMIERE_OPCODE_NAME(JUMP);
        LUMIERE_OPCODE_NAME(JUMP_IF_FALSE);
        LUMIERE_OPCODE_NAME(NEGATE);
        LUMIERE_OPCODE_NAME(NOT);
        LUMIERE_OPCODE_NAME(ADD);
        LUMIERE_OPCODE_NAME(SUBTRACT);
        LUMIERE_OPCODE_NAME(MULTIPLY);
        LUMIERE_OPCODE_NAME(DIVIDE);
        LUMIERE_OPCODE_NAME(MODULO);
        LUMIERE_OPCODE_NAME(EQUAL);
        LUMIERE_OPCODE_NAME(NOT_EQUAL);
        LUMIERE_OPCODE_NAME(LESS);
        LUMIERE_OPCODE_NAME(LESS_EQUAL);
        LUMIERE_OPCODE_NAME(GREATER);
        LUMIERE_OPCODE_NAME(GREATER_EQUAL);
        LUMIERE_OPCODE_NAME(CALL);
        LUMIERE_OPCODE_NAME(CALL_GLOBAL);
        LUMIERE_OPCODE_NAME(CALL_GLOBAL_LONG);
        LUMIERE_OPCODE_NAME(CALL_MEMBER);
        LUMIERE_OPCODE_NAME(CALL_MEMBER_LONG);
        LUMIERE_OPCODE_NAME(CALL_PARENT);
        LUMIERE_OPCODE_NAME(CALL_PARENT_LONG);
        LUMIERE_OPCODE_NAME(GET_MEMBER);
        LUMIERE_OPCODE_NAME(GET_MEMBER_LONG);
        LUMIERE_OPCODE_NAME(GET_PARENT);
        LUMIERE_OPCODE_NAME(GET_PARENT_LONG);
        LUMIERE_OPCODE_NAME(SET_MEMBER);
        LUMIERE_OPCODE_NAME(SET_MEMBER_LONG);
        LUMIERE_OPCODE_NAME(CLASS);
        LUMIERE_OPCODE_NAME(INTERFACE);
        LUMIERE_OPCODE_NAME(NAMESPACE);
        LUMIERE_OPCODE_NAME(LIST);
        LUMIERE_OPCODE_NAME(DICTIONARY);
        LUMIERE_OPCODE_NAME(SEQUENCE_LENGTH);
        LUMIERE_OPCODE_NAME(INDEX_GET);
        LUMIERE_OPCODE_NAME(INDEX_SET);
        LUMIERE_OPCODE_NAME(CAST);
        LUMIERE_OPCODE_NAME(CAST_LONG);
        LUMIERE_OPCODE_NAME(TYPE_CHECK);
        LUMIERE_OPCODE_NAME(TYPE_CHECK_LONG);
        LUMIERE_OPCODE_NAME(ASSERT_TYPE);
        LUMIERE_OPCODE_NAME(ASSERT_TYPE_LONG);
        LUMIERE_OPCODE_NAME(MATCH_ERROR);
        LUMIERE_OPCODE_NAME(POP);
        LUMIERE_OPCODE_NAME(RETURN);
    }
#undef LUMIERE_OPCODE_NAME
    return "<OPCODE_INVALIDE>";
}

std::string disassemble(const ModuleBytecode &module)
{
    std::ostringstream out;
    out << "bytecode module\n";
    for (std::size_t i = 0; i < module.functions.size(); ++i)
    {
        const FunctionBytecode &function = module.functions[i];
        out << "\nfunction " << i << ' ' << function.name << " arity=" << function.source_arity
            << " locals=" << function.local_slot_count << " captures=" << function.capture_count << '\n';
        std::size_t offset = 0;
        while (offset < function.chunk.code.size())
        {
            disassemble_instruction(out, module, function, offset);
        }
    }
    return out.str();
}

} // namespace lumiere
