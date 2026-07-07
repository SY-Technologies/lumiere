#include "lumiere/interpreter/vm/bytecode.hpp"

#include <limits>
#include <stdexcept>

namespace lumiere
{

std::uint8_t Chunk::add_constant(Value value)
{
    if (constants.size() >= std::numeric_limits<std::uint8_t>::max())
    {
        throw std::runtime_error("trop de constantes dans un chunk VM");
    }

    constants.push_back(std::move(value));
    return static_cast<std::uint8_t>(constants.size() - 1);
}

void Chunk::write_opcode(Opcode opcode, SourceLocation location)
{
    write_byte(static_cast<std::uint8_t>(opcode), location);
}

void Chunk::write_byte(std::uint8_t byte, SourceLocation location)
{
    code.push_back(byte);
    locations.push_back(location);
}

} // namespace lumiere
