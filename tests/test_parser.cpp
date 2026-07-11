#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"
#include "lumiere/parser/printer.hpp"
#include "lumiere/source_file.hpp"

namespace
{

using lumiere::AstPrinter;
using lumiere::Lexer;
using lumiere::Parser;
using lumiere::SOURCE_FILE_EXTENSION;
using lumiere::is_source_file;
using lumiere::is_test_source_file;

TEST(SourceFile, RecognizesConfiguredExtensionAndTestSuffix)
{
    const std::filesystem::path source = "main" + std::string(SOURCE_FILE_EXTENSION);
    const std::filesystem::path test_source = "maths_test" + std::string(SOURCE_FILE_EXTENSION);

    EXPECT_TRUE(is_source_file(source));
    EXPECT_FALSE(is_test_source_file(source));
    EXPECT_TRUE(is_source_file(test_source));
    EXPECT_TRUE(is_test_source_file(test_source));
    EXPECT_FALSE(is_source_file("main.txt"));
}
using lumiere::StmtList;

StmtList parse_program(const std::string &source)
{
    Lexer lexer(source);
    Parser parser(lexer.tokenise());
    return parser.parse();
}

std::pair<StmtList, bool> parse_program_with_status(const std::string &source)
{
    Lexer lexer(source);
    Parser parser(lexer.tokenise());
    StmtList program = parser.parse();
    return {std::move(program), parser.had_error()};
}

std::filesystem::path repo_examples_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "examples";
}

