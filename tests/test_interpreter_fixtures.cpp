#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef interface
#undef interface
#endif
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"

namespace
{

std::mutex g_stdio_capture_mutex;

using lumiere::Lexer;
using lumiere::Parser;
using lumiere::Program;
using lumiere::RuntimeError;
using lumiere::TreeWalker;

#if !LUMIERE_ENABLE_LUMINET
#define SKIP_IF_LUMINET_DISABLED() GTEST_SKIP() << "LumiNet is disabled on this platform"
#else
#define SKIP_IF_LUMINET_DISABLED() do {} while (false)
#endif

#ifdef _WIN32
using TestSocket = SOCKET;
constexpr TestSocket kInvalidTestSocket = INVALID_SOCKET;
using TestRecvSize = int;

void initialize_test_socket_platform()
{
    static std::once_flag winsock_once;
    std::call_once(winsock_once, []() {
        WSADATA wsa_data{};
        ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
    });
}
#else
using TestSocket = int;
constexpr TestSocket kInvalidTestSocket = -1;
using TestRecvSize = ssize_t;

void initialize_test_socket_platform()
{
}
#endif

TestSocket test_open_socket(int family, int type, int protocol)
{
    initialize_test_socket_platform();
    return ::socket(family, type, protocol);
}

bool test_socket_valid(TestSocket socket)
{
#ifdef _WIN32
    return socket != INVALID_SOCKET;
#else
    return socket >= 0;
#endif
}

void test_close_socket(TestSocket socket)
{
#ifdef _WIN32
    if (socket != INVALID_SOCKET)
    {
        ::closesocket(socket);
    }
#else
    if (socket >= 0)
    {
        ::close(socket);
    }
#endif
}

int test_set_reuseaddr(TestSocket socket)
{
    int reuse = 1;
#ifdef _WIN32
    return ::setsockopt(socket,
                        SOL_SOCKET,
                        SO_REUSEADDR,
                        reinterpret_cast<const char *>(&reuse),
                        static_cast<int>(sizeof(reuse)));
#else
    return ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
}

TestRecvSize test_recv(TestSocket socket, void *buffer, std::size_t size, int flags = 0)
{
#ifdef _WIN32
    return ::recv(socket, static_cast<char *>(buffer), static_cast<int>(size), flags);
#else
    return ::recv(socket, buffer, size, flags);
#endif
}

int test_send(TestSocket socket, const void *buffer, std::size_t size, int flags = 0)
{
#ifdef _WIN32
    return ::send(socket, static_cast<const char *>(buffer), static_cast<int>(size), flags);
#else
    return ::send(socket, buffer, size, flags);
#endif
}

bool test_wait_until_readable(TestSocket socket, std::chrono::milliseconds timeout)
{
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket, &read_set);
    timeval duration{};
    duration.tv_sec = static_cast<long>(timeout.count() / 1000);
    duration.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
#ifdef _WIN32
    return ::select(0, &read_set, nullptr, nullptr, &duration) == 1;
#else
    return ::select(socket + 1, &read_set, nullptr, nullptr, &duration) == 1;
#endif
}

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

std::string normalize_fixture_paths(std::string text)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    const std::string marker = "/tests/fixtures/interpreter/";
    std::size_t marker_pos = text.find(marker);
    while (marker_pos != std::string::npos)
    {
        std::size_t start = marker_pos;
        while (start > 0)
        {
            const char ch = text[start - 1];
            if (ch == '"' || ch == '(' || ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t')
            {
                break;
            }
            --start;
        }

        text.erase(start, marker_pos - start);
        marker_pos = text.find(marker, start + marker.size());
    }
    return text;
}

std::string normalize_path_text(const std::filesystem::path &path)
{
    return path.generic_string();
}

std::pair<std::string, bool> execute_program(const std::string &source)
{
    std::lock_guard<std::mutex> lock(g_stdio_capture_mutex);
    Lexer lexer(source);
    Parser parser(lexer.tokenise());

    Program program;
    program.statements = parser.parse();
    program.source_path = "<test>";
    program.source_text = source;

    if (parser.had_error())
    {
        throw std::runtime_error("unexpected parse failure in execute_program");
    }

    TreeWalker walker;

    std::ostringstream captured;
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    bool completed = true;

    try
    {
        walker.execute(program);
    }
    catch (...)
    {
        completed = false;
    }

    std::cout.rdbuf(previous);
    return {captured.str(), completed};
}

std::pair<std::string, bool> execute_program_with_input(const std::string &source,
                                                        const std::string &input)
{
    std::lock_guard<std::mutex> lock(g_stdio_capture_mutex);
    Lexer lexer(source);
    Parser parser(lexer.tokenise());

    Program program;
    program.statements = parser.parse();
    program.source_path = "<test>";
    program.source_text = source;

    if (parser.had_error())
    {
        throw std::runtime_error("unexpected parse failure in execute_program_with_input");
    }

    TreeWalker walker;

    std::ostringstream captured;
    std::istringstream provided_input(input);
    auto *previous_output = std::cout.rdbuf(captured.rdbuf());
    auto *previous_input = std::cin.rdbuf(provided_input.rdbuf());
    bool completed = true;

    try
    {
        walker.execute(program);
    }
    catch (...)
    {
        completed = false;
    }

    std::cin.rdbuf(previous_input);
    std::cout.rdbuf(previous_output);
    return {captured.str(), completed};
}

std::tuple<std::string, bool, std::string> execute_program_with_error(const std::string &source)
{
    std::lock_guard<std::mutex> lock(g_stdio_capture_mutex);
    Lexer lexer(source);
    Parser parser(lexer.tokenise());

    Program program;
    program.statements = parser.parse();
    program.source_path = "<test>";
    program.source_text = source;

    if (parser.had_error())
    {
        throw std::runtime_error("unexpected parse failure in execute_program_with_error");
    }

    TreeWalker walker;

    std::ostringstream captured;
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    bool completed = true;
    std::string error_message;

    try
    {
        walker.execute(program);
    }
    catch (const RuntimeError &err)
    {
        completed = false;
        error_message = err.what();
    }
    catch (...)
    {
        completed = false;
        error_message = "unknown error";
    }

    std::cout.rdbuf(previous);
    return {captured.str(), completed, error_message};
}

std::pair<bool, std::string> execute_program_without_capture_with_error(const std::string &source)
{
    Lexer lexer(source);
    Parser parser(lexer.tokenise());

    Program program;
    program.statements = parser.parse();
    program.source_path = "<test>";
    program.source_text = source;

    if (parser.had_error())
    {
        throw std::runtime_error("unexpected parse failure in execute_program_without_capture_with_error");
    }

    TreeWalker walker;

    try
    {
        walker.execute(program);
        return {true, ""};
    }
    catch (const RuntimeError &err)
    {
        return {false, err.what()};
    }
    catch (...)
    {
        return {false, "unknown error"};
    }
}

std::tuple<std::string, bool, std::string> execute_program_with_error_and_import_path(
    const std::string &source,
    const std::filesystem::path &import_path)
{
    std::lock_guard<std::mutex> lock(g_stdio_capture_mutex);
    Lexer lexer(source);
    Parser parser(lexer.tokenise());

    Program program;
    program.statements = parser.parse();
    program.source_path = (import_path / "main.lum").string();
    program.source_text = source;

    if (parser.had_error())
    {
        throw std::runtime_error("unexpected parse failure in execute_program_with_error_and_import_path");
    }

    TreeWalker walker;
    walker.add_import_path(import_path);

    std::ostringstream captured;
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    bool completed = true;
    std::string error_message;

    try
    {
        walker.execute(program);
    }
    catch (const RuntimeError &err)
    {
        completed = false;
        error_message = err.what();
    }
    catch (...)
    {
        completed = false;
        error_message = "unknown error";
    }

    std::cout.rdbuf(previous);
    return {captured.str(), completed, error_message};
}

std::pair<std::string, bool> execute_program_with_import_path(const std::string &source,
                                                              const std::filesystem::path &import_path)
{
    std::lock_guard<std::mutex> lock(g_stdio_capture_mutex);
    Lexer lexer(source);
    Parser parser(lexer.tokenise());

    Program program;
    program.statements = parser.parse();
    program.source_path = (import_path / "main.lum").string();
    program.source_text = source;

    if (parser.had_error())
    {
        throw std::runtime_error("unexpected parse failure in execute_program_with_import_path");
    }

    TreeWalker walker;
    walker.add_import_path(import_path);

    std::ostringstream captured;
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    bool completed = true;

    try
    {
        walker.execute(program);
    }
    catch (...)
    {
        completed = false;
    }

    std::cout.rdbuf(previous);
    return {captured.str(), completed};
}

void write_module(const std::filesystem::path &path, const std::string &source)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream module_file(path);
    module_file << source;
}

TEST(InterpreterFixtures, MatchesFixtureExpectations)
{
#ifndef LUMIERE_INTERPRETER_FIXTURE_DIR
    GTEST_SKIP() << "interpreter fixture directory not configured";
#else
    const std::filesystem::path fixtures_dir(LUMIERE_INTERPRETER_FIXTURE_DIR);
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

        Lexer lexer(read_text_file(source_path));
        Parser parser(lexer.tokenise());

        Program program;
        program.statements = parser.parse();
        program.source_path = source_path.string();
        program.source_text = read_text_file(source_path);

        ASSERT_FALSE(parser.had_error()) << case_dir.string();

        TreeWalker walker;
        walker.add_import_path(case_dir);

        std::ostringstream captured;
        std::istringstream provided_input(file_exists(case_dir / "stdin.txt")
                                              ? read_text_file(case_dir / "stdin.txt")
                                              : std::string{});
        auto *previous = std::cout.rdbuf(captured.rdbuf());
        auto *previous_input = std::cin.rdbuf(provided_input.rdbuf());
        bool completed = true;
        std::string error_message;

        try
        {
            walker.execute(program);
        }
        catch (const RuntimeError &err)
        {
            completed = false;
            error_message = err.what();
        }
        catch (...)
        {
            completed = false;
            error_message = "unknown error";
        }

        std::cout.rdbuf(previous);
        std::cin.rdbuf(previous_input);

        const std::filesystem::path stdout_path = case_dir / "expected.stdout";
        const std::filesystem::path stderr_path = case_dir / "expected.stderr";
        const std::filesystem::path stderr_contains_path = case_dir / "expected.stderr.contains";
        const std::filesystem::path stdout_contains_path = case_dir / "expected.stdout.contains";

        if (std::filesystem::exists(stdout_path))
        {
            EXPECT_EQ(trim_trailing_whitespace(captured.str()),
                      trim_trailing_whitespace(read_text_file(stdout_path)))
                << case_dir.string();
        }
        else if (std::filesystem::exists(stdout_contains_path))
        {
            const std::string expected_fragment = trim_trailing_whitespace(read_text_file(stdout_contains_path));
            EXPECT_NE(captured.str().find(expected_fragment), std::string::npos) << case_dir.string();
        }
        else
        {
            EXPECT_TRUE(trim_trailing_whitespace(captured.str()).empty()) << case_dir.string();
        }

        if (std::filesystem::exists(stderr_path))
        {
            EXPECT_FALSE(completed) << case_dir.string();
            EXPECT_EQ(trim_trailing_whitespace(normalize_fixture_paths(error_message)),
                      trim_trailing_whitespace(normalize_fixture_paths(read_text_file(stderr_path))))
                << case_dir.string();
        }
        else if (std::filesystem::exists(stderr_contains_path))
        {
            EXPECT_FALSE(completed) << case_dir.string();
            const std::string expected_fragment = trim_trailing_whitespace(read_text_file(stderr_contains_path));
            EXPECT_NE(error_message.find(expected_fragment), std::string::npos) << case_dir.string();
        }
        else
        {
            EXPECT_TRUE(completed) << case_dir.string() << "\n" << error_message;
        }
    }

    EXPECT_TRUE(saw_fixture);
#endif
}

TEST(InterpreterRuntimeCall, ExecutesUserPrincipalThroughRuntimeCall)
{
    const auto [output, completed] = execute_program(
        R"(fonction saluer(nom: Texte) -> Texte {
    retourne "bonjour " + nom
}

fonction principal() {
    afficher(saluer("runtime"))
}
)");

    EXPECT_TRUE(completed);
    EXPECT_EQ(trim_trailing_whitespace(output), "bonjour runtime");
}

TEST(InterpreterAgirSelon, ExecutesFirstMatchingLiteralBranch)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit valeur = 2\n"
        "  agir selon valeur {\n"
        "    1 -> afficher(\"un\")\n"
        "    2, 3 -> afficher(\"petit\")\n"
        "    sinon -> afficher(\"autre\")\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "petit\n");
}

TEST(InterpreterAgirSelon, BindsTypedPatternWithinMatchingBranch)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit valeur = 7\n"
        "  agir selon valeur {\n"
        "    n: Entier -> afficher(n)\n"
        "    sinon -> afficher(\"autre\")\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "7\n");
}

TEST(InterpreterAgirSelon, MatchesRienPattern)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit valeur = rien\n"
        "  agir selon valeur {\n"
        "    n: Entier -> afficher(n)\n"
        "    rien -> afficher(\"vide\")\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "vide\n");
}

TEST(InterpreterAgirSelon, RejectsWhenNoBranchMatchesAndNoSinonIsPresent)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  agir selon 9 {\n"
        "    1 -> afficher(\"un\")\n"
        "  }\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("aucune branche de 'agir selon' ne correspond"), std::string::npos);
}

TEST(InterpreterAgirSelon, TypedBindingDoesNotLeakOutsideMatchedBranch)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  agir selon 4 {\n"
        "    n: Entier -> afficher(n)\n"
        "  }\n"
        "  afficher(n)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_EQ(output, "4\n");
    EXPECT_NE(error.find("introuvable"), std::string::npos);
}

TEST(InterpreterExpressions, SupportsArithmeticComparisonAndLogic)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  afficher(6 * 7)\n"
        "  afficher(8 / 2)\n"
        "  afficher(7 % 3)\n"
        "  afficher(3 < 4)\n"
        "  afficher(4 >= 4)\n"
        "  afficher(vrai et faux)\n"
        "  afficher(vrai ou faux)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "42\n4\n1\nvrai\nvrai\nfaux\nvrai\n");
}

TEST(InterpreterExpressions, SupportsAssignmentAndTextConcatenation)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit nom = \"Ada\"\n"
        "  nom = nom + \" Lovelace\"\n"
        "  afficher(\"Nom: \" + nom)\n"
        "  afficher(\"Age: \" + 36)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Nom: Ada Lovelace\nAge: 36\n");
}

TEST(InterpreterExpressions, ShortCircuitsLogicalAndAndOr)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  afficher(faux et (1 / 0 == 0))\n"
        "  afficher(vrai ou inconnu)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "faux\nvrai\n");
}

TEST(InterpreterExpressions, RejectsAssignmentToUndeclaredVariable)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  inconnu = 1\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("introuvable"), std::string::npos);
}

TEST(InterpreterExpressions, RejectsAssignmentToFixeBinding)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit fixe x = 1\n"
        "  x = 2\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("est fixe"), std::string::npos);
}

TEST(InterpreterExpressions, RejectsRedeclarationInSameScope)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit x = 1\n"
        "  soit x = 2\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("deja declare"), std::string::npos);
}

TEST(InterpreterExpressions, AllowsShadowingInInnerScope)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit x = 1\n"
        "  {\n"
        "    soit x = 2\n"
        "    afficher(x)\n"
        "  }\n"
        "  afficher(x)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "2\n1\n");
}

TEST(InterpreterExpressionsMatrix, RejectsInvalidBinaryOperatorCombinations)
{
    struct Case
    {
        std::string expression;
        std::string expected_fragment;
    };

    const std::vector<Case> cases = {
        {"\"a\" - \"b\"", "attend"},
        {"vrai + faux", "attend"},
        {"\"a\" < \"b\"", "attend"},
        {"1 et 2", "operande gauche de type Logique"},
        {"1 ou 2", "operande gauche de type Logique"},
        {"1 / 0", "division par"},
        {"1 % 0", "modulo par"},
    };

    for (const auto &test_case : cases)
    {
        const auto [output, completed, error] = execute_program_with_error(
            "fonction principal() {\n"
            "  afficher(" + test_case.expression + ")\n"
            "}\n");

        EXPECT_FALSE(completed) << test_case.expression;
        EXPECT_TRUE(output.empty()) << test_case.expression;
        EXPECT_NE(error.find(test_case.expected_fragment), std::string::npos) << test_case.expression;
    }
}

