#include <gtest/gtest.h>

#include "lumiere/analysis/analysis.hpp"
#include "lumiere/analysis/inspection.hpp"
#include "lumiere/diagnostics/diagnostic.hpp"

namespace
{

using lumiere::AnalysisResult;
using lumiere::Diagnostic;
using lumiere::DiagnosticSeverity;
using lumiere::analyze_source;
using lumiere::diagnostics_to_json;
using lumiere::inspect_source;
using lumiere::inspection_to_json;

TEST(AnalysisDiagnostics, ReportsLexicalErrorWithByteRange)
{
    const AnalysisResult result = analyze_source("soit café = @\n", "main.lum");

    ASSERT_TRUE(result.has_errors());
    ASSERT_EQ(result.diagnostics.size(), 1u);
    const Diagnostic &diagnostic = result.diagnostics.front();
    EXPECT_EQ(diagnostic.code, "LUM-L0001");
    EXPECT_EQ(diagnostic.source_path, "main.lum");
    EXPECT_EQ(diagnostic.range.start, 13u);
    EXPECT_EQ(diagnostic.range.end, 14u);
    EXPECT_EQ(diagnostic.range.line, 1u);
    EXPECT_EQ(diagnostic.range.column, 14u);
}

TEST(AnalysisDiagnostics, RecoversAfterIndependentParserErrors)
{
    const AnalysisResult result = analyze_source(
        "soit = 1\n"
        "soit = 2\n",
        "main.lum");

    EXPECT_TRUE(result.has_errors());
    EXPECT_TRUE(result.statements.empty());
    ASSERT_EQ(result.diagnostics.size(), 2u);
    EXPECT_EQ(result.diagnostics[0].code, "LUM-P0001");
    EXPECT_EQ(result.diagnostics[1].code, "LUM-P0001");
    EXPECT_EQ(result.diagnostics[0].range.line, 1u);
    EXPECT_EQ(result.diagnostics[1].range.line, 2u);
}

TEST(AnalysisDiagnostics, KeepsEndOfFileRangeInsideSource)
{
    const std::string source = "soit valeur =";
    const AnalysisResult result = analyze_source(source, "main.lum");

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics.front().range.start, source.size());
    EXPECT_EQ(result.diagnostics.front().range.end, source.size());
}

TEST(AnalysisDiagnostics, SerializesStableJsonProtocol)
{
    const Diagnostic diagnostic{
        "LUM-P0001",
        DiagnosticSeverity::ERROR_LEVEL,
        "attendu \"nom\"",
        "ignored.lum",
        {3, 4, 1, 4},
    };

    EXPECT_EQ(
        diagnostics_to_json({diagnostic}, "src/main.lum"),
        "{\"protocolVersion\":1,\"source\":\"src/main.lum\",\"diagnostics\":["
        "{\"code\":\"LUM-P0001\",\"severity\":\"error\","
        "\"message\":\"attendu \\\"nom\\\"\",\"range\":{\"start\":3,\"end\":4},"
        "\"line\":1,\"column\":4}]}\n");
}

TEST(SourceInspection, DescribesFunctionDeclarationsAndReferences)
{
    const std::string source =
        "fonction doubler(valeur: Entier) -> Entier { retourne valeur * 2 }\n"
        "soit résultat = doubler(21)\n";

    const auto declaration = inspect_source(source, source.find("doubler"));
    const auto reference = inspect_source(source, source.rfind("doubler"));

    ASSERT_TRUE(declaration.has_value());
    ASSERT_TRUE(reference.has_value());
    EXPECT_EQ(declaration->detail, "fonction doubler(valeur: Entier) -> Entier");
    EXPECT_EQ(reference->detail, declaration->detail);
    EXPECT_EQ(reference->start_offset, source.rfind("doubler"));
}

TEST(SourceInspection, DescribesLanguageKeywords)
{
    const auto inspection = inspect_source("soit valeur = 1\n", 1);

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "soit");
    EXPECT_EQ(inspection->detail, "Déclare une variable.");
}

TEST(SourceInspection, ReturnsNullForUnknownIdentifiers)
{
    EXPECT_EQ(inspection_to_json(inspect_source("inconnu()\n", 2)),
              "{\"protocolVersion\":1,\"inspection\":null}");
}

} // namespace
