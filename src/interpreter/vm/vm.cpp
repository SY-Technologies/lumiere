#include "lumiere/interpreter/vm/vm.hpp"

#include <stdexcept>

namespace lumiere
{

void VM::execute(Program &)
{
    throw std::runtime_error("le backend VM n'est pas encore implemente");
}

} // namespace lumiere