TEST(InterpreterCollections, SupportsListLiteralAndIndexAccess)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit nombres = [10, 20, 30]\n"
        "  afficher(nombres[1])\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "20\n");
}

TEST(InterpreterCollections, SupportsDictionaryLiteralAndIndexAccess)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit ages = {\"Ada\": 36, \"Grace\": 47}\n"
        "  afficher(ages[\"Grace\"])\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "47\n");
}

TEST(InterpreterCollections, SupportsTextIndexingAndTextIteration)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  afficher(\"Salut\"[1])\n"
        "  pour chaque ch dans \"OK\" {\n"
        "    afficher(ch)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "a\nO\nK\n");
}

TEST(InterpreterCollections, SupportsUnicodeTextIndexingAndTextIteration)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  afficher(\"été\"[0])\n"
        "  afficher(\"été\"[2])\n"
        "  pour chaque ch dans \"éà\" {\n"
        "    afficher(ch)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "é\né\né\nà\n");
}

TEST(InterpreterCollections, RejectsListIndexOutOfBounds)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit nombres = [10, 20]\n"
        "  afficher(nombres[2])\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("indice hors limites"), std::string::npos);
}

TEST(InterpreterCollections, RejectsDictionaryLookupForMissingKey)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit ages = {\"Ada\": 36}\n"
        "  afficher(ages[\"Grace\"])\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("cle introuvable"), std::string::npos);
}

TEST(InterpreterCollections, RejectsNonIterablePourTarget)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  pour chaque n dans 42 {\n"
        "    afficher(n)\n"
        "  }\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("n'est pas iterable"), std::string::npos);
}

TEST(InterpreterCollections, RejectsNegativeTextIndex)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\"[-1])\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("indice hors limites"), std::string::npos);
}

TEST(InterpreterCollections, RejectsIndexedAssignmentOnText)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit mot = \"abc\"\n"
        "  mot[0] = 'z'\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("affectation par indice impossible"), std::string::npos);
}

TEST(InterpreterCollections, SupportsDictionaryAssignmentOfNewKey)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit d = {\"a\": 1}\n"
        "  d[\"b\"] = 2\n"
        "  afficher(d[\"b\"])\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "2\n");
}

TEST(InterpreterControlFlow, SupportsPourOnLists)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  pour chaque n dans [1, 2, 3] {\n"
        "    afficher(n)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "1\n2\n3\n");
}

TEST(InterpreterControlFlow, SupportsBreakAndContinueInLoops)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  pour chaque n dans [1, 2, 3, 4] {\n"
        "    si (n == 2) { continuer }\n"
        "    afficher(n)\n"
        "    si (n == 3) { arrêter }\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "1\n3\n");
}

TEST(InterpreterControlFlow, SupportsWhileLoopMutation)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit i = 0\n"
        "  tant que (i < 3) {\n"
        "    afficher(i)\n"
        "    i = i + 1\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "0\n1\n2\n");
}

TEST(InterpreterControlFlow, PropagatesReturnOutOfLoop)
{
    const auto [output, completed] = execute_program(
        "fonction trouver() {\n"
        "  pour chaque n dans [1, 2, 3] {\n"
        "    si (n == 2) { retourne n }\n"
        "  }\n"
        "  retourne 0\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(trouver())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "2\n");
}

TEST(InterpreterControlFlow, ContinueInsideTryStillRunsFinally)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  pour chaque n dans [1, 2] {\n"
        "    essayer {\n"
        "      afficher(n)\n"
        "      continuer\n"
        "    } attraper (e: Entier) {\n"
        "      afficher(e)\n"
        "    } finalement {\n"
        "      afficher(\"fin\")\n"
        "    }\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "1\nfin\n2\nfin\n");
}

TEST(InterpreterCasts, SupportsPrimitiveCasts)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  afficher(42 en Décimal)\n"
        "  afficher(\"123\" en Entier)\n"
        "  afficher(\"vrai\" en Logique)\n"
        "  afficher('A' en Entier)\n"
        "  afficher(66 en Symbole)\n"
        "  afficher(9 en Texte)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "42\n123\nvrai\n65\nB\n9\n");
}

TEST(InterpreterCasts, SupportsUnicodeSymbolLiteralsAndTextToSymbolCast)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  afficher('é')\n"
        "  afficher('é' en Entier)\n"
        "  afficher(\"ç\" en Symbole)\n"
        "  afficher('à')\n"
        "  afficher('œ')\n"
        "  afficher(\"ù\" en Symbole)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "é\n233\nç\nà\nœ\nù\n");
}

TEST(InterpreterCasts, RejectsInvalidPrimitiveCasts)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\" en Entier)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("conversion"), std::string::npos);
}

TEST(InterpreterCasts, RejectsInvalidLogicAndSymbolCasts)
{
    auto [output1, completed1, error1] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"peut-etre\" en Logique)\n"
        "}\n");

    EXPECT_FALSE(completed1);
    EXPECT_TRUE(output1.empty());
    EXPECT_NE(error1.find("conversion"), std::string::npos);

    auto [output2, completed2, error2] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(1114112 en Symbole)\n"
        "}\n");

    EXPECT_FALSE(completed2);
    EXPECT_TRUE(output2.empty());
    EXPECT_NE(error2.find("conversion"), std::string::npos);
}