std::string read_text_file(const std::filesystem::path &path)
{
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

TEST(ParserErrors, TracksWhetherLastParseFailed)
{
    Lexer bad_lexer("fonction principal( { }\n");
    Parser bad_parser(bad_lexer.tokenise());
    const StmtList bad_program = bad_parser.parse();

    EXPECT_TRUE(bad_program.empty());
    EXPECT_TRUE(bad_parser.had_error());

    Lexer good_lexer("fonction principal() {}\n");
    Parser good_parser(good_lexer.tokenise());
    const StmtList good_program = good_parser.parse();

    EXPECT_FALSE(good_program.empty());
    EXPECT_FALSE(good_parser.had_error());
}

TEST(ParserAgirSelon, ParsesTypedLiteralAndElseBranches)
{
    const StmtList program = parse_program(
        "agir selon valeur {\n"
        "  1, 2 -> afficher(\"petit\")\n"
        "  n: Entier -> { afficher(n) }\n"
        "  sinon -> afficher(\"autre\")\n"
        "}\n");

    ASSERT_EQ(program.size(), 1u);

    AstPrinter printer;
    EXPECT_EQ(
        printer.print(*program.front()),
        "(agir-selon (ident valeur) [(branch [(literal (literal 1)) (literal (literal 2))] (expr (call (ident afficher) (args (arg _ (literal \"petit\")))))) (branch [(typed n Entier)] (block (expr (call (ident afficher) (args (arg _ (ident n)))))))] (expr (call (ident afficher) (args (arg _ (literal \"autre\"))))))");
}

TEST(ParserImports, ParsesDottedModuleIdentifiers)
{
    const StmtList program = parse_program("importer outils.maths.calcul comme calc\n");

    ASSERT_EQ(program.size(), 1u);

    AstPrinter printer;
    EXPECT_EQ(printer.print(*program.front()), "(import outils.maths.calcul comme calc)");
}

TEST(ParserImports, ParsesSelectiveImportsWithAliases)
{
    const StmtList program = parse_program("importer outils.maths.calcul.{tripler, base comme origine}\n");

    ASSERT_EQ(program.size(), 1u);

    AstPrinter printer;
    EXPECT_EQ(printer.print(*program.front()), "(import outils.maths.calcul.{tripler, base comme origine})");
}

TEST(ParserImports, ParsesNamespaceAndSelectiveImportsTogether)
{
    const StmtList program = parse_program(
        "importer outils.maths comme maths\n"
        "importer outils.maths.{tripler, base comme origine}\n");

    ASSERT_EQ(program.size(), 2u);

    AstPrinter printer;
    EXPECT_EQ(printer.print(*program[0]), "(import outils.maths comme maths)");
    EXPECT_EQ(printer.print(*program[1]), "(import outils.maths.{tripler, base comme origine})");
}

TEST(ParserVisibility, ParsesExplicitPublicTopLevelDeclarations)
{
    const StmtList program = parse_program(
        "public soit version: Entier = 1\n"
        "public fonction expose() {}\n"
        "public classe API {}\n"
        "public interface Contrat { fonction faire() }\n");

    ASSERT_EQ(program.size(), 4u);

    AstPrinter printer;
    EXPECT_EQ(printer.print(*program[0]), "(var-decl version Entier false public (literal 1))");
    EXPECT_EQ(printer.print(*program[1]), "(func-decl expose public [] _ (block))");
    EXPECT_EQ(printer.print(*program[2]), "(class-decl API public _ [] (block))");
    EXPECT_EQ(printer.print(*program[3]), "(interface-decl Contrat public (block (func-decl faire public [] _ _)))");
}

TEST(ParserFunctions, ParsesTypedParametersDefaultsAndReturnType)
{
    const StmtList program = parse_program(
        "fonction somme(a: Entier, b: Entier = 2) -> Entier {\n"
        "  retourne a + b\n"
        "}\n");

    ASSERT_EQ(program.size(), 1u);
    const auto *function = dynamic_cast<const lumiere::FunctionDeclStmt *>(program.front().get());
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(function->params.size(), 2u);
    EXPECT_EQ(function->name.lexeme, "somme");
    EXPECT_EQ(function->params[0].name, "a");
    EXPECT_EQ(function->params[0].type_token.lexeme, "Entier");
    EXPECT_EQ(function->params[0].default_value, nullptr);
    EXPECT_EQ(function->params[1].name, "b");
    EXPECT_EQ(function->params[1].type_token.lexeme, "Entier");
    ASSERT_NE(function->params[1].default_value, nullptr);
    const auto *default_literal = dynamic_cast<const lumiere::LiteralExpr *>(function->params[1].default_value.get());
    ASSERT_NE(default_literal, nullptr);
    EXPECT_EQ(default_literal->token.lexeme, "2");
    EXPECT_EQ(function->return_type.lexeme, "Entier");
    EXPECT_NE(function->body, nullptr);
}

TEST(ParserFunctions, ParsesGenericTypeAnnotationsInParametersAndReturnType)
{
    const StmtList program = parse_program(
        "fonction calculer_moyenne(nombs: Liste[Entier]) -> Décimal {\n"
        "  retourne 0\n"
        "}\n");

    ASSERT_EQ(program.size(), 1u);
    const auto *function = dynamic_cast<const lumiere::FunctionDeclStmt *>(program.front().get());
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(function->params.size(), 1u);
    EXPECT_EQ(function->params[0].name, "nombs");
    EXPECT_EQ(function->params[0].type_token.lexeme, "Liste[Entier]");
    EXPECT_EQ(function->return_type.lexeme, "Décimal");
}

TEST(ParserFunctions, ParsesFixedListTypeAnnotations)
{
    const StmtList program = parse_program(
        "fonction prendre(notes: ListeFixe[Entier, 3]) -> ListeFixe[Entier, 3] {\n"
        "  retourne notes\n"
        "}\n");

    ASSERT_EQ(program.size(), 1u);
    const auto *function = dynamic_cast<const lumiere::FunctionDeclStmt *>(program.front().get());
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(function->params.size(), 1u);
    EXPECT_EQ(function->params[0].type_token.lexeme, "ListeFixe[Entier,3]");
    EXPECT_EQ(function->return_type.lexeme, "ListeFixe[Entier,3]");
}

TEST(ParserErrors, RejectsMalformedGenericTypeAnnotation)
{
    const auto [program, had_error] = parse_program_with_status(
        "fonction calculer(nombs: Liste[Entier) -> Entier {}\n");
    EXPECT_TRUE(program.empty());
    EXPECT_TRUE(had_error);
}

TEST(ParserExpressions, ParsesCallMemberIndexAndNamedArgumentsChains)
{
    const StmtList program = parse_program(
        "objet.methode(alpha: 1, beta: liste[2])[3]\n");

    ASSERT_EQ(program.size(), 1u);

    AstPrinter printer;
    EXPECT_EQ(
        printer.print(*program.front()),
        "(expr (index (call (member (ident objet) methode) (args (arg alpha (literal 1)) (arg beta (index (ident liste) (literal 2))))) (literal 3)))");
}

TEST(ParserExpressions, ParsesAnonymousFunctionExpressions)
{
    const StmtList program = parse_program(
        "LumiTest.test(\"addition\", fonction(a: Entier, b: Entier) -> Entier { retourne a + b })\n");

    ASSERT_EQ(program.size(), 1u);

    AstPrinter printer;
    EXPECT_EQ(
        printer.print(*program.front()),
        "(expr (call (member (ident LumiTest) test) (args (arg _ (literal \"addition\")) (arg _ (func-expr [(a Entier) (b Entier)] Entier (block (return (binary + (ident a) (ident b)))))))))");
}

TEST(ParserExpressions, ParsesTypeChecks)
{
    const StmtList program = parse_program("valeur est ListeFixe[Entier, 3]\n");

    ASSERT_EQ(program.size(), 1u);

    AstPrinter printer;
    EXPECT_EQ(
        printer.print(*program.front()),
        "(expr (type-check (ident valeur) ListeFixe[Entier,3]))");
}

TEST(ParserExpressions, ParsesDictionaryAndListLiterals)
{
    const StmtList program = parse_program(
        "soit data = {\"nom\": \"Ada\", \"notes\": [1, 2, 3]}\n");

    ASSERT_EQ(program.size(), 1u);
    const auto *var_decl = dynamic_cast<const lumiere::VarDeclStmt *>(program.front().get());
    ASSERT_NE(var_decl, nullptr);
    ASSERT_NE(var_decl->initializer, nullptr);
    const auto *dict = dynamic_cast<const lumiere::DictionaryExpr *>(var_decl->initializer.get());
    ASSERT_NE(dict, nullptr);
    ASSERT_EQ(dict->entries.size(), 2u);

    const auto *first_key = dynamic_cast<const lumiere::LiteralExpr *>(dict->entries[0].key.get());
    const auto *first_value = dynamic_cast<const lumiere::LiteralExpr *>(dict->entries[0].value.get());
    ASSERT_NE(first_key, nullptr);
    ASSERT_NE(first_value, nullptr);
    EXPECT_EQ(first_key->token.lexeme, "\"nom\"");
    EXPECT_EQ(first_value->token.lexeme, "\"Ada\"");

    const auto *second_key = dynamic_cast<const lumiere::LiteralExpr *>(dict->entries[1].key.get());
    const auto *second_value = dynamic_cast<const lumiere::ListExpr *>(dict->entries[1].value.get());
    ASSERT_NE(second_key, nullptr);
    ASSERT_NE(second_value, nullptr);
    EXPECT_EQ(second_key->token.lexeme, "\"notes\"");
    ASSERT_EQ(second_value->elements.size(), 3u);
}

TEST(ParserImports, RejectsMalformedSelectiveImport)
{
    const StmtList program = parse_program("importer outils.maths.calcul.{tripler comme}\n");
    EXPECT_TRUE(program.empty());
}

TEST(ParserVisibility, RejectsPrivateTopLevelClassAndInterface)
{
    EXPECT_TRUE(parse_program("privé classe Cachee {}\n").empty());
    EXPECT_TRUE(parse_program("privé interface Cachee {}\n").empty());
}

TEST(ParserVisibility, AcceptsAccentlessAliasesForAccentedKeywords)
{
    const StmtList program = parse_program(
        "classe Rapport realise Presentable {\n"
        "  prive fonction secret() {\n"
        "    arreter\n"
        "  }\n"
        "}\n");

    ASSERT_EQ(program.size(), 1u);
    const auto *class_decl = dynamic_cast<const lumiere::ClassDeclStmt *>(program.front().get());
    ASSERT_NE(class_decl, nullptr);
    ASSERT_EQ(class_decl->interfaces.size(), 1u);
    EXPECT_EQ(class_decl->interfaces[0].lexeme, "Presentable");
}

TEST(ParserControlFlow, ParsesTryCatchFinallyAndLoops)
{
    const StmtList program = parse_program(
        "essayer { afficher(1) } attraper (e: Entier) { afficher(e) } finalement { afficher(2) }\n"
        "pour chaque x dans [1, 2] { afficher(x) }\n"
        "tant que (faux) { afficher(0) }\n");

    ASSERT_EQ(program.size(), 3u);

    AstPrinter printer;
    EXPECT_EQ(printer.print(*program[0]), "(try (block (expr (call (ident afficher) (args (arg _ (literal 1)))))) [(catch e Entier (block (expr (call (ident afficher) (args (arg _ (ident e)))))))] (block (expr (call (ident afficher) (args (arg _ (literal 2)))))))");
    EXPECT_EQ(printer.print(*program[1]), "(for x (list (literal 1) (literal 2)) (block (expr (call (ident afficher) (args (arg _ (ident x)))))))");
    EXPECT_EQ(printer.print(*program[2]), "(while (literal faux) (block (expr (call (ident afficher) (args (arg _ (literal 0)))))))");
}

TEST(ParserClasses, RejectsFieldInitializersInClassBodyForNow)
{
    EXPECT_TRUE(parse_program(
        "classe Compteur {\n"
        "  valeur: Entier = 5\n"
        "}\n").empty());
}

TEST(ParserImports, RejectsAliasAfterSelectiveImport)
{
    EXPECT_TRUE(parse_program("importer outils.maths.calcul.{tripler} comme calc\n").empty());
}

TEST(ParserImports, RejectsEmptySelectiveImportList)
{
    const auto [program, had_error] = parse_program_with_status("importer outils.maths.{}\n");
    EXPECT_TRUE(program.empty());
    EXPECT_TRUE(had_error);
}

TEST(ParserImports, RejectsMissingModuleName)
{
    const auto [program, had_error] = parse_program_with_status("importer comme alias\n");
    EXPECT_TRUE(program.empty());
    EXPECT_TRUE(had_error);
}

TEST(ParserErrors, RejectsTryWithoutCatch)
{
    EXPECT_TRUE(parse_program("essayer { afficher(1) }\n").empty());
}

TEST(ParserErrors, RejectsInterfaceMembersThatAreNotFunctions)
{
    EXPECT_TRUE(parse_program(
        "interface Contrat {\n"
        "  soit x = 1\n"
        "}\n").empty());
}

TEST(ParserErrors, RejectsMissingForEachKeyword)
{
    EXPECT_TRUE(parse_program(
        "pour i dans [1, 2] {\n"
        "  afficher(i)\n"
        "}\n").empty());
}

TEST(ParserErrors, RejectsAgirSelonElseBranchBeforePatternsComplete)
{
    EXPECT_TRUE(parse_program(
        "agir selon valeur {\n"
        "  sinon afficher(\"oops\")\n"
        "}\n").empty());
}

TEST(ParserErrors, RejectsFunctionParameterWithoutType)
{
    const auto [program, had_error] = parse_program_with_status(
        "fonction principal(x) {}\n");
    EXPECT_TRUE(program.empty());
    EXPECT_TRUE(had_error);
}

TEST(ParserErrors, RejectsNamedArgumentWithoutValue)
{
    const auto [program, had_error] = parse_program_with_status(
        "afficher(message:)\n");
    EXPECT_TRUE(program.empty());
    EXPECT_TRUE(had_error);
}

TEST(ParserErrors, RejectsMissingArrowInAgirSelonBranch)
{
    const auto [program, had_error] = parse_program_with_status(
        "agir selon valeur {\n"
        "  1 afficher(\"un\")\n"
        "}\n");
    EXPECT_TRUE(program.empty());
    EXPECT_TRUE(had_error);
}

TEST(ParserErrors, RejectsClassParentWithoutIdentifier)
{
    const auto [program, had_error] = parse_program_with_status(
        "classe Enfant : {\n"
        "}\n");
    EXPECT_TRUE(program.empty());
    EXPECT_TRUE(had_error);
}

TEST(ParserErrors, RejectsDictionaryLiteralMissingColon)
{
    const auto [program, had_error] = parse_program_with_status(
        "soit x = {\"a\" 1}\n");
    EXPECT_TRUE(program.empty());
    EXPECT_TRUE(had_error);
}

TEST(ParserExamples, ParsesAllRepositoryExamples)
{
    const std::filesystem::path examples_dir = repo_examples_dir();
    ASSERT_TRUE(std::filesystem::exists(examples_dir));

    for (const auto &entry : std::filesystem::recursive_directory_iterator(examples_dir))
    {
        if (!entry.is_regular_file() || !is_source_file(entry.path()))
        {
            continue;
        }

        Lexer lexer(read_text_file(entry.path()));
        Parser parser(lexer.tokenise());
        const StmtList program = parser.parse();

        EXPECT_FALSE(program.empty()) << entry.path().string();
        EXPECT_FALSE(parser.had_error()) << entry.path().string();
    }
}

} // namespace
