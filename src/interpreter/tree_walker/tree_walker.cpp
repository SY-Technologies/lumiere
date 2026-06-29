#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

#include <sstream>

namespace lumiere
{

RuntimeError::RuntimeError(std::string message,
                           std::string source_path,
                           std::string source_text,
                           uint32_t line,
                           uint32_t column,
                           std::vector<StackFrame> stack_trace)
    : std::runtime_error(message),
      message(std::move(message)),
      source_path(std::move(source_path)),
      source_text(std::move(source_text)),
      line(line),
      column(column),
      stack_trace(std::move(stack_trace))
{
    formatted_message = format();
}

const char *RuntimeError::what() const noexcept
{
    return formatted_message.c_str();
}

const std::string &RuntimeError::raw_message() const noexcept
{
    return message;
}

std::string RuntimeError::format() const
{
    std::ostringstream out;
    out << "Traceback (most recent call last):";

    if (!stack_trace.empty())
    {
        for (const StackFrame &frame : stack_trace)
        {
            out << "\n  " << format_frame(frame);
        }
    }

    const std::string location = format_location();
    if (!location.empty())
    {
        out << "\n  " << location;
    }

    const std::string source_snippet = format_source_snippet();
    if (!source_snippet.empty())
    {
        out << "\n" << source_snippet;
    }

    out << "\nerreur d'execution: " << message;

    return out.str();
}

std::string RuntimeError::format_location() const
{
    if (source_path.empty() && line == 0 && column == 0)
    {
        return {};
    }

    std::ostringstream out;
    out << "File ";
    if (!source_path.empty())
    {
        out << "\"" << source_path << "\"";
    }
    else
    {
        out << "\"<source>\"";
    }

    if (line != 0)
    {
        out << ", line " << line;
        if (column != 0)
        {
            out << ", column " << column;
        }
    }
    return out.str();
}

std::string RuntimeError::format_source_snippet() const
{
    if (source_text.empty() || line == 0)
    {
        return {};
    }

    std::istringstream input(source_text);
    std::string source_line;
    for (uint32_t current_line = 1; current_line <= line; ++current_line)
    {
        if (!std::getline(input, source_line))
        {
            return {};
        }
    }

    std::ostringstream out;
    out << "    " << source_line;
    if (column != 0)
    {
        out << "\n    " << std::string(column > 1 ? column - 1 : 0, ' ') << "^";
    }
    return out.str();
}

std::string RuntimeError::format_frame(const StackFrame &frame)
{
    std::ostringstream out;
    out << "in " << frame.function_name;

    if (!frame.source_path.empty() || frame.line != 0 || frame.column != 0)
    {
        out << " (";
        if (!frame.source_path.empty())
        {
            out << frame.source_path;
        }
        else
        {
            out << "<source>";
        }

        if (frame.line != 0)
        {
            out << ":" << frame.line;
            if (frame.column != 0)
            {
                out << ":" << frame.column;
            }
        }
        out << ")";
    }

    return out.str();
}

} // namespace lumiere