TEST(InterpreterIntrospection, SupportsTypeChecksAndTypeQueries)
{
    const auto [output, completed] = execute_program(
        "interface Presentable {\n"
        "  fonction presenter()\n"
        "}\n"
        "classe Animal {\n"
        "  nom: Texte\n"
        "}\n"
        "classe Chien : Animal réalise Presentable {\n"
        "  fonction presenter() { retourne ici.nom }\n"
        "}\n"
        "fonction principal() {\n"
        "  soit chien = Chien(nom: \"Rex\")\n"
        "  soit notes: Liste[Entier] = [1, 2, 3]\n"
        "  soit fixe trio = notes.en_liste_fixe(3)\n"
        "  afficher(chien est Chien)\n"
        "  afficher(chien est Animal)\n"
        "  afficher(chien est Presentable)\n"
        "  afficher(trio est ListeFixe[Entier, 3])\n"
        "  afficher(type_de(chien))\n"
        "  afficher(type_de(trio))\n"
        "  afficher(type_de(notes))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "vrai\nvrai\nvrai\nvrai\nChien\nListeFixe[Entier, 3]\nListe[Entier]\n");
}

TEST(InterpreterIntrospection, RejectsFalseTypeChecks)
{
    const auto [output, completed] = execute_program(
        "classe Animal {}\n"
        "classe Chien : Animal {}\n"
        "fonction principal() {\n"
        "  soit chien = Chien()\n"
        "  afficher(chien est Texte)\n"
        "  afficher([1, 2] est Dictionnaire[Texte, Entier])\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "faux\nfaux\n");
}

TEST(InterpreterObjects, SupportsClassConstructionFieldAccessAndMethodCalls)
{
    const auto [output, completed] = execute_program(
        "classe Personne {\n"
        "  nom: Texte\n"
        "  age: Entier\n"
        "\n"
        "  fonction saluer() {\n"
        "    afficher(\"Bonjour \" + ici.nom)\n"
        "  }\n"
        "}\n"
        "\n"
        "fonction principal() {\n"
        "  soit p = Personne(nom: \"Ada\", age: 36)\n"
        "  afficher(p.nom)\n"
        "  afficher(p.age)\n"
        "  p.saluer()\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Ada\n36\nBonjour Ada\n");
}

TEST(InterpreterObjects, SupportsInheritedFieldsAndMethods)
{
    const auto [output, completed] = execute_program(
        "classe Animal {\n"
        "  nom: Texte\n"
        "  fonction decrire() {\n"
        "    afficher(\"Animal: \" + ici.nom)\n"
        "  }\n"
        "}\n"
        "classe Chien : Animal {\n"
        "  race: Texte\n"
        "}\n"
        "fonction principal() {\n"
        "  soit c = Chien(nom: \"Rex\", race: \"Berger\")\n"
        "  afficher(c.nom)\n"
        "  afficher(c.race)\n"
        "  c.decrire()\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Rex\nBerger\nAnimal: Rex\n");
}

TEST(InterpreterObjects, RejectsOverrideWithMismatchedSignature)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Animal {\n"
        "  fonction parler(mot: Texte) -> Texte {\n"
        "    retourne mot\n"
        "  }\n"
        "}\n"
        "classe Chien : Animal {\n"
        "  remplace fonction parler(mot: Entier) -> Texte {\n"
        "    retourne \"aboiement\"\n"
        "  }\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("meme signature"), std::string::npos);
}

TEST(InterpreterObjects, SupportsMemberAndIndexedAssignment)
{
    const auto [output, completed] = execute_program(
        "classe Boite {\n"
        "  valeur: Entier\n"
        "}\n"
        "fonction principal() {\n"
        "  soit b = Boite(valeur: 1)\n"
        "  b.valeur = 9\n"
        "  afficher(b.valeur)\n"
        "  soit xs = [1, 2, 3]\n"
        "  xs[1] = 42\n"
        "  afficher(xs[1])\n"
        "  soit d = {\"a\": 1}\n"
        "  d[\"a\"] = 7\n"
        "  afficher(d[\"a\"])\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "9\n42\n7\n");
}

TEST(InterpreterObjects, RejectsPrivateFieldAccessOutsideIci)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Coffre {\n"
        "  privé secret: Entier\n"
        "}\n"
        "fonction principal() {\n"
        "  soit c = Coffre(secret: 123)\n"
        "  afficher(c.secret)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("champ prive"), std::string::npos);
}

TEST(InterpreterObjects, AcceptsAccentlessAliasesForAccentedKeywords)
{
    const auto [output, completed] = execute_program(
        "interface Presentable {\n"
        "  fonction presenter() -> Texte\n"
        "}\n"
        "classe Coffre realise Presentable {\n"
        "  prive secret: Texte\n"
        "  fonction presenter() -> Texte {\n"
        "    retourne ici.secret\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  soit coffre = Coffre(secret: \"ok\")\n"
        "  afficher(coffre.presenter())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "ok\n");
}

TEST(InterpreterObjects, AllowsPrivateFieldAccessThroughIci)
{
    const auto [output, completed] = execute_program(
        "classe Coffre {\n"
        "  privé secret: Entier\n"
        "  fonction reveler() {\n"
        "    afficher(ici.secret)\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  soit c = Coffre(secret: 123)\n"
        "  c.reveler()\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "123\n");
}

TEST(InterpreterObjects, RejectsPrivateMethodAccessOutsideIci)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Coffre {\n"
        "  privé fonction secret() {\n"
        "    afficher(1)\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  soit c = Coffre()\n"
        "  c.secret()\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("methode privee"), std::string::npos);
}

TEST(InterpreterObjects, RejectsBareParentUsage)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(parent)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("parent"), std::string::npos);
}

TEST(InterpreterObjects, RejectsUnknownConstructorField)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Boite {\n"
        "  valeur: Entier\n"
        "}\n"
        "fonction principal() {\n"
        "  soit b = Boite(inconnu: 1)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("champ inconnu"), std::string::npos);
}

TEST(InterpreterObjects, RejectsDuplicateConstructorFieldInitialization)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Boite {\n"
        "  valeur: Entier\n"
        "}\n"
        "fonction principal() {\n"
        "  soit b = Boite(valeur: 1, valeur: 2)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("plusieurs fois"), std::string::npos);
}

TEST(InterpreterObjects, RejectsMemberAccessOnNonObject)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit n = 1\n"
        "  afficher(n.valeur)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("acces membre impossible"), std::string::npos);
}

TEST(InterpreterObjects, RejectsAssignmentToPrivateFieldOutsideIci)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Coffre {\n"
        "  privé secret: Entier\n"
        "}\n"
        "fonction principal() {\n"
        "  soit c = Coffre(secret: 1)\n"
        "  c.secret = 2\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("champ prive"), std::string::npos);
}

TEST(InterpreterObjects, EnforcesTypedConstructorFieldsAndAssignments)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Boite {\n"
        "  valeur: Entier\n"
        "}\n"
        "fonction principal() {\n"
        "  soit b = Boite(valeur: 3)\n"
        "  b.valeur = \"oops\"\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("champ 'valeur'"), std::string::npos);
    EXPECT_NE(error.find("Entier"), std::string::npos);
}

TEST(InterpreterObjects, AcceptsSubclassAndInterfaceTypedValues)
{
    const auto [output, completed] = execute_program(
        "interface Presentable {\n"
        "  fonction presenter()\n"
        "}\n"
        "classe Animal {\n"
        "  nom: Texte\n"
        "}\n"
        "classe Chien : Animal réalise Presentable {\n"
        "  fonction presenter() {\n"
        "    retourne ici.nom\n"
        "  }\n"
        "}\n"
        "fonction montrer(animal: Animal, presentable: Presentable) {\n"
        "  afficher(animal.nom)\n"
        "  afficher(presentable.presenter())\n"
        "}\n"
        "fonction principal() {\n"
        "  soit chien = Chien(nom: \"Rex\")\n"
        "  montrer(chien, chien)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Rex\nRex\n");
}

TEST(InterpreterObjects, RejectsInterfaceImplementationWithMismatchedSignature)
{
    const auto [output, completed, error] = execute_program_with_error(
        "interface Presentable {\n"
        "  fonction presenter() -> Texte\n"
        "}\n"
        "classe Chien réalise Presentable {\n"
        "  fonction presenter(extra: Texte) -> Texte {\n"
        "    retourne extra\n"
        "  }\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("signature requise"), std::string::npos);
}

TEST(InterpreterObjects, RejectsPrivateInterfaceImplementationMethod)
{
    const auto [output, completed, error] = execute_program_with_error(
        "interface Presentable {\n"
        "  fonction presenter() -> Texte\n"
        "}\n"
        "classe Chien réalise Presentable {\n"
        "  privé fonction presenter() -> Texte {\n"
        "    retourne \"Rex\"\n"
        "  }\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("ne peut pas etre privee"), std::string::npos);
}

TEST(InterpreterObjects, RejectsMethodCallOnNonCallableMember)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Boite {\n"
        "  valeur: Entier\n"
        "}\n"
        "fonction principal() {\n"
        "  soit b = Boite(valeur: 3)\n"
        "  b.valeur()\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("n'est pas une fonction"), std::string::npos);
}

TEST(InterpreterObjects, SupportsParentMethodCalls)
{
    const auto [output, completed] = execute_program(
        "classe Animal {\n"
        "  nom: Texte\n"
        "  fonction decrire() {\n"
        "    afficher(\"Animal: \" + ici.nom)\n"
        "  }\n"
        "}\n"
        "classe Chien : Animal {\n"
        "  remplace fonction decrire() {\n"
        "    parent.decrire()\n"
        "    afficher(\"Chien: \" + ici.nom)\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  soit c = Chien(nom: \"Rex\")\n"
        "  c.decrire()\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Animal: Rex\nChien: Rex\n");
}

TEST(InterpreterObjects, RejectsOverrideWithoutRemplace)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Animal {\n"
        "  fonction parler() {\n"
        "    afficher(\"animal\")\n"
        "  }\n"
        "}\n"
        "classe Chien : Animal {\n"
        "  fonction parler() {\n"
        "    afficher(\"chien\")\n"
        "  }\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("utilisez remplace"), std::string::npos);
}

TEST(InterpreterObjects, RejectsRemplaceWithoutParentMethod)
{
    const auto [output, completed, error] = execute_program_with_error(
        "classe Animal {\n"
        "}\n"
        "classe Chien : Animal {\n"
        "  remplace fonction parler() {\n"
        "    afficher(\"chien\")\n"
        "  }\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("remplace utilise sans methode parente"), std::string::npos);
}

TEST(InterpreterErrors, SupportsTypedThrowCatchAndFinally)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  essayer {\n"
        "    lancer 42\n"
        "  } attraper (e: Entier) {\n"
        "    afficher(e)\n"
        "  } finalement {\n"
        "    afficher(\"fin\")\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "42\nfin\n");
}

TEST(InterpreterErrors, RunsFinallyWhenThrownValueIsUnhandled)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  essayer {\n"
        "    lancer 42\n"
        "  } attraper (e: Texte) {\n"
        "    afficher(e)\n"
        "  } finalement {\n"
        "    afficher(\"fin\")\n"
        "  }\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_EQ(output, "fin\n");
    EXPECT_NE(error.find("Traceback (most recent call last):"), std::string::npos);
    EXPECT_NE(error.find("in principal"), std::string::npos);
    EXPECT_NE(error.find("exception non attrapee: 42"), std::string::npos);
}

TEST(InterpreterErrors, CatchesRuntimeErrorAsTexte)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  essayer {\n"
        "    soit xs = [1]\n"
        "    afficher(xs[3])\n"
        "  } attraper (e: Texte) {\n"
        "    afficher(e)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_NE(output.find("indice hors limites"), std::string::npos);
}

TEST(InterpreterErrors, RunsFinallyAfterHandledCatch)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  essayer {\n"
        "    lancer 7\n"
        "  } attraper (e: Entier) {\n"
        "    afficher(e)\n"
        "  } finalement {\n"
        "    afficher(\"toujours\")\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "7\ntoujours\n");
}

TEST(InterpreterErrors, FinallyRunsBeforeReturnLeavesTry)
{
    const auto [output, completed] = execute_program(
        "fonction f() {\n"
        "  essayer {\n"
        "    retourne 7\n"
        "  } attraper (e: Entier) {\n"
        "    afficher(e)\n"
        "  } finalement {\n"
        "    afficher(\"fin\")\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(f())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "fin\n7\n");
}

TEST(InterpreterErrors, FinallyRunsBeforeBreakLeavesTry)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  pour chaque n dans [1, 2, 3] {\n"
        "    essayer {\n"
        "      afficher(n)\n"
        "      arrêter\n"
        "    } attraper (e: Entier) {\n"
        "      afficher(e)\n"
        "    } finalement {\n"
        "      afficher(\"fin\")\n"
        "    }\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "1\nfin\n");
}

TEST(InterpreterErrors, CatchClauseOrderMatters)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  essayer {\n"
        "    lancer \"oops\"\n"
        "  } attraper (x: Universel) {\n"
        "    afficher(\"universel\")\n"
        "  } attraper (x: Texte) {\n"
        "    afficher(\"texte\")\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "universel\n");
}

TEST(InterpreterModules, ImportsModuleNamespaceFromFile)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_import_test";
    std::filesystem::create_directories(import_root);

    const std::filesystem::path module_path = import_root / "Calculs.lum";
    write_module(
        module_path,
        "public fonction doubler(x: Entier) {\n"
        "  retourne x * 2\n"
        "}\n"
        "public soit reponse = 21\n");

    const auto [output, completed] = execute_program_with_import_path(
        "importer Calculs\n"
        "fonction principal() {\n"
        "  afficher(Calculs.reponse)\n"
        "  afficher(Calculs.doubler(21))\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "21\n42\n");
}

TEST(InterpreterModules, ImportsDottedPackagePathAndUsesFinalSegmentAlias)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_package_import_test";

    const std::filesystem::path module_path = import_root / "outils" / "maths" / "calcul.lum";
    write_module(
        module_path,
        "public fonction tripler(x: Entier) {\n"
        "  retourne x * 3\n"
        "}\n"
        "public soit base = 14\n");

    const auto [output, completed] = execute_program_with_import_path(
        "importer outils.maths.calcul\n"
        "fonction principal() {\n"
        "  afficher(calcul.base)\n"
        "  afficher(calcul.tripler(14))\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "14\n42\n");
}

TEST(InterpreterModules, KeepsInternalDeclarationsOutOfModuleNamespace)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_module_visibility_test";

    const std::filesystem::path module_path = import_root / "Secrets.lum";
    write_module(
        module_path,
        "public soit visible = 7\n"
        "soit cache = 99\n");

    const auto [output, completed, error] = execute_program_with_error_and_import_path(
        "importer Secrets\n"
        "fonction principal() {\n"
        "  afficher(Secrets.visible)\n"
        "  afficher(Secrets.cache)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed);
    EXPECT_EQ(output, "7\n");
    EXPECT_NE(error.find("membre introuvable"), std::string::npos);
}

TEST(InterpreterModules, SupportsSelectiveImportsWithAliases)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_selective_import_test";

    const std::filesystem::path module_path = import_root / "outils" / "maths" / "calcul.lum";
    write_module(
        module_path,
        "public fonction tripler(x: Entier) {\n"
        "  retourne x * 3\n"
        "}\n"
        "public soit base = 14\n"
        "soit cache = 99\n");

    const auto [output, completed] = execute_program_with_import_path(
        "importer outils.maths.calcul.{tripler, base comme origine}\n"
        "fonction principal() {\n"
        "  afficher(origine)\n"
        "  afficher(tripler(14))\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "14\n42\n");
}

TEST(InterpreterModules, RejectsSelectiveImportOfInternalMember)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_selective_import_error_test";

    const std::filesystem::path module_path = import_root / "Secrets.lum";
    write_module(
        module_path,
        "public soit visible = 7\n"
        "soit cache = 99\n");

    const auto [output, completed, error] = execute_program_with_error_and_import_path(
        "importer Secrets.{cache}\n"
        "fonction principal() {\n"
        "  afficher(cache)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("membre non exporte"), std::string::npos);
}

TEST(InterpreterModules, RejectsUnknownModule)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_missing_module_test";
    std::filesystem::create_directories(import_root);

    const auto [output, completed, error] = execute_program_with_error_and_import_path(
        "importer Fantome\n"
        "fonction principal() {\n"
        "  afficher(1)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("module introuvable"), std::string::npos);
}

TEST(InterpreterModules, RejectsDuplicateSelectiveBindings)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_duplicate_selective_binding_test";
    write_module(import_root / "Calculs.lum",
                 "public soit a = 1\n"
                 "public soit b = 2\n");

    const auto [output, completed, error] = execute_program_with_error_and_import_path(
        "importer Calculs.{a, b comme a}\n"
        "fonction principal() {\n"
        "  afficher(a)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("deja declare"), std::string::npos);
}

TEST(InterpreterModules, RejectsNamespaceAliasCollision)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_namespace_alias_collision_test";
    write_module(import_root / "Calculs.lum", "public soit a = 1\n");
    write_module(import_root / "Autre.lum", "public soit b = 2\n");

    const auto [output, completed, error] = execute_program_with_error_and_import_path(
        "importer Calculs comme m\n"
        "importer Autre comme m\n"
        "fonction principal() {\n"
        "  afficher(m.a)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("deja declare"), std::string::npos);
}

TEST(InterpreterModules, PreservesInheritedBehaviorAcrossNamespaceImports)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_namespace_inheritance_test";
    write_module(import_root / "Animaux.lum",
                 "public classe Animal {\n"
                 "  nom: Texte\n"
                 "  fonction decrire() -> Texte {\n"
                 "    retourne \"Animal:\" + ici.nom\n"
                 "  }\n"
                 "}\n"
                 "public classe Chien : Animal {\n"
                 "  race: Texte\n"
                 "}\n");

    const auto [output, completed] = execute_program_with_import_path(
        "importer Animaux\n"
        "fonction principal() {\n"
        "  soit chien = Animaux.Chien(nom: \"Rex\", race: \"Berger\")\n"
        "  afficher(chien.nom)\n"
        "  afficher(chien.decrire())\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Rex\nAnimal:Rex\n");
}

TEST(InterpreterModules, RejectsSelectiveImportOfUnknownExportedName)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_unknown_export_test";
    write_module(import_root / "Calculs.lum", "public soit base = 21\n");

    const auto [output, completed, error] = execute_program_with_error_and_import_path(
        "importer Calculs.{inconnu}\n"
        "fonction principal() {\n"
        "  afficher(1)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("membre non exporte"), std::string::npos);
}

TEST(InterpreterModules, NamespaceAndSelectiveImportCanCoexist)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_import_coexist_test";
    write_module(import_root / "Calculs.lum",
                 "public soit base = 21\n"
                 "public soit autre = 42\n");

    const auto [output, completed] = execute_program_with_import_path(
        "importer Calculs\n"
        "importer Calculs.{base comme origine}\n"
        "fonction principal() {\n"
        "  afficher(Calculs.base)\n"
        "  afficher(origine)\n"
        "  afficher(Calculs.autre)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "21\n21\n42\n");
}

TEST(InterpreterModules, DetectsImportCycles)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_cycle_import_test";
    write_module(import_root / "A.lum",
                 "importer B\n"
                 "public soit a = 1\n");
    write_module(import_root / "B.lum",
                 "importer A\n"
                 "public soit b = 2\n");

    const auto [output, completed, error] = execute_program_with_error_and_import_path(
        "importer A\n"
        "fonction principal() {\n"
        "  afficher(1)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("cycle d'import"), std::string::npos);
}

TEST(InterpreterModules, CachesModuleExecutionAcrossMultipleImports)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_cache_import_test";
    write_module(import_root / "Compteur.lum",
                 "public soit valeur = 41\n"
                 "afficher(\"charge\")\n");

    const auto [output, completed] = execute_program_with_import_path(
        "importer Compteur\n"
        "importer Compteur comme Encore\n"
        "fonction principal() {\n"
        "  afficher(Compteur.valeur)\n"
        "  afficher(Encore.valeur)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "charge\n41\n41\n");
}

TEST(InterpreterModules, RejectsSyntaxErrorsInsideImportedModule)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_import_parse_error_test";
    write_module(import_root / "Cassé.lum",
                 "public fonction casser( {\n"
                 "  retourne 1\n"
                 "}\n");

    const auto [output, completed, error] = execute_program_with_error_and_import_path(
        "importer Cassé\n"
        "fonction principal() {\n"
        "  afficher(1)\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("erreur de syntaxe"), std::string::npos);
}

TEST(InterpreterBuiltinModules, SupportsCheminAndFichierModules)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_builtin_module_test";
    std::filesystem::create_directories(import_root);

    const std::filesystem::path file_path = import_root / "note.txt";
    {
        std::ofstream file(file_path);
        file << "bonjour";
    }

    const std::string source =
        "importer Chemin.{joindre, nom, nom_sans_extension, dossier}\n"
        "importer Fichier.{existe, lire_texte}\n"
        "fonction principal() {\n"
        "  soit chemin = joindre(\"" + import_root.string() + "\", \"note.txt\")\n"
        "  afficher(existe(chemin))\n"
        "  afficher(nom(chemin))\n"
        "  afficher(nom_sans_extension(chemin))\n"
        "  afficher(dossier(chemin))\n"
        "  afficher(lire_texte(chemin))\n"
        "}\n";

    const auto [output, completed] = execute_program_with_import_path(source, import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "vrai\nnote.txt\nnote\n" + normalize_path_text(import_root) + "\nbonjour\n");
}

TEST(InterpreterBuiltinModules, SupportsExpandedCheminModuleOperations)
{
    const std::filesystem::path current = std::filesystem::current_path();
    const std::filesystem::path expected_absolute = (current / ".." / "autre" / "fichier.txt").lexically_normal();
    const std::filesystem::path expected_normalized = std::filesystem::path("/tmp/alpha/../beta/config.lum").lexically_normal();

    const std::string source =
        "importer Chemin.{absolu, dossier, dossier_courant, est_absolu, est_relatif, extension, joindre, nom, nom_sans_extension, normaliser, parties}\n"
        "fonction principal() {\n"
        "  soit chemin = joindre(\"/tmp\", \"alpha\", \"..\", \"beta\", \"config.lum\")\n"
        "  afficher(dossier_courant())\n"
        "  afficher(chemin)\n"
        "  afficher(absolu(\"../autre/fichier.txt\"))\n"
        "  afficher(nom(chemin))\n"
        "  afficher(nom_sans_extension(chemin))\n"
        "  afficher(extension(chemin))\n"
        "  afficher(dossier(chemin))\n"
        "  soit morceaux = parties(chemin)\n"
        "  afficher(morceaux.taille())\n"
        "  afficher(morceaux[0])\n"
        "  afficher(morceaux[1])\n"
        "  afficher(morceaux[2])\n"
        "  afficher(est_absolu(chemin))\n"
        "  afficher(est_relatif(\"../autre\"))\n"
        "  afficher(normaliser(\"/tmp/alpha/../beta/config.lum\"))\n"
        "}\n";

    const auto [output, completed] = execute_program(source);

    EXPECT_TRUE(completed);

    std::istringstream lines(output);
    std::vector<std::string> actual;
    for (std::string line; std::getline(lines, line); )
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        actual.push_back(line);
    }

    ASSERT_EQ(actual.size(), 14u);
    EXPECT_EQ(actual[0], normalize_path_text(current));
    EXPECT_EQ(actual[1], "/tmp/beta/config.lum");
    EXPECT_EQ(actual[2], normalize_path_text(expected_absolute));
    EXPECT_EQ(actual[3], "config.lum");
    EXPECT_EQ(actual[4], "config");
    EXPECT_EQ(actual[5], ".lum");
    EXPECT_EQ(actual[6], "/tmp/beta");
    EXPECT_EQ(actual[7], "3");
    EXPECT_EQ(actual[8], "tmp");
    EXPECT_EQ(actual[9], "beta");
    EXPECT_EQ(actual[10], "config.lum");
    EXPECT_EQ(actual[11], "vrai");
    EXPECT_EQ(actual[12], "vrai");
    EXPECT_EQ(actual[13], "/tmp/beta/config.lum");
}

TEST(InterpreterBuiltinModules, SupportsExpandedFichierModuleOperations)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_fichier_expanded_test";
    std::filesystem::remove_all(import_root);
    std::filesystem::create_directories(import_root);

    const std::filesystem::path source_file = import_root / "source.txt";
    {
        std::ofstream file(source_file);
        file << "bonjour\nmonde\n";
    }

    std::filesystem::create_directories(import_root / "liste");
    {
        std::ofstream(import_root / "liste" / "b.txt") << "b";
        std::ofstream(import_root / "liste" / "a.txt") << "a";
    }

    const std::string source =
        "importer Fichier.{ajouter_texte, creer_dossiers, ecrire_texte, est_dossier, est_fichier, existe, lire_lignes, lire_texte, lister, modifie_le, taille}\n"
        "fonction principal() {\n"
        "  soit dossier = \"" + (import_root / "crees" / "nested").string() + "\"\n"
        "  soit texte = \"" + (import_root / "sortie.txt").string() + "\"\n"
        "  soit source = \"" + source_file.string() + "\"\n"
        "  soit liste = \"" + (import_root / "liste").string() + "\"\n"
        "  creer_dossiers(dossier)\n"
        "  ecrire_texte(texte, \"alpha\")\n"
        "  ajouter_texte(texte, \"-beta\")\n"
        "  afficher(existe(dossier))\n"
        "  afficher(est_dossier(dossier))\n"
        "  afficher(est_fichier(texte))\n"
        "  afficher(lire_texte(texte))\n"
        "  soit lignes = lire_lignes(source)\n"
        "  afficher(lignes.taille())\n"
        "  afficher(lignes[0])\n"
        "  afficher(lignes[1])\n"
        "  afficher(taille(texte))\n"
        "  afficher(modifie_le(texte))\n"
        "  soit elements = lister(liste)\n"
        "  afficher(elements.taille())\n"
        "  afficher(elements[0])\n"
        "  afficher(elements[1])\n"
        "}\n";

    const auto [output, completed] = execute_program_with_import_path(source, import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);

    std::istringstream lines(output);
    std::vector<std::string> actual;
    for (std::string line; std::getline(lines, line); )
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        actual.push_back(line);
    }

    ASSERT_EQ(actual.size(), 12u);
    EXPECT_EQ(actual[0], "vrai");
    EXPECT_EQ(actual[1], "vrai");
    EXPECT_EQ(actual[2], "vrai");
    EXPECT_EQ(actual[3], "alpha-beta");
    EXPECT_EQ(actual[4], "2");
    EXPECT_EQ(actual[5], "bonjour");
    EXPECT_EQ(actual[6], "monde");
    EXPECT_EQ(actual[7], "10");
    EXPECT_EQ(actual[8].size(), 20u);
    EXPECT_EQ(actual[8].at(4), '-');
    EXPECT_EQ(actual[8].at(7), '-');
    EXPECT_EQ(actual[8].at(10), 'T');
    EXPECT_EQ(actual[8].back(), 'Z');
    EXPECT_EQ(actual[9], "2");
    EXPECT_EQ(actual[10], (import_root / "liste" / "a.txt").string());
    EXPECT_EQ(actual[11], (import_root / "liste" / "b.txt").string());
}

TEST(InterpreterBuiltinModules, SupportsFichierWriteLinesCopyMoveAndDelete)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_fichier_mutation_test";
    std::filesystem::remove_all(import_root);
    std::filesystem::create_directories(import_root);

    const std::string source =
        "importer Fichier.{copier, deplacer, ecrire_lignes, existe, lire_lignes, lire_texte, supprimer}\n"
        "fonction principal() {\n"
        "  soit source = \"" + (import_root / "source.txt").string() + "\"\n"
        "  soit copie = \"" + (import_root / "copie.txt").string() + "\"\n"
        "  soit deplace = \"" + (import_root / "deplace.txt").string() + "\"\n"
        "  ecrire_lignes(source, [\"un\", \"deux\", \"trois\"])\n"
        "  copier(source, copie)\n"
        "  deplacer(copie, deplace)\n"
        "  afficher(lire_texte(source))\n"
        "  soit lignes = lire_lignes(deplace)\n"
        "  afficher(lignes.taille())\n"
        "  afficher(lignes[2])\n"
        "  afficher(existe(copie))\n"
        "  afficher(existe(deplace))\n"
        "  supprimer(source)\n"
        "  afficher(existe(source))\n"
        "}\n";

    const auto [output, completed] = execute_program_with_import_path(source, import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "un\ndeux\ntrois\n3\ntrois\nfaux\nvrai\nfaux\n");
}

