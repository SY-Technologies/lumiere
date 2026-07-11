#include "lumiere/interpreter/vm/bytecode.hpp"

#include <limits>
#include <stdexcept>

namespace lumiere
{

namespace
{

constexpr std::size_t k_max_long_index = 0xFFFFFF;

void write_index_ref(Chunk &chunk,
                     const Opcode short_opcode,
                     const Opcode long_opcode,
                     const std::size_t index,
                     const SourceLocation location,
                     const char *description)
{
    if (index <= std::numeric_limits<std::uint8_t>::max())
    {
        chunk.write_opcode(short_opcode, location);
        chunk.write_byte(static_cast<std::uint8_t>(index), location);
        return;
    }

    if (index > k_max_long_index)
    {
        throw std::runtime_error(std::string("VM : index trop grand pour ") + description);
    }

    chunk.write_opcode(long_opcode, location);
    chunk.write_byte(static_cast<std::uint8_t>((index >> 16) & 0xFF), location);
    chunk.write_byte(static_cast<std::uint8_t>((index >> 8) & 0xFF), location);
    chunk.write_byte(static_cast<std::uint8_t>(index & 0xFF), location);
}

}

    std::size_t Chunk::add_constant(Value value)
    {
        if (constants.size() > k_max_long_index)
        {
            throw std::runtime_error(
                "VM : trop de constantes dans le corps bytecode d'une fonction ; le format actuel prend en charge au plus 16777216 constantes par fonction");
        }

        constants.push_back(std::move(value));
        return constants.size() - 1;
    }

    void Chunk::write_opcode(Opcode opcode, SourceLocation location)
    {
        write_byte(static_cast<std::uint8_t>(opcode), location);
    }

    void Chunk::write_byte(std::uint8_t byte, SourceLocation location)
    {
        // the first opcode in the enum will have byte 0x00, the second one 0x01 etc
        code.push_back(byte);
        locations.push_back(location);
    }

    void Chunk::write_u16(const std::uint16_t value, const SourceLocation location)
    {
        write_byte(static_cast<std::uint8_t>((value >> 8) & 0xFF), location);
        write_byte(static_cast<std::uint8_t>(value & 0xFF), location);
    }

    void Chunk::write_u24(const std::size_t value, const SourceLocation location)
    {
        if (value > 0xFFFFFF)
        {
            throw std::out_of_range("bytecode index exceeds 24 bits");
        }
        write_byte(static_cast<std::uint8_t>((value >> 16) & 0xFF), location);
        write_byte(static_cast<std::uint8_t>((value >> 8) & 0xFF), location);
        write_byte(static_cast<std::uint8_t>(value & 0xFF), location);
    }

    void Chunk::write_constant_ref(const std::size_t index, const SourceLocation location)
    {
        if (index <= std::numeric_limits<std::uint8_t>::max())
        {
            write_opcode(Opcode::CONSTANT, location);
            write_byte(static_cast<std::uint8_t>(index), location);
            return;
        }

        if (index > k_max_long_index)
        {
            throw std::runtime_error(
                "VM : index de constante trop grand pour CONSTANT_LONG");
        }

        write_opcode(Opcode::CONSTANT_LONG, location);
        write_byte(static_cast<std::uint8_t>((index >> 16) & 0xFF), location);
        write_byte(static_cast<std::uint8_t>((index >> 8) & 0xFF), location);
        write_byte(static_cast<std::uint8_t>(index & 0xFF), location);
    }

    void Chunk::write_global_ref(const std::size_t index, const SourceLocation location)
    {
        write_index_ref(*this, Opcode::GET_GLOBAL, Opcode::GET_GLOBAL_LONG, index, location, "GET_GLOBAL_LONG");
    }

    void Chunk::write_cast_ref(const std::size_t index, const SourceLocation location)
    {
        write_index_ref(*this, Opcode::CAST, Opcode::CAST_LONG, index, location, "CAST_LONG");
    }

    void Chunk::write_type_check_ref(const std::size_t index, const SourceLocation location)
    {
        write_index_ref(*this, Opcode::TYPE_CHECK, Opcode::TYPE_CHECK_LONG, index, location, "TYPE_CHECK_LONG");
    }

    void Chunk::write_type_assertion(const std::size_t index, const SourceLocation location)
    {
        write_index_ref(*this, Opcode::ASSERT_TYPE, Opcode::ASSERT_TYPE_LONG, index, location, "ASSERT_TYPE_LONG");
    }

    void Chunk::write_global_call(const std::size_t index,
                                  const std::uint8_t arity,
                                  const SourceLocation location)
    {
        write_index_ref(*this, Opcode::CALL_GLOBAL, Opcode::CALL_GLOBAL_LONG, index, location, "CALL_GLOBAL_LONG");
        write_byte(arity, location);
    }

    void Chunk::write_member_call(const std::size_t index,
                                  const std::uint8_t arity,
                                  const SourceLocation location)
    {
        write_index_ref(*this, Opcode::CALL_MEMBER, Opcode::CALL_MEMBER_LONG, index, location, "CALL_MEMBER_LONG");
        write_byte(arity, location);
    }

    void Chunk::write_member_get(const std::size_t index, const SourceLocation location)
    {
        write_index_ref(*this, Opcode::GET_MEMBER, Opcode::GET_MEMBER_LONG, index, location, "GET_MEMBER_LONG");
    }

} // namespace lumiere
