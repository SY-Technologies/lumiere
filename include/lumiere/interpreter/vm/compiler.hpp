#pragma once

#include "lumiere/interpreter/iinterpreter.hpp"
#include "lumiere/interpreter/vm/bytecode.hpp"
#include "lumiere/interpreter/vm/lir.hpp"

#include <stdexcept>
#include <string>

namespace lumiere
{

class VmCompileError : public std::runtime_error
{
  public:
    explicit VmCompileError(const std::string &message) : std::runtime_error(message) {}
};

class VmCompiler
{
  public:
    LirModule lower_to_lir(Program &program);
    ModuleBytecode compile_for_inspection(Program &program);
    ModuleBytecode compile(Program &program);
};

} // namespace lumiere
