#include "lumiere/analysis/analysis.hpp"

#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"

#include <algorithm>
#include <utility>

namespace lumiere
{

bool AnalysisResult::has_errors() const noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::ERROR;
    });
}

AnalysisResult analyze_source(std::string source, std::string source_path)
{
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenise();

    AnalysisResult result;
    result.diagnostics = lexer.diagnostics();
    for (Diagnostic &diagnostic : result.diagnostics)
    {
        diagnostic.source_path = source_path;
    }

    if (!result.has_errors())
    {
        Parser parser(std::move(tokens));
        result.statements = parser.parse();
        for (Diagnostic diagnostic : parser.diagnostics())
        {
            diagnostic.source_path = source_path;
            result.diagnostics.push_back(std::move(diagnostic));
        }
    }
    return result;
}

} // namespace lumiere