TEST(InterpreterBuiltinModules, SupportsRecursiveListingAndSplitDirectoryDeletion)
{
    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_fichier_recursive_test";
    std::filesystem::remove_all(import_root);
    std::filesystem::create_directories(import_root);

    std::filesystem::create_directories(import_root / "arbre" / "alpha");
    std::filesystem::create_directories(import_root / "arbre" / "beta");
    std::filesystem::create_directories(import_root / "vide");
    {
        std::ofstream(import_root / "arbre" / "alpha" / "a.txt") << "a";
        std::ofstream(import_root / "arbre" / "beta" / "b.txt") << "b";
    }

    const std::string source =
        "importer Fichier.{est_dossier, existe, lister_recursif, supprimer_arbre, supprimer_dossier}\n"
        "fonction principal() {\n"
        "  soit arbre = \"" + (import_root / "arbre").string() + "\"\n"
        "  soit vide = \"" + (import_root / "vide").string() + "\"\n"
        "  soit elements = lister_recursif(arbre)\n"
        "  afficher(elements.taille())\n"
        "  afficher(elements[0])\n"
        "  afficher(elements[1])\n"
        "  afficher(elements[2])\n"
        "  afficher(elements[3])\n"
        "  supprimer_dossier(vide)\n"
        "  afficher(existe(vide))\n"
        "  supprimer_arbre(arbre)\n"
        "  afficher(existe(arbre))\n"
        "  afficher(est_dossier(arbre))\n"
        "}\n";

    const auto [output, completed] = execute_program_with_import_path(source, import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_TRUE(completed);

    std::istringstream lines(output);
    std::vector<std::string> actual;
    for (std::string line; std::getline(lines, line); )
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        actual.push_back(line);
    }

    ASSERT_EQ(actual.size(), 8u);
    EXPECT_EQ(actual[0], "4");
    EXPECT_EQ(actual[1], (import_root / "arbre" / "alpha").string());
    EXPECT_EQ(actual[2], (import_root / "arbre" / "alpha" / "a.txt").string());
    EXPECT_EQ(actual[3], (import_root / "arbre" / "beta").string());
    EXPECT_EQ(actual[4], (import_root / "arbre" / "beta" / "b.txt").string());
    EXPECT_EQ(actual[5], "faux");
    EXPECT_EQ(actual[6], "faux");
    EXPECT_EQ(actual[7], "faux");
}

TEST(InterpreterBuiltinModules, RejectsInvalidCheminArguments)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Chemin.{joindre}\n"
        "fonction principal() {\n"
        "  afficher(joindre(1))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Chemin.joindre"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsReadingMissingFile)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Fichier.{lire_texte}\n"
        "fonction principal() {\n"
        "  afficher(lire_texte(\"/definitivement/introuvable.txt\"))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("impossible d'ouvrir"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsInvalidExpandedFichierUsage)
{
    auto [output1, completed1, error1] = execute_program_with_error(
        "importer Fichier.{ecrire_texte}\n"
        "fonction principal() {\n"
        "  ecrire_texte(\"x.txt\", 1)\n"
        "}\n");

    EXPECT_FALSE(completed1);
    EXPECT_TRUE(output1.empty());
    EXPECT_NE(error1.find("Fichier.ecrire_texte attend un contenu de type Texte"), std::string::npos);

    auto [output2, completed2, error2] = execute_program_with_error(
        "importer Fichier.{lire_lignes}\n"
        "fonction principal() {\n"
        "  lire_lignes(1)\n"
        "}\n");

    EXPECT_FALSE(completed2);
    EXPECT_TRUE(output2.empty());
    EXPECT_NE(error2.find("Fichier.lire_lignes attend un chemin de type Texte"), std::string::npos);

    auto [output3, completed3, error3] = execute_program_with_error(
        "importer Fichier.{lister}\n"
        "fonction principal() {\n"
        "  lister(\"/definitivement/introuvable-dossier\")\n"
        "}\n");

    EXPECT_FALSE(completed3);
    EXPECT_TRUE(output3.empty());
    EXPECT_NE(error3.find("Fichier.lister a echoue"), std::string::npos);

    auto [output4, completed4, error4] = execute_program_with_error(
        "importer Fichier.{ecrire_lignes}\n"
        "fonction principal() {\n"
        "  ecrire_lignes(\"x.txt\", [\"a\", 1])\n"
        "}\n");

    EXPECT_FALSE(completed4);
    EXPECT_TRUE(output4.empty());
    EXPECT_NE(error4.find("Fichier.ecrire_lignes attend une liste contenant uniquement des valeurs de type Texte"), std::string::npos);

    auto [output5, completed5, error5] = execute_program_with_error(
        "importer Fichier.{supprimer}\n"
        "fonction principal() {\n"
        "  supprimer(\"/definitivement/introuvable-fichier\")\n"
        "}\n");

    EXPECT_FALSE(completed5);
    EXPECT_TRUE(output5.empty());
    EXPECT_NE(error5.find("Fichier.supprimer a echoue"), std::string::npos);

    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_fichier_recursive_errors_test";
    std::filesystem::remove_all(import_root);
    std::filesystem::create_directories(import_root / "non_vide");
    std::ofstream(import_root / "non_vide" / "f.txt") << "x";
    std::ofstream(import_root / "pas_dossier.txt") << "x";

    const auto [output6, completed6, error6] = execute_program_with_error_and_import_path(
        "importer Fichier.{supprimer_dossier}\n"
        "fonction principal() {\n"
        "  supprimer_dossier(\"" + (import_root / "non_vide").string() + "\")\n"
        "}\n",
        import_root);

    EXPECT_FALSE(completed6);
    EXPECT_TRUE(output6.empty());
    EXPECT_NE(error6.find("Fichier.supprimer_dossier a echoue"), std::string::npos);

    const auto [output7, completed7, error7] = execute_program_with_error_and_import_path(
        "importer Fichier.{supprimer_dossier}\n"
        "fonction principal() {\n"
        "  supprimer_dossier(\"" + (import_root / "pas_dossier.txt").string() + "\")\n"
        "}\n",
        import_root);

    EXPECT_FALSE(completed7);
    EXPECT_TRUE(output7.empty());
    EXPECT_NE(error7.find("n'est pas un dossier"), std::string::npos);

    const auto [output8, completed8, error8] = execute_program_with_error_and_import_path(
        "importer Fichier.{supprimer_arbre}\n"
        "fonction principal() {\n"
        "  supprimer_arbre(\"" + (import_root / "introuvable").string() + "\")\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed8);
    EXPECT_TRUE(output8.empty());
    EXPECT_NE(error8.find("Fichier.supprimer_arbre a echoue"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsNamedArgumentsWhereUnsupported)
{
    auto [output1, completed1, error1] = execute_program_with_error(
        "importer Chemin.{nom}\n"
        "fonction principal() {\n"
        "  afficher(nom(chemin: \"abc\"))\n"
        "}\n");

    EXPECT_FALSE(completed1);
    EXPECT_TRUE(output1.empty());
    EXPECT_NE(error1.find("positionnel"), std::string::npos);

    auto [output2, completed2, error2] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(valeur: 1)\n"
        "}\n");

    EXPECT_FALSE(completed2);
    EXPECT_TRUE(output2.empty());
    EXPECT_NE(error2.find("arguments nommes"), std::string::npos);
}

TEST(InterpreterBuiltinModulesMatrix, RejectsInvalidBuiltinArityAndTypes)
{
    struct Case
    {
        std::string source;
        std::string expected_fragment;
    };

    const std::vector<Case> cases = {
        {
            "importer Fichier.{existe}\n"
            "fonction principal() {\n"
            "  afficher(existe())\n"
            "}\n",
            "Fichier.existe"
        },
        {
            "importer Fichier.{existe}\n"
            "fonction principal() {\n"
            "  afficher(existe(1))\n"
            "}\n",
            "Fichier.existe attend un chemin de type Texte"
        },
        {
            "importer Fichier.{est_fichier}\n"
            "fonction principal() {\n"
            "  afficher(est_fichier())\n"
            "}\n",
            "Fichier.est_fichier"
        },
        {
            "importer Fichier.{est_dossier}\n"
            "fonction principal() {\n"
            "  afficher(est_dossier(1))\n"
            "}\n",
            "Fichier.est_dossier attend un chemin de type Texte"
        },
        {
            "importer Fichier.{taille}\n"
            "fonction principal() {\n"
            "  afficher(taille())\n"
            "}\n",
            "Fichier.taille"
        },
        {
            "importer Fichier.{modifie_le}\n"
            "fonction principal() {\n"
            "  afficher(modifie_le(1))\n"
            "}\n",
            "Fichier.modifie_le attend un chemin de type Texte"
        },
        {
            "importer Fichier.{ecrire_texte}\n"
            "fonction principal() {\n"
            "  ecrire_texte(\"a.txt\")\n"
            "}\n",
            "Fichier.ecrire_texte"
        },
        {
            "importer Fichier.{ecrire_lignes}\n"
            "fonction principal() {\n"
            "  ecrire_lignes(1, [])\n"
            "}\n",
            "Fichier.ecrire_lignes attend un chemin de type Texte"
        },
        {
            "importer Fichier.{ajouter_texte}\n"
            "fonction principal() {\n"
            "  ajouter_texte(1, \"x\")\n"
            "}\n",
            "Fichier.ajouter_texte attend un chemin de type Texte"
        },
        {
            "importer Fichier.{copier}\n"
            "fonction principal() {\n"
            "  copier(\"a\")\n"
            "}\n",
            "Fichier.copier"
        },
        {
            "importer Fichier.{deplacer}\n"
            "fonction principal() {\n"
            "  deplacer(1, \"b\")\n"
            "}\n",
            "Fichier.deplacer attend un source de type Texte"
        },
        {
            "importer Fichier.{supprimer}\n"
            "fonction principal() {\n"
            "  supprimer(1)\n"
            "}\n",
            "Fichier.supprimer attend un chemin de type Texte"
        },
        {
            "importer Fichier.{lister_recursif}\n"
            "fonction principal() {\n"
            "  lister_recursif(1)\n"
            "}\n",
            "Fichier.lister_recursif attend un chemin de type Texte"
        },
        {
            "importer Fichier.{supprimer_dossier}\n"
            "fonction principal() {\n"
            "  supprimer_dossier(1)\n"
            "}\n",
            "Fichier.supprimer_dossier attend un chemin de type Texte"
        },
        {
            "importer Fichier.{supprimer_arbre}\n"
            "fonction principal() {\n"
            "  supprimer_arbre(1)\n"
            "}\n",
            "Fichier.supprimer_arbre attend un chemin de type Texte"
        },
        {
            "importer Fichier.{creer_dossiers}\n"
            "fonction principal() {\n"
            "  creer_dossiers(1)\n"
            "}\n",
            "Fichier.creer_dossiers attend un chemin de type Texte"
        },
        {
            "importer Fichier.{lister}\n"
            "fonction principal() {\n"
            "  lister(1)\n"
            "}\n",
            "Fichier.lister attend un chemin de type Texte"
        },
        {
            "importer Chemin.{nom}\n"
            "fonction principal() {\n"
            "  afficher(nom())\n"
            "}\n",
            "Chemin.nom"
        },
        {
            "importer Chemin.{dossier}\n"
            "fonction principal() {\n"
            "  afficher(dossier(1))\n"
            "}\n",
            "Chemin.dossier attend un chemin de type Texte"
        },
        {
            "importer Chemin.{absolu}\n"
            "fonction principal() {\n"
            "  afficher(absolu())\n"
            "}\n",
            "Chemin.absolu"
        },
        {
            "importer Chemin.{nom_sans_extension}\n"
            "fonction principal() {\n"
            "  afficher(nom_sans_extension(1))\n"
            "}\n",
            "Chemin.nom_sans_extension attend un chemin de type Texte"
        },
        {
            "importer Chemin.{extension}\n"
            "fonction principal() {\n"
            "  afficher(extension())\n"
            "}\n",
            "Chemin.extension"
        },
        {
            "importer Chemin.{parties}\n"
            "fonction principal() {\n"
            "  afficher(parties(1))\n"
            "}\n",
            "Chemin.parties attend un chemin de type Texte"
        },
        {
            "importer Chemin.{est_absolu}\n"
            "fonction principal() {\n"
            "  afficher(est_absolu())\n"
            "}\n",
            "Chemin.est_absolu"
        },
        {
            "importer Chemin.{est_relatif}\n"
            "fonction principal() {\n"
            "  afficher(est_relatif(1))\n"
            "}\n",
            "Chemin.est_relatif attend un chemin de type Texte"
        },
        {
            "importer Chemin.{normaliser}\n"
            "fonction principal() {\n"
            "  afficher(normaliser())\n"
            "}\n",
            "Chemin.normaliser"
        },
        {
            "importer Chemin.{dossier_courant}\n"
            "fonction principal() {\n"
            "  afficher(dossier_courant(1))\n"
            "}\n",
            "Chemin.dossier_courant"
        },
        {
            "importer Chemin.{joindre}\n"
            "fonction principal() {\n"
            "  afficher(joindre())\n"
            "}\n",
            "Chemin.joindre attend au moins un segment"
        },
    };

    for (const auto &test_case : cases)
    {
        const auto [output, completed, error] = execute_program_with_error(test_case.source);
        EXPECT_FALSE(completed) << test_case.source;
        EXPECT_TRUE(output.empty()) << test_case.source;
        EXPECT_NE(error.find(test_case.expected_fragment), std::string::npos) << test_case.source;
    }
}

TEST(InterpreterBuiltinModules, SupportsTempsModule)
{
    const auto [output, completed] = execute_program(
        "importer Temps\n"
        "fonction principal() {\n"
        "  soit instant = Temps.depuis_horodatage(1704078245123)\n"
        "  afficher(instant.année())\n"
        "  afficher(instant.mois())\n"
        "  afficher(instant.jour())\n"
        "  afficher(instant.heure())\n"
        "  afficher(instant.minute())\n"
        "  afficher(instant.seconde())\n"
        "  afficher(instant.milliseconde())\n"
        "  afficher(instant.formater(\"AAAA-MM-JJ HH:mm:ss.SSS\"))\n"
        "  afficher(instant.en_horodatage())\n"
        "  soit analyse = Temps.analyser(\"2024-06-07 08:09:10.011\", \"AAAA-MM-JJ HH:mm:ss.SSS\")\n"
        "  afficher(analyse.formater(\"AAAA/MM/JJ HH:mm:ss.SSS\"))\n"
        "  soit durée = Temps.entre(Temps.depuis_horodatage(1000), Temps.depuis_horodatage(3723004))\n"
        "  afficher(durée.en_millisecondes())\n"
        "  afficher(durée.en_secondes() > 3722.0)\n"
        "  afficher(durée.en_minutes() > 62.0)\n"
        "  afficher(durée.en_heures() > 1.0)\n"
        "  soit plus_tard = instant.ajouter(Temps.minutes(2))\n"
        "  soit plus_tot = plus_tard.soustraire(Temps.secondes(30))\n"
        "  afficher(plus_tard.formater(\"HH:mm:ss\"))\n"
        "  afficher(plus_tot.formater(\"HH:mm:ss\"))\n"
        "  afficher(Temps.millisecondes(5).en_millisecondes())\n"
        "  afficher(Temps.secondes(2).en_millisecondes())\n"
        "  afficher(Temps.minutes(3).en_millisecondes())\n"
        "  afficher(Temps.heures(1).en_millisecondes())\n"
        "  afficher(Temps.jours(1).en_millisecondes())\n"
        "  soit avant = Temps.horodatage()\n"
        "  Temps.attendre(Temps.millisecondes(1))\n"
        "  soit après = Temps.horodatage()\n"
        "  afficher(après >= avant)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "2024\n"
        "1\n"
        "1\n"
        "3\n"
        "4\n"
        "5\n"
        "123\n"
        "2024-01-01 03:04:05.123\n"
        "1704078245123\n"
        "2024/06/07 08:09:10.011\n"
        "3722004\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "03:06:05\n"
        "03:05:35\n"
        "5\n"
        "2000\n"
        "180000\n"
        "3600000\n"
        "86400000\n"
        "vrai\n");
}

TEST(InterpreterBuiltinModules, RejectsInvalidTempsUsage)
{
    struct Case
    {
        std::string source;
        std::string expected_fragment;
    };

    const std::vector<Case> cases = {
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  Temps.horodatage(1)\n"
            "}\n",
            "Temps.horodatage"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  Temps.depuis_horodatage(\"abc\")\n"
            "}\n",
            "Temps.depuis_horodatage attend une valeur de type Entier"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  Temps.analyser(\"2024-01-01\", \"AAAA-MM\")\n"
            "}\n",
            "Temps.analyser a echoue"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  Temps.analyser(\"2024-13-01\", \"AAAA-MM-JJ\")\n"
            "}\n",
            "Temps.analyser a echoue"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  Temps.entre(1, Temps.maintenant())\n"
            "}\n",
            "Temps.entre attend une valeur de type Instant"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  Temps.attendre(Temps.millisecondes(-1))\n"
            "}\n",
            "Temps.attendre attend une duree positive"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  soit instant = Temps.maintenant()\n"
            "  instant.formater(1)\n"
            "}\n",
            "Instant.formater attend une valeur de type Texte"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  soit instant = Temps.maintenant()\n"
            "  instant.ajouter(1)\n"
            "}\n",
            "Instant.ajouter attend une valeur de type Durée"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  soit durée = Temps.secondes(1)\n"
            "  durée.en_secondes(1)\n"
            "}\n",
            "Durée.en_secondes"
        },
    };

    for (const auto &test_case : cases)
    {
        const auto [output, completed, error] = execute_program_with_error(test_case.source);
        EXPECT_FALSE(completed) << test_case.source;
        EXPECT_TRUE(output.empty()) << test_case.source;
        EXPECT_NE(error.find(test_case.expected_fragment), std::string::npos) << test_case.source;
    }
}

TEST(InterpreterBuiltinModules, SupportsAleatoireModule)
{
    const auto [output, completed] = execute_program(
        "importer Aléatoire\n"
        "importer Aléatoire comme Hasard\n"
        "fonction principal() {\n"
        "  Aléatoire.graine(42)\n"
        "  soit a = Aléatoire.entier(1, 100)\n"
        "  Aléatoire.graine(42)\n"
        "  soit b = Hasard.entier(1, 100)\n"
        "  afficher(a == b)\n"
        "  Aléatoire.graine(7)\n"
        "  soit x = Aléatoire.décimal()\n"
        "  Aléatoire.graine(7)\n"
        "  soit y = Hasard.décimal()\n"
        "  afficher(x == y)\n"
        "  soit borne = Aléatoire.décimal_entre(10.0, 20.0)\n"
        "  afficher(borne >= 10.0)\n"
        "  afficher(borne <= 20.0)\n"
        "  soit fruits = [\"pomme\", \"banane\", \"cerise\", \"datte\"]\n"
        "  Aléatoire.graine(99)\n"
        "  soit choix1 = Aléatoire.choisir(fruits)\n"
        "  Aléatoire.graine(99)\n"
        "  soit choix2 = Hasard.choisir(fruits)\n"
        "  afficher(choix1 == choix2)\n"
        "  Aléatoire.graine(5)\n"
        "  soit sample = Aléatoire.échantillon(fruits, 3)\n"
        "  afficher(sample.taille())\n"
        "  afficher(sample[0] != sample[1])\n"
        "  afficher(sample[1] != sample[2])\n"
        "  afficher(sample[0] != sample[2])\n"
        "  Aléatoire.graine(11)\n"
        "  soit nombres = [1, 2, 3, 4]\n"
        "  soit retour = Aléatoire.mélanger(nombres)\n"
        "  afficher(retour == nombres)\n"
        "  afficher(nombres.taille())\n"
        "  afficher(nombres.contient(1))\n"
        "  afficher(nombres.contient(2))\n"
        "  afficher(nombres.contient(3))\n"
        "  afficher(nombres.contient(4))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "3\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "4\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n");
}

TEST(InterpreterBuiltinModules, SupportsAccentlessAleatoireModuleAliasAndDecimalBuiltinAlias)
{
    const auto [output, completed] = execute_program_with_input(
        "importer Aleatoire\n"
        "fonction principal() {\n"
        "  Aleatoire.graine(42)\n"
        "  afficher(Aleatoire.entier(1, 10) >= 1)\n"
        "  afficher(Aleatoire.entier(1, 10) <= 10)\n"
        "  afficher(lire_decimal())\n"
        "}\n",
        "3.5\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "vrai\nvrai\n3.5\n");
}

TEST(InterpreterBuiltinModules, RejectsInvalidAleatoireUsage)
{
    struct Case
    {
        std::string source;
        std::string expected_fragment;
    };

    const std::vector<Case> cases = {
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.graine()\n"
            "}\n",
            "Aléatoire.graine"
        },
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.entier(10, 1)\n"
            "}\n",
            "Aléatoire.entier attend min <= max"
        },
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.décimal(1)\n"
            "}\n",
            "Aléatoire.décimal"
        },
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.décimal_entre(5.0, 1.0)\n"
            "}\n",
            "Aléatoire.décimal_entre attend min <= max"
        },
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.choisir([])\n"
            "}\n",
            "liste vide"
        },
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.choisir(\"abc\")\n"
            "}\n",
            "Aléatoire.choisir attend une Liste"
        },
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.mélanger(\"abc\")\n"
            "}\n",
            "Aléatoire.mélanger attend une Liste"
        },
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.échantillon([1, 2], 3)\n"
            "}\n",
            "Aléatoire.échantillon attend 0 <= n <= taille"
        },
        {
            "importer Aléatoire\n"
            "fonction principal() {\n"
            "  Aléatoire.échantillon([1, 2], -1)\n"
            "}\n",
            "Aléatoire.échantillon attend 0 <= n <= taille"
        },
    };

    for (const auto &test_case : cases)
    {
        const auto [output, completed, error] = execute_program_with_error(test_case.source);
        EXPECT_FALSE(completed) << test_case.source;
        EXPECT_TRUE(output.empty()) << test_case.source;
        EXPECT_NE(error.find(test_case.expected_fragment), std::string::npos) << test_case.source;
    }
}

