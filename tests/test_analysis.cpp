#include <gtest/gtest.h>

#include "lumiere/analysis/analysis.hpp"
#include "lumiere/analysis/inspection.hpp"
#include "lumiere/analysis/semantic_analysis.hpp"
#include "lumiere/analysis/semantic_index.hpp"
#include "lumiere/analysis/semantic_type.hpp"
#include "lumiere/diagnostics/diagnostic.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace
{

using lumiere::AnalysisResult;
using lumiere::Diagnostic;
using lumiere::DiagnosticSeverity;
using lumiere::analyze_source;
using lumiere::diagnostics_to_json;
using lumiere::inspect_source;
using lumiere::inspection_to_json;

bool has_diagnostic(const AnalysisResult &result, const std::string_view code)
{
    return std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [&](const Diagnostic &diagnostic) {
            return diagnostic.code == code;
        });
}

TEST(SemanticTypes, AdoptReplacesAlreadyInternedTypes)
{
    lumiere::TypeInterner previous;
    const lumiere::SemanticTypeRef previous_integer = previous.builtin("Entier");

    lumiere::TypeInterner next;
    EXPECT_FALSE(lumiere::same_type(previous_integer, next.builtin("Entier")));

    next.adopt(previous);
    EXPECT_TRUE(lumiere::same_type(previous_integer, next.builtin("Entier")));
}

// Binding tests: SemanticModel::index (semantic_index.hpp) is populated
// alongside the existing find_value/find_type/m_value_symbols bookkeeping
// (declare_value/declare_type in semantic_analysis.cpp), not instead of it.
// These pin down that the two agree, since nothing yet reads the index in
// place of find_value/find_type -- a regression here would otherwise be
// invisible until something starts relying on the index instead.

TEST(SemanticIndexBinding, RecordsAModuleLevelValueDeclarationMatchingFindValue)
{
    const std::string source = "soit x = 5\nfonction f() -> Rien {}\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_FALSE(result.has_errors()) << diagnostics_to_json(result.diagnostics, "main.lum");
    ASSERT_NE(result.model, nullptr);

    const lumiere::SemanticSymbol *x = result.model->find_value("x");
    const lumiere::SemanticSymbol *f = result.model->find_value("f");
    ASSERT_NE(x, nullptr);
    ASSERT_NE(f, nullptr);

    const lumiere::Symbol *indexed_x = result.model->index.lookup(lumiere::kModuleScopeId, "x", lumiere::SymbolNamespace::Value);
    const lumiere::Symbol *indexed_f = result.model->index.lookup(lumiere::kModuleScopeId, "f", lumiere::SymbolNamespace::Value);
    ASSERT_NE(indexed_x, nullptr);
    ASSERT_NE(indexed_f, nullptr);

    EXPECT_EQ(indexed_x->kind, x->kind);
    EXPECT_EQ(indexed_x->name, "x");
    EXPECT_EQ(indexed_x->declaration_span.start, source.find("x"));
    EXPECT_EQ(indexed_x->declaration_span.end, source.find("x") + 1);

    EXPECT_EQ(indexed_f->kind, f->kind);
    EXPECT_EQ(indexed_f->name, "f");
    EXPECT_EQ(indexed_f->declaration_span.start, source.rfind('f'));
}

TEST(SemanticIndexBinding, RecordsAModuleLevelTypeDeclarationMatchingFindType)
{
    const std::string source = "classe Point {}\ninterface Forme {}\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_FALSE(result.has_errors()) << diagnostics_to_json(result.diagnostics, "main.lum");
    ASSERT_NE(result.model, nullptr);

    const lumiere::SemanticTypeRef *point_type = result.model->find_type("Point");
    const lumiere::SemanticTypeRef *forme_type = result.model->find_type("Forme");
    ASSERT_NE(point_type, nullptr);
    ASSERT_NE(forme_type, nullptr);

    const lumiere::Symbol *indexed_point = result.model->index.lookup(lumiere::kModuleScopeId, "Point", lumiere::SymbolNamespace::Type);
    const lumiere::Symbol *indexed_forme = result.model->index.lookup(lumiere::kModuleScopeId, "Forme", lumiere::SymbolNamespace::Type);
    ASSERT_NE(indexed_point, nullptr);
    ASSERT_NE(indexed_forme, nullptr);

    EXPECT_EQ(indexed_point->kind, lumiere::SemanticSymbolKind::CLASS);
    EXPECT_TRUE(lumiere::same_type(indexed_point->type, *point_type));

    EXPECT_EQ(indexed_forme->kind, lumiere::SemanticSymbolKind::INTERFACE);
    EXPECT_TRUE(lumiere::same_type(indexed_forme->type, *forme_type));
}

TEST(SemanticIndexBinding, ADuplicateDeclarationDiagnosesAndLeavesTheFirstSymbolIndexed)
{
    // declare_value/declare_type only records into the index after the
    // existing emplace succeeds -- a name declared twice must still leave
    // exactly one Symbol behind (the first), not two, and not crash. This is
    // the index's analogue of the parser-recovery guarantee: a rejected
    // second declaration is simply absent, the same way m_value_symbols
    // already only ever gains one entry for it.
    const std::string source = "soit x = 1\nsoit x = 2\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_TRUE(has_diagnostic(result, "LUM-S0001"));
    ASSERT_NE(result.model, nullptr);

    const lumiere::Scope *module_scope = result.model->index.scope(lumiere::kModuleScopeId);
    ASSERT_NE(module_scope, nullptr);
    const auto count = std::count_if(
        module_scope->symbols.begin(), module_scope->symbols.end(),
        [&](const lumiere::SymbolId id) {
            const lumiere::Symbol *symbol = result.model->index.symbol(id);
            return symbol != nullptr && symbol->name == "x";
        });
    EXPECT_EQ(count, 1);
}

