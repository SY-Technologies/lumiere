#pragma once

#include "lumiere/diagnostics/diagnostic.hpp"
#include "lumiere/parser/ast.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace lumiere
{

struct AnalysisResult
{
    StmtList statements;
    std::vector<Diagnostic> diagnostics;

    /**
     * @brief Reports whether analysis produced at least one error diagnostic.
     *
     * Warnings, information, and hints do not make this result erroneous.
     */
    [[nodiscard]] bool has_errors() const noexcept;
};

struct AnalysisOptions
{
    bool consume_last_expression = false;
};

/**
 * @brief Lexes, parses, and semantically validates one complete source buffer.
 *
 * Lexical errors prevent parsing. Parser errors are recovered where possible
 * so one call can report multiple independent syntax problems. No executable
 * AST is returned when errors are present.
 *
 * @param source Exact source text to analyze.
 * @param source_path Logical path attached to diagnostics, including for
 *        unsaved editor buffers.
 * @return Validated statements and backend-independent structured diagnostics.
 */
AnalysisResult analyze_source(std::string source,
                              std::string source_path = {},
                              AnalysisOptions options = {});

} // namespace lumiere
