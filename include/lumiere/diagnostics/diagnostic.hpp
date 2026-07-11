#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lumiere
{

enum class DiagnosticSeverity
{
    ERROR_LEVEL,
    WARNING_LEVEL,
    INFORMATION_LEVEL,
    HINT_LEVEL,
};

struct SourceRange
{
    /** Zero-based UTF-8 byte offset of the first covered byte. */
    std::size_t start = 0;
    /** Zero-based exclusive UTF-8 byte offset; may equal start at EOF. */
    std::size_t end = 0;
    /** One-based display line of the range start. */
    std::size_t line = 1;
    /** One-based display column of the range start. */
    std::size_t column = 1;
};

struct Diagnostic
{
    std::string code;
    DiagnosticSeverity severity = DiagnosticSeverity::ERROR_LEVEL;
    std::string message;
    std::string source_path;
    SourceRange range;
};

/**
 * @brief Returns the stable machine-readable spelling of a severity.
 */
[[nodiscard]] std::string_view diagnostic_severity_name(DiagnosticSeverity severity);

/**
 * @brief Formats one diagnostic for human-readable terminal output.
 *
 * The message is localized for Lumiere users and must not be parsed by tools.
 */
[[nodiscard]] std::string format_diagnostic(const Diagnostic &diagnostic);

/**
 * @brief Serializes diagnostics using Lumiere diagnostic protocol version 1.
 *
 * The returned JSON is newline-terminated for command-line streaming. Editor
 * integrations should validate protocolVersion before consuming diagnostics.
 *
 * @param diagnostics Diagnostics to serialize.
 * @param source_path Logical path identifying the analyzed source buffer.
 */
[[nodiscard]] std::string diagnostics_to_json(const std::vector<Diagnostic> &diagnostics,
                                              std::string_view source_path);

} // namespace lumiere
