#include <gtest/gtest.h>

#include "lumiere/analysis/analysis.hpp"
#include "lumiere/analysis/inspection.hpp"
#include "lumiere/diagnostics/diagnostic.hpp"

#include <algorithm>
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
        "/// Informe du double d'une valeur.\n"
        "fonction doubler(valeur: Entier) -> Entier { retourne valeur * 2 }\n";

    const auto inspection = inspect_source(source, source.rfind("doubler"));

    ASSERT_TRUE(inspection.has_value());
    EXPECT_EQ(inspection->label, "doubler");
    EXPECT_EQ(inspection->kind, "fonction");
    EXPECT_EQ(inspection->documentation, "Informe du double d'une valeur.");
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

TEST(SourceInspection, DocumentsUserClassMethodReferences)
{
    const std::string source =
        "/// Un point dans le plan.\n"
        "classe Point {\n"
        "  /// Fixe l'abscisse du point.\n"
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

} // namespace
