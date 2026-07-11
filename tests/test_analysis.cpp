#include <gtest/gtest.h>

#include "lumiere/analysis/analysis.hpp"
#include "lumiere/diagnostics/diagnostic.hpp"

namespace
{

using lumiere::AnalysisResult;
using lumiere::Diagnostic;
using lumiere::DiagnosticSeverity;
using lumiere::analyze_source;
using lumiere::diagnostics_to_json;

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

} // namespace