// Step 3 of the rollout: local declarations get SymbolIds too (completing
// what step 2 did for module-level declare_value/declare_type), and every
// resolved IdentifierExpr read/write records an Occurrence against them.
// See docs/stage1-semantic-index-design.md's rollout plan.

TEST(SemanticIndexBinding, RecordsLocalParameterAndVariableDeclarationsInANestedScope)
{
    const std::string source =
        "fonction f(x: Entier) -> Rien {\n"
        "    soit y = x\n"
        "}\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_FALSE(result.has_errors()) << diagnostics_to_json(result.diagnostics, "main.lum");
    ASSERT_NE(result.model, nullptr);

    // Locals don't live in the module scope -- they're recorded in whichever
    // nested Scope push_scope() opened for the function body. Scan every
    // scope but the module's for them.
    const lumiere::Symbol *param_x = nullptr;
    const lumiere::Symbol *local_y = nullptr;
    for (std::uint32_t raw = 1;
         const lumiere::Scope *scope = result.model->index.scope(lumiere::ScopeId{raw});
         ++raw)
    {
        for (const lumiere::SymbolId id : scope->symbols)
        {
            const lumiere::Symbol *symbol = result.model->index.symbol(id);
            ASSERT_NE(symbol, nullptr);
            if (symbol->name == "x")
            {
                param_x = symbol;
            }
            if (symbol->name == "y")
            {
                local_y = symbol;
            }
        }
    }
    ASSERT_NE(param_x, nullptr);
    ASSERT_NE(local_y, nullptr);
    EXPECT_EQ(param_x->kind, lumiere::SemanticSymbolKind::PARAMETER);
    EXPECT_EQ(local_y->kind, lumiere::SemanticSymbolKind::VARIABLE);
    EXPECT_NE(param_x->scope, lumiere::kModuleScopeId);
}

TEST(SemanticIndexBinding, RecordsAReadOccurrenceForALocalVariable)
{
    const std::string source =
        "fonction f() -> Rien {\n"
        "    soit x = 1\n"
        "    soit y = x\n"
        "}\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_FALSE(result.has_errors()) << diagnostics_to_json(result.diagnostics, "main.lum");
    ASSERT_NE(result.model, nullptr);

    const std::size_t read_offset = source.rfind('x'); // the `x` in `soit y = x`
    const lumiere::SourceId main = result.model->index.source("main.lum");
    const lumiere::Occurrence *occurrence = result.model->index.occurrence_at(main, read_offset);
    ASSERT_NE(occurrence, nullptr);
    EXPECT_FALSE(occurrence->is_write);

    const lumiere::Symbol *resolved = result.model->index.symbol(occurrence->symbol);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->name, "x");
    EXPECT_EQ(resolved->kind, lumiere::SemanticSymbolKind::VARIABLE);
    // Resolves back to the declaring `x`, not a distinct symbol.
    EXPECT_EQ(resolved->declaration_span.start, source.find('x'));
}

TEST(SemanticIndexBinding, RecordsAWriteOccurrenceForAnAssignmentTarget)
{
    const std::string source =
        "fonction f() -> Rien {\n"
        "    soit x = 1\n"
        "    x = 2\n"
        "}\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_FALSE(result.has_errors()) << diagnostics_to_json(result.diagnostics, "main.lum");
    ASSERT_NE(result.model, nullptr);

    const std::size_t assign_offset = source.rfind('x'); // the `x` in `x = 2`
    const lumiere::SourceId main = result.model->index.source("main.lum");
    const lumiere::Occurrence *occurrence = result.model->index.occurrence_at(main, assign_offset);
    ASSERT_NE(occurrence, nullptr);
    EXPECT_TRUE(occurrence->is_write);

    const lumiere::Symbol *resolved = result.model->index.symbol(occurrence->symbol);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->name, "x");
}

TEST(SemanticIndexBinding, RecordsAReadOccurrenceForAModuleLevelValue)
{
    const std::string source = "soit x = 5\nsoit y = x\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_FALSE(result.has_errors()) << diagnostics_to_json(result.diagnostics, "main.lum");
    ASSERT_NE(result.model, nullptr);

    const std::size_t read_offset = source.rfind('x');
    const lumiere::SourceId main = result.model->index.source("main.lum");
    const lumiere::Occurrence *occurrence = result.model->index.occurrence_at(main, read_offset);
    ASSERT_NE(occurrence, nullptr);
    EXPECT_FALSE(occurrence->is_write);
    EXPECT_EQ(result.model->index.symbol(occurrence->symbol)->scope, lumiere::kModuleScopeId);
}

TEST(SemanticIndexBinding, UnresolvedNamesRecordNoOccurrenceButStayWellFormed)
{
    // analyze_source only builds a model at all once the parser has zero
    // diagnostics (analysis.cpp bails before semantic analysis otherwise),
    // so "recovery" at this layer means a name that fails to *resolve*
    // (diagnose_value_read's LUM-S0057 path) rather than a reparsed tree.
    // Recovery rule 2 (docs/stage1-semantic-index-design.md) says such a
    // read must simply not be recorded, never recorded with a dangling
    // SymbolId -- checked here by scanning every offset in the source and
    // confirming occurrence_at is either nullptr or resolves cleanly.
    const std::string source =
        "fonction f() -> Rien {\n"
        "    soit y = inconnu\n"
        "}\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_TRUE(has_diagnostic(result, "LUM-S0057"));
    ASSERT_NE(result.model, nullptr);

    const lumiere::SourceId main = result.model->index.source("main.lum");
    const std::size_t unresolved_offset = source.rfind("inconnu");
    for (std::size_t offset = 0; offset <= source.size(); ++offset)
    {
        if (const lumiere::Occurrence *occurrence = result.model->index.occurrence_at(main, offset))
        {
            EXPECT_NE(result.model->index.symbol(occurrence->symbol), nullptr) << "offset " << offset;
            EXPECT_NE(offset, unresolved_offset) << "an unresolved read must not record an occurrence";
        }
    }
}

