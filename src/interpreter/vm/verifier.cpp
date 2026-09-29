#include "lumiere/interpreter/vm/verifier.hpp"

#include <string>
#include <vector>

namespace lumiere
{
namespace
{

/**
 * @brief Walks one function's code once, checking every instruction.
 *
 * The walk is linear, so it also discovers where each instruction begins. Jump
 * targets are collected during the walk and checked against those boundaries at
 * the end, because a forward jump names an offset that has not been reached yet.
 */
class FunctionVerifier
{
public:
    FunctionVerifier(const ModuleBytecode &module, const FunctionBytecode &function)
        : m_module(module), m_function(function), m_chunk(function.chunk)
    {
    }

    std::optional<std::string> verify()
    {
        if (m_chunk.code.empty())
        {
            return fail("le code est vide");
        }
        m_starts.assign(m_chunk.code.size() + 1, false);

        Opcode last = Opcode::RETURN;
        while (m_offset < m_chunk.code.size())
        {
            m_instruction = m_offset;
            m_starts[m_offset] = true;
            if (auto problem = step(last))
            {
                return problem;
            }
        }
        if (m_offset != m_chunk.code.size())
        {
            return fail("la dernière instruction dépasse la fin du code");
        }

        // Falling past the last instruction would run off the end of the chunk,
        // so a function must end on one that never falls through. JUMP_IF_FALSE
        // is one, whatever its name says: it carries both targets and always
        // takes one of them. Leaving it off this list refused every function
        // whose last block ends in a branch -- which is what a `tant que` over
        // `et` or `ou` lays out, since the condition's blocks are appended after
        // the loop's exit -- so the default engine could not run the loop at all.
        if (last != Opcode::RETURN && last != Opcode::JUMP && last != Opcode::JUMP_IF_FALSE &&
            last != Opcode::PROPAGATE && last != Opcode::MATCH_ERROR)
        {
            return fail("la fonction peut continuer après sa dernière instruction");
        }

        for (const auto &[target, site] : m_jumps)
        {
            if (target >= m_starts.size() || !m_starts[target])
            {
                m_instruction = site;
                return fail("la cible de saut " + std::to_string(target) +
                            " ne tombe pas au début d'une instruction");
            }
        }
        return std::nullopt;
    }

private:
    std::optional<std::string> fail(const std::string &reason) const
    {
        return "fonction '" + m_function.name + "', décalage " + std::to_string(m_instruction) + " : " + reason;
    }

    [[nodiscard]] bool available(const std::size_t bytes) const
    {
        return m_offset + bytes <= m_chunk.code.size();
    }

    std::size_t u8() { return m_chunk.code[m_offset++]; }
    std::size_t u16()
    {
        const std::size_t high = u8();
        return (high << 8) | u8();
    }
    std::size_t u24()
    {
        const std::size_t high = u8();
        const std::size_t middle = u8();
        return (high << 16) | (middle << 8) | u8();
    }

    std::optional<std::string> step(Opcode &last)
    {
        const Opcode opcode = static_cast<Opcode>(u8());
        last = opcode;
        if (static_cast<std::uint8_t>(opcode) > static_cast<std::uint8_t>(Opcode::RETURN))
        {
            return fail("opcode inconnu " + std::to_string(static_cast<unsigned>(opcode)));
        }

        // Each case consumes exactly what the interpreter consumes for that opcode.
        switch (opcode)
        {
        case Opcode::CONSTANT:
            return index(1, m_chunk.constants.size(), "constante");
        case Opcode::CONSTANT_LONG:
            return index(3, m_chunk.constants.size(), "constante");
        case Opcode::GET_GLOBAL:
        case Opcode::SET_GLOBAL:
        case Opcode::INIT_GLOBAL:
            return index(1, m_module.globals.size(), "globale");
        case Opcode::GET_GLOBAL_LONG:
        case Opcode::SET_GLOBAL_LONG:
        case Opcode::INIT_GLOBAL_LONG:
            return index(3, m_module.globals.size(), "globale");
        case Opcode::GET_LOCAL:
        case Opcode::SET_LOCAL:
            return index(1, m_function.local_slot_count, "variable locale");
        case Opcode::CLEAR_LOCALS:
            return local_range();
        case Opcode::GET_CAPTURE:
        case Opcode::SET_CAPTURE:
            return index(1, m_function.capture_count, "capture");
        case Opcode::LIST:
        case Opcode::DICTIONARY:
        case Opcode::ENSEMBLE:
            // The operand is a count, bounded by its own encoding.
            return skip(1);
        case Opcode::CLOSURE:
            return closure();
        case Opcode::JUMP:
            return jump(1);
        case Opcode::JUMP_IF_FALSE:
            return jump(2);
        case Opcode::CALL:
            return call(0, 0);
        case Opcode::CALL_GLOBAL:
            return call(1, m_module.globals.size());
        case Opcode::CALL_GLOBAL_LONG:
            return call(3, m_module.globals.size());
        case Opcode::CALL_MEMBER:
        case Opcode::CALL_PARENT:
            return call(1, m_module.members.size());
        case Opcode::CALL_MEMBER_LONG:
        case Opcode::CALL_PARENT_LONG:
            return call(3, m_module.members.size());
        case Opcode::GET_MEMBER:
        case Opcode::GET_PARENT:
        case Opcode::SET_MEMBER:
            return index(1, m_module.members.size(), "membre");
        case Opcode::GET_MEMBER_LONG:
        case Opcode::GET_PARENT_LONG:
        case Opcode::SET_MEMBER_LONG:
            return index(3, m_module.members.size(), "membre");
        case Opcode::CLASS:
            return index(2, m_module.classes.size(), "classe");
        case Opcode::INTERFACE:
            return index(2, m_module.interfaces.size(), "interface");
        case Opcode::NAMESPACE:
            return index(2, m_module.namespaces.size(), "espace de noms");
        case Opcode::ASSERT_TYPE:
            return index(1, m_module.annotations.size(), "annotation");
        case Opcode::ASSERT_TYPE_LONG:
            return index(3, m_module.annotations.size(), "annotation");
        case Opcode::CAST:
        case Opcode::TYPE_CHECK:
        case Opcode::RESULT_FAILURE_TYPE:
            return index(1, m_module.types.size(), "type");
        case Opcode::CAST_LONG:
        case Opcode::TYPE_CHECK_LONG:
        case Opcode::RESULT_FAILURE_TYPE_LONG:
            return index(3, m_module.types.size(), "type");
        default:
            // Every remaining opcode takes no operand.
            return std::nullopt;
        }
    }

