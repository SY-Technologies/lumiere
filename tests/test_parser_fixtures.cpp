#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"
#include "lumiere/parser/printer.hpp"

namespace
{

using lumiere::AstPrinter;
using lumiere::Lexer;
using lumiere::Parser;
using lumiere::StmtList;

std::string read_text_file(const std::filesystem::path &path)
{
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string trim_trailing_whitespace(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
    {
        text.pop_back();
    }
    return text;
}

bool file_exists(const std::filesystem::path &path)
{
    return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

std::string render_program(const StmtList &program)
{
    AstPrinter printer;
    std::ostringstream out;
    for (std::size_t i = 0; i < program.size(); ++i)
    {
        if (i > 0)
        {
            out << '\n';
        }
        out << printer.print(*program[i]);
    }
    return out.str();
}

TEST(ParserFixtures, MatchesFixtureExpectations)
{
#ifndef LUMIERE_PARSER_FIXTURE_DIR
    GTEST_SKIP() << "parser fixture directory not configured";
#else
    const std::filesystem::path fixtures_dir(LUMIERE_PARSER_FIXTURE_DIR);
    ASSERT_TRUE(std::filesystem::exists(fixtures_dir));

    bool saw_fixture = false;
    for (const auto &entry : std::filesystem::directory_iterator(fixtures_dir))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        saw_fixture = true;
        const std::filesystem::path case_dir = entry.path();
        const std::filesystem::path source_path = case_dir / "main.lum";
        ASSERT_TRUE(std::filesystem::exists(source_path)) << case_dir.string();

        const std::string source = read_text_file(source_path);
        Lexer lexer(source);
        Parser parser(lexer.tokenise());
        const StmtList program = parser.parse();

        const std::filesystem::path ast_path = case_dir / "expected.ast";
        const std::filesystem::path error_path = case_dir / "expected.error";
        const std::filesystem::path ast_contains_path = case_dir / "expected.ast.contains";

        ASSERT_TRUE(std::filesystem::exists(ast_path) || std::filesystem::exists(error_path) || std::filesystem::exists(ast_contains_path))
            << "fixture must contain expected.ast, expected.ast.contains or expected.error: " << case_dir.string();

        if (std::filesystem::exists(ast_path))
        {
            EXPECT_FALSE(parser.had_error()) << case_dir.string();
            EXPECT_EQ(trim_trailing_whitespace(render_program(program)),
                      trim_trailing_whitespace(read_text_file(ast_path)))
                << case_dir.string();
        }
        else if (file_exists(ast_contains_path))
        {
            EXPECT_FALSE(parser.had_error()) << case_dir.string();
            const std::string expected_fragment = trim_trailing_whitespace(read_text_file(ast_contains_path));
            EXPECT_NE(render_program(program).find(expected_fragment), std::string::npos) << case_dir.string();
        }

        if (std::filesystem::exists(error_path))
        {
            EXPECT_TRUE(parser.had_error()) << case_dir.string();
            EXPECT_TRUE(program.empty()) << case_dir.string();
        }
    }

    EXPECT_TRUE(saw_fixture);
#endif
}

} // namespace