TEST(SemanticIndexBinding, ADuplicateLocalDeclarationDiagnosesAndLeavesTheFirstSymbolIndexed)
{
    // Same guarantee as the module-level duplicate test above, but for
    // declare_local's own emplace-then-index sequencing.
    const std::string source =
        "fonction f() -> Rien {\n"
        "    soit x = 1\n"
        "    soit x = 2\n"
        "}\n";
    const AnalysisResult result = analyze_source(source, "main.lum");
    ASSERT_TRUE(has_diagnostic(result, "LUM-S0007"));
    ASSERT_NE(result.model, nullptr);

    int count = 0;
    for (std::uint32_t raw = 1;
         const lumiere::Scope *scope = result.model->index.scope(lumiere::ScopeId{raw});
         ++raw)
    {
        for (const lumiere::SymbolId id : scope->symbols)
        {
            const lumiere::Symbol *symbol = result.model->index.symbol(id);
            ASSERT_NE(symbol, nullptr);
            if (symbol->name == "x")
            {
                ++count;
            }
        }
    }
    EXPECT_EQ(count, 1);
}

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

TEST(SemanticPropagation, RequiresExplicitCompatibleResultBoundary)
{
    const std::string errors =
        "classe ErreurSource réalise Erreur {}\n"
        "classe ErreurCible réalise Erreur {}\n";
    const AnalysisResult non_result = analyze_source(
        errors +
        "fonction source() -> Résultat[Entier, ErreurSource] { retourne Échec(ErreurSource()) }\n"
        "fonction cible() -> Entier { retourne source() ou propager }\n",
        "non_result.lum");
    EXPECT_TRUE(has_diagnostic(non_result, "LUM-S0045"));

    const AnalysisResult implicit = analyze_source(
        errors +
        "fonction source() -> Résultat[Entier, ErreurSource] { retourne Échec(ErreurSource()) }\n"
        "fonction cible() { source() ou propager }\n",
        "implicit.lum");
    EXPECT_TRUE(has_diagnostic(implicit, "LUM-S0045"));

    const AnalysisResult incompatible = analyze_source(
        errors +
        "fonction source() -> Résultat[Entier, ErreurSource] { retourne Échec(ErreurSource()) }\n"
        "fonction cible() -> Résultat[Entier, ErreurCible] {\n"
        "  retourne Succès(source() ou propager)\n"
        "}\n",
        "incompatible.lum");
    EXPECT_TRUE(has_diagnostic(incompatible, "LUM-S0046"));

    const AnalysisResult compatible = analyze_source(
        errors +
        "type Source = Résultat[Entier, ErreurSource]\n"
        "type Cible = Résultat[Rien, ErreurSource | ErreurCible]\n"
        "fonction source() -> Source { retourne Succès(1) }\n"
        "fonction cible() -> Cible {\n"
        "  afficher(source() ou propager)\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "compatible.lum");
    EXPECT_FALSE(compatible.has_errors());
}

TEST(SemanticPropagation, MatchTerminatorAcceptsOnlyFailurePatterns)
{
    const std::string error =
        "classe ErreurTest réalise Erreur {}\n";
    const AnalysisResult success = analyze_source(
        error +
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Succès(1) }\n"
        "fonction cible() -> Résultat[Rien, ErreurTest] {\n"
        "  agir selon source() {\n"
        "    Succès(v) -> propager\n"
        "    Échec(e) -> ignorer\n"
        "  }\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "success_branch.lum");
    EXPECT_TRUE(has_diagnostic(success, "LUM-S0047"));

    const AnalysisResult failure = analyze_source(
        error +
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Échec(ErreurTest()) }\n"
        "fonction cible() -> Résultat[Rien, ErreurTest] {\n"
        "  agir selon source() {\n"
        "    Succès(v) -> afficher(v)\n"
        "    Échec(e) -> propager\n"
        "  }\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "failure_branch.lum");
    EXPECT_FALSE(failure.has_errors());

    const AnalysisResult ignored_non_result = analyze_source(
        "fonction principal() {\n"
        "  agir selon 1 {\n"
        "    1 -> ignorer\n"
        "    sinon -> rien\n"
        "  }\n"
        "}\n",
        "ignored_non_result.lum");
    EXPECT_TRUE(has_diagnostic(ignored_non_result, "LUM-S0048"));
}

TEST(SemanticPropagation, PreservesPendingAndNestedResultObligations)
{
    const std::string errors =
        "classe ErreurPremière réalise Erreur {}\n"
        "classe ErreurSeconde réalise Erreur {}\n";

    const AnalysisResult pending = analyze_source(
        errors +
        "fonction première() -> Résultat[Entier, ErreurPremière] { retourne Succès(1) }\n"
        "fonction seconde() -> Résultat[Entier, ErreurSeconde] { retourne Succès(2) }\n"
        "fonction cible() -> Résultat[Rien, ErreurPremière | ErreurSeconde] {\n"
        "  soit en_attente = première()\n"
        "  seconde() ou propager\n"
        "  ignorer en_attente\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "pending_before_propagation.lum");
    EXPECT_TRUE(has_diagnostic(pending, "LUM-S0029"));

    const AnalysisResult nested = analyze_source(
        errors +
        "fonction imbriquée() -> Résultat[Résultat[Entier, ErreurPremière], ErreurSeconde] {\n"
        "  retourne Succès(Succès(1))\n"
        "}\n"
        "fonction cible() -> Résultat[Rien, ErreurSeconde] {\n"
        "  imbriquée() ou propager\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "discarded_inner_result.lum");
    EXPECT_TRUE(has_diagnostic(nested, "LUM-S0028"));
}

TEST(SemanticPropagation, UsesTheNearestExplicitCallableBoundary)
{
    const std::string source =
        "classe ErreurTest réalise Erreur {}\n"
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Succès(1) }\n";

    const AnalysisResult top_level = analyze_source(
        source + "source() ou propager\n",
        "top_level_propagation.lum");
    EXPECT_TRUE(has_diagnostic(top_level, "LUM-S0045"));

    const AnalysisResult nested = analyze_source(
        source +
        "fonction externe() -> Résultat[Rien, ErreurTest] {\n"
        "  soit interne = fonction() { source() ou propager }\n"
        "  interne()\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "nested_non_result_propagation.lum");
    EXPECT_TRUE(has_diagnostic(nested, "LUM-S0045"));

    const AnalysisResult valid = analyze_source(
        source +
        "fonction avec_défaut(valeur: Entier = source() ou propager) -> Résultat[Entier, ErreurTest] {\n"
        "  retourne Succès(valeur)\n"
        "}\n"
        "fonction principal() {\n"
        "  soit interne = fonction() -> Résultat[Entier, ErreurTest] {\n"
        "    retourne Succès(source() ou propager)\n"
        "  }\n"
        "  ignorer interne()\n"
        "  ignorer avec_défaut()\n"
        "}\n",
        "valid_callable_boundaries.lum");
    EXPECT_FALSE(valid.has_errors());
}

TEST(SemanticPropagation, NarrowsMatchTerminatorErrorsPerBranch)
{
    const std::string source =
        "classe ErreurA réalise Erreur {}\n"
        "classe ErreurB réalise Erreur {}\n"
        "fonction source() -> Résultat[Entier, ErreurA | ErreurB] { retourne Succès(1) }\n";

    const AnalysisResult narrowed = analyze_source(
        source +
        "fonction cible() -> Résultat[Rien, ErreurA] {\n"
        "  agir selon source() {\n"
        "    Succès(_) -> ignorer\n"
        "    Échec(_: ErreurA) -> propager\n"
        "    Échec(_: ErreurB) -> ignorer\n"
        "  }\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "narrowed_match_propagation.lum");
    EXPECT_FALSE(narrowed.has_errors());

    const AnalysisResult widened_branch = analyze_source(
        source +
        "fonction cible() -> Résultat[Rien, ErreurA] {\n"
        "  agir selon source() {\n"
        "    Succès(_) -> ignorer\n"
        "    Échec(_: ErreurA), Échec(_: ErreurB) -> propager\n"
        "  }\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "wide_match_propagation.lum");
    EXPECT_TRUE(has_diagnostic(widened_branch, "LUM-S0046"));

    const AnalysisResult open_error = analyze_source(
        source +
        "fonction ouverte() -> Résultat[Entier, Erreur] { retourne Succès(1) }\n"
        "fonction cible() -> Résultat[Rien, ErreurA] {\n"
        "  agir selon ouverte() {\n"
        "    Succès(_) -> ignorer\n"
        "    Échec(_: ErreurA) -> propager\n"
        "    Échec(_) -> ignorer\n"
        "  }\n"
        "  retourne Succès(rien)\n"
        "}\n",
        "open_error_match_propagation.lum");
    EXPECT_FALSE(open_error.has_errors());
}

TEST(SemanticResultObligations, PreserveReturnContractThroughFunctionValues)
{
    const std::string error =
        "classe ErreurTest réalise Erreur {}\n";
    const AnalysisResult local = analyze_source(
        error +
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Succès(1) }\n"
        "fonction principal() {\n"
        "  soit opération = source\n"
        "  opération()\n"
        "}\n",
        "local.lum");
    EXPECT_TRUE(has_diagnostic(local, "LUM-S0028"));

    const AnalysisResult global = analyze_source(
        error +
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Succès(1) }\n"
        "soit opération = source\n"
        "fonction principal() { opération() }\n",
        "global.lum");
    EXPECT_TRUE(has_diagnostic(global, "LUM-S0028"));

    const AnalysisResult forward_global = analyze_source(
        error +
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Succès(1) }\n"
        "fonction principal() { opération() }\n"
        "soit opération = source\n",
        "forward_global.lum");
    EXPECT_TRUE(has_diagnostic(forward_global, "LUM-S0028"));

    const AnalysisResult handled = analyze_source(
        error +
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Succès(1) }\n"
        "fonction principal() {\n"
        "  soit opération = source\n"
        "  ignorer opération()\n"
        "}\n",
        "handled.lum");
    EXPECT_FALSE(handled.has_errors());

    const AnalysisResult reassigned = analyze_source(
        error +
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Succès(1) }\n"
        "fonction ordinaire() -> Entier { retourne 1 }\n"
        "fonction principal() {\n"
        "  soit opération = ordinaire\n"
        "  opération = source\n"
        "  opération()\n"
        "}\n",
        "reassigned.lum");
    EXPECT_TRUE(has_diagnostic(reassigned, "LUM-S0028"));

    const AnalysisResult anonymous = analyze_source(
        error +
        "fonction principal() {\n"
        "  soit opération = fonction() -> Résultat[Entier, ErreurTest] {\n"
        "    retourne Succès(1)\n"
        "  }\n"
        "  opération()\n"
        "}\n",
        "anonymous.lum");
    EXPECT_TRUE(has_diagnostic(anonymous, "LUM-S0028"));
}

