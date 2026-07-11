#include "lir_to_bytecode.hpp"

#include "lumiere/interpreter/vm/compiler.hpp"

#include <unordered_map>

namespace lumiere
{
namespace
{

// Maps LIR source coordinates into bytecode source coordinates.
SourceLocation bc_loc(const LirSourceLocation &location)
{
    return SourceLocation{location.line, location.column};
}

std::uint8_t require_u8_index(const std::size_t index, const char *what)
{
    if (index > 0xFF)
    {
        throw VmCompileError(std::string("VM: ") + what + " depasse le format bytecode sur un octet");
    }
    return static_cast<std::uint8_t>(index);
}

std::uint16_t require_u16_offset(const std::size_t offset, const char *what)
{
    if (offset > 0xFFFF)
    {
        throw VmCompileError(std::string("VM: ") + what + " depasse le format bytecode sur deux octets");
    }
    return static_cast<std::uint16_t>(offset);
}

std::size_t instr_size(const LirModule &module,
                       const LirInstruction &instruction,
                       std::size_t &constant_count)
{
    switch (instruction.opcode)
    {
    case LirOpcode::IR_OP_CONSTANT:
    {
        const Value &value = module.constants.at(instruction.operands.at(0).index).value;
        if (value.is_rien() || value.is_logique())
        {
            return 1;
        }
        return constant_count++ <= 0xFF ? 2 : 4;
    }
    case LirOpcode::IR_OP_LOAD_GLOBAL:
    case LirOpcode::IR_OP_STORE_GLOBAL:
    case LirOpcode::IR_OP_INIT_GLOBAL:
        return instruction.operands.at(0).index <= 0xFF ? 2 : 4;
    case LirOpcode::IR_OP_CAST:
    case LirOpcode::IR_OP_TYPE_CHECK:
    case LirOpcode::IR_OP_ASSERT_TYPE:
        return instruction.operands.at(1).index <= 0xFF ? 2 : 4;
    case LirOpcode::IR_OP_LOAD_LOCAL:
    case LirOpcode::IR_OP_STORE_LOCAL:
    case LirOpcode::IR_OP_LOAD_CAPTURE:
    case LirOpcode::IR_OP_STORE_CAPTURE:
        return 2;
    case LirOpcode::IR_OP_CLOSURE:
        return 4 + (instruction.operands.size() - 1) * 2;
    case LirOpcode::IR_OP_TRY_BEGIN:
        return 3;
    case LirOpcode::IR_OP_TRY_END:
    case LirOpcode::IR_OP_THROW:
        return 1;
    case LirOpcode::IR_OP_EXCEPTION_VALUE:
        return 0;
    case LirOpcode::IR_OP_NOT:
    case LirOpcode::IR_OP_NEGATE:
    case LirOpcode::IR_OP_ADD:
    case LirOpcode::IR_OP_SUBTRACT:
    case LirOpcode::IR_OP_MULTIPLY:
    case LirOpcode::IR_OP_DIVIDE:
    case LirOpcode::IR_OP_MODULO:
    case LirOpcode::IR_OP_EQUAL:
    case LirOpcode::IR_OP_NOT_EQUAL:
    case LirOpcode::IR_OP_LESS:
    case LirOpcode::IR_OP_LESS_EQUAL:
    case LirOpcode::IR_OP_GREATER:
    case LirOpcode::IR_OP_GREATER_EQUAL:
    case LirOpcode::IR_OP_DISCARD:
        return 1;
    case LirOpcode::IR_OP_CALL:
        return 2 + ((instruction.operands.size() - 1) / 2) * 2;
    case LirOpcode::IR_OP_CALL_GLOBAL:
        return (instruction.operands.at(0).index <= 0xFF ? 3 : 5) +
               ((instruction.operands.size() - 1) / 2) * 2;
    case LirOpcode::IR_OP_CALL_MEMBER:
    case LirOpcode::IR_OP_CALL_PARENT:
        return (instruction.operands.at(1).index <= 0xFF ? 3 : 5) +
               ((instruction.operands.size() - 2) / 2) * 2;
    case LirOpcode::IR_OP_GET_MEMBER:
    case LirOpcode::IR_OP_GET_PARENT:
    case LirOpcode::IR_OP_SET_MEMBER:
        return instruction.operands.at(1).index <= 0xFF ? 2 : 4;
    case LirOpcode::IR_OP_CLASS:
    case LirOpcode::IR_OP_INTERFACE:
    case LirOpcode::IR_OP_NAMESPACE:
        return 3;
    case LirOpcode::IR_OP_LIST:
    case LirOpcode::IR_OP_DICTIONARY:
        return 2;
    case LirOpcode::IR_OP_SEQUENCE_LENGTH:
    case LirOpcode::IR_OP_INDEX_GET:
    case LirOpcode::IR_OP_INDEX_SET:
    case LirOpcode::IR_OP_MATCH_ERROR:
        return 1;
    default:
        throw VmCompileError("VM: calcul de taille bytecode LIR non pris en charge pour cette instruction");
    }
}

std::size_t term_size(const LirTerminator &terminator)
{
    switch (terminator.kind)
    {
    case LirTerminatorKind::IR_TERM_JUMP:
        return 3;
    case LirTerminatorKind::IR_TERM_BRANCH:
        return 5;
    case LirTerminatorKind::IR_TERM_RETURN_NIL:
        return 2;
    case LirTerminatorKind::IR_TERM_RETURN_VALUE:
        return 1;
    }

    throw VmCompileError("VM: calcul de taille bytecode LIR non pris en charge pour ce terminateur");
}

// Encodes one regular LIR instruction into bytecode.
void emit_instr(const LirModule &module,
                const LirInstruction &instruction,
                const std::unordered_map<std::size_t, std::size_t> &block_offsets,
                Chunk &chunk)
{
    switch (instruction.opcode)
    {
    case LirOpcode::IR_OP_CONSTANT:
    {
        const Value &value = module.constants.at(instruction.operands.at(0).index).value;
        if (value.is_rien())
        {
            chunk.write_opcode(Opcode::NIL, bc_loc(instruction.source));
            return;
        }
        if (value.is_logique())
        {
            chunk.write_opcode(value.as_logique() ? Opcode::TRUE_VALUE : Opcode::FALSE_VALUE,
                               bc_loc(instruction.source));
            return;
        }

        chunk.write_constant_ref(chunk.add_constant(value), bc_loc(instruction.source));
        return;
    }
    case LirOpcode::IR_OP_LOAD_GLOBAL:
    {
        const std::size_t global_index = instruction.operands.at(0).index;
        chunk.write_global_ref(global_index, bc_loc(instruction.source));
        return;
    }
    case LirOpcode::IR_OP_STORE_GLOBAL:
    {
        const std::size_t index = instruction.operands.at(0).index;
        if (index <= 0xFF)
        {
            chunk.write_opcode(Opcode::SET_GLOBAL, bc_loc(instruction.source));
            chunk.write_byte(static_cast<std::uint8_t>(index), bc_loc(instruction.source));
        }
        else
        {
            chunk.write_opcode(Opcode::SET_GLOBAL_LONG, bc_loc(instruction.source));
            chunk.write_u24(index, bc_loc(instruction.source));
        }
        return;
    }
    case LirOpcode::IR_OP_INIT_GLOBAL:
    {
        const std::size_t index = instruction.operands.at(0).index;
        chunk.write_opcode(index <= 0xFF ? Opcode::INIT_GLOBAL : Opcode::INIT_GLOBAL_LONG,
                           bc_loc(instruction.source));
        if (index <= 0xFF)
        {
            chunk.write_byte(static_cast<std::uint8_t>(index), bc_loc(instruction.source));
        }
        else
        {
            chunk.write_u24(index, bc_loc(instruction.source));
        }
        return;
    }
    case LirOpcode::IR_OP_LOAD_LOCAL:
        chunk.write_opcode(Opcode::GET_LOCAL, bc_loc(instruction.source));
        chunk.write_byte(require_u8_index(instruction.operands.at(0).index, "l'index de local"),
                         bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_STORE_LOCAL:
        chunk.write_opcode(Opcode::SET_LOCAL, bc_loc(instruction.source));
        chunk.write_byte(require_u8_index(instruction.operands.at(0).index, "l'index de local"),
                         bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_LOAD_CAPTURE:
        chunk.write_opcode(Opcode::GET_CAPTURE, bc_loc(instruction.source));
        chunk.write_byte(require_u8_index(instruction.operands.at(0).index, "l'index de capture"),
                         bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_STORE_CAPTURE:
        chunk.write_opcode(Opcode::SET_CAPTURE, bc_loc(instruction.source));
        chunk.write_byte(require_u8_index(instruction.operands.at(0).index, "l'index de capture"),
                         bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_CLOSURE:
    {
        const std::size_t function_index = instruction.operands.at(0).index;
        chunk.write_opcode(Opcode::CLOSURE, bc_loc(instruction.source));
        chunk.write_u16(require_u16_offset(function_index, "l'index de fonction fermee"),
                        bc_loc(instruction.source));
        chunk.write_byte(require_u8_index(instruction.operands.size() - 1, "le nombre de captures"),
                         bc_loc(instruction.source));
        for (std::size_t i = 1; i < instruction.operands.size(); ++i)
        {
            const LirOperand source = instruction.operands[i];
            const bool is_capture = source.kind == LirOperandKind::IR_OPERAND_CAPTURE;
            if (!is_capture && source.kind != LirOperandKind::IR_OPERAND_LOCAL)
            {
                throw VmCompileError("VM: source de capture LIR invalide");
            }
            chunk.write_byte(is_capture ? 1 : 0, bc_loc(instruction.source));
            chunk.write_byte(require_u8_index(source.index, "l'index source de capture"),
                             bc_loc(instruction.source));
        }
        return;
    }
    case LirOpcode::IR_OP_TRY_BEGIN:
        chunk.write_opcode(Opcode::TRY_BEGIN, bc_loc(instruction.source));
        chunk.write_u16(require_u16_offset(block_offsets.at(instruction.operands.at(0).index), "le gestionnaire"),
                        bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_TRY_END:
        chunk.write_opcode(Opcode::TRY_END, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_EXCEPTION_VALUE:
        return;
    case LirOpcode::IR_OP_THROW:
        chunk.write_opcode(Opcode::THROW, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_NEGATE:
        chunk.write_opcode(Opcode::NEGATE, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_NOT:
        chunk.write_opcode(Opcode::NOT, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_ADD:
        chunk.write_opcode(Opcode::ADD, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_SUBTRACT:
        chunk.write_opcode(Opcode::SUBTRACT, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_MULTIPLY:
        chunk.write_opcode(Opcode::MULTIPLY, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_DIVIDE:
        chunk.write_opcode(Opcode::DIVIDE, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_MODULO:
        chunk.write_opcode(Opcode::MODULO, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_EQUAL:
        chunk.write_opcode(Opcode::EQUAL, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_NOT_EQUAL:
        chunk.write_opcode(Opcode::NOT_EQUAL, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_LESS:
        chunk.write_opcode(Opcode::LESS, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_LESS_EQUAL:
        chunk.write_opcode(Opcode::LESS_EQUAL, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_GREATER:
        chunk.write_opcode(Opcode::GREATER, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_GREATER_EQUAL:
        chunk.write_opcode(Opcode::GREATER_EQUAL, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_CALL:
    {
        chunk.write_opcode(Opcode::CALL, bc_loc(instruction.source));
        const std::size_t arity = (instruction.operands.size() - 1) / 2;
        chunk.write_byte(static_cast<std::uint8_t>(arity), bc_loc(instruction.source));
        for (std::size_t i = 1; i < instruction.operands.size(); i += 2)
        {
            chunk.write_u16(require_u16_offset(instruction.operands[i].index, "l'index de nom d'argument"),
                            bc_loc(instruction.source));
        }
        return;
    }
    case LirOpcode::IR_OP_CALL_GLOBAL:
    {
        const std::size_t arity = (instruction.operands.size() - 1) / 2;
        chunk.write_global_call(instruction.operands.at(0).index,
                                static_cast<std::uint8_t>(arity),
                                bc_loc(instruction.source));
        for (std::size_t i = 1; i < instruction.operands.size(); i += 2)
        {
            chunk.write_u16(require_u16_offset(instruction.operands[i].index, "l'index de nom d'argument"),
                            bc_loc(instruction.source));
        }
        return;
    }
    case LirOpcode::IR_OP_CALL_MEMBER:
    case LirOpcode::IR_OP_CALL_PARENT:
    {
        const std::size_t arity = (instruction.operands.size() - 2) / 2;
        const std::size_t member = instruction.operands.at(1).index;
        if (instruction.opcode == LirOpcode::IR_OP_CALL_MEMBER)
        {
            chunk.write_member_call(member, static_cast<std::uint8_t>(arity), bc_loc(instruction.source));
        }
        else
        {
            chunk.write_opcode(member <= 0xFF ? Opcode::CALL_PARENT : Opcode::CALL_PARENT_LONG,
                               bc_loc(instruction.source));
            if (member <= 0xFF)
            {
                chunk.write_byte(static_cast<std::uint8_t>(member), bc_loc(instruction.source));
            }
            else
            {
                chunk.write_u24(member, bc_loc(instruction.source));
            }
            chunk.write_byte(static_cast<std::uint8_t>(arity), bc_loc(instruction.source));
        }
        for (std::size_t i = 2; i < instruction.operands.size(); i += 2)
        {
            chunk.write_u16(require_u16_offset(instruction.operands[i].index, "l'index de nom d'argument"),
                            bc_loc(instruction.source));
        }
        return;
    }
    case LirOpcode::IR_OP_GET_MEMBER:
        chunk.write_member_get(instruction.operands.at(1).index, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_GET_PARENT:
    {
        const std::size_t member = instruction.operands.at(1).index;
        chunk.write_opcode(member <= 0xFF ? Opcode::GET_PARENT : Opcode::GET_PARENT_LONG,
                           bc_loc(instruction.source));
        if (member <= 0xFF)
        {
            chunk.write_byte(static_cast<std::uint8_t>(member), bc_loc(instruction.source));
        }
        else
        {
            chunk.write_u24(member, bc_loc(instruction.source));
        }
        return;
    }
    case LirOpcode::IR_OP_SET_MEMBER:
    {
        const std::size_t index = instruction.operands.at(1).index;
        if (index <= 0xFF)
        {
            chunk.write_opcode(Opcode::SET_MEMBER, bc_loc(instruction.source));
            chunk.write_byte(static_cast<std::uint8_t>(index), bc_loc(instruction.source));
        }
        else
        {
            chunk.write_opcode(Opcode::SET_MEMBER_LONG, bc_loc(instruction.source));
            chunk.write_u24(index, bc_loc(instruction.source));
        }
        return;
    }
    case LirOpcode::IR_OP_CLASS:
        chunk.write_opcode(Opcode::CLASS, bc_loc(instruction.source));
        chunk.write_u16(require_u16_offset(instruction.operands.at(0).index, "l'index de classe"),
                        bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_INTERFACE:
        chunk.write_opcode(Opcode::INTERFACE, bc_loc(instruction.source));
        chunk.write_u16(require_u16_offset(instruction.operands.at(0).index, "l'index d'interface"),
                        bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_NAMESPACE:
        chunk.write_opcode(Opcode::NAMESPACE, bc_loc(instruction.source));
        chunk.write_u16(require_u16_offset(instruction.operands.at(0).index, "l'index d'espace de noms"),
                        bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_LIST:
        chunk.write_opcode(Opcode::LIST, bc_loc(instruction.source));
        chunk.write_byte(static_cast<std::uint8_t>(instruction.operands.size()), bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_DICTIONARY:
        chunk.write_opcode(Opcode::DICTIONARY, bc_loc(instruction.source));
        chunk.write_byte(static_cast<std::uint8_t>(instruction.operands.size() / 2), bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_SEQUENCE_LENGTH:
        chunk.write_opcode(Opcode::SEQUENCE_LENGTH, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_INDEX_GET:
        chunk.write_opcode(Opcode::INDEX_GET, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_INDEX_SET:
        chunk.write_opcode(Opcode::INDEX_SET, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_CAST:
        chunk.write_cast_ref(instruction.operands.at(1).index, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_TYPE_CHECK:
        chunk.write_type_check_ref(instruction.operands.at(1).index, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_ASSERT_TYPE:
        chunk.write_type_assertion(instruction.operands.at(1).index, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_MATCH_ERROR:
        chunk.write_opcode(Opcode::MATCH_ERROR, bc_loc(instruction.source));
        return;
    case LirOpcode::IR_OP_DISCARD:
        chunk.write_opcode(Opcode::POP, bc_loc(instruction.source));
        return;
    default:
        throw VmCompileError("VM: l'emission bytecode LIR ne prend pas encore en charge cette instruction");
    }
}

// Encodes the unique control-flow exit of the current block.
void emit_term(const LirTerminator &terminator,
               const std::unordered_map<std::size_t, std::size_t> &block_offsets,
               Chunk &chunk)
{
    switch (terminator.kind)
    {
    case LirTerminatorKind::IR_TERM_JUMP:
        chunk.write_opcode(Opcode::JUMP, bc_loc(terminator.source));
        chunk.write_u16(
            require_u16_offset(block_offsets.at(terminator.operands.at(0).index), "la cible de saut"),
            bc_loc(terminator.source));
        return;
    case LirTerminatorKind::IR_TERM_BRANCH:
        chunk.write_opcode(Opcode::JUMP_IF_FALSE, bc_loc(terminator.source));
        chunk.write_u16(
            require_u16_offset(block_offsets.at(terminator.operands.at(1).index), "la cible de branche vraie"),
            bc_loc(terminator.source));
        chunk.write_u16(
            require_u16_offset(block_offsets.at(terminator.operands.at(2).index), "la cible de branche fausse"),
            bc_loc(terminator.source));
        return;
    case LirTerminatorKind::IR_TERM_RETURN_NIL:
        chunk.write_opcode(Opcode::NIL, bc_loc(terminator.source));
        chunk.write_opcode(Opcode::RETURN, bc_loc(terminator.source));
        return;
    case LirTerminatorKind::IR_TERM_RETURN_VALUE:
        chunk.write_opcode(Opcode::RETURN, bc_loc(terminator.source));
        return;
    default:
        throw VmCompileError("VM: l'emission bytecode LIR ne prend pas encore en charge ce terminateur");
    }
}

std::unordered_map<std::size_t, std::size_t> compute_block_offsets(const LirModule &module,
                                                                   const LirFunction &lir_function)
{
    std::unordered_map<std::size_t, std::size_t> block_offsets;
    std::size_t offset = 0;
    std::size_t constant_count = 0;

    for (const LirBlock &block : lir_function.blocks)
    {
        block_offsets.emplace(block.index, offset);
        for (const LirInstruction &instruction : block.instructions)
        {
            offset += instr_size(module, instruction, constant_count);
        }

        if (block.terminator == nullptr)
        {
            throw VmCompileError("VM: bloc LIR sans terminateur");
        }
        offset += term_size(*block.terminator);
    }

    return block_offsets;
}

void emit_fn(const LirModule &module,
             const LirFunction &lir_function,
             FunctionBytecode &bytecode_function)
{
    const std::unordered_map<std::size_t, std::size_t> block_offsets =
        compute_block_offsets(module, lir_function);

    for (const LirBlock &block : lir_function.blocks)
    {
        for (const LirInstruction &instruction : block.instructions)
        {
            emit_instr(module, instruction, block_offsets, bytecode_function.chunk);
        }

        emit_term(*block.terminator, block_offsets, bytecode_function.chunk);
    }
}

} // namespace

ModuleBytecode LirToBytecode::emit(const LirModule &module, const std::size_t entry_function_index)
{
    ModuleBytecode bytecode_module;
    bytecode_module.globals.reserve(module.globals.size());
    for (const LirGlobal &global : module.globals)
    {
        bytecode_module.globals.push_back(global.name);
    }
    bytecode_module.types.reserve(module.types.size());
    for (const LirType &type : module.types)
    {
        bytecode_module.types.push_back(type.name);
    }
    bytecode_module.members.reserve(module.members.size());
    for (const LirMember &member : module.members)
    {
        bytecode_module.members.push_back(member.name);
    }
    for (const LirClassDescriptor &klass : module.classes)
    {
        VmClassDescriptor descriptor;
        descriptor.name = klass.name;
        descriptor.parent = klass.parent;
        descriptor.interfaces = klass.interfaces;
        for (const LirFieldDescriptor &field : klass.fields)
        {
            descriptor.fields.push_back({field.name, field.type, field.is_private, field.is_fixed});
        }
        for (const LirMethodDescriptor &method : klass.methods)
        {
            VmMethodDescriptor emitted;
            emitted.name = method.name;
            emitted.function_index = method.function_index;
            emitted.parameter_types = method.parameter_types;
            emitted.return_type = method.return_type;
            emitted.is_private = method.is_private;
            emitted.is_override = method.is_override;
            for (const LirOperand source : method.capture_sources)
            {
                if (source.kind != LirOperandKind::IR_OPERAND_LOCAL &&
                    source.kind != LirOperandKind::IR_OPERAND_CAPTURE)
                {
                    throw VmCompileError("VM: source de capture de methode invalide");
                }
                emitted.capture_sources.push_back({source.kind == LirOperandKind::IR_OPERAND_CAPTURE,
                                                   source.index});
            }
            descriptor.methods.push_back(std::move(emitted));
        }
        bytecode_module.classes.push_back(std::move(descriptor));
    }
    for (const LirInterfaceDescriptor &interface : module.interfaces)
    {
        VmInterfaceDescriptor descriptor;
        descriptor.name = interface.name;
        for (const LirInterfaceMethodDescriptor &method : interface.methods)
        {
            descriptor.methods.push_back({method.name, method.parameter_types, method.return_type});
        }
        bytecode_module.interfaces.push_back(std::move(descriptor));
    }
    bytecode_module.argument_names = module.argument_names;
    for (const LirNamespaceDescriptor &name_space : module.namespaces)
    {
        VmNamespaceDescriptor descriptor;
        for (const LirNamespaceMember &member : name_space.members)
        {
            descriptor.members.push_back({member.name, member.global_index});
        }
        bytecode_module.namespaces.push_back(std::move(descriptor));
    }
    bytecode_module.entry_function_index = entry_function_index;
    for (std::size_t i = 0; i < module.functions.size(); ++i)
    {
        if (module.functions[i].name == "__module_init__")
        {
            bytecode_module.initializer_function_index = i;
            break;
        }
    }
    bytecode_module.initializer_function_indices = module.initializer_functions;
    if (bytecode_module.initializer_function_indices.empty() &&
        bytecode_module.initializer_function_index != static_cast<std::size_t>(-1))
    {
        bytecode_module.initializer_function_indices.push_back(bytecode_module.initializer_function_index);
    }

    for (const LirFunction &lir_function : module.functions)
    {
        FunctionBytecode bytecode_function;
        bytecode_function.name = lir_function.name;
        bytecode_function.source_path = lir_function.source_path;
        bytecode_function.source_text = lir_function.source_text;
        bytecode_function.arity = lir_function.params.size();
        bytecode_function.source_arity = lir_function.source_arity;
        bytecode_function.optional_params = lir_function.optional_params;
        bytecode_function.local_slot_count = lir_function.params.size() + lir_function.locals.size();
        bytecode_function.capture_count = lir_function.captures.size();
        emit_fn(module, lir_function, bytecode_function);
        bytecode_module.functions.push_back(std::move(bytecode_function));
    }

    return bytecode_module;
}

} // namespace lumiere
