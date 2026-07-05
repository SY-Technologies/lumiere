#pragma once

#include "lumiere/interpreter/iinterpreter.hpp"

namespace lumiere
{

class VM : public Backend
{
public:
    void execute(Program &program) override;
};

} // namespace lumiere