TEST(SemanticResultObligations, SharedBindingsKeepOneObligation)
{
    const std::string declaration =
        "classe ErreurTest réalise Erreur {}\n"
        "fonction source() -> Résultat[Entier, ErreurTest] { retourne Succès(1) }\n";

    const AnalysisResult overwritten_alias = analyze_source(
        declaration +
            "fonction principal() {\n"
            "  soit premier = source()\n"
            "  soit second = premier\n"
            "  premier = source()\n"
            "  ignorer second\n"
            "  ignorer premier\n"
            "}\n",
        "overwritten_alias.lum");
    EXPECT_FALSE(overwritten_alias.has_errors());

    const AnalysisResult nested_alias = analyze_source(
        declaration +
            "fonction principal() {\n"
            "  soit premier = source()\n"
            "  {\n"
            "    soit second = premier\n"
            "  }\n"
            "  ignorer premier\n"
            "}\n",
        "nested_alias.lum");
    EXPECT_FALSE(nested_alias.has_errors());

    const AnalysisResult unhandled = analyze_source(
        declaration +
            "fonction principal() {\n"
            "  soit premier = source()\n"
            "  soit second = premier\n"
            "}\n",
        "unhandled.lum");
    EXPECT_TRUE(has_diagnostic(unhandled, "LUM-S0029"));
}

