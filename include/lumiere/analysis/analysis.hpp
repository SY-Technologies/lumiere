#pragma once

#include "lumiere/diagnostics/diagnostic.hpp"
#include "lumiere/parser/ast.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lumiere
{

class SemanticModel;

struct AnalysisResult
{
    StmtList statements;
    std::vector<Diagnostic> diagnostics;

    /**
     * @brief What this analysis established: every name, type and signature.
     *
     * Kept so a later analysis of the same session can start from it — the
     * shell is the only caller that does. It points into `statements`, so it
     * is only good for as long as they are.
     */
    std::shared_ptr<SemanticModel> model;

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
    // Set when the file is about to be run as a program rather than read as a
    // module. A module has no entry point and needs none, so this is off by
    // default: `lumiere check` on a module must not demand a 'principal' the
    // module has no business declaring.
    bool require_entry_point = false;
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
                              AnalysisOptions options = {},
                              const SemanticModel *previous = nullptr);

} // namespace lumiere
