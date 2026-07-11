#include "lumiere/diagnostics/diagnostic.hpp"

#include <sstream>

namespace lumiere
{
namespace
{

std::string json_string(const std::string_view value)
{
    std::ostringstream out;
    out << '"';
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (character < 0x20)
            {
                constexpr char digits[] = "0123456789abcdef";
                out << "\\u00" << digits[character >> 4] << digits[character & 0x0F];
            }
            else
            {
                out << static_cast<char>(character);
            }
            break;
        }
    }
    out << '"';
    return out.str();
}

} // namespace

std::string_view diagnostic_severity_name(const DiagnosticSeverity severity)
{
    switch (severity)
    {
    case DiagnosticSeverity::ERROR_LEVEL:
        return "error";
    case DiagnosticSeverity::WARNING_LEVEL:
        return "warning";
    case DiagnosticSeverity::INFORMATION_LEVEL:
        return "information";
    case DiagnosticSeverity::HINT_LEVEL:
        return "hint";
    }
    return "error";
}

std::string format_diagnostic(const Diagnostic &diagnostic)
{
    const char *severity = "erreur";
    switch (diagnostic.severity)
    {
    case DiagnosticSeverity::ERROR_LEVEL:
        break;
    case DiagnosticSeverity::WARNING_LEVEL:
        severity = "avertissement";
        break;
    case DiagnosticSeverity::INFORMATION_LEVEL:
        severity = "information";
        break;
    case DiagnosticSeverity::HINT_LEVEL:
        severity = "suggestion";
        break;
    }

    std::ostringstream out;
    if (!diagnostic.source_path.empty())
    {
        out << diagnostic.source_path << ':';
    }
    out << diagnostic.range.line << ':' << diagnostic.range.column
        << ": " << severity
        << '[' << diagnostic.code << "]: " << diagnostic.message;
    return out.str();
}

std::string diagnostics_to_json(const std::vector<Diagnostic> &diagnostics,
                                const std::string_view source_path)
{
    std::ostringstream out;
    out << "{\"protocolVersion\":1,\"source\":" << json_string(source_path) << ",\"diagnostics\":[";
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        if (i > 0)
        {
            out << ',';
        }
        const Diagnostic &diagnostic = diagnostics[i];
        out << "{\"code\":" << json_string(diagnostic.code)
            << ",\"severity\":" << json_string(diagnostic_severity_name(diagnostic.severity))
            << ",\"message\":" << json_string(diagnostic.message)
            << ",\"range\":{\"start\":" << diagnostic.range.start
            << ",\"end\":" << diagnostic.range.end
            << "},\"line\":" << diagnostic.range.line
            << ",\"column\":" << diagnostic.range.column << '}';
    }
    out << "]}\n";
    return out.str();
}

} // namespace lumiere