TEST(SemanticResultErrors, RequiresAnErrorTypeInEveryResult)
{
    const AnalysisResult valid = analyze_source(
        "classe ErreurBase réalise Erreur {}\n"
        "classe ErreurEnfant : ErreurBase {}\n"
        "classe ErreurAutre réalise Erreur {}\n"
        "type MarqueurErreur = Erreur\n"
        "classe ErreurAlias réalise MarqueurErreur {}\n"
        "type Erreurs = ErreurEnfant | ErreurAutre\n"
        "type Sortie = Résultat[Entier, Erreurs]\n"
        "fonction réussir() -> Sortie { retourne Succès(1) }\n"
        "fonction aliasée() -> Résultat[Entier, ErreurAlias] { retourne Succès(3) }\n"
        "fonction abstraite() -> Résultat[Entier, Erreur] { retourne Succès(2) }\n",
        "valid_error_types.lum");
    EXPECT_FALSE(valid.has_errors());

    for (const std::string type :
         {"Texte", "Entier", "Rien", "Universel",
          "Résultat[Entier, ErreurValide]"})
    {
        const AnalysisResult invalid = analyze_source(
            "classe ErreurValide réalise Erreur {}\n"
            "type Sortie = Résultat[Entier, " + type + "]\n",
            "invalid_error_type.lum");
        EXPECT_TRUE(has_diagnostic(invalid, "LUM-S0049"))
            << type;
    }

    const AnalysisResult mixed_alias = analyze_source(
        "classe ErreurValide réalise Erreur {}\n"
        "type Mélange = ErreurValide | Texte\n"
        "type Sortie = Résultat[Entier, Mélange]\n",
        "mixed_error_alias.lum");
    EXPECT_TRUE(has_diagnostic(mixed_alias, "LUM-S0049"));

    const AnalysisResult nested = analyze_source(
        "type Lot = Liste[Résultat[Entier, Texte]]\n",
        "nested_invalid_result.lum");
    EXPECT_TRUE(has_diagnostic(nested, "LUM-S0049"));
}

TEST(SemanticResultConstructors, RequireACompleteResultContext)
{
    const std::string error =
        "classe ErreurTest réalise Erreur {}\n";

    for (const std::string constructor :
         {"Succès(1)", "Échec(ErreurTest())"})
    {
        const AnalysisResult returned = analyze_source(
            error +
                "fonction invalide() -> Universel {\n"
                "  retourne " + constructor + "\n"
                "}\n",
            "non_result_return.lum");
        EXPECT_TRUE(has_diagnostic(returned, "LUM-S0050"))
            << constructor;

        const AnalysisResult inferred = analyze_source(
            error +
                "fonction invalide() {\n"
                "  retourne " + constructor + "\n"
                "}\n",
            "inferred_result_return.lum");
        EXPECT_TRUE(has_diagnostic(inferred, "LUM-S0032"))
            << constructor;
    }

    const AnalysisResult valid_contexts = analyze_source(
        error +
        "fonction accepter(valeur: Résultat[Entier, ErreurTest]) { ignorer valeur }\n"
        "fonction principal() {\n"
        "  soit résultat: Résultat[Entier, ErreurTest] = Succès(1)\n"
        "  ignorer résultat\n"
        "  résultat = Échec(ErreurTest())\n"
        "  accepter(valeur: Succès(2))\n"
        "  soit liste: Liste[Résultat[Entier, ErreurTest]] = [Succès(3), Échec(ErreurTest())]\n"
        "  ignorer résultat\n"
        "}\n",
        "valid_result_contexts.lum");
    EXPECT_FALSE(valid_contexts.has_errors());

    const AnalysisResult wrong_failure = analyze_source(
        error +
        "fonction invalide() -> Résultat[Entier, ErreurTest] {\n"
        "  retourne Échec(\"pas une erreur\")\n"
        "}\n",
        "wrong_failure_payload.lum");
    EXPECT_TRUE(wrong_failure.has_errors());

    const AnalysisResult no_context = analyze_source(
        error +
        "fonction principal() {\n"
        "  soit résultat = Succès(1)\n"
        "}\n",
        "missing_result_context.lum");
    EXPECT_TRUE(has_diagnostic(no_context, "LUM-S0042"));
    EXPECT_TRUE(has_diagnostic(no_context, "LUM-S0050"));

    const AnalysisResult erased_contexts = analyze_source(
        error +
        "fonction convertir() -> Universel {\n"
        "  retourne Succès(1) en Universel\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(Échec(ErreurTest()))\n"
        "}\n",
        "erased_result_contexts.lum");
    EXPECT_TRUE(has_diagnostic(erased_contexts, "LUM-S0050"));
}

