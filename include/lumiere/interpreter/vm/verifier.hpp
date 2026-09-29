#pragma once

#include "lumiere/interpreter/vm/bytecode.hpp"

#include <optional>
#include <string>

namespace lumiere
{

/**
 * @brief Checks a compiled module once, before any of it runs.
 *
 * Returns the first problem found, or nothing when the module is safe to
 * execute: every opcode known, every operand present in the stream, every table
 * index in range, every jump landing on the first byte of an instruction, and
 * every function ending on an instruction that cannot fall past its own code.
 *
 * The interpreter depends on this having passed. It reads opcodes and operands
 * without bounds-checking them again, which used to cost a comparison per byte
 * on the hottest path in the runtime. Malformed bytecode is therefore refused
 * before execution starts rather than part-way through a program, which is both
 * cheaper and a clearer contract.
 *
 * Jump targets are checked against instruction boundaries here, which the
 * interpreter never did: it only checked that a target was inside the code.
 */
[[nodiscard]] std::optional<std::string> verify_module(const ModuleBytecode &module);

} // namespace lumiere
