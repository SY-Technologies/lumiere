#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "lumiere/interpreter/vm/lir.hpp"
#include "lumiere/interpreter/vm/bytecode.hpp"

namespace
{

using lumiere::LirFunction;
using lumiere::LirInstruction;
using lumiere::LirModule;
using lumiere::LirOpcode;
using lumiere::LirOperand;
using lumiere::LirSourceLocation;
using lumiere::LirTerminator;

TEST(VmLir, AppendsBlocksWithStableIndices)
{
    LirFunction function;
    function.name = "principal";

    EXPECT_EQ(function.append_block().index, 0u);
    EXPECT_EQ(function.append_block().index, 1u);
    EXPECT_EQ(function.blocks.size(), 2u);
}

TEST(VmLir, RejectsInstructionAfterTerminator)
{
    LirFunction function;
    function.name = "principal";
    function.append_block();
    function.set_terminator(0, LirTerminator::return_nil());

    EXPECT_THROW(
        function.append_instruction(0, LirInstruction::make(LirOpcode::IR_OP_CONSTANT, LirOperand::temp(0), {LirOperand::constant(0)})),
        std::logic_error);
}

TEST(VmLir, RejectsDuplicateTerminators)
{
    LirFunction function;
    function.name = "principal";
    function.append_block();
    function.set_terminator(0, LirTerminator::return_nil());

    EXPECT_THROW(function.set_terminator(0, LirTerminator::return_nil()), std::logic_error);
}

TEST(VmLir, RejectsTerminatorTargetingMissingBlock)
{
    LirFunction function;
    function.name = "principal";
    function.append_block();

    EXPECT_THROW(function.set_terminator(0, LirTerminator::jump(1)), std::out_of_range);
}

TEST(VmLir, PrintsReadableModuleSyntax)
{
    LirModule module;
    module.name = "demo";

    const std::size_t one = module.add_constant(lumiere::Value::entier(1), "1");
    const std::size_t afficher = module.add_global("afficher");
    EXPECT_EQ(afficher, 0u);

    LirFunction &function = module.append_function("principal");
    function.params.push_back({0, "x"});
    function.locals.push_back({1, "somme"});
    function.temps = {0, 1};
    function.entry_block = function.append_block().index;
    function.append_block();

    function.append_instruction(
        0,
        LirInstruction::make(LirOpcode::IR_OP_CONSTANT,
                             LirOperand::temp(0),
                             {LirOperand::constant(one)},
                             {4, 10}));
    function.append_instruction(
        0,
        LirInstruction::make(LirOpcode::IR_OP_STORE_LOCAL,
                             LirOperand::local(1),
                             {LirOperand::local(1), LirOperand::temp(0)},
                             {4, 14}));
    function.set_terminator(0, LirTerminator::jump(1, {5, 3}));
    function.set_terminator(1, LirTerminator::return_value(LirOperand::local(1), {6, 3}));

    EXPECT_EQ(
        lumiere::to_string(module),
        "module demo\n"
        "constants:\n"
        "  K0 = 1\n"
        "globals:\n"
        "  G0 = afficher\n"
        "types:\n"
        "members:\n"
        "\n"
        "function principal\n"
        "params:\n"
        "  L0 = x\n"
        "locals:\n"
        "  L1 = somme\n"
        "captures:\n"
        "temps:\n"
        "  T0\n"
        "  T1\n"
        "entry:\n"
        "  B0\n"
        "\n"
        "B0:\n"
        "  T0 = IR_OP_CONSTANT K0 @line:4,col:10\n"
        "  IR_OP_STORE_LOCAL L1, T0 @line:4,col:14\n"
        "  IR_TERM_JUMP B1 @line:5,col:3\n"
        "\n"
        "B1:\n"
        "  IR_TERM_RETURN_VALUE L1 @line:6,col:3\n");
}

TEST(VmLir, ReusesExistingGlobalSlotsForRepeatedNames)
{
    LirModule module;

    const std::size_t afficher_a = module.add_global("afficher");
    const std::size_t afficher_b = module.add_global("afficher");
    const std::size_t autre = module.add_global("autre");

    EXPECT_EQ(afficher_a, 0u);
    EXPECT_EQ(afficher_b, 0u);
    EXPECT_EQ(autre, 1u);
    EXPECT_EQ(module.globals.size(), 2u);
}

TEST(VmBytecode, DisassemblesInstructionOffsetsAndSourceLocations)
{
    lumiere::ModuleBytecode module;
    lumiere::FunctionBytecode function;
    function.name = "principal";
    function.chunk.write_opcode(lumiere::Opcode::NIL, {2, 3});
    function.chunk.write_opcode(lumiere::Opcode::RETURN, {2, 4});
    module.functions.push_back(std::move(function));

    EXPECT_EQ(lumiere::disassemble(module),
              "bytecode module\n"
              "\n"
              "function 0 principal arity=0 locals=0 captures=0\n"
              "  0000  NIL                     @line:2,col:3\n"
              "  0001  RETURN                  @line:2,col:4\n");
}

} // namespace