TEST(SemanticNatives, InfersTextMethodsWithoutRequiringAModuleImport)
{
    for (const auto *prefix : {"", "importer Texte\n"})
    {
        const auto result = analyze_source(std::string(prefix) + R"lum(
fonction identite(t: Texte) -> Texte { retourne t }
fonction principal() {
    soit t: Texte = "é".repeter(3).inverser()
    soit copie: Texte = identite(t.majuscules())
    soit morceaux: Liste[Texte] = t.separer("é")
    soit longueur: Entier = t.taille()
    soit vide: Logique = t.est_vide()
    soit suite: Texte = t.sous_texte(1)
    soit repeter = t.repeter
    soit repetition: Texte = repeter(2)
    ignorer "42".en_entier()
}
)lum", "text_methods.lum");
        EXPECT_FALSE(result.has_errors()) << diagnostics_to_json(result.diagnostics, "text_methods.lum");
    }
}

TEST(SemanticNatives, ChecksTextMethodArgumentsFromTheSharedManifest)
{
    for (const auto &[expression, code] : {
             std::pair{"\"x\".repeter()", "LUM-S0017"},
             std::pair{"\"x\".repeter(\"incorrect\")", "LUM-S0019"},
             std::pair{"\"x\".repeter(nombre: 2)", "LUM-S0018"},
             std::pair{"\"x\".sous_texte(0, 1, 2)", "LUM-S0015"}})
    {
        SCOPED_TRACE(expression);
        const auto result = analyze_source("fonction principal() { " + std::string(expression) + " }\n", "invalid_text_method.lum");
        EXPECT_TRUE(has_diagnostic(result, code));
    }
}

TEST(SemanticNatives, HttpManifestMatchesCanonicalRuntimeContract)
{
    const AnalysisResult all_options = analyze_source(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  ignorer LumiNet.HTTP.obtenir(\n"
        "    \"http://localhost\",\n"
        "    entêtes: {},\n"
        "    corps: \"\",\n"
        "    type: \"text/plain\",\n"
        "    délai: rien)\n"
        "}\n",
        "http_options.lum");
    EXPECT_FALSE(all_options.has_errors());

    const AnalysisResult noncanonical_name = analyze_source(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  ignorer LumiNet.HTTP.obtenir(\"http://localhost\", entetes: {})\n"
        "}\n",
        "http_noncanonical.lum");
    EXPECT_TRUE(has_diagnostic(noncanonical_name, "LUM-S0014"));
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
    EXPECT_EQ(declaration->signature, "fonction doubler(valeur: Entier) -> Entier");
    EXPECT_EQ(declaration->return_type, "Entier");
    EXPECT_EQ(reference->signature, declaration->signature);
    EXPECT_EQ(reference->start_offset, source.rfind("doubler"));
}

TEST(SourceInspection, DescribesLanguageKeywords)
{
    const auto inspection = inspect_source("soit valeur = 1\n", 1);

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "soit");
    EXPECT_EQ(inspection->kind, "mot-clé");
    EXPECT_EQ(inspection->documentation, "Déclare une variable.");
}

TEST(SourceInspection, AttachesDocumentationCommentsToDeclarations)
{
    const std::string source =
        "/** Informe du double d'une valeur. */\n"
        "fonction doubler(valeur: Entier) -> Entier { retourne valeur * 2 }\n";

    const auto inspection = inspect_source(source, source.rfind("doubler"));

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "doubler");
    EXPECT_EQ(inspection->kind, "fonction");
    EXPECT_EQ(inspection->documentation, "Informe du double d'une valeur.");
}

TEST(SourceInspection, DoesNotAttachLegacyTripleSlashComments)
{
    const std::string source =
        "/// Ancienne documentation.\n"
        "fonction calculer() {}\n";

    const auto inspection = inspect_source(source, source.find("calculer"));

    ASSERT_TRUE(inspection.has_value());
    EXPECT_TRUE(inspection->documentation.empty());
}

TEST(SourceInspection, DocumentsCommonBuiltins)
{
    const std::string source = "afficher(\"bonjour\")\n";

    const auto inspection = inspect_source(source, 0);

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "afficher");
    EXPECT_EQ(inspection->kind, "fonction");
    EXPECT_EQ(inspection->signature, "afficher(texte : Texte) -> Rien");
    EXPECT_FALSE(inspection->documentation.empty());
}

TEST(SourceInspection, DocumentsStdlibMethodsOnTextValues)
{
    const std::string source =
        "soit message: Texte = \"Bonjour\"\n"
        "afficher(message.taille())\n";

    const auto inspection =
        inspect_source(source, source.rfind("taille"));

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "taille");
    EXPECT_EQ(inspection->kind, "méthode");
    EXPECT_EQ(inspection->signature, "taille() -> Entier");
    EXPECT_EQ(inspection->return_type, "Entier");
    EXPECT_FALSE(inspection->documentation.empty());
}

TEST(SourceInspection, DocumentsModuleFreeFunctions)
{
    const std::string source = "afficher(racine(9))\n";

    const auto inspection = inspect_source(source, source.find("racine"));

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "racine");
    EXPECT_EQ(inspection->kind, "fonction");
    EXPECT_EQ(inspection->signature, "racine(valeur : Décimal) -> Décimal");
    EXPECT_FALSE(inspection->documentation.empty());
}