    std::optional<std::string> skip(const std::size_t bytes)
    {
        if (!available(bytes))
        {
            return fail("opérande tronquée");
        }
        m_offset += bytes;
        return std::nullopt;
    }

    std::optional<std::string> index(const std::size_t width, const std::size_t limit, const char *what)
    {
        if (!available(width))
        {
            return fail(std::string("opérande ") + what + " tronquée");
        }
        const std::size_t value = width == 1 ? u8() : width == 2 ? u16() : u24();
        if (value >= limit)
        {
            return fail(std::string("index de ") + what + ' ' + std::to_string(value) + " hors table (" +
                        std::to_string(limit) + ")");
        }
        return std::nullopt;
    }

    // CLEAR_LOCALS names a run of slots rather than one, so both ends have to
    // be inside the frame: the interpreter walks the range without checking.
    std::optional<std::string> local_range()
    {
        if (!available(2))
        {
            return fail("opérande de plage de variables locales tronquée");
        }
        const std::size_t first = u8();
        const std::size_t count = u8();
        if (first + count > m_function.local_slot_count)
        {
            return fail("plage de variables locales " + std::to_string(first) + '+' + std::to_string(count) +
                        " hors du cadre (" + std::to_string(m_function.local_slot_count) + ')');
        }
        return std::nullopt;
    }

    std::optional<std::string> jump(const std::size_t targets)
    {
        for (std::size_t i = 0; i < targets; ++i)
        {
            if (!available(2))
            {
                return fail("cible de saut tronquée");
            }
            m_jumps.emplace_back(u16(), m_instruction);
        }
        return std::nullopt;
    }

    std::optional<std::string> closure()
    {
        if (auto problem = index(2, m_module.functions.size(), "fonction"))
        {
            return problem;
        }
        if (!available(1))
        {
            return fail("nombre de captures tronqué");
        }
        const std::size_t captures = u8();
        // Each capture is a source flag and an index into that source.
        return skip(captures * 2);
    }

    std::optional<std::string> call(const std::size_t callee_width, const std::size_t callee_limit)
    {
        if (callee_width != 0)
        {
            if (auto problem = index(callee_width, callee_limit, "appel"))
            {
                return problem;
            }
        }
        if (!available(1))
        {
            return fail("arité tronquée");
        }
        const std::size_t arity = u8();
        // One argument-name index per argument, empty when positional.
        for (std::size_t i = 0; i < arity; ++i)
        {
            if (auto problem = index(2, m_module.argument_names.size() + 1, "nom d'argument"))
            {
                return problem;
            }
        }
        return std::nullopt;
    }

    const ModuleBytecode &m_module;
    const FunctionBytecode &m_function;
    const Chunk &m_chunk;
    std::size_t m_offset = 0;
    std::size_t m_instruction = 0;
    std::vector<bool> m_starts;
    std::vector<std::pair<std::size_t, std::size_t>> m_jumps;
};

} // namespace

std::optional<std::string> verify_module(const ModuleBytecode &module)
{
    for (const FunctionBytecode &function : module.functions)
    {
        FunctionVerifier verifier(module, function);
        if (auto problem = verifier.verify())
        {
            return problem;
        }
    }
    return std::nullopt;
}

} // namespace lumiere
