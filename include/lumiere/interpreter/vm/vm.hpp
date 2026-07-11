#pragma once

#include "lumiere/interpreter/iinterpreter.hpp"
#include "lumiere/interpreter/vm/bytecode.hpp"

namespace lumiere
{

class VM : public Backend
{
public:
    void execute(Program &program) override;

private:
    Value run(const ModuleBytecode &module);
};

} // namespace lumiere