TEST(SourceInspection, ResolvesQualifiedModuleDocumentationWithoutNameCollisions)
{
    const std::string maths =
        "importer Maths\n"
        "soit valeur = Maths.absolu(-2)\n";
    const std::string chemin =
        "importer Chemin comme C\n"
        "soit valeur = C.absolu(\".\")\n";

    const auto maths_inspection = inspect_source(maths, maths.rfind("absolu"));
    const auto chemin_inspection = inspect_source(chemin, chemin.rfind("absolu"));

    ASSERT_TRUE(maths_inspection.has_value());
    ASSERT_TRUE(chemin_inspection.has_value());
    EXPECT_EQ(maths_inspection->signature, "absolu(valeur : Décimal) -> Entier | Décimal");
    EXPECT_EQ(chemin_inspection->signature, "absolu(chemin : Texte) -> Texte");
}

TEST(SourceInspection, ResolvesSelectiveImportAliasesToTheirOriginalDocumentation)
{
    const std::string source =
        "importer Chemin.{absolu comme chemin_absolu}\n"
        "soit valeur = chemin_absolu(\".\")\n";

    const auto inspection = inspect_source(source, source.rfind("chemin_absolu"));

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "chemin_absolu");
    EXPECT_EQ(inspection->signature, "absolu(chemin : Texte) -> Texte");
    EXPECT_FALSE(inspection->documentation.empty());
}

TEST(SourceInspection, DocumentsNewlyCoveredNativeApis)
{
    const std::string source =
        "importer Chemin\n"
        "importer Texte\n"
        "soit chemin = Chemin.joindre(\"a\", \"b\")\n"
        "soit texte = Texte.convertir_entier(42)\n"
        "soit longueur = Texte.taille(texte)\n";

    const auto joindre = inspect_source(source, source.find("joindre"));
    const auto convertir = inspect_source(source, source.find("convertir_entier"));
    const auto taille = inspect_source(source, source.rfind("taille"));

    ASSERT_TRUE(joindre.has_value());
    ASSERT_TRUE(convertir.has_value());
    ASSERT_TRUE(taille.has_value());
    EXPECT_EQ(joindre->signature, "joindre(segments : Texte) -> Texte");
    EXPECT_EQ(convertir->signature, "convertir_entier(valeur : Entier) -> Texte");
    EXPECT_EQ(taille->signature, "taille(texte : Texte) -> Entier");
}

TEST(SourceInspection, DocumentsNestedLumiNetNamespaces)
{
    const std::string source =
        "importer LumiNet comme Réseau\n"
        "soit adresse = Réseau.Adresse.analyser(\"127.0.0.1\")\n";

    const auto inspection = inspect_source(source, source.find("analyser"));
    const auto type = inspect_source(source, source.find("Adresse"));

    ASSERT_TRUE(inspection.has_value());
    ASSERT_TRUE(type.has_value());
    EXPECT_EQ(inspection->signature,
              "analyser(texte : Texte) -> Résultat[AdresseRéseau,ErreurAdresse]");
    EXPECT_FALSE(inspection->documentation.empty());
    EXPECT_EQ(type->kind, "classe");
    EXPECT_FALSE(type->documentation.empty());
}

TEST(SourceInspection, DocumentsUserClassMethodReferences)
{
    const std::string source =
        "/** Un point dans le plan. */\n"
        "classe Point {\n"
        "  /** Fixe l'abscisse du point. */\n"
        "  fonction abscisse(valeur: Entier) { retourne valeur }\n"
        "}\n"
        "soit point = Point()\n"
        "point.abscisse(3)\n";

    const auto inspection =
        inspect_source(source, source.rfind("abscisse"));

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "abscisse");
    EXPECT_EQ(inspection->kind, "méthode");
    EXPECT_EQ(inspection->signature, "fonction abscisse(valeur: Entier) -> Rien");
    EXPECT_EQ(inspection->documentation, "Fixe l'abscisse du point.");
}

TEST(SourceInspection, EscapesControlCharactersInJson)
{
    // The escaper this serializes through used to remember only the quote and
    // the backslash -- any other control character (a tab, a newline, a raw
    // 0x01) went into the output byte for byte, which is not valid JSON and
    // would desynchronize whatever parsed it. A doc comment is exactly where
    // one of these can appear: it is free-form text a user wrote, not a
    // grammar production that could plausibly forbid a tab.
    lumiere::Inspection inspection;
    inspection.label = "tabulee";
    inspection.kind = "fonction";
    inspection.signature = "tabulee() -> Rien";
    inspection.documentation = "Colonne un\tcolonne deux\nligne suivante avec \"guillemets\" et \x01.";

    const std::string json = inspection_to_json(inspection);

    const std::string expected_documentation =
        "\"documentation\":\"Colonne un\\tcolonne deux\\nligne suivante avec \\\"guillemets\\\" et \\u0001.\"";
    EXPECT_NE(json.find(expected_documentation), std::string::npos) << json;

    // No literal control byte reached the output: every one of them was
    // escaped, not just the two escape_json used to remember.
    EXPECT_EQ(json.find('\t'), std::string::npos);
    EXPECT_EQ(json.find('\n'), std::string::npos);
    EXPECT_EQ(json.find('\x01'), std::string::npos);
}

