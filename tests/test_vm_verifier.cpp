#include "lumiere/interpreter/vm/verifier.hpp"

#include <gtest/gtest.h>

#include <string>

namespace lumiere
{
namespace
{

constexpr SourceLocation kSite{1, 1};

/** @brief A module holding one function, whose code the test writes by hand. */
struct Handbuilt
{
    ModuleBytecode module;

    explicit Handbuilt(const std::size_t locals = 0)
    {
        module.functions.emplace_back();
        function().name = "essai";
        function().local_slot_count = locals;
    }

    FunctionBytecode &function() { return module.functions.front(); }
    Chunk &chunk() { return function().chunk; }

    void op(const Opcode opcode) { chunk().write_opcode(opcode, kSite); }
    void byte(const std::uint8_t value) { chunk().write_byte(value, kSite); }
    void u16(const std::uint16_t value) { chunk().write_u16(value, kSite); }

    [[nodiscard]] std::optional<std::string> verify() const { return verify_module(module); }
};

} // namespace

TEST(VmVerifier, AcceptsAFunctionThatOnlyReturns)
{
    Handbuilt built;
    built.op(Opcode::RETURN);
    EXPECT_FALSE(built.verify().has_value());
}

TEST(VmVerifier, RejectsAnUnknownOpcode)
{
    Handbuilt built;
    built.byte(240);
    built.op(Opcode::RETURN);
    const auto problem = built.verify();
    ASSERT_TRUE(problem.has_value());
    EXPECT_NE(problem->find("opcode inconnu"), std::string::npos) << *problem;
}

TEST(VmVerifier, RejectsATruncatedOperand)
{
    Handbuilt built;
    built.op(Opcode::CONSTANT); // its index byte is missing
    const auto problem = built.verify();
    ASSERT_TRUE(problem.has_value());
    EXPECT_NE(problem->find("tronquée"), std::string::npos) << *problem;
}

TEST(VmVerifier, RejectsAnIndexPastItsTable)
{
    Handbuilt built;
    built.op(Opcode::CONSTANT);
    built.byte(7); // the constant pool is empty
    built.op(Opcode::RETURN);
    const auto problem = built.verify();
    ASSERT_TRUE(problem.has_value());
    EXPECT_NE(problem->find("hors table"), std::string::npos) << *problem;
}

TEST(VmVerifier, RejectsALocalSlotTheFrameDoesNotHave)
{
    Handbuilt built(2);
    built.op(Opcode::GET_LOCAL);
    built.byte(5);
    built.op(Opcode::RETURN);
    const auto problem = built.verify();
    ASSERT_TRUE(problem.has_value());
    EXPECT_NE(problem->find("variable locale"), std::string::npos) << *problem;
}

TEST(VmVerifier, RejectsAFunctionThatRunsPastItsCode)
{
    Handbuilt built;
    built.op(Opcode::NIL); // nothing stops execution after this
    const auto problem = built.verify();
    ASSERT_TRUE(problem.has_value());
    EXPECT_NE(problem->find("après sa dernière instruction"), std::string::npos) << *problem;
}

TEST(VmVerifier, AcceptsATwoWayBranchAsTheLastInstruction)
{
    // JUMP_IF_FALSE names both of its targets and always takes one, so it can
    // end a function. The compiler lays one out last whenever a `tant que`
    // condition uses `et` or `ou`.
    Handbuilt built;
    built.chunk().add_constant(Value::logique(true));
    built.op(Opcode::JUMP);
    built.u16(4); // over the RETURN, to the CONSTANT
    built.op(Opcode::RETURN); // offset 3
    built.op(Opcode::CONSTANT); // offset 4
    built.byte(0);
    built.op(Opcode::JUMP_IF_FALSE); // offset 6, the last instruction
    built.u16(3);
    built.u16(3);
    EXPECT_FALSE(built.verify().has_value()) << *built.verify();
}

TEST(VmVerifier, RejectsAJumpIntoTheMiddleOfAnInstruction)
{
    Handbuilt built;
    built.chunk().add_constant(Value::entier(1));
    built.op(Opcode::CONSTANT); // offset 0, its operand is at offset 1
    built.byte(0);
    built.op(Opcode::JUMP);
    built.u16(1); // lands on the operand, not on an instruction
    built.op(Opcode::RETURN);
    const auto problem = built.verify();
    ASSERT_TRUE(problem.has_value());
    EXPECT_NE(problem->find("ne tombe pas au début"), std::string::npos) << *problem;
}

TEST(VmVerifier, RejectsAJumpPastTheEnd)
{
    Handbuilt built;
    built.op(Opcode::JUMP);
    built.u16(900);
    const auto problem = built.verify();
    ASSERT_TRUE(problem.has_value());
    EXPECT_NE(problem->find("cible de saut"), std::string::npos) << *problem;
}

TEST(VmVerifier, AcceptsAJumpOntoAnInstructionBoundary)
{
    Handbuilt built;
    built.chunk().add_constant(Value::entier(1));
    built.op(Opcode::JUMP);
    built.u16(3); // the CONSTANT below starts here
    built.op(Opcode::CONSTANT);
    built.byte(0);
    built.op(Opcode::RETURN);
    EXPECT_FALSE(built.verify().has_value());
}

} // namespace lumiere
