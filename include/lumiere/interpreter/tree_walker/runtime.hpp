#pragma once

#include "lumiere/lexer/token.hpp"
#include "lumiere/interpreter/runtime/value.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lumiere
{

struct StackFrame
{
    std::string function_name;
    std::string source_path;
    uint32_t line = 0;
    uint32_t column = 0;
};

struct RuntimeError : std::runtime_error
{
    std::string message;
    std::string source_path;
    std::string source_text;
    uint32_t line = 0;
    uint32_t column = 0;
    std::vector<StackFrame> stack_trace;
    std::string formatted_message;

    RuntimeError(std::string message,
                 std::string source_path = {},
                 std::string source_text = {},
                 uint32_t line = 0,
                 uint32_t column = 0,
                 std::vector<StackFrame> stack_trace = {});

    const char *what() const noexcept override;
    const std::string &raw_message() const noexcept;

    std::string format() const;

private:
    std::string format_location() const;
    std::string format_source_snippet() const;
    static std::string format_frame(const StackFrame &frame);
};

struct ReturnSignal
{
    Value value = Value::rien();
};

struct ThrownSignal
{
    Value value = Value::rien();
    std::string source_path;
    std::string source_text;
    uint32_t line = 0;
    uint32_t column = 0;
    std::vector<StackFrame> stack_trace;
};

struct BreakSignal {};
struct ContinueSignal {};

inline StackFrame make_stack_frame(const std::string &function_name,
                                   const std::string &source_path,
                                   const Token &token)
{
    return StackFrame{
        function_name,
        source_path,
        token.line,
        token.column,
    };
}

} // namespace lumiere
