#pragma once

#include "lumiere/interpreter/vm/bytecode.hpp"
#include "lumiere/interpreter/vm/lir.hpp"

namespace lumiere
{

class LirToBytecode
{
public:
    ModuleBytecode emit(const LirModule &module, std::size_t entry_function_index);
};

} // namespace lumiere