TEST(InterpreterBuiltinModules, SupportsLumiNetAdresseAndDns)
{
    SKIP_IF_LUMINET_DISABLED();
    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  soit adresse = LumiNet.Adresse.analyser(\"127.0.0.1:8080\")\n"
        "  afficher(adresse.hôte)\n"
        "  afficher(adresse.port)\n"
        "  afficher(adresse.en_texte())\n"
        "  soit locale = LumiNet.Adresse.locale()\n"
        "  afficher(locale.hôte != \"\")\n"
        "  afficher(LumiNet.Adresse.est_valide(\"127.0.0.1\"))\n"
        "  afficher(LumiNet.Adresse.est_ipv4(\"127.0.0.1\"))\n"
        "  afficher(LumiNet.Adresse.est_ipv6(\"::1\"))\n"
        "  afficher(LumiNet.Adresse.est_locale(\"127.0.0.1\"))\n"
        "  soit ip = LumiNet.DNS.résoudre(\"localhost\")\n"
        "  afficher(ip != \"\")\n"
        "  soit toutes = LumiNet.DNS.résoudre_tous(\"localhost\")\n"
        "  afficher(toutes.taille() >= 1)\n"
        "  soit inverse = LumiNet.DNS.résoudre_inverse(\"127.0.0.1\")\n"
        "  afficher(inverse != \"\")\n"
        "}\n");

    EXPECT_TRUE(completed) << error;
    EXPECT_EQ(
        output,
        "127.0.0.1\n"
        "8080\n"
        "127.0.0.1:8080\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "vrai\n");
}

TEST(InterpreterBuiltinModules, SupportsLumiNetTcpClientAndServer)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    auto future = std::async(std::launch::async, [promise = std::move(port_promise)]() mutable -> std::string {
        const TestSocket server_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        if (!test_socket_valid(server_fd))
        {
            return "socket";
        }

        test_set_reuseaddr(server_fd);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            test_close_socket(server_fd);
            return "bind";
        }

        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        if (::getsockname(server_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len) != 0)
        {
            test_close_socket(server_fd);
            return "getsockname";
        }
        if (::listen(server_fd, 1) != 0)
        {
            test_close_socket(server_fd);
            return "listen";
        }
        promise.set_value(ntohs(bound.sin_port));

        if (!test_wait_until_readable(server_fd, std::chrono::seconds(5)))
        {
            test_close_socket(server_fd);
            return "accept timeout";
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const TestSocket client_fd = ::accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (!test_socket_valid(client_fd))
        {
            test_close_socket(server_fd);
            return "accept";
        }

        std::string line;
        char ch = '\0';
        while (true)
        {
            if (!test_wait_until_readable(client_fd, std::chrono::seconds(5)))
            {
                line = "receive timeout";
                break;
            }
            const TestRecvSize received = test_recv(client_fd, &ch, 1, 0);
            if (received <= 0)
            {
                break;
            }
            if (ch == '\n')
            {
                break;
            }
            if (ch != '\r')
            {
                line.push_back(ch);
            }
        }

        test_close_socket(client_fd);
        test_close_socket(server_fd);
        return line;
    });

    const int port = port_future.get();

    const auto [client_output, client_completed, client_error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  soit connexion = LumiNet.TCP.connecter(\"127.0.0.1\", " + std::to_string(port) + ")\n"
        "  connexion.écrire(\"bonjour\\n\")\n"
        "  afficher(connexion.est_connecté())\n"
        "  connexion.fermer()\n"
        "}\n");

    const std::string server_line = future.get();

    EXPECT_TRUE(client_completed) << client_error;
    EXPECT_EQ(client_output, "vrai\n");
    EXPECT_EQ(server_line, "bonjour\\n");
}

TEST(InterpreterBuiltinModules, SupportsLumiNetUdp)
{
    SKIP_IF_LUMINET_DISABLED();
    const TestSocket probe_fd = test_open_socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_TRUE(test_socket_valid(probe_fd));
    sockaddr_in probe_addr{};
    probe_addr.sin_family = AF_INET;
    probe_addr.sin_port = htons(0);
    probe_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(probe_fd, reinterpret_cast<sockaddr *>(&probe_addr), sizeof(probe_addr)), 0);
    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(::getsockname(probe_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len), 0);
    const int port = ntohs(bound.sin_port);
    test_close_socket(probe_fd);

    const std::string receiver_source =
        "importer LumiNet\n"
        "importer Temps\n"
        "fonction principal() {\n"
        "  soit socket = LumiNet.UDP.ouvrir(" + std::to_string(port) + ")\n"
        "  socket.définir_délai(Temps.secondes(2))\n"
        "  soit paquet = socket.recevoir()\n"
        "  afficher(paquet.données)\n"
        "  afficher(paquet.adresse != \"\")\n"
        "  afficher(paquet.port > 0)\n"
        "  socket.fermer()\n"
        "}\n";

    auto future = std::async(std::launch::async, [receiver_source]() {
        return execute_program_with_error(receiver_source);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const TestSocket sender_fd = test_open_socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_TRUE(test_socket_valid(sender_fd));
    sockaddr_in sender_addr{};
    sender_addr.sin_family = AF_INET;
    sender_addr.sin_port = htons(static_cast<uint16_t>(port));
    sender_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const std::string payload = "salut";
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        ASSERT_GT(::sendto(sender_fd,
                           payload.data(),
                           static_cast<int>(payload.size()),
                           0,
                           reinterpret_cast<const sockaddr *>(&sender_addr),
                           sizeof(sender_addr)),
                  0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    test_close_socket(sender_fd);

    const auto [receiver_output, receiver_completed, receiver_error] = future.get();

    EXPECT_TRUE(receiver_completed) << receiver_error;
    EXPECT_EQ(receiver_output, "salut\nvrai\nvrai\n");
}

TEST(InterpreterBuiltinModules, RejectsInvalidLumiNetUsage)
{
    SKIP_IF_LUMINET_DISABLED();
    struct Case
    {
        std::string source;
        std::string expected_fragment;
    };

    const std::vector<Case> cases = {
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.Adresse.analyser(\"abc\")\n"
            "}\n",
            "LumiNet.Adresse.analyser"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.DNS.résoudre_inverse(\"pas_une_ip\")\n"
            "}\n",
            "LumiNet.DNS.résoudre_inverse requiert une adresse IP valide"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.TCP.connecter(\"127.0.0.1\")\n"
            "}\n",
            "LumiNet.TCP.connecter"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  soit s = LumiNet.TCP.Serveur()\n"
            "  s.écouter(\"127.0.0.1\", 19130)\n"
            "}\n",
            "quand_connexion"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.UDP.ouvrir(70000)\n"
            "}\n",
            "port entre 0 et 65535"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  soit socket = LumiNet.UDP.ouvrir()\n"
            "  socket.envoyer(\"salut\", \"127.0.0.1\", 70000)\n"
            "}\n",
            "SocketUDP.envoyer requiert un port entre 0 et 65535"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  soit socket = LumiNet.UDP.ouvrir()\n"
            "  socket.envoyer_octets([1, 2, 3], \"127.0.0.1\", 70000)\n"
            "}\n",
            "SocketUDP.envoyer_octets requiert un port entre 0 et 65535"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  soit socket = LumiNet.UDP.ouvrir()\n"
            "  socket.diffuser(\"salut\", 70000)\n"
            "}\n",
            "SocketUDP.diffuser requiert un port entre 0 et 65535"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.Canal.connecter(\"http://example.com\")\n"
            "}\n",
            "LumiNet.Canal.connecter"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.HTTP.obtenir(\"https://example.com\")\n"
            "}\n",
            "ne prend actuellement en charge que http"
        },
    };

    for (const auto &test_case : cases)
    {
        const auto [output, completed, error] = execute_program_with_error(test_case.source);
        EXPECT_FALSE(completed) << test_case.source;
        EXPECT_TRUE(output.empty()) << test_case.source;
        EXPECT_NE(error.find(test_case.expected_fragment), std::string::npos) << test_case.source;
    }
}

TEST(InterpreterBuiltinModules, SupportsLumiNetHttpClient)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    auto future = std::async(std::launch::async, [promise = std::move(port_promise)]() mutable -> std::string {
        const TestSocket server_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        if (!test_socket_valid(server_fd))
        {
            return "socket";
        }
        test_set_reuseaddr(server_fd);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            test_close_socket(server_fd);
            return "bind";
        }
        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(server_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        if (::listen(server_fd, 1) != 0)
        {
            test_close_socket(server_fd);
            return "listen";
        }
        promise.set_value(ntohs(bound.sin_port));
        if (!test_wait_until_readable(server_fd, std::chrono::seconds(5)))
        {
            test_close_socket(server_fd);
            return "accept timeout";
        }
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const TestSocket client_fd = ::accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (!test_socket_valid(client_fd))
        {
            test_close_socket(server_fd);
            return "accept";
        }
        std::string request;
        char buffer[4096];
        while (true)
        {
            const TestRecvSize received = test_recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0)
            {
                break;
            }
            request.append(buffer, buffer + received);
            if (request.find("\r\n\r\n") != std::string::npos)
            {
                break;
            }
        }
        const std::string response =
            "HTTP/1.1 201 OK\r\n"
            "Content-Length: 7\r\n"
            "Content-Type: text/plain\r\n"
            "X-Reponse: salut\r\n"
            "Connection: close\r\n"
            "\r\n"
            "bonjour";
        test_send(client_fd, response.data(), response.size(), 0);
        test_close_socket(client_fd);
        test_close_socket(server_fd);
        return request;
    });

    const int port = port_future.get();
    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  soit réponse = LumiNet.HTTP.créer(\"http://127.0.0.1:" + std::to_string(port) + "/api\", corps: \"charge\", type: \"text/plain\")\n"
        "  afficher(réponse.statut)\n"
        "  afficher(réponse.corps)\n"
        "  afficher(réponse.succès)\n"
        "  afficher(réponse.entête(\"X-Reponse\"))\n"
        "}\n");
    const std::string request = future.get();

    EXPECT_TRUE(completed) << error;
    EXPECT_EQ(output, "201\nbonjour\nvrai\nsalut\n");
    EXPECT_NE(request.find("POST /api HTTP/1.1"), std::string::npos);
    EXPECT_NE(request.find("Content-Type: text/plain"), std::string::npos);
    EXPECT_NE(request.find("charge"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsTruncatedLumiNetHttpResponseBodies)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    auto future = std::async(std::launch::async, [promise = std::move(port_promise)]() mutable -> std::string {
        const TestSocket server_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        if (!test_socket_valid(server_fd))
        {
            return "socket";
        }
        test_set_reuseaddr(server_fd);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            test_close_socket(server_fd);
            return "bind";
        }
        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(server_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        if (::listen(server_fd, 1) != 0)
        {
            test_close_socket(server_fd);
            return "listen";
        }
        promise.set_value(ntohs(bound.sin_port));
        if (!test_wait_until_readable(server_fd, std::chrono::seconds(5)))
        {
            test_close_socket(server_fd);
            return "accept timeout";
        }
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const TestSocket client_fd = ::accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (!test_socket_valid(client_fd))
        {
            test_close_socket(server_fd);
            return "accept";
        }

        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos)
        {
            const TestRecvSize received = test_recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0)
            {
                break;
            }
            request.append(buffer, buffer + received);
        }

        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 10\r\n"
            "Connection: close\r\n"
            "\r\n"
            "short";
        test_send(client_fd, response.data(), response.size(), 0);
        test_close_socket(client_fd);
        test_close_socket(server_fd);
        return request;
    });

    const int port = port_future.get();
    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  LumiNet.HTTP.obtenir(\"http://127.0.0.1:" + std::to_string(port) + "/\")\n"
        "}\n");

    const std::string request = future.get();
    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("message HTTP tronqué"), std::string::npos);
    EXPECT_NE(request.find("GET / HTTP/1.1"), std::string::npos);
}

TEST(InterpreterBuiltinModules, SupportsLumiNetHttpServer)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    const std::string server_source =
        "importer LumiNet\n"
        "soit serveur_global = rien\n"
        "fonction journal(req: Universel, rep: Universel, suivant: Universel) {\n"
        "  rep.définir_entête(\"X-Test\", \"ok\")\n"
        "  suivant()\n"
        "}\n"
        "fonction bonjour(req: Universel, rep: Universel) {\n"
        "  rep.envoyer(200, req.paramètre(\"nom\") + \":\" + req.requête(\"salut\", \"non\") + \":\" + req.entête(\"Authorization\"))\n"
        "  serveur_global.arreter()\n"
        "}\n"
        "fonction principal() {\n"
        "  soit serveur = LumiNet.HTTP.Serveur()\n"
        "  serveur_global = serveur\n"
        "  serveur.avant(journal)\n"
        "  serveur.OBTENIR(\"/bonjour/:nom\", bonjour)\n"
        "  serveur.écouter(\"127.0.0.1\", " ;

    auto server_future = std::async(std::launch::async, [source_prefix = server_source, promise = std::move(port_promise)]() mutable {
        const TestSocket probe_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        test_set_reuseaddr(probe_fd);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(probe_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(probe_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        const int port = ntohs(bound.sin_port);
        test_close_socket(probe_fd);
        promise.set_value(port);
        const std::string source = source_prefix + std::to_string(port) + ")\n}\n";
        return execute_program_without_capture_with_error(source);
    });

    const int port = port_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const TestSocket client_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(test_socket_valid(client_fd));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::connect(client_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
    const std::string request =
        "GET /bonjour/Ada?salut=oui HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Authorization: jeton\r\n"
        "Connection: close\r\n"
        "\r\n";
    ASSERT_GT(test_send(client_fd, request.data(), request.size(), 0), 0);
    std::string response;
    char buffer[4096];
    while (true)
    {
        const TestRecvSize received = test_recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0)
        {
            break;
        }
        response.append(buffer, buffer + received);
    }
    test_close_socket(client_fd);

    const auto [server_completed, server_error] = server_future.get();
    EXPECT_TRUE(server_completed) << server_error;
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("X-Test: ok"), std::string::npos);
    EXPECT_NE(response.find("Ada:oui:jeton"), std::string::npos);
}

TEST(InterpreterBuiltinModules, SupportsLumiNetHttpServerFileResponsesWithHtmlContentType)
{
    SKIP_IF_LUMINET_DISABLED();
    const std::filesystem::path html_path = std::filesystem::temp_directory_path() / "lumiere_http_server_page.html";
    {
        std::ofstream html_file(html_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(html_file.is_open());
        html_file << "<!doctype html><h1>bonjour</h1>";
    }

    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    auto server_future = std::async(std::launch::async, [promise = std::move(port_promise), html_path]() mutable {
        const TestSocket probe_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        test_set_reuseaddr(probe_fd);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(probe_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(probe_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        const int port = ntohs(bound.sin_port);
        test_close_socket(probe_fd);
        promise.set_value(port);

        const std::string source =
            "importer LumiNet\n"
            "soit serveur_global = rien\n"
            "fonction page(req: Universel, rep: Universel) {\n"
            "  rep.envoyer_fichier(200, \"" + html_path.string() + "\")\n"
            "  serveur_global.arreter()\n"
            "}\n"
            "fonction principal() {\n"
            "  soit serveur = LumiNet.HTTP.Serveur()\n"
            "  serveur_global = serveur\n"
            "  serveur.OBTENIR(\"/\", page)\n"
            "  serveur.écouter(\"127.0.0.1\", " + std::to_string(port) + ")\n"
            "}\n";
        return execute_program_without_capture_with_error(source);
    });

    const int port = port_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const TestSocket client_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(test_socket_valid(client_fd));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::connect(client_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    ASSERT_GT(test_send(client_fd, request.data(), request.size(), 0), 0);
    std::string response;
    char buffer[4096];
    while (true)
    {
        const TestRecvSize received = test_recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0)
        {
            break;
        }
        response.append(buffer, buffer + received);
    }
    test_close_socket(client_fd);

    const auto [server_completed, server_error] = server_future.get();
    EXPECT_TRUE(server_completed) << server_error;
    EXPECT_NE(response.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: text/html; charset=utf-8"), std::string::npos);
    EXPECT_NE(response.find("<!doctype html><h1>bonjour</h1>"), std::string::npos);

    std::filesystem::remove(html_path);
}

TEST(InterpreterBuiltinModules, UsesAccurateLumiNetHttpStatusReasonPhrases)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    auto server_future = std::async(std::launch::async, [promise = std::move(port_promise)]() mutable {
        const TestSocket probe_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        test_set_reuseaddr(probe_fd);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(probe_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(probe_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        const int port = ntohs(bound.sin_port);
        test_close_socket(probe_fd);
        promise.set_value(port);

        const std::string source =
            "importer LumiNet\n"
            "soit serveur_global = rien\n"
            "fonction aller(req: Universel, rep: Universel) {\n"
            "  rep.rediriger(\"/cible\")\n"
            "  serveur_global.arreter()\n"
            "}\n"
            "fonction principal() {\n"
            "  soit serveur = LumiNet.HTTP.Serveur()\n"
            "  serveur_global = serveur\n"
            "  serveur.OBTENIR(\"/\", aller)\n"
            "  serveur.écouter(\"127.0.0.1\", " + std::to_string(port) + ")\n"
            "}\n";
        return execute_program_without_capture_with_error(source);
    });

    const int port = port_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const TestSocket client_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(test_socket_valid(client_fd));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::connect(client_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
    const std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Connection: close\r\n"
        "\r\n";
    ASSERT_GT(test_send(client_fd, request.data(), request.size(), 0), 0);
    std::string response;
    char buffer[4096];
    while (true)
    {
        const TestRecvSize received = test_recv(client_fd, buffer, sizeof(buffer), 0);
        if (received <= 0)
        {
            break;
        }
        response.append(buffer, buffer + received);
    }
    test_close_socket(client_fd);

    const auto [server_completed, server_error] = server_future.get();
    EXPECT_TRUE(server_completed) << server_error;
    EXPECT_NE(response.find("HTTP/1.1 302 Found"), std::string::npos);
    EXPECT_NE(response.find("Location: /cible"), std::string::npos);
}

TEST(InterpreterBuiltinModules, SupportsLumiNetCanalStandalone)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    const std::string server_source =
        "importer LumiNet\n"
        "soit serveur_global = rien\n"
        "fonction sur_connexion(client: Universel) {\n"
        "  client.envoyer(\"bonjour\")\n"
        "}\n"
        "fonction sur_message(client: Universel, message: Universel) {\n"
        "  client.envoyer(\"écho:\" + message)\n"
        "  client.fermer()\n"
        "  serveur_global.arreter()\n"
        "}\n"
        "fonction principal() {\n"
        "  soit serveur = LumiNet.Canal.Serveur()\n"
        "  serveur_global = serveur\n"
        "  serveur.quand_connexion(sur_connexion)\n"
        "  serveur.quand_message(sur_message)\n"
        "  serveur.écouter(\"127.0.0.1\", ";

    auto server_future = std::async(std::launch::async, [source_prefix = server_source, promise = std::move(port_promise)]() mutable {
        const TestSocket probe_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        test_set_reuseaddr(probe_fd);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(probe_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(probe_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        const int port = ntohs(bound.sin_port);
        test_close_socket(probe_fd);
        promise.set_value(port);
        return execute_program_without_capture_with_error(source_prefix + std::to_string(port) + ")\n}\n");
    });

    const int port = port_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction reçu(message: Universel) {\n"
        "  afficher(message)\n"
        "}\n"
        "fonction principal() {\n"
        "  soit canal = LumiNet.Canal.connecter(\"ws://127.0.0.1:" + std::to_string(port) + "/\")\n"
        "  canal.quand_message(reçu)\n"
        "  canal.envoyer(\"salut\")\n"
        "  canal.attendre()\n"
        "}\n");

    const auto [server_completed, server_error] = server_future.get();
    EXPECT_TRUE(completed) << error;
    EXPECT_TRUE(server_completed) << server_error;
    EXPECT_EQ(output, "bonjour\nécho:salut\n");
}

TEST(InterpreterBuiltinModules, SupportsLumiNetHttpCanalUpgrade)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    const std::string server_source =
        "importer LumiNet\n"
        "soit serveur_global = rien\n"
        "fonction sur_client(client: Universel) {\n"
        "  client.quand_message(fonction_relais)\n"
        "}\n"
        "fonction fonction_relais(message: Universel) {\n"
        "}\n";

    auto server_future = std::async(std::launch::async, [promise = std::move(port_promise)]() mutable {
        const TestSocket probe_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        test_set_reuseaddr(probe_fd);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(probe_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(probe_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        const int port = ntohs(bound.sin_port);
        test_close_socket(probe_fd);
        promise.set_value(port);
        const std::string source =
            "importer LumiNet\n"
            "soit serveur_global = rien\n"
            "fonction sur_client(client: Universel) {\n"
            "  client.envoyer(\"prêt\")\n"
            "  client.fermer()\n"
            "  serveur_global.arreter()\n"
            "}\n"
            "fonction principal() {\n"
            "  soit serveur = LumiNet.HTTP.Serveur()\n"
            "  serveur_global = serveur\n"
            "  serveur.canal(\"/chat\", sur_client)\n"
            "  serveur.écouter(\"127.0.0.1\", " + std::to_string(port) + ")\n"
            "}\n";
        return execute_program_without_capture_with_error(source);
    });

    const int port = port_future.get();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const TestSocket client_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(test_socket_valid(client_fd));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::connect(client_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
    const std::string request =
        "GET /chat HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    ASSERT_GT(test_send(client_fd, request.data(), request.size(), 0), 0);
    std::string response;
    char buffer[4096];
    while (response.find("\r\n\r\n") == std::string::npos)
    {
        const TestRecvSize received = test_recv(client_fd, buffer, sizeof(buffer), 0);
        ASSERT_GT(received, 0);
        response.append(buffer, buffer + received);
    }
    ASSERT_NE(response.find("101 Switching Protocols"), std::string::npos);
    const std::size_t header_end = response.find("\r\n\r\n") + 4;
    std::string leftover = response.substr(header_end);
    unsigned char header[2];
    if (leftover.size() >= 2)
    {
        header[0] = static_cast<unsigned char>(leftover[0]);
        header[1] = static_cast<unsigned char>(leftover[1]);
        leftover.erase(0, 2);
    }
    else
    {
        ASSERT_EQ(test_recv(client_fd, header, sizeof(header), 0), 2);
    }
    const std::size_t payload_size = header[1] & 0x7f;
    std::string payload = leftover;
    if (payload.size() < payload_size)
    {
        const std::size_t missing = payload_size - payload.size();
        const std::size_t previous = payload.size();
        payload.resize(payload_size);
        ASSERT_EQ(test_recv(client_fd, payload.data() + previous, missing, 0), static_cast<TestRecvSize>(missing));
    }
    else if (payload.size() > payload_size)
    {
        payload.resize(payload_size);
    }
    test_close_socket(client_fd);
    const auto [server_completed, server_error] = server_future.get();
    EXPECT_TRUE(server_completed) << server_error;
    EXPECT_EQ(payload, "prêt");
}

TEST(InterpreterBuiltinModules, RejectsLumiNetCanalHandshakeThatIsNotARealUpgrade)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    auto future = std::async(std::launch::async, [promise = std::move(port_promise)]() mutable -> std::string {
        const TestSocket server_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        if (!test_socket_valid(server_fd))
        {
            return "socket";
        }

        test_set_reuseaddr(server_fd);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            test_close_socket(server_fd);
            return "bind";
        }

        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(server_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        if (::listen(server_fd, 1) != 0)
        {
            test_close_socket(server_fd);
            return "listen";
        }
        promise.set_value(ntohs(bound.sin_port));
        if (!test_wait_until_readable(server_fd, std::chrono::seconds(5)))
        {
            test_close_socket(server_fd);
            return "accept timeout";
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const TestSocket client_fd = ::accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (!test_socket_valid(client_fd))
        {
            test_close_socket(server_fd);
            return "accept";
        }

        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos)
        {
            const TestRecvSize received = test_recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0)
            {
                break;
            }
            request.append(buffer, buffer + received);
        }

        const std::string response =
            "HTTP/1.1 200 OK\r\n"
            "X-Leurre: 101\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        test_send(client_fd, response.data(), response.size(), 0);
        test_close_socket(client_fd);
        test_close_socket(server_fd);
        return request;
    });

    const int port = port_future.get();
    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  LumiNet.Canal.connecter(\"ws://127.0.0.1:" + std::to_string(port) + "/\")\n"
        "}\n");

    const std::string request = future.get();
    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("poignée de main websocket refusée"), std::string::npos);
    EXPECT_NE(request.find("Upgrade: websocket"), std::string::npos);
}

TEST(InterpreterBuiltinModules, SupportsLumiNetCanalWithFragmentedFrameHeader)
{
    SKIP_IF_LUMINET_DISABLED();
    std::promise<int> port_promise;
    std::future<int> port_future = port_promise.get_future();
    auto future = std::async(std::launch::async, [promise = std::move(port_promise)]() mutable -> std::string {
        const TestSocket server_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
        if (!test_socket_valid(server_fd))
        {
            return "socket";
        }

        test_set_reuseaddr(server_fd);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            test_close_socket(server_fd);
            return "bind";
        }

        sockaddr_in bound{};
        socklen_t bound_len = sizeof(bound);
        ::getsockname(server_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len);
        if (::listen(server_fd, 1) != 0)
        {
            test_close_socket(server_fd);
            return "listen";
        }
        promise.set_value(ntohs(bound.sin_port));
        if (!test_wait_until_readable(server_fd, std::chrono::seconds(5)))
        {
            test_close_socket(server_fd);
            return "accept timeout";
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const TestSocket client_fd = ::accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (!test_socket_valid(client_fd))
        {
            test_close_socket(server_fd);
            return "accept";
        }

        std::string request;
        char buffer[4096];
        while (request.find("\r\n\r\n") == std::string::npos)
        {
            const TestRecvSize received = test_recv(client_fd, buffer, sizeof(buffer), 0);
            if (received <= 0)
            {
                test_close_socket(client_fd);
                test_close_socket(server_fd);
                return "recv";
            }
            request.append(buffer, buffer + received);
        }

        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
            "\r\n";
        test_send(client_fd, response.data(), response.size(), 0);

        const unsigned char first_header = 0x81;
        const unsigned char second_header = 0x07;
        const std::string payload = "bonjour";
        test_send(client_fd, &first_header, 1, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        test_send(client_fd, &second_header, 1, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        test_send(client_fd, payload.data(), payload.size(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const unsigned char close_frame[2] = {0x88, 0x00};
        test_send(client_fd, close_frame, sizeof(close_frame), 0);

        test_close_socket(client_fd);
        test_close_socket(server_fd);
        return "ok";
    });

    const int port = port_future.get();
    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction reçu(message: Universel) {\n"
        "  afficher(message)\n"
        "}\n"
        "fonction principal() {\n"
        "  soit canal = LumiNet.Canal.connecter(\"ws://127.0.0.1:" + std::to_string(port) + "/\")\n"
        "  canal.quand_message(reçu)\n"
        "  canal.attendre()\n"
        "}\n");

    EXPECT_EQ(future.get(), "ok");
    EXPECT_TRUE(completed) << error;
    EXPECT_EQ(output, "bonjour\n");
}

TEST(InterpreterBuiltinModules, SupportsMathsModule)
{
    const auto [output, completed] = execute_program(
        "importer Maths\n"
        "fonction principal() {\n"
        "  afficher(Maths.pi > 3)\n"
        "  afficher(Maths.absolu(-7))\n"
        "  afficher(Maths.absolu(-2.5))\n"
        "  afficher(Maths.min(4, 9))\n"
        "  afficher(Maths.max(1.5, 2))\n"
        "  afficher(Maths.arrondir(3.6))\n"
        "  afficher(Maths.plancher(3.9))\n"
        "  afficher(Maths.plafond(3.1))\n"
        "  afficher(Maths.tronquer(3.9))\n"
        "  afficher(Maths.racine(81))\n"
        "  afficher(Maths.racine_n(27, 3))\n"
        "  afficher(Maths.puissance(2, 5))\n"
        "  afficher(Maths.log(Maths.e))\n"
        "  afficher(Maths.log10(100))\n"
        "  afficher(Maths.log2(8))\n"
        "  afficher(Maths.sin(Maths.pi / 2))\n"
        "  afficher(Maths.cos(0))\n"
        "  afficher(Maths.tan(Maths.pi / 4))\n"
        "  afficher(Maths.atan2(1, 1) > 0)\n"
        "  afficher(Maths.degres_vers_radians(180))\n"
        "  afficher(Maths.radians_vers_degres(Maths.pi))\n"
        "  afficher(Maths.est_non_nombre(Maths.non_nombre))\n"
        "  afficher(Maths.est_infini(Maths.infini))\n"
        "  afficher(Maths.est_pair(4))\n"
        "  afficher(Maths.est_impair(3))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "vrai\n7\n2.5\n4\n2\n4\n3\n4\n3\n9\n3\n32\n1\n2\n3\n1\n1\n1\nvrai\n3.14159\n180\nvrai\nvrai\nvrai\nvrai\n");
}

TEST(InterpreterBuiltinModules, SupportsSelectiveImportFromMathsModule)
{
    const auto [output, completed] = execute_program(
        "importer Maths.{absolu, est_non_nombre, min, non_nombre, puissance comme pow}\n"
        "fonction principal() {\n"
        "  afficher(absolu(-8))\n"
        "  afficher(min(8, 3))\n"
        "  afficher(pow(3, 3))\n"
        "  afficher(est_non_nombre(non_nombre))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "8\n3\n27\nvrai\n");
}

TEST(InterpreterBuiltinModules, PreservesMathsAliasesForCompatibility)
{
    const auto [output, completed] = execute_program(
        "importer Maths\n"
        "fonction principal() {\n"
        "  afficher(Maths.abs(-6))\n"
        "  afficher(Maths.arrondi(2.6))\n"
        "  afficher(Maths.sinus(Maths.pi / 2))\n"
        "  afficher(Maths.cosinus(0))\n"
        "  afficher(Maths.tangente(Maths.pi / 4))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "6\n3\n1\n1\n1\n");
}

TEST(InterpreterBuiltinModules, RejectsInvalidMathsUsage)
{
    auto [output1, completed1, error1] = execute_program_with_error(
        "importer Maths\n"
        "fonction principal() {\n"
        "  afficher(Maths.racine(-1))\n"
        "}\n");

    EXPECT_FALSE(completed1);
    EXPECT_TRUE(output1.empty());
    EXPECT_NE(error1.find("Maths.racine"), std::string::npos);

    auto [output2, completed2, error2] = execute_program_with_error(
        "importer Maths.{min}\n"
        "fonction principal() {\n"
        "  afficher(min(1))\n"
        "}\n");

    EXPECT_FALSE(completed2);
    EXPECT_TRUE(output2.empty());
    EXPECT_NE(error2.find("Maths.min"), std::string::npos);

    auto [output3, completed3, error3] = execute_program_with_error(
        "importer Maths\n"
        "fonction principal() {\n"
        "  afficher(Maths.racine_n(16, 0))\n"
        "}\n");

    EXPECT_FALSE(completed3);
    EXPECT_TRUE(output3.empty());
    EXPECT_NE(error3.find("Maths.racine_n"), std::string::npos);
}

TEST(InterpreterStandardLibrary, SupportsTexteMethods)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit texte = \"  Bonjour Monde  \"\n"
        "  afficher(texte.elaguer())\n"
        "  afficher(texte.elaguer().majuscules())\n"
        "  afficher(\"LUMIERE\".minuscules())\n"
        "  afficher(\"bonjour\".est_vide())\n"
        "  afficher(\"bonjour\".contient(\"jour\"))\n"
        "  afficher(\"bonjour\".index_de(\"jour\"))\n"
        "  afficher(\"bonjour\".commence_par(\"bon\"))\n"
        "  afficher(\"bonjour\".finit_par(\"jour\"))\n"
        "  afficher(\"bonjour\".inverser())\n"
        "  afficher(\"ab\".repeter(3))\n"
        "  afficher(\"bonjour\".remplacer(\"jour\", \"soir\"))\n"
        "  afficher(\"aaa\".remplacer_tout(\"a\", \"b\"))\n"
        "  afficher(\"bonjour\".inserer(7, \" monde\"))\n"
        "  afficher(\"Bonjour, monde!\".supprimer(0, 8))\n"
        "  afficher(\"bonjour\".sous_texte(3, 4))\n"
        "  afficher(\"Bonjour, monde!\".sous_texte(9))\n"
        "  soit morceaux = \"a,b,c\".separer(\",\")\n"
        "  afficher(morceaux.taille())\n"
        "  afficher(morceaux[1])\n"
        "  soit lignes = \"a\\nb\\nc\".separer_lignes()\n"
        "  afficher(lignes.joindre(\"|\"))\n"
        "  afficher(\"42\".en_entier())\n"
        "  afficher(\"3.14\".en_decimal())\n"
        "  afficher(\"vrai\".en_logique())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Bonjour Monde\nBONJOUR MONDE\nlumiere\nfaux\nvrai\n3\nvrai\nvrai\nruojnob\nababab\nbonsoir\nbbb\nbonjour monde\n monde!\njour\nmonde!\n3\nb\na\\nb\\nc\n42\n3.14\nvrai\n");
}

TEST(InterpreterStandardLibrary, SupportsTexteModuleHelpers)
{
    const auto [output, completed] = execute_program(
        "importer Texte\n"
        "fonction principal() {\n"
        "  soit t = \"  Bonjour, monde!  \"\n"
        "  afficher(Texte.taille(t))\n"
        "  afficher(Texte.est_vide(\"\"))\n"
        "  afficher(Texte.contient(t, \"monde\"))\n"
        "  afficher(Texte.index_de(t, \"monde\"))\n"
        "  afficher(Texte.commence_par(\"Bonjour\", \"Bon\"))\n"
        "  afficher(Texte.finit_par(\"Bonjour!\", \"!\"))\n"
        "  afficher(Texte.elaguer(t))\n"
        "  afficher(Texte.minuscules(\"BONJOUR\"))\n"
        "  afficher(Texte.majuscules(\"bonjour\"))\n"
        "  afficher(Texte.separer(\"a,b,c\", \",\").joindre(\"|\"))\n"
        "  afficher(Texte.separer_lignes(\"a\\nb\").joindre(\"|\"))\n"
        "  afficher(Texte.remplacer(\"bonjour monde\", \"monde\", \"lumiere\"))\n"
        "  afficher(Texte.joindre([\"bonjour\", \"monde\"], \", \"))\n"
        "  afficher(Texte.convertir_entier(42))\n"
        "  afficher(Texte.convertir_decimal(3.14))\n"
        "  afficher(Texte.convertir_logique(vrai))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "19\nvrai\nvrai\n11\nvrai\nvrai\nBonjour, monde!\nbonjour\nBONJOUR\na|b|c\na\\nb\nbonjour lumiere\nbonjour, monde\n42\n3.14\nvrai\n");
}

TEST(InterpreterStandardLibrary, CoversTexteBoundaryCasesComprehensively)
{
    const auto [output, completed] = execute_program(
        "importer Texte\n"
        "fonction principal() {\n"
        "  afficher(\"\".taille())\n"
        "  afficher(\"\".est_vide())\n"
        "  afficher(\"abc\".index_de(\"z\"))\n"
        "  afficher(\"abc\".contient(\"\"))\n"
        "  afficher(\"abc\".commence_par(\"\"))\n"
        "  afficher(\"abc\".finit_par(\"\"))\n"
        "  afficher(\"abc\".repeter(0))\n"
        "  afficher(\"abc\".inserer(0, \"-\"))\n"
        "  afficher(\"abc\".inserer(3, \"-\"))\n"
        "  afficher(\"abc\".supprimer(0, 0))\n"
        "  afficher(\"abc\".supprimer(1, 50))\n"
        "  afficher(\"abc\".sous_texte(0))\n"
        "  afficher(\"abc\".sous_texte(3))\n"
        "  afficher(\"abc\".sous_texte(0, 0))\n"
        "  afficher(\"   \".elaguer())\n"
        "  afficher(\"   abc\".elaguer_gauche())\n"
        "  afficher(\"abc   \".elaguer_droite())\n"
        "  afficher(\"aaaa\".remplacer(\"aa\", \"b\"))\n"
        "  afficher(\"aaaa\".remplacer_tout(\"aa\", \"b\"))\n"
        "  afficher(\"abc\".separer(\",\").taille())\n"
        "  afficher(\"abc,\".separer(\",\").taille())\n"
        "  afficher(\"\".separer(\",\").taille())\n"
        "  afficher(Texte.joindre([], \",\"))\n"
        "  afficher(Texte.convertir_entier(-42))\n"
        "  afficher(Texte.convertir_decimal(2))\n"
        "  afficher(Texte.convertir_logique(faux))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "0\nvrai\n-1\nvrai\nvrai\nvrai\n\n-abc\nabc-\nabc\na\nabc\n\n\n\nabc\nabc\nbb\nbb\n1\n2\n1\n\n-42\n2\nfaux\n");
}

TEST(InterpreterStandardLibrary, KeepsTexteMethodAndModuleFormsConsistent)
{
    const auto [output, completed] = execute_program(
        "importer Texte\n"
        "fonction principal() {\n"
        "  soit t = \"  Abc,Def  \"\n"
        "  afficher(t.elaguer() == Texte.elaguer(t))\n"
        "  afficher(t.contient(\",\") == Texte.contient(t, \",\"))\n"
        "  afficher(t.index_de(\"Def\") == Texte.index_de(t, \"Def\"))\n"
        "  afficher(t.commence_par(\"  A\") == Texte.commence_par(t, \"  A\"))\n"
        "  afficher(t.finit_par(\"  \") == Texte.finit_par(t, \"  \"))\n"
        "  afficher(t.minuscules() == Texte.minuscules(t))\n"
        "  afficher(t.majuscules() == Texte.majuscules(t))\n"
        "  afficher(t.separer(\",\").taille() == Texte.separer(t, \",\").taille())\n"
        "  afficher(t.separer_lignes().taille() == Texte.separer_lignes(t).taille())\n"
        "  afficher(t.remplacer(\"Def\", \"XYZ\") == Texte.remplacer(t, \"Def\", \"XYZ\"))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "vrai\nvrai\nvrai\nvrai\nvrai\nvrai\nvrai\nvrai\nvrai\nvrai\n");
}

TEST(InterpreterStandardLibrary, SupportsListeAndDictionnaireMethods)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit xs = [1, 2]\n"
        "  afficher(xs.taille())\n"
        "  afficher(xs.vide())\n"
        "  afficher(xs.contient(2))\n"
        "  afficher(xs.ajouter(3))\n"
        "  afficher(xs.joindre(\"-\"))\n"
        "  xs.inserer(1, 99)\n"
        "  afficher(xs.joindre(\",\"))\n"
        "  afficher(xs.retirer_a(1))\n"
        "  afficher(xs.joindre(\",\"))\n"
        "  soit d = {\"a\": 1, \"b\": 2}\n"
        "  afficher(d.taille())\n"
        "  afficher(d.contient(\"a\"))\n"
        "  afficher(d.cles().joindre(\"|\"))\n"
        "  afficher(d.valeurs().joindre(\"|\"))\n"
        "  afficher(d.paires().taille())\n"
        "  afficher(d.paires()[0].taille())\n"
        "  afficher(d.retirer(\"a\"))\n"
        "  afficher(d.taille())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "2\nfaux\nvrai\n3\n1-2-3\n1,99,2,3\n99\n1,2,3\n2\nvrai\na|b\n1|2\n2\n2\n1\n1\n");
}

TEST(InterpreterStandardLibrary, SupportsListeFixeMethods)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit xs = ListeFixe.remplir(Entier, 3, 7)\n"
        "  afficher(xs.taille())\n"
        "  afficher(xs.vide())\n"
        "  afficher(xs.contient(7))\n"
        "  afficher(xs.joindre(\"-\"))\n"
        "  soit dyn = xs.en_liste()\n"
        "  afficher(dyn.joindre(\",\"))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "3\nfaux\nvrai\n7-7-7\n7,7,7\n");
}

TEST(InterpreterStandardLibrary, RejectsInvalidMethodArguments)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\".sous_texte())\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Texte.sous_texte"), std::string::npos);
}

TEST(InterpreterStandardLibrary, RejectsInvalidTexteOperations)
{
    auto [output1, completed1, error1] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\".repeter(-1))\n"
        "}\n");

    EXPECT_FALSE(completed1);
    EXPECT_TRUE(output1.empty());
    EXPECT_NE(error1.find("Texte.repeter"), std::string::npos);

    auto [output2, completed2, error2] = execute_program_with_error(
        "importer Texte\n"
        "fonction principal() {\n"
        "  afficher(Texte.separer(\"abc\", \"\"))\n"
        "}\n");

    EXPECT_FALSE(completed2);
    EXPECT_TRUE(output2.empty());
    EXPECT_NE(error2.find("Texte.separer"), std::string::npos);

    auto [output3, completed3, error3] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"peut-etre\".en_logique())\n"
        "}\n");

    EXPECT_FALSE(completed3);
    EXPECT_TRUE(output3.empty());
    EXPECT_NE(error3.find("Texte.en_logique"), std::string::npos);

    auto [output4, completed4, error4] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\".inserer(4, \"x\"))\n"
        "}\n");

    EXPECT_FALSE(completed4);
    EXPECT_TRUE(output4.empty());
    EXPECT_NE(error4.find("position d'insertion hors limites"), std::string::npos);

    auto [output5, completed5, error5] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\".supprimer(-1, 1))\n"
        "}\n");

    EXPECT_FALSE(completed5);
    EXPECT_TRUE(output5.empty());
    EXPECT_NE(error5.find("suppression hors limites"), std::string::npos);

    auto [output6, completed6, error6] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\".sous_texte(-1))\n"
        "}\n");

    EXPECT_FALSE(completed6);
    EXPECT_TRUE(output6.empty());
    EXPECT_NE(error6.find("indice de debut hors limites"), std::string::npos);

    auto [output7, completed7, error7] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\".sous_texte(0, -1))\n"
        "}\n");

    EXPECT_FALSE(completed7);
    EXPECT_TRUE(output7.empty());
    EXPECT_NE(error7.find("longueur negative interdite"), std::string::npos);

    auto [output8, completed8, error8] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\".remplacer(\"\", \"x\"))\n"
        "}\n");

    EXPECT_FALSE(completed8);
    EXPECT_TRUE(output8.empty());
    EXPECT_NE(error8.find("Texte.remplacer"), std::string::npos);

    auto [output9, completed9, error9] = execute_program_with_error(
        "fonction principal() {\n"
        "  afficher(\"abc\".remplacer_tout(\"\", \"x\"))\n"
        "}\n");

    EXPECT_FALSE(completed9);
    EXPECT_TRUE(output9.empty());
    EXPECT_NE(error9.find("Texte.remplacer_tout"), std::string::npos);
}

TEST(InterpreterStandardLibrary, RejectsRemovedTexteAliases)
{
    const struct
    {
        std::string source;
        std::string expected_fragment;
    } cases[] = {
        {
            "fonction principal() {\n"
            "  afficher(\"  x  \".rogner())\n"
            "}\n",
            "rogner"
        },
        {
            "fonction principal() {\n"
            "  afficher(\"HELLO\".minuscule())\n"
            "}\n",
            "minuscule"
        },
        {
            "fonction principal() {\n"
            "  afficher(\"hello\".majuscule())\n"
            "}\n",
            "majuscule"
        },
        {
            "fonction principal() {\n"
            "  afficher(\"a,b\".decouper(\",\").taille())\n"
            "}\n",
            "decouper"
        },
        {
            "fonction principal() {\n"
            "  afficher(\"\".vide())\n"
            "}\n",
            "vide"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.depuis_entier(42))\n"
            "}\n",
            "depuis_entier"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.depuis_decimal(3.14))\n"
            "}\n",
            "depuis_decimal"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.depuis_logique(vrai))\n"
            "}\n",
            "depuis_logique"
        },
    };

    for (const auto &test_case : cases)
    {
        const auto [output, completed, error] = execute_program_with_error(test_case.source);
        EXPECT_FALSE(completed) << test_case.source;
        EXPECT_TRUE(output.empty()) << test_case.source;
        EXPECT_NE(error.find(test_case.expected_fragment), std::string::npos) << test_case.source;
    }
}

TEST(InterpreterStandardLibrary, RejectsInvalidTexteModuleUsageComprehensively)
{
    const struct
    {
        std::string source;
        std::string expected_fragment;
    } cases[] = {
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.taille(1))\n"
            "}\n",
            "Texte.taille attend une valeur de type Texte"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.contient(\"abc\", valeur: \"a\"))\n"
            "}\n",
            "n'accepte pas d'arguments nommes"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.joindre(\"abc\", \",\"))\n"
            "}\n",
            "Texte.joindre attend une Liste"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.joindre([\"a\", 1], \",\"))\n"
            "}\n",
            "Texte.joindre attend une valeur de type Texte"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.convertir_entier(\"42\"))\n"
            "}\n",
            "Texte.convertir_entier attend une valeur de type Entier"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.convertir_decimal(\"3.14\"))\n"
            "}\n",
            "Texte.convertir_decimal attend une valeur numerique"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.convertir_logique(\"vrai\"))\n"
            "}\n",
            "Texte.convertir_logique"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.remplacer(\"abc\", \"\", \"x\"))\n"
            "}\n",
            "Texte.remplacer attend une cible non vide"
        },
        {
            "importer Texte\n"
            "fonction principal() {\n"
            "  afficher(Texte.separer(\"abc\", \"\"))\n"
            "}\n",
            "Texte.separer attend un separateur non vide"
        },
    };

    for (const auto &test_case : cases)
    {
        const auto [output, completed, error] = execute_program_with_error(test_case.source);
        EXPECT_FALSE(completed) << test_case.source;
        EXPECT_TRUE(output.empty()) << test_case.source;
        EXPECT_NE(error.find(test_case.expected_fragment), std::string::npos) << test_case.source;
    }
}

TEST(InterpreterStandardIO, SupportsAfficherInlineAndReadBuiltins)
{
    const auto [output, completed] = execute_program_with_input(
        "fonction principal() {\n"
        "  afficher_inline(\"Nom: \")\n"
        "  soit nom = lire()\n"
        "  soit age = lire_entier()\n"
        "  soit taille = lire_décimal()\n"
        "  soit actif = lire_logique()\n"
        "  afficher(nom)\n"
        "  afficher(age)\n"
        "  afficher(taille)\n"
        "  afficher(actif)\n"
        "}\n",
        "Ada\n36\n1.75\nvrai\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Nom: Ada\n36\n1.75\nvrai\n");
}

TEST(InterpreterStandardIO, RejectsInvalidReadInputsAndArguments)
{
    auto [output1, completed1, error1] = execute_program_with_error(
        "fonction principal() {\n"
        "  lire(\"x\")\n"
        "}\n");

    EXPECT_FALSE(completed1);
    EXPECT_TRUE(output1.empty());
    EXPECT_NE(error1.find("lire n'accepte pas d'arguments"), std::string::npos);

    Lexer lexer(
        "fonction principal() {\n"
        "  afficher(lire_logique())\n"
        "}\n");
    Parser parser(lexer.tokenise());

    Program program;
    program.statements = parser.parse();
    program.source_path = "<test>";
    program.source_text = "fonction principal() { afficher(lire_logique()) }";

    TreeWalker walker;
    std::ostringstream captured;
    std::istringstream provided_input("peut-etre\n");
    auto *previous_output = std::cout.rdbuf(captured.rdbuf());
    auto *previous_input = std::cin.rdbuf(provided_input.rdbuf());
    bool completed2 = true;
    std::string error2;

    try
    {
        walker.execute(program);
    }
    catch (const RuntimeError &err)
    {
        completed2 = false;
        error2 = err.what();
    }
    catch (...)
    {
        completed2 = false;
        error2 = "unknown error";
    }

    std::cin.rdbuf(previous_input);
    std::cout.rdbuf(previous_output);

    EXPECT_FALSE(completed2);
    EXPECT_TRUE(captured.str().empty());
    EXPECT_NE(error2.find("lire_logique"), std::string::npos);
}

TEST(InterpreterFunctions, SupportsDefaultParameters)
{
    const auto [output, completed] = execute_program(
        "fonction saluer(nom: Texte = \"Ada\") {\n"
        "  afficher(nom)\n"
        "}\n"
        "fonction principal() {\n"
        "  saluer()\n"
        "  saluer(\"Grace\")\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Ada\nGrace\n");
}

TEST(InterpreterFunctions, RejectsMissingRequiredArgument)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction saluer(nom: Texte) {\n"
        "  afficher(nom)\n"
        "}\n"
        "fonction principal() {\n"
        "  saluer()\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("argument manquant"), std::string::npos);
}

TEST(InterpreterFunctions, RejectsMismatchedNamedArgument)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction saluer(nom: Texte) {\n"
        "  afficher(nom)\n"
        "}\n"
        "fonction principal() {\n"
        "  saluer(prenom: \"Ada\")\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("aucun parametre nomme"), std::string::npos);
}

TEST(InterpreterFunctions, RejectsTooManyArguments)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction identite(x: Entier) {\n"
        "  retourne x\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(identite(1, 2))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("trop d'arguments"), std::string::npos);
}

TEST(InterpreterFunctions, ReturnsRienWhenNoExplicitReturn)
{
    const auto [output, completed] = execute_program(
        "fonction f() {\n"
        "  afficher(\"dedans\")\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(f())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "dedans\nrien\n");
}

TEST(InterpreterFunctions, DefaultParametersCanReferenceEarlierParameters)
{
    const auto [output, completed] = execute_program(
        "fonction somme(a: Entier, b: Entier = a + 1) {\n"
        "  retourne a + b\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(somme(4))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "9\n");
}

TEST(InterpreterFunctions, ProvidedArgumentSkipsDefaultEvaluation)
{
    const auto [output, completed] = execute_program(
        "fonction identite(x: Universel = inconnu) {\n"
        "  retourne x\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(identite(7))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "7\n");
}

TEST(InterpreterFunctions, UsesCallerScopeWhenEvaluatingArguments)
{
    const auto [output, completed] = execute_program(
        "fonction afficher_nom(nom: Texte) {\n"
        "  afficher(nom)\n"
        "}\n"
        "fonction principal() {\n"
        "  soit nom = \"Ada\"\n"
        "  afficher_nom(nom)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Ada\n");
}

TEST(InterpreterFunctions, UsesCallerScopeForEachArgumentBeforeBindingParameters)
{
    const auto [output, completed] = execute_program(
        "fonction paire(a: Entier, b: Entier) {\n"
        "  afficher(a)\n"
        "  afficher(b)\n"
        "}\n"
        "fonction principal() {\n"
        "  soit base = 4\n"
        "  paire(base, base + 1)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "4\n5\n");
}

TEST(InterpreterFunctions, SupportsNamedArgumentsOutOfOrder)
{
    const auto [output, completed] = execute_program(
        "fonction coordonnees(x: Entier, y: Entier, etiquette: Texte = \"pt\") {\n"
        "  afficher(etiquette + \":\" + x + \",\" + y)\n"
        "}\n"
        "fonction principal() {\n"
        "  coordonnees(y: 9, x: 4, etiquette: \"A\")\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "A:4,9\n");
}

TEST(InterpreterFunctions, RejectsDuplicateNamedArguments)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction coordonnees(x: Entier, y: Entier) {\n"
        "  afficher(x)\n"
        "  afficher(y)\n"
        "}\n"
        "fonction principal() {\n"
        "  coordonnees(x: 1, y: 2, x: 3)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("fourni plusieurs fois"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesAnnotatedVariableAssignment)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit total: Entier = 3\n"
        "  total = \"oops\"\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("variable 'total'"), std::string::npos);
    EXPECT_NE(error.find("Entier"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesGenericListAnnotations)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Liste[Entier] = [1, 2, \"oops\"]\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Liste[Entier]"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesGenericDictionaryAnnotations)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Dictionnaire[Texte, Entier] = {\"ada\": 12, \"grace\": \"oops\"}\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Dictionnaire[Texte, Entier]"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesNestedGenericAnnotations)
{
    const auto [output, completed] = execute_program(
        "fonction principal() {\n"
        "  soit groupes: Liste[Liste[Entier]] = [[1, 2], [3, 4]]\n"
        "  afficher(groupes.taille())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "2\n");
}

TEST(InterpreterFunctions, SupportsFixedListConversionAndPreservesElementType)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Liste[Entier] = [1, 2, 3]\n"
        "  soit fixe trio: ListeFixe[Entier, 3] = notes.en_liste_fixe(3)\n"
        "  afficher(trio.taille())\n"
        "  trio[1] = 9\n"
        "  afficher(trio[1])\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(output, "3\n9\n");
}

TEST(InterpreterFunctions, SupportsFixedListFactoryAndIteration)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit zeros = ListeFixe.remplir(Entier, 3, 0)\n"
        "  pour chaque valeur dans zeros {\n"
        "    afficher_inline(valeur)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(output, "000");
}

TEST(InterpreterFunctions, RejectsFixedListLengthMismatchDuringConversion)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Liste[Entier] = [1, 2]\n"
        "  soit fixe trio = notes.en_liste_fixe(3)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Liste.en_liste_fixe"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesFixedListElementTypeOnAssignment)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Liste[Entier] = [1, 2, 3]\n"
        "  soit fixe trio = notes.en_liste_fixe(3)\n"
        "  trio[0] = \"oops\"\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("liste fixe"), std::string::npos);
    EXPECT_NE(error.find("Entier"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesFixedListFactoryElementType)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit zeros = ListeFixe.remplir(Entier, 2, \"oops\")\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("ListeFixe.remplir"), std::string::npos);
    EXPECT_NE(error.find("Entier"), std::string::npos);
}

TEST(InterpreterFunctions, ConvertsFixedListBackToDynamicListWithTypeMetadata)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit zeros = ListeFixe.remplir(Entier, 2, 0)\n"
        "  soit valeurs = zeros.en_liste()\n"
        "  valeurs.ajouter(\"oops\")\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Liste.ajouter"), std::string::npos);
    EXPECT_NE(error.find("Entier"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesGenericListMutationsAfterDeclaration)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Liste[Entier] = [1, 2]\n"
        "  notes.ajouter(\"oops\")\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Liste.ajouter"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesGenericDictionaryMutationsAfterDeclaration)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Dictionnaire[Texte, Entier] = {\"ada\": 12}\n"
        "  notes[\"grace\"] = \"oops\"\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("dictionnaire"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesNestedGenericMutationsThroughAliases)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit groupes: Liste[Liste[Entier]] = [[1, 2], [3, 4]]\n"
        "  soit premier = groupes[0]\n"
        "  premier.ajouter(\"oops\")\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Liste.ajouter"), std::string::npos);
    EXPECT_NE(error.find("Entier"), std::string::npos);
}

TEST(InterpreterFunctions, PreservesGenericTypeThroughTexteSeparer)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit morceaux = \"a,b\".separer(\",\")\n"
        "  morceaux.ajouter(3)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Liste.ajouter"), std::string::npos);
    EXPECT_NE(error.find("Texte"), std::string::npos);
}

TEST(InterpreterFunctions, PreservesGenericTypeThroughDictionnaireCles)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Dictionnaire[Texte, Entier] = {\"ada\": 1}\n"
        "  soit cles = notes.cles()\n"
        "  cles.ajouter(7)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Liste.ajouter"), std::string::npos);
    EXPECT_NE(error.find("Texte"), std::string::npos);
}

TEST(InterpreterFunctions, PreservesGenericTypeThroughDictionnaireValeurs)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Dictionnaire[Texte, Entier] = {\"ada\": 1}\n"
        "  soit valeurs = notes.valeurs()\n"
        "  valeurs.ajouter(\"oops\")\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Liste.ajouter"), std::string::npos);
    EXPECT_NE(error.find("Entier"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesParameterTypes)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction doubler(n: Entier) {\n"
        "  retourne n * 2\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(doubler(\"deux\"))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("parametre 'n'"), std::string::npos);
    EXPECT_NE(error.find("Entier"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesReturnTypes)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction nom() -> Texte {\n"
        "  retourne 42\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(nom())\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("fonction 'nom'"), std::string::npos);
    EXPECT_NE(error.find("Texte"), std::string::npos);
}

TEST(InterpreterFunctions, EnforcesImplicitReturnTypes)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction nom() -> Texte {\n"
        "  afficher(\"trace\")\n"
        "}\n"
        "fonction principal() {\n"
        "  nom()\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_EQ(output, "trace\n");
    EXPECT_NE(error.find("fonction 'nom'"), std::string::npos);
    EXPECT_NE(error.find("Texte"), std::string::npos);
}

} // namespace