TEST(SourceInspection, ResolvesMemberCallOnceAnImportItDependsOnIsThreaded)
{
    // inspect_source used to analyze the buffer with no source_path at all
    // (the parameter did not exist), so a file whose own type-checking
    // depends on a locally imported module -- here, a method parameter typed
    // by an imported class -- always failed semantic analysis: the import
    // could never resolve without a path to resolve it against. That made
    // analysis.has_errors() true, and inspect_source returned nullopt before
    // ever looking at the member access. It also used to re-run semantic
    // analysis a second time, from scratch, with no path or imports either,
    // just to answer a member access -- so even a caller that supplied the
    // path to the first pass would have lost it again on the second. Passing
    // source_path through and reusing analysis.model (the first pass' own,
    // correctly import-resolved model) is what this test guards end to end.
    const auto root = std::filesystem::temp_directory_path() / "lumiere_inspect_local_import";
    std::filesystem::create_directories(root);
    const auto module_file = root / "Formes.lum";
    const auto main_file = root / "main.lum";

    {
        std::ofstream module_out(module_file);
        module_out << "public classe Point {}\n";
    }

    const std::string source =
        "importer Formes\n"
        "classe Boite {\n"
        "  /** Range un point dans la boîte. */\n"
        "  fonction stocker(valeur: Formes.Point) -> Rien {}\n"
        "}\n"
        "fonction principal() {\n"
        "  soit boite = Boite()\n"
        "  boite.stocker(Formes.Point())\n"
        "}\n";
    {
        std::ofstream main_out(main_file);
        main_out << source;
    }

    // With no source_path, the import cannot resolve, semantic analysis
    // reports an error, and inspect_source bails out before it ever reaches
    // the member access.
    const auto without_path = inspect_source(source, source.rfind("stocker"));
    EXPECT_FALSE(without_path.has_value());

    // With the real path, the import resolves, analysis is clean, and the
    // member access on `boite` (a class declared in this same buffer) now
    // resolves through the reused, correctly-built model.
    const auto with_path =
        inspect_source(source, source.rfind("stocker"), main_file.string());

    std::filesystem::remove_all(root);

    ASSERT_TRUE(with_path.has_value());
    EXPECT_EQ(with_path->label, "stocker");
    EXPECT_EQ(with_path->kind, "méthode");
    EXPECT_EQ(with_path->documentation, "Range un point dans la boîte.");
}

TEST(AnalysisDiagnostics, RejectsLocalVariableShadowingImportModuleName)
{
    const AnalysisResult result = analyze_source(
        "importer Maths\n"
        "fonction principal() {\n"
        "  soit Maths = 5\n"
        "}\n",
        "main.lum");

    EXPECT_TRUE(result.has_errors());
    bool found = false;
    for (const auto &d : result.diagnostics)
    {
        if (d.code == "LUM-S0044")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(AnalysisDiagnostics, RejectsLocalVariableShadowingImportAlias)
{
    const AnalysisResult result = analyze_source(
        "importer Maths comme M\n"
        "fonction principal() {\n"
        "  soit M = 5\n"
        "}\n",
        "main.lum");

    EXPECT_TRUE(result.has_errors());
    bool found = false;
    for (const auto &d : result.diagnostics)
    {
        if (d.code == "LUM-S0044")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(AnalysisDiagnostics, RejectsLocalVariableShadowingSelectiveImport)
{
    const AnalysisResult result = analyze_source(
        "importer Maths.{absolu}\n"
        "fonction principal() {\n"
        "  soit absolu = 5\n"
        "}\n",
        "main.lum");

    EXPECT_TRUE(result.has_errors());
    bool found = false;
    for (const auto &d : result.diagnostics)
    {
        if (d.code == "LUM-S0044")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(AnalysisDiagnostics, RejectsLocalVariableShadowingSelectiveImportAlias)
{
    const AnalysisResult result = analyze_source(
        "importer Maths.{absolu comme a}\n"
        "fonction principal() {\n"
        "  soit a = 5\n"
        "}\n",
        "main.lum");

    EXPECT_TRUE(result.has_errors());
    bool found = false;
    for (const auto &d : result.diagnostics)
    {
        if (d.code == "LUM-S0044")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(AnalysisDiagnostics, RejectsParameterShadowingImportName)
{
    const AnalysisResult result = analyze_source(
        "importer Maths\n"
        "fonction principal(Maths: Entier) {\n"
        "}\n",
        "main.lum");

    EXPECT_TRUE(result.has_errors());
    bool found = false;
    for (const auto &d : result.diagnostics)
    {
        if (d.code == "LUM-S0044")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(AnalysisDiagnostics, RejectsNestedFunctionShadowingImportName)
{
    const AnalysisResult result = analyze_source(
        "importer Maths\n"
        "fonction principal() {\n"
        "  fonction Maths() {}\n"
        "}\n",
        "main.lum");

    EXPECT_TRUE(result.has_errors());
    bool found = false;
    for (const auto &d : result.diagnostics)
    {
        if (d.code == "LUM-S0044")
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(AnalysisDiagnostics, AllowsImportUsageWithoutShadowing)
{
    const AnalysisResult result = analyze_source(
        "importer Maths\n"
        "fonction principal() {\n"
        "  afficher(Maths.pi)\n"
        "}\n",
        "main.lum");

    EXPECT_FALSE(result.has_errors());
}

TEST(SourceInspection, ReturnsNullForUnknownIdentifiers)
{
    EXPECT_EQ(inspection_to_json(inspect_source("inconnu()\n", 2)),
              "{\"protocolVersion\":2,\"inspection\":null}");
}

TEST(AnalysisDiagnostics, AcceptsCheminJoindreWithMoreThanOneSegment)
{
    // Chemin.joindre's native implementation loops over an arbitrary number
    // of Texte segments (see chemin.cpp), but its declared signature used to
    // list only one positional parameter, so the analyzer rejected any call
    // beyond a single segment even though the runtime happily supported it.
    const AnalysisResult result = analyze_source(
        "importer Chemin.{joindre}\n"
        "fonction principal() {\n"
        "  afficher(joindre(\"un\", \"deux\", \"trois\"))\n"
        "}\n",
        "main.lum");

    EXPECT_FALSE(result.has_errors());
}

TEST(AnalysisDiagnostics, RejectsCheminJoindreVariadicSegmentOfWrongType)
{
    const AnalysisResult result = analyze_source(
        "importer Chemin.{joindre}\n"
        "fonction principal() {\n"
        "  afficher(joindre(\"un\", 2))\n"
        "}\n",
        "main.lum");

    EXPECT_TRUE(result.has_errors());
}

} // namespace
