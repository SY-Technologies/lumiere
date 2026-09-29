#pragma once

#include "lumiere/interpreter/iinterpreter.hpp"
#include "lumiere/interpreter/vm/bytecode.hpp"

namespace lumiere
{

class VM : public Backend
{
public:
    void execute(Program &program) override;

    /**
     * @brief Runs a module that has already been verified.
     *
     * The precondition is the whole point of verify_module: operands, jump
     * targets and table indices are checked once, there, so the interpreter can
     * read them afterwards without checking again. Handing this a module that
     * verify_module has not accepted is undefined, and
     * VmVerifier.AcceptedBytecodeSurvivesExecution is what holds that claim to
     * account.
     */
    Value run(const ModuleBytecode &module);
};

} // namespace lumiere
