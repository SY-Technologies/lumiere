#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
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
#include <fcntl.h>
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

#include "luminet_shared.hpp"
#if LUMIERE_ENABLE_LUMIDESSIN_WINDOW
// Only the LumiDessin window smoke tests need this -- see their own
// comments for why the test process itself pushes a real SDL event.
#include <SDL3/SDL.h>
#endif
#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"

namespace
{

std::mutex g_stdio_capture_mutex;

#if LUMIERE_ENABLE_LUMIDESSIN_WINDOW
// setenv isn't available on MSVC; _putenv_s is its Windows equivalent but
// always overwrites, so an existing value is checked first to match
// setenv's "third argument 0" no-overwrite behaviour the call sites rely
// on (a CI environment that already set SDL_VIDEODRIVER is respected).
void set_env_if_unset(const char *name, const char *value)
{
#ifdef _WIN32
    if (std::getenv(name) == nullptr)
    {
        _putenv_s(name, value);
    }
#else
    setenv(name, value, 0);
#endif
}
#endif

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

bool test_tcp_binding_available()
{
    const TestSocket socket = test_open_socket(AF_INET, SOCK_STREAM, 0);
    if (!test_socket_valid(socket))
    {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(0);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool available = ::bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    test_close_socket(socket);
    return available;
}

#define SKIP_IF_TCP_BINDING_UNAVAILABLE()                                                \
    do                                                                                   \
    {                                                                                    \
        if (!test_tcp_binding_available())                                               \
        {                                                                                \
            GTEST_SKIP() << "local TCP binding is unavailable in this environment";      \
        }                                                                                \
    } while (false)


std::string trim_trailing_whitespace(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
    {
        text.pop_back();
    }
    return text;
}



std::string normalize_path_text(const std::filesystem::path &path)
{
    return path.generic_string();
}

// A path embedded in Lumière *source* text has to escape the Lumière
// lexer's own \n/\t/\\/\" syntax: on Windows, path.string() contains
// raw backslashes ("C:\Users\...") that the lexer otherwise tries to
// parse as escape sequences (producing "échappement invalide" errors).
std::string lumiere_string_literal_text(const std::string &raw)
{
    std::string escaped;
    escaped.reserve(raw.size());
    for (const char ch : raw)
    {
        if (ch == '\\' || ch == '"')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
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

std::tuple<std::string, bool, std::string> execute_program_with_input_and_error(const std::string &source, const std::string &input)
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
        throw std::runtime_error("unexpected parse failure in execute_program_with_input_and_error");
    }

    TreeWalker walker;

    std::ostringstream captured;
    std::istringstream provided_input(input);
    auto *previous_output = std::cout.rdbuf(captured.rdbuf());
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

    std::cin.rdbuf(previous_input);
    std::cout.rdbuf(previous_output);
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

// The fixture corpus under tests/fixtures/interpreter used to be replayed here,
// through Lexer -> Parser -> TreeWalker. That pipeline skips the analyzer and
// only ever ran one engine, so the expectations drifted onto a path no user
// takes: three of them pinned a runtime message for a program the CLI rejects
// statically, with a better diagnostic. scripts/conformance owns the corpus now
// -- it runs each case through the real CLI under both engines and checks that
// they agree, which is the property a single-engine replay cannot check. See the
// `conformance` test registered in CMakeLists.txt.

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
    EXPECT_NE(error.find("déjà déclaré"), std::string::npos);
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
    EXPECT_NE(error.find("clé introuvable"), std::string::npos);
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
    EXPECT_NE(error.find("n'est pas itérable"), std::string::npos);
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
    EXPECT_EQ(output, "42.0\n123\nvrai\n65\nB\n9\n");
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
    EXPECT_NE(error.find("même signature"), std::string::npos);
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
    EXPECT_NE(error.find("champ privé"), std::string::npos);
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
    EXPECT_NE(error.find("méthode privée"), std::string::npos);
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
    EXPECT_NE(error.find("membre introuvable"), std::string::npos);
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
    EXPECT_NE(error.find("champ privé"), std::string::npos);
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
    EXPECT_NE(error.find("ne peut pas être privée"), std::string::npos);
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
    EXPECT_NE(error.find("remplace utilise sans méthode parente"), std::string::npos);
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
    EXPECT_NE(error.find("membre non exporté"), std::string::npos);
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
    EXPECT_NE(error.find("déjà déclaré"), std::string::npos);
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
    EXPECT_NE(error.find("déjà déclaré"), std::string::npos);
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
    EXPECT_NE(error.find("membre non exporté"), std::string::npos);
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
        "  soit chemin = joindre(\"" + lumiere_string_literal_text(import_root.string()) + "\", \"note.txt\")\n"
        "  afficher(existe(chemin) ou propager)\n"
        "  afficher(nom(chemin))\n"
        "  afficher(nom_sans_extension(chemin))\n"
        "  afficher(dossier(chemin))\n"
        "  afficher(lire_texte(chemin) ou propager)\n"
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
        "  soit dossier = \"" + lumiere_string_literal_text((import_root / "crees" / "nested").string()) + "\"\n"
        "  soit texte = \"" + lumiere_string_literal_text((import_root / "sortie.txt").string()) + "\"\n"
        "  soit source = \"" + lumiere_string_literal_text(source_file.string()) + "\"\n"
        "  soit liste = \"" + lumiere_string_literal_text((import_root / "liste").string()) + "\"\n"
        "  ignorer creer_dossiers(dossier)\n"
        "  ignorer ecrire_texte(texte, \"alpha\")\n"
        "  ignorer ajouter_texte(texte, \"-beta\")\n"
        "  afficher(existe(dossier) ou propager)\n"
        "  afficher(est_dossier(dossier) ou propager)\n"
        "  afficher(est_fichier(texte) ou propager)\n"
        "  afficher(lire_texte(texte) ou propager)\n"
        "  soit lignes = lire_lignes(source) ou propager\n"
        "  afficher(lignes.taille())\n"
        "  afficher(lignes[0])\n"
        "  afficher(lignes[1])\n"
        "  afficher(taille(texte) ou propager)\n"
        "  afficher(modifie_le(texte) ou propager)\n"
        "  soit elements = lister(liste) ou propager\n"
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
        "  soit source = \"" + lumiere_string_literal_text((import_root / "source.txt").string()) + "\"\n"
        "  soit copie = \"" + lumiere_string_literal_text((import_root / "copie.txt").string()) + "\"\n"
        "  soit deplace = \"" + lumiere_string_literal_text((import_root / "deplace.txt").string()) + "\"\n"
        "  ignorer ecrire_lignes(source, [\"un\", \"deux\", \"trois\"])\n"
        "  ignorer copier(source, copie)\n"
        "  ignorer deplacer(copie, deplace)\n"
        "  afficher(lire_texte(source) ou propager)\n"
        "  soit lignes = lire_lignes(deplace) ou propager\n"
        "  afficher(lignes.taille())\n"
        "  afficher(lignes[2])\n"
        "  afficher(existe(copie) ou propager)\n"
        "  afficher(existe(deplace) ou propager)\n"
        "  ignorer supprimer(source)\n"
        "  afficher(existe(source) ou propager)\n"
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
        "  soit arbre = \"" + lumiere_string_literal_text((import_root / "arbre").string()) + "\"\n"
        "  soit vide = \"" + lumiere_string_literal_text((import_root / "vide").string()) + "\"\n"
        "  soit elements = lister_recursif(arbre) ou propager\n"
        "  afficher(elements.taille())\n"
        "  afficher(elements[0])\n"
        "  afficher(elements[1])\n"
        "  afficher(elements[2])\n"
        "  afficher(elements[3])\n"
        "  ignorer supprimer_dossier(vide)\n"
        "  afficher(existe(vide) ou propager)\n"
        "  ignorer supprimer_arbre(arbre)\n"
        "  afficher(existe(arbre) ou propager)\n"
        "  afficher(est_dossier(arbre) ou propager)\n"
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

TEST(InterpreterBuiltinModules, RejectsAnEmptyCheminJoindreSegmentInsteadOfLeavingAStrayTrailingSeparator)
{
    // A trailing empty segment used to be silently accepted: joindre("a",
    // "b", "") returned "a/b/" (a stray trailing separator, via
    // std::filesystem::path::operator/= appending "" as a real component)
    // instead of being rejected or ignored.
    const auto [output, completed, error] = execute_program_with_error(
        "importer Chemin.{joindre}\n"
        "fonction principal() {\n"
        "  afficher(joindre(\"a\", \"b\", \"\"))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Chemin.joindre n'accepte pas de segment vide"), std::string::npos);
}

TEST(InterpreterBuiltinModules, ExposesCheminSeparateurAsForwardSlashRegardlessOfPlatform)
{
    // Chemin's other outputs are all normalized to forward-slash form (see
    // path_to_text in chemin.cpp); separateur used to expose the
    // platform-native separator instead, which is "\" on Windows and
    // contradicts every other value this module produces.
    const auto [output, completed] = execute_program(
        "importer Chemin.{separateur}\n"
        "fonction principal() {\n"
        "  afficher(separateur)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "/\n");
}

TEST(InterpreterBuiltinModules, RejectsANegativeBaseToANonIntegerPower)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Maths\n"
        "fonction principal() {\n"
        "  afficher(Maths.puissance(-1.0, 0.5))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find(
                  "Maths.puissance ne peut pas élever une valeur négative à une puissance non entière"),
              std::string::npos);
}

TEST(InterpreterBuiltinModules, ComputesExactIntegerPowersInsteadOfLosingPrecisionThroughDouble)
{
    // Maths.puissance's general path converts both operands to double and
    // calls std::pow. For Entier operands whose true result exceeds 2^53,
    // that loses precision -- confirmed against this machine's libm:
    // pow(3.0, 34.0) rounds to 16677181699666570.0, while the true value
    // 3**34 = 16677181699666569 is only 16677181699666568.0 once rounded to
    // the nearest double. When both operands are Entier with a
    // non-negative exponent, the result is computed exactly with int64
    // repeated squaring instead, and only converted to Decimal at the end.
    const auto [output, completed] = execute_program(
        "importer Maths\n"
        "fonction principal() {\n"
        "  afficher(Maths.puissance(3, 34))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "16677181699666568.0\n");
}

TEST(InterpreterBuiltinModules, RejectsAnActualDotDotPathSegment)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Fichier\n"
        "fonction principal() {\n"
        "  afficher(Fichier.existe(\"../en-dehors\"))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Fichier.existe rejette les chemins contenant '..'"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsReadingMissingFile)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Fichier.{lire_texte}\n"
        "fonction principal() {\n"
        "  afficher(lire_texte(\"/definitivement/introuvable.txt\") ou propager)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("principal a échoué"), std::string::npos);
}

TEST(InterpreterBuiltinModules, ReportsADiskFullWriteFailureInsteadOfSilentlySucceeding)
{
    // /dev/full is a standard POSIX/Linux device that accepts any write and
    // always reports it as failed with ENOSPC, and is the deterministic
    // stand-in this codebase's own tooling can't otherwise construct for "the
    // disk is full": std::ofstream's internal buffer can accept `<<` without
    // complaint and only discover the failure once that buffer is actually
    // flushed, which previously happened only in the stream's destructor,
    // after ecrire_texte had already returned success.
    if (!std::filesystem::exists("/dev/full"))
    {
        GTEST_SKIP() << "/dev/full is not available on this platform";
    }

    const auto [output, completed, error] = execute_program_with_error(
        "importer Fichier.{ecrire_texte}\n"
        "fonction principal() {\n"
        "  ecrire_texte(\"/dev/full\", \"un contenu assez long pour forcer un vidage du tampon\") ou propager\n"
        "}\n");

    EXPECT_FALSE(completed) << "a full disk must be reported, not silently accepted";
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("échec pendant l'écriture"), std::string::npos) << error;
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
        "  lister(\"/definitivement/introuvable-dossier\") ou propager\n"
        "}\n");

    EXPECT_FALSE(completed3);
    EXPECT_TRUE(output3.empty());
    EXPECT_NE(error3.find("principal a échoué"), std::string::npos);

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
        "  supprimer(\"/definitivement/introuvable-fichier\") ou propager\n"
        "}\n");

    EXPECT_FALSE(completed5);
    EXPECT_TRUE(output5.empty());
    EXPECT_NE(error5.find("principal a échoué"), std::string::npos);

    const std::filesystem::path import_root = std::filesystem::temp_directory_path() / "lumiere_fichier_recursive_errors_test";
    std::filesystem::remove_all(import_root);
    std::filesystem::create_directories(import_root / "non_vide");
    std::ofstream(import_root / "non_vide" / "f.txt") << "x";
    std::ofstream(import_root / "pas_dossier.txt") << "x";

    const auto [output6, completed6, error6] = execute_program_with_error_and_import_path(
        "importer Fichier.{supprimer_dossier}\n"
        "fonction principal() {\n"
        "  supprimer_dossier(\"" + lumiere_string_literal_text((import_root / "non_vide").string()) + "\") ou propager\n"
        "}\n",
        import_root);

    EXPECT_FALSE(completed6);
    EXPECT_TRUE(output6.empty());
    EXPECT_NE(error6.find("principal a échoué"), std::string::npos);

    const auto [output7, completed7, error7] = execute_program_with_error_and_import_path(
        "importer Fichier.{supprimer_dossier}\n"
        "fonction principal() {\n"
        "  supprimer_dossier(\"" + lumiere_string_literal_text((import_root / "pas_dossier.txt").string()) + "\") ou propager\n"
        "}\n",
        import_root);

    EXPECT_FALSE(completed7);
    EXPECT_TRUE(output7.empty());
    EXPECT_NE(error7.find("principal a échoué"), std::string::npos);

    const auto [output8, completed8, error8] = execute_program_with_error_and_import_path(
        "importer Fichier.{supprimer_arbre}\n"
        "fonction principal() {\n"
        "  supprimer_arbre(\"" + lumiere_string_literal_text((import_root / "introuvable").string()) + "\") ou propager\n"
        "}\n",
        import_root);

    std::filesystem::remove_all(import_root);

    EXPECT_FALSE(completed8);
    EXPECT_TRUE(output8.empty());
    EXPECT_NE(error8.find("principal a échoué"), std::string::npos);
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
    EXPECT_NE(error2.find("arguments nommés"), std::string::npos);
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
        "  soit analyse = Temps.analyser(\"2024-06-07 08:09:10.011\", \"AAAA-MM-JJ HH:mm:ss.SSS\") ou propager\n"
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
            "  Temps.analyser(\"2024-01-01\", \"AAAA-MM\") ou propager\n"
            "}\n",
            "Temps.analyser"
        },
        {
            "importer Temps\n"
            "fonction principal() {\n"
            "  Temps.analyser(\"2024-13-01\", \"AAAA-MM-JJ\") ou propager\n"
            "}\n",
            "Temps.analyser"
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
            "Temps.attendre attend une durée positive"
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
        "  afficher(lire_decimal() ou propager)\n"
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
        "  soit adresse = LumiNet.Adresse.analyser(\"127.0.0.1:8080\") ou propager\n"
        "  afficher(adresse.hôte)\n"
        "  afficher(adresse.port)\n"
        "  afficher(adresse.en_texte())\n"
        "  soit locale = LumiNet.Adresse.locale() ou propager\n"
        "  afficher(locale.hôte != \"\")\n"
        "  afficher(LumiNet.Adresse.est_valide(\"127.0.0.1\"))\n"
        "  afficher(LumiNet.Adresse.est_ipv4(\"127.0.0.1\"))\n"
        "  afficher(LumiNet.Adresse.est_ipv6(\"::1\"))\n"
        "  afficher(LumiNet.Adresse.est_locale(\"127.0.0.1\"))\n"
        "  soit ip = LumiNet.DNS.résoudre(\"localhost\") ou propager\n"
        "  afficher(ip != \"\")\n"
        "  soit toutes = LumiNet.DNS.résoudre_tous(\"localhost\") ou propager\n"
        "  afficher(toutes.taille() >= 1)\n"
        "  soit inverse = LumiNet.DNS.résoudre_inverse(\"127.0.0.1\") ou propager\n"
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
    const TestSocket server_fd =
        test_open_socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(test_socket_valid(server_fd));
    test_set_reuseaddr(server_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(
            server_fd,
            reinterpret_cast<sockaddr *>(&addr),
            sizeof(addr)) != 0)
    {
        test_close_socket(server_fd);
        GTEST_SKIP() << "TCP binding is unavailable";
    }

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(
        ::getsockname(
            server_fd,
            reinterpret_cast<sockaddr *>(&bound),
            &bound_len),
        0);
    ASSERT_EQ(::listen(server_fd, 1), 0);
    const int port = ntohs(bound.sin_port);

    auto future = std::async(
        std::launch::async,
        [server_fd]() -> std::string {
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

    const auto [client_output, client_completed, client_error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  soit connexion = LumiNet.TCP.connecter(\"127.0.0.1\", " + std::to_string(port) + ") ou propager\n"
        "  connexion.écrire(\"bonjour\\n\") ou propager\n"
        "  afficher(connexion.est_connecté())\n"
        "  connexion.fermer()\n"
        "}\n");

    const std::string server_line = future.get();

    EXPECT_TRUE(client_completed) << client_error;
    EXPECT_EQ(client_output, "vrai\n");
    EXPECT_EQ(server_line, "bonjour");
}

TEST(InterpreterBuiltinModules, RejectsAnOversizedLireOctetsRequest)
{
    SKIP_IF_LUMINET_DISABLED();
    const TestSocket server_fd =
        test_open_socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(test_socket_valid(server_fd));
    test_set_reuseaddr(server_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(
            server_fd,
            reinterpret_cast<sockaddr *>(&addr),
            sizeof(addr)) != 0)
    {
        test_close_socket(server_fd);
        GTEST_SKIP() << "TCP binding is unavailable";
    }

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(
        ::getsockname(
            server_fd,
            reinterpret_cast<sockaddr *>(&bound),
            &bound_len),
        0);
    ASSERT_EQ(::listen(server_fd, 1), 0);
    const int port = ntohs(bound.sin_port);

    // The server only needs to accept and hold the connection open long
    // enough for the client to call lire_octets; it never has to send
    // anything, because the size cap is checked before any recv.
    auto future = std::async(
        std::launch::async,
        [server_fd]() -> bool {
        if (!test_wait_until_readable(server_fd, std::chrono::seconds(5)))
        {
            test_close_socket(server_fd);
            return false;
        }
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const TestSocket client_fd = ::accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        const bool accepted = test_socket_valid(client_fd);
        if (accepted)
        {
            test_wait_until_readable(client_fd, std::chrono::milliseconds(500));
            test_close_socket(client_fd);
        }
        test_close_socket(server_fd);
        return accepted;
    });

    const auto [client_output, client_completed, client_error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  soit connexion = LumiNet.TCP.connecter(\"127.0.0.1\", " + std::to_string(port) + ") ou propager\n"
        "  afficher(connexion.lire_octets(20971521) ou propager)\n"
        "  connexion.fermer()\n"
        "}\n");

    EXPECT_TRUE(future.get());
    EXPECT_FALSE(client_completed);
    EXPECT_TRUE(client_output.empty());
    EXPECT_NE(client_error.find("ConnexionTCP.lire_octets ne peut pas lire plus de 10 Mo en un seul appel"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RespectsConnectTimeoutInsteadOfHangingUntilTheOsGivesUp)
{
    SKIP_IF_LUMINET_DISABLED();
    const TestSocket server_fd = test_open_socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_TRUE(test_socket_valid(server_fd));
    test_set_reuseaddr(server_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
    {
        test_close_socket(server_fd);
        GTEST_SKIP() << "TCP binding is unavailable";
    }

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(::getsockname(server_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len), 0);
    // A backlog of 1, never accepted, is the black hole this test needs: once
    // the accept queue is full, this platform drops further SYNs silently
    // (tcp_abort_on_overflow=0) instead of sending RST, which is exactly the
    // "connect() never gets a reply" scenario a connect timeout has to bound.
    ASSERT_EQ(::listen(server_fd, 1), 0);
    const int port = ntohs(bound.sin_port);

    // Fire a few non-blocking connects to fill (and overflow) that queue.
    // Non-blocking so this setup can never itself hang on the OS's own SYN
    // retry timeout; the sockets are left open and unaccepted for the
    // duration of the test below.
    std::vector<TestSocket> filler_sockets;
    for (int i = 0; i < 4; ++i)
    {
        TestSocket filler = test_open_socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_TRUE(test_socket_valid(filler));
#ifdef _WIN32
        u_long non_blocking = 1;
        ::ioctlsocket(filler, FIONBIO, &non_blocking);
#else
        const int filler_flags = ::fcntl(filler, F_GETFL, 0);
        ::fcntl(filler, F_SETFL, filler_flags | O_NONBLOCK);
#endif
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(static_cast<uint16_t>(port));
        target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::connect(filler, reinterpret_cast<sockaddr *>(&target), sizeof(target));
        filler_sockets.push_back(filler);
    }

    const auto started = std::chrono::steady_clock::now();
    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "importer Temps\n"
        "fonction principal() {\n"
        "  afficher(LumiNet.TCP.connecter(\"127.0.0.1\", " + std::to_string(port) + ", délai: Temps.millisecondes(300)) ou propager)\n"
        "}\n");
    const auto elapsed = std::chrono::steady_clock::now() - started;

    for (TestSocket &filler : filler_sockets)
    {
        test_close_socket(filler);
    }
    test_close_socket(server_fd);

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_LT(elapsed, std::chrono::seconds(5))
        << "connect() should be bounded by délai, not the OS's own much longer SYN retry timeout";
    EXPECT_NE(error.find("LumiNet.TCP.connecter"), std::string::npos) << error;
}

TEST(InterpreterBuiltinModules, BoundsDnsResolutionInsteadOfBlockingForever)
{
    SKIP_IF_LUMINET_DISABLED();

    // A 0ms deadline can never be met: even the fastest possible run still has
    // to spin up the background thread and reach the wait, so this reliably
    // exercises the abandon-and-return-EAI_AGAIN path rather than the
    // happens-to-finish-in-time path, deterministically and without depending
    // on any real DNS slowness. Looped, under ASan, to also prove the
    // abandoned lookup's own result never leaks once it finishes late.
    bool observed_timeout = false;
    for (int i = 0; i < 20; ++i)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        addrinfo *result = nullptr;

        const auto started = std::chrono::steady_clock::now();
        const int rc = lumiere::getaddrinfo_with_timeout("localhost", nullptr, &hints, &result, 0);
        const auto elapsed = std::chrono::steady_clock::now() - started;

        EXPECT_LT(elapsed, std::chrono::seconds(2))
            << "a 0ms deadline must not block on the resolver's own timing";

        if (rc == EAI_AGAIN)
        {
            observed_timeout = true;
        }
        else
        {
            ASSERT_EQ(rc, 0);
            ASSERT_NE(result, nullptr);
            ::freeaddrinfo(result);
        }
    }
    EXPECT_TRUE(observed_timeout) << "expected at least one 0ms lookup to be abandoned";

    // The normal, generously-timed path still resolves correctly.
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    addrinfo *result = nullptr;
    ASSERT_EQ(lumiere::getaddrinfo_with_timeout("localhost", nullptr, &hints, &result), 0);
    ASSERT_NE(result, nullptr);
    ::freeaddrinfo(result);
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
    if (::bind(
            probe_fd,
            reinterpret_cast<sockaddr *>(&probe_addr),
            sizeof(probe_addr)) != 0)
    {
        test_close_socket(probe_fd);
        GTEST_SKIP() << "UDP binding is unavailable";
    }
    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(::getsockname(probe_fd, reinterpret_cast<sockaddr *>(&bound), &bound_len), 0);
    const int port = ntohs(bound.sin_port);

    // Keep the native endpoint bound. The language endpoint asks the OS for
    // its own port and announces readiness before we send the test packet.
    const std::string receiver_source =
        "importer LumiNet\n"
        "importer Temps\n"
        "fonction principal() {\n"
        "  soit socket = LumiNet.UDP.ouvrir(0) ou propager\n"
        "  socket.définir_délai(Temps.secondes(2))\n"
        "  socket.envoyer(\"pret\", \"127.0.0.1\", " + std::to_string(port) + ") ou propager\n"
        "  soit paquet = socket.recevoir() ou propager\n"
        "  afficher(paquet.données)\n"
        "  afficher(paquet.adresse != \"\")\n"
        "  afficher(paquet.port > 0)\n"
        "  socket.fermer()\n"
        "}\n";

    auto future = std::async(std::launch::async, [receiver_source]() {
        return execute_program_with_error(receiver_source);
    });

    const bool ready = test_wait_until_readable(probe_fd, std::chrono::seconds(2));
    if (ready)
    {
        char announcement[4]{};
        sockaddr_in receiver_addr{};
        socklen_t receiver_len = sizeof(receiver_addr);
        const auto received = ::recvfrom(probe_fd, announcement, sizeof(announcement), 0,
                                         reinterpret_cast<sockaddr *>(&receiver_addr), &receiver_len);
        EXPECT_EQ(received, 4);
        EXPECT_EQ(std::string(announcement, sizeof(announcement)), "pret");
        const std::string payload = "salut";
        EXPECT_GT(::sendto(probe_fd, payload.data(), static_cast<int>(payload.size()),
                           0,
                           reinterpret_cast<const sockaddr *>(&receiver_addr),
                           receiver_len),
                  0);
    }
    test_close_socket(probe_fd);

    const auto [receiver_output, receiver_completed, receiver_error] = future.get();

    EXPECT_TRUE(ready) << receiver_error;
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
        bool requires_udp_binding = false;
    };

    const std::vector<Case> cases = {
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.Adresse.analyser(\"abc\") ou propager\n"
            "}\n",
            "LumiNet.Adresse.analyser"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.DNS.résoudre_inverse(\"pas_une_ip\") ou propager\n"
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
            "  soit socket = LumiNet.UDP.ouvrir() ou propager\n"
            "  socket.envoyer(\"salut\", \"127.0.0.1\", 70000)\n"
            "}\n",
            "SocketUDP.envoyer requiert un port entre 0 et 65535",
            true
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  soit socket = LumiNet.UDP.ouvrir() ou propager\n"
            "  socket.envoyer_octets([1, 2, 3], \"127.0.0.1\", 70000)\n"
            "}\n",
            "SocketUDP.envoyer_octets requiert un port entre 0 et 65535",
            true
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  soit socket = LumiNet.UDP.ouvrir() ou propager\n"
            "  socket.diffuser(\"salut\", 70000)\n"
            "}\n",
            "SocketUDP.diffuser requiert un port entre 0 et 65535",
            true
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.Canal.connecter(\"http://example.com\") ou propager\n"
            "}\n",
            "LumiNet.Canal.connecter"
        },
        {
            "importer LumiNet\n"
            "fonction principal() {\n"
            "  LumiNet.HTTP.obtenir(\"https://example.com\") ou propager\n"
            "}\n",
            "ne prend actuellement en charge que http"
        },
    };

    bool udp_binding_available = false;
    const TestSocket udp_probe =
        test_open_socket(AF_INET, SOCK_DGRAM, 0);
    if (test_socket_valid(udp_probe))
    {
        sockaddr_in probe_address{};
        probe_address.sin_family = AF_INET;
        probe_address.sin_port = htons(0);
        probe_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        udp_binding_available =
            ::bind(
                udp_probe,
                reinterpret_cast<sockaddr *>(&probe_address),
                sizeof(probe_address)) == 0;
        test_close_socket(udp_probe);
    }

    for (const auto &test_case : cases)
    {
        if (test_case.requires_udp_binding &&
            !udp_binding_available)
        {
            continue;
        }
        const auto [output, completed, error] = execute_program_with_error(test_case.source);
        EXPECT_FALSE(completed) << test_case.source;
        EXPECT_TRUE(output.empty()) << test_case.source;
        EXPECT_NE(error.find(test_case.expected_fragment), std::string::npos) << test_case.source;
    }
}

TEST(InterpreterBuiltinModules, SupportsLumiNetHttpClient)
{
    SKIP_IF_LUMINET_DISABLED();
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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
        "  soit réponse = LumiNet.HTTP.créer(\"http://127.0.0.1:" + std::to_string(port) + "/api\", corps: \"charge\", type: \"text/plain\") ou propager\n"
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
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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
        "  LumiNet.HTTP.obtenir(\"http://127.0.0.1:" + std::to_string(port) + "/\") ou propager\n"
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
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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
            "  rep.envoyer_fichier(200, \"" + lumiere_string_literal_text(html_path.string()) + "\")\n"
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
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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
        "  soit canal = LumiNet.Canal.connecter(\"ws://127.0.0.1:" + std::to_string(port) + "/\") ou propager\n"
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
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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
        if (test_wait_until_readable(client_fd, std::chrono::milliseconds(200)))
        {
            char discard_buf[256];
            test_recv(client_fd, discard_buf, sizeof(discard_buf), 0);
        }
        test_close_socket(client_fd);
        test_close_socket(server_fd);
        return request;
    });

    const int port = port_future.get();
    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  LumiNet.Canal.connecter(\"ws://127.0.0.1:" + std::to_string(port) + "/\") ou propager\n"
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
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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

        const std::string ws_key_prefix = "Sec-WebSocket-Key: ";
        const std::size_t ws_key_start = request.find(ws_key_prefix);
        std::string ws_accept;
        if (ws_key_start != std::string::npos)
        {
            const std::size_t ws_key_end = request.find("\r\n", ws_key_start);
            const std::string ws_key = request.substr(ws_key_start + ws_key_prefix.size(),
                                                       ws_key_end - ws_key_start - ws_key_prefix.size());
            ws_accept = lumiere::websocket_accept_key(ws_key);
        }
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + ws_accept + "\r\n"
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

        if (test_wait_until_readable(client_fd, std::chrono::milliseconds(200)))
        {
            char discard_buf[256];
            test_recv(client_fd, discard_buf, sizeof(discard_buf), 0);
        }

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
        "  soit canal = LumiNet.Canal.connecter(\"ws://127.0.0.1:" + std::to_string(port) + "/\") ou propager\n"
        "  canal.quand_message(reçu)\n"
        "  canal.attendre()\n"
        "}\n");

    EXPECT_EQ(future.get(), "ok");
    EXPECT_TRUE(completed) << error;
    EXPECT_EQ(output, "bonjour\n");
}

TEST(InterpreterBuiltinModules, ReportsAWebSocketFrameTruncatedMidPayloadAsAnErrorNotAGracefulClose)
{
    SKIP_IF_LUMINET_DISABLED();
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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

        const std::string ws_key_prefix = "Sec-WebSocket-Key: ";
        const std::size_t ws_key_start = request.find(ws_key_prefix);
        std::string ws_accept;
        if (ws_key_start != std::string::npos)
        {
            const std::size_t ws_key_end = request.find("\r\n", ws_key_start);
            const std::string ws_key = request.substr(ws_key_start + ws_key_prefix.size(),
                                                       ws_key_end - ws_key_start - ws_key_prefix.size());
            ws_accept = lumiere::websocket_accept_key(ws_key);
        }
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + ws_accept + "\r\n"
            "\r\n";
        test_send(client_fd, response.data(), response.size(), 0);

        // A complete, valid header announcing a 7-byte payload ("bonjour"),
        // then only 3 of those bytes, then a clean close: the frame started
        // and never finished, which is not the same thing as the channel
        // ending cleanly between frames.
        const unsigned char header[2] = {0x81, 0x07};
        test_send(client_fd, header, sizeof(header), 0);
        test_send(client_fd, "bon", 3, 0);
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
        "  soit canal = LumiNet.Canal.connecter(\"ws://127.0.0.1:" + std::to_string(port) + "/\") ou propager\n"
        "  canal.quand_message(reçu)\n"
        "  canal.attendre() ou propager\n"
        "}\n");

    EXPECT_EQ(future.get(), "ok");
    EXPECT_FALSE(completed) << "a truncated mid-frame payload must surface as an error, not be treated as a graceful close";
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("a reçu une trame websocket incomplète"), std::string::npos) << error;
}

TEST(InterpreterBuiltinModules, RejectsAnOversizedFragmentedWebSocketMessage)
{
    SKIP_IF_LUMINET_DISABLED();
    SKIP_IF_TCP_BINDING_UNAVAILABLE();
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

        const std::string ws_key_prefix = "Sec-WebSocket-Key: ";
        const std::size_t ws_key_start = request.find(ws_key_prefix);
        std::string ws_accept;
        if (ws_key_start != std::string::npos)
        {
            const std::size_t ws_key_end = request.find("\r\n", ws_key_start);
            const std::string ws_key = request.substr(ws_key_start + ws_key_prefix.size(),
                                                       ws_key_end - ws_key_start - ws_key_prefix.size());
            ws_accept = lumiere::websocket_accept_key(ws_key);
        }
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + ws_accept + "\r\n"
            "\r\n";
        test_send(client_fd, response.data(), response.size(), 0);

        // kMaxWebSocketMessageBytes (canal_runtime.cpp) is 10 MiB. Each frame's
        // payload is capped at 65535 bytes by the parser, so this sends one
        // opening text fragment (fin=0) plus enough 0x0 continuation frames
        // (fin=0) to push the reassembled total past the cap without ever
        // sending a fin=1 frame -- exactly the "endless small continuation
        // frames" shape the cap exists to stop.
        const std::size_t frame_payload_size = 65535;
        const std::string payload(frame_payload_size, 'a');
        auto send_frame = [&](unsigned char opcode, bool fin) {
            unsigned char header[4];
            header[0] = static_cast<unsigned char>((fin ? 0x80 : 0x00) | opcode);
            header[1] = 126; // extended 16-bit length follows
            header[2] = static_cast<unsigned char>((frame_payload_size >> 8) & 0xFF);
            header[3] = static_cast<unsigned char>(frame_payload_size & 0xFF);
            test_send(client_fd, header, sizeof(header), 0);
            test_send(client_fd, payload.data(), payload.size(), 0);
        };

        send_frame(0x1, false);
        constexpr int kFramesToExceedCap = 161; // 161 * 65535 > 10 MiB
        for (int i = 0; i < kFramesToExceedCap; ++i)
        {
            send_frame(0x0, false);
        }

        if (test_wait_until_readable(client_fd, std::chrono::milliseconds(500)))
        {
            char discard_buf[256];
            test_recv(client_fd, discard_buf, sizeof(discard_buf), 0);
        }

        test_close_socket(client_fd);
        test_close_socket(server_fd);
        return "ok";
    });

    const int port = port_future.get();
    const auto [output, completed, error] = execute_program_with_error(
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  soit canal = LumiNet.Canal.connecter(\"ws://127.0.0.1:" + std::to_string(port) + "/\") ou propager\n"
        "  canal.attendre() ou propager\n"
        "}\n");

    EXPECT_EQ(future.get(), "ok");
    EXPECT_FALSE(completed);
    EXPECT_NE(error.find("trop volumineux"), std::string::npos) << error;
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
    // tan(pi/4) is 0.9999999999999999 in double arithmetic, and pi is not
    // 3.14159. Both used to print rounded to six significant digits, which hid
    // what the runtime had actually computed; decimals now print the shortest
    // text that reads back as the same value, and a whole-numbered Décimal
    // keeps its point so it cannot be mistaken for an Entier.
    EXPECT_EQ(output,
              "vrai\n7\n2.5\n4\n2.0\n4\n3\n4\n3\n9.0\n3.0\n32.0\n1.0\n2.0\n3.0\n1.0\n1.0\n"
              "0.9999999999999999\nvrai\n3.141592653589793\n180.0\nvrai\nvrai\nvrai\nvrai\n");
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
    EXPECT_EQ(output, "8\n3\n27.0\nvrai\n");
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
    // tangente(pi/4) is not exactly 1; see SupportsMathsModule above.
    EXPECT_EQ(output, "6\n3\n1.0\n1.0\n0.9999999999999999\n");
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

TEST(InterpreterBuiltinModules, SupportsCollectionsModuleCoreOperations)
{
    const auto [output, completed] = execute_program(
        "importer Collections.{étendue, transformer, filtrer, réduire, trouver, position, tout, au_moins_un, trier, trier_par, inverser}\n"
        "fonction principal() {\n"
        "  afficher(étendue(0, 5, 1))\n"
        "  afficher(étendue(5, 0, -1))\n"
        "  afficher(étendue(0, 0, 1))\n"
        "  afficher(transformer(étendue(1, 5, 1), fonction(x: Universel) -> Universel { retourne x * x }))\n"
        "  afficher(filtrer(étendue(0, 10, 1), fonction(x: Universel) -> Universel { retourne x % 2 == 0 }))\n"
        "  afficher(réduire(étendue(1, 5, 1), 0, fonction(acc: Universel, x: Universel) -> Universel { retourne acc + x }))\n"
        "  afficher(trouver(étendue(0, 10, 1), fonction(x: Universel) -> Universel { retourne x > 5 }))\n"
        "  afficher(trouver(étendue(0, 3, 1), fonction(x: Universel) -> Universel { retourne x > 50 }))\n"
        "  afficher(position(étendue(0, 10, 1), fonction(x: Universel) -> Universel { retourne x > 5 }))\n"
        "  afficher(position(étendue(0, 3, 1), fonction(x: Universel) -> Universel { retourne x > 50 }))\n"
        "  afficher(tout(étendue(0, 5, 1), fonction(x: Universel) -> Universel { retourne x >= 0 }))\n"
        "  afficher(tout([], fonction(x: Universel) -> Universel { retourne faux }))\n"
        "  afficher(au_moins_un(étendue(0, 5, 1), fonction(x: Universel) -> Universel { retourne x == 3 }))\n"
        "  afficher(au_moins_un([], fonction(x: Universel) -> Universel { retourne vrai }))\n"
        "  afficher(trier([3, 1, 2]))\n"
        "  afficher(trier([3, 1.5, 2]))\n"
        "  afficher(trier([\"banane\", \"abricot\", \"cerise\"]))\n"
        "  afficher(trier_par([\"bb\", \"a\", \"ccc\"], fonction(x: Universel) -> Universel { retourne x.taille() }))\n"
        "  afficher(inverser([1, 2, 3]))\n"
        "  afficher(inverser([]))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "[0, 1, 2, 3, 4]\n"
        "[5, 4, 3, 2, 1]\n"
        "[]\n"
        "[1, 4, 9, 16]\n"
        "[0, 2, 4, 6, 8]\n"
        "10\n"
        "6\n"
        "rien\n"
        "6\n"
        "rien\n"
        "vrai\n"
        "vrai\n"
        "vrai\n"
        "faux\n"
        "[1, 2, 3]\n"
        "[1.5, 2, 3]\n"
        "[abricot, banane, cerise]\n"
        "[a, bb, ccc]\n"
        "[3, 2, 1]\n"
        "[]\n");
}

TEST(InterpreterBuiltinModules, CollectionsAlgorithmsAcceptEveryIterableKindMatchingPourChaque)
{
    // Collections algorithms must accept the same five iterable kinds as
    // `pour chaque`, with the same contract: a Dictionnaire yields its keys.
    const auto [output, completed] = execute_program(
        "importer Collections.{transformer}\n"
        "fonction principal() {\n"
        "  soit liste_fixe = [1, 2, 3].en_liste_fixe(3)\n"
        "  soit ensemble = [3, 1, 2].en_ensemble()\n"
        "  soit dictionnaire = {\"un\": 1, \"deux\": 2}\n"
        "  afficher(transformer(liste_fixe, fonction(x: Universel) -> Universel { retourne x }))\n"
        "  afficher(transformer(ensemble, fonction(x: Universel) -> Universel { retourne x }))\n"
        "  afficher(transformer(dictionnaire, fonction(x: Universel) -> Universel { retourne x }))\n"
        "  afficher(transformer(\"abc\", fonction(x: Universel) -> Universel { retourne x }))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "[1, 2, 3]\n[3, 1, 2]\n[un, deux]\n[a, b, c]\n");
}

TEST(InterpreterBuiltinModules, RejectsANonIterableCollectionsArgument)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Collections.{filtrer}\n"
        "fonction principal() {\n"
        "  afficher(filtrer(5, fonction(x: Universel) -> Universel { retourne vrai }))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(
        error.find("Collections.filtrer attend une valeur itérable (Liste, ListeFixe, Ensemble, Dictionnaire ou Texte)"),
        std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsACollectionsPredicateThatDoesNotReturnLogique)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Collections.{filtrer}\n"
        "fonction principal() {\n"
        "  afficher(filtrer([1, 2, 3], fonction(x: Universel) -> Universel { retourne 5 }))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Collections.filtrer : le prédicat doit retourner Logique"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsAZeroStepInCollectionsÉtendue)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Collections.{étendue}\n"
        "fonction principal() {\n"
        "  afficher(étendue(0, 5, 0))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Collections.étendue: pas ne peut pas être zéro"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsMixedTypesInCollectionsTrier)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer Collections.{trier}\n"
        "fonction principal() {\n"
        "  afficher(trier([1, \"a\"]))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Collections.trier ne peut pas trier des valeurs de types différents ensemble"), std::string::npos);
}

TEST(InterpreterBuiltinModules, RejectsSortingNonNombreInCollectionsTrier)
{
    // non_nombre has no order relative to anything (including itself), and
    // letting it reach std::stable_sort's comparator would be undefined
    // behavior, not just a surprising result.
    const auto [output, completed, error] = execute_program_with_error(
        "importer Maths\n"
        "importer Collections.{trier}\n"
        "fonction principal() {\n"
        "  afficher(trier([1.0, Maths.non_nombre]))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Collections.trier ne peut pas trier non_nombre"), std::string::npos);
}

TEST(InterpreterBuiltinModules, SupportsJsonParsingEncodingAndErrors)
{
    // Mirrors what was manually verified against both engines on the CLI
    // before this test was written. Lumiere text literals have no escape
    // syntax, so JSON source text containing '"' is built by concatenating
    // a Symbole quote ('"') in, exactly as a real Lumiere program would have
    // to.
    const auto [output, completed] = execute_program(
        "importer JSON\n"
        "importer Maths\n"
        "fonction principal() {\n"
        "  soit q = '\"'\n"
        "  soit texte = \"{\" + q + \"nom\" + q + \": \" + q + \"Ada\" + q + \", \" + q + \"age\" + q + \": 36, \" + q + \"actif\" + q + \": true, \" + q + \"tags\" + q + \": [1, 2.5, null, \" + q + \"x\" + q + \"]}\"\n"
        "  soit v = agir selon JSON.analyser(texte) {\n"
        "    Succès(val) -> val\n"
        "    Échec(e) -> { afficher(\"echec inattendu: \" + e.cause) rien }\n"
        "  }\n"
        "  afficher(\"nom: \" + v[\"nom\"])\n"
        "  afficher(\"age: \" + v[\"age\"])\n"
        "  afficher(\"actif: \" + v[\"actif\"])\n"
        "  afficher(\"tags: \" + v[\"tags\"])\n"
        "  soit compact = agir selon JSON.encoder(v) {\n"
        "    Succès(t) -> t\n"
        "    Échec(e) -> \"echec: \" + e.cause\n"
        "  }\n"
        "  afficher(compact)\n"
        "  soit joli = agir selon JSON.encoder_indenté(v, 2) {\n"
        "    Succès(t) -> t\n"
        "    Échec(e) -> \"echec: \" + e.cause\n"
        "  }\n"
        "  afficher(joli)\n"
        "  soit mauvais = \"{\" + q + \"a\" + q + \": 1,}\"\n"
        "  agir selon JSON.analyser(mauvais) {\n"
        "    Succès(_) -> afficher(\"inattendu: succès\")\n"
        "    Échec(e) -> afficher(\"erreur: \" + e.cause + \" ligne=\" + e.ligne + \" colonne=\" + e.colonne)\n"
        "  }\n"
        "  soit texte_dup = \"{\" + q + \"a\" + q + \": 1, \" + q + \"a\" + q + \": 2}\"\n"
        "  agir selon JSON.analyser(texte_dup) {\n"
        "    Succès(_) -> afficher(\"inattendu: succès\")\n"
        "    Échec(e) -> afficher(\"erreur dup: \" + e.cause)\n"
        "  }\n"
        "  agir selon JSON.analyser(\"99999999999999999999\") {\n"
        "    Succès(_) -> afficher(\"inattendu: succès\")\n"
        "    Échec(e) -> afficher(\"erreur gros: \" + e.cause)\n"
        "  }\n"
        "  agir selon JSON.encoder(Maths.non_nombre) {\n"
        "    Succès(_) -> afficher(\"inattendu: succès\")\n"
        "    Échec(e) -> afficher(\"erreur non_nombre: \" + e.cause)\n"
        "  }\n"
        "  soit cyclique_liste = [1, 2, 3]\n"
        "  cyclique_liste[0] = cyclique_liste\n"
        "  agir selon JSON.encoder(cyclique_liste) {\n"
        "    Succès(_) -> afficher(\"inattendu: succès\")\n"
        "    Échec(e) -> afficher(\"erreur cycle: \" + e.cause + \" chemin=\" + e.chemin)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "nom: Ada\n"
        "age: 36\n"
        "actif: vrai\n"
        "tags: [1, 2.5, rien, x]\n"
        "{\"nom\":\"Ada\",\"age\":36,\"actif\":true,\"tags\":[1,2.5,null,\"x\"]}\n"
        "{\n"
        "  \"nom\": \"Ada\",\n"
        "  \"age\": 36,\n"
        "  \"actif\": true,\n"
        "  \"tags\": [\n"
        "    1,\n"
        "    2.5,\n"
        "    null,\n"
        "    \"x\"\n"
        "  ]\n"
        "}\n"
        "erreur: clé de chaîne attendue dans un objet JSON ligne=1 colonne=9\n"
        "erreur dup: clé d'objet dupliquée: \"a\"\n"
        "erreur gros: nombre entier hors des limites de Entier\n"
        "erreur non_nombre: infini et non_nombre ne sont pas des nombres JSON\n"
        "erreur cycle: structure cyclique détectée chemin=$[0]\n");
}

TEST(InterpreterBuiltinModules, JsonAnalyserDecodesUnicodeEscapesAndRejectsRawControlCharacters)
{
    const auto [output, completed] = execute_program(
        "importer JSON\n"
        "fonction principal() {\n"
        "  soit q = '\"'\n"
        "  soit texte = \"\" + q + \"A\\\\u00e9\\\\ud83d\\\\ude00\" + q\n"
        "  agir selon JSON.analyser(texte) {\n"
        "    Succès(v) -> afficher(v)\n"
        "    Échec(e) -> afficher(\"erreur: \" + e.cause)\n"
        "  }\n"
        "  soit avec_controle = \"\" + q + \"a\" + \"\t\" + \"b\" + q\n"
        "  agir selon JSON.analyser(avec_controle) {\n"
        "    Succès(_) -> afficher(\"inattendu: succès\")\n"
        "    Échec(e) -> afficher(\"erreur controle: \" + e.cause)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "Aé😀\n"
        "erreur controle: caractère de contrôle non échappé dans une chaîne\n");
}

TEST(InterpreterBuiltinModules, RejectsJsonEncoderIndenteOutOfRangeSpaces)
{
    const auto [output, completed, error] = execute_program_with_error(
        "importer JSON\n"
        "fonction principal() {\n"
        "  afficher(JSON.encoder_indenté(42, 9))\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("JSON.encoder_indenté attend un nombre d'espaces entre 0 et 8"), std::string::npos);
}


TEST(InterpreterBuiltinModules, RegexSupportsAnalyserCorrespondChercherTrouverTousAndRemplacer)
{
    // Mirrors what was manually verified against both engines on the CLI
    // before this test was written (a(b+)c against several inputs).
    const auto [output, completed] = execute_program(
        "importer Regex\n"
        "fonction principal() {\n"
        "  soit motif = agir selon Regex.analyser(\"a(b+)c\") {\n"
        "    Succès(m) -> m\n"
        "    Échec(e) -> { afficher(\"echec analyser: \" + e.cause) rien }\n"
        "  }\n"
        "  afficher(\"correspond abc: \" + Regex.correspond(motif, \"abc\"))\n"
        "  afficher(\"correspond abbbc: \" + Regex.correspond(motif, \"abbbc\"))\n"
        "  afficher(\"correspond xabcx: \" + Regex.correspond(motif, \"xabcx\"))\n"
        "  soit trouve = Regex.chercher(motif, \"xxabbcyy\")\n"
        "  si trouve != rien {\n"
        "    afficher(\"texte: \" + trouve.texte())\n"
        "    afficher(\"début: \" + trouve.début())\n"
        "    afficher(\"fin: \" + trouve.fin())\n"
        "    afficher(\"groupe1: \" + trouve.groupe(1))\n"
        "    afficher(\"groupes: \" + trouve.groupes())\n"
        "  } sinon {\n"
        "    afficher(\"pas trouvé\")\n"
        "  }\n"
        "  soit sans_match = Regex.chercher(motif, \"zzz\")\n"
        "  afficher(\"sans_match est rien: \" + (sans_match == rien))\n"
        "  soit tous = Regex.trouver_tous(motif, \"ac abc abbc\")\n"
        "  afficher(\"nombre de correspondances: \" + tous.taille())\n"
        "  pour chaque m dans tous {\n"
        "    afficher(\"  match: \" + m.texte())\n"
        "  }\n"
        "  afficher(Regex.remplacer(motif, \"abc et abbc\", \"[$1]\"))\n"
        "  afficher(Regex.remplacer_tout(motif, \"abc et abbc\", \"[$1]\"))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "correspond abc: vrai\n"
        "correspond abbbc: vrai\n"
        "correspond xabcx: faux\n"
        "texte: abbc\n"
        "début: 2\n"
        "fin: 6\n"
        "groupe1: bb\n"
        "groupes: [bb]\n"
        "sans_match est rien: vrai\n"
        "nombre de correspondances: 2\n"
        "  match: abc\n"
        "  match: abbc\n"
        "[b] et abbc\n"
        "[b] et [bb]\n");
}

TEST(InterpreterBuiltinModules, RegexSupportsAnchorsClassesBoundedRepetitionAlternationAndShorthand)
{
    // Mirrors what was manually verified against both engines on the CLI:
    // ^/$ anchors, [a-c] ranges, [^0-9] negation, {m,n} bounded repetition
    // (greedy), alternation, ASCII \d\s\w shorthand classes, malformed
    // patterns, and a non-ASCII (Unicode scalar) literal match.
    const auto [output, completed] = execute_program(
        "importer Regex\n"
        "fonction executer() -> Résultat[Rien, Regex.ErreurRegex] {\n"
        "  soit m1 = Regex.analyser(\"^abc$\") ou propager\n"
        "  afficher(\"anchors abc: \" + Regex.correspond(m1, \"abc\"))\n"
        "  afficher(\"anchors xabc: \" + Regex.correspond(m1, \"xabc\"))\n"
        "\n"
        "  soit m2 = Regex.analyser(\"[a-c]+\") ou propager\n"
        "  soit r2 = Regex.chercher(m2, \"zzabccbaZZ\")\n"
        "  si r2 != rien { afficher(\"class match: \" + r2.texte()) } sinon { afficher(\"class: aucun\") }\n"
        "\n"
        "  soit m3 = Regex.analyser(\"[^0-9]+\") ou propager\n"
        "  soit r3 = Regex.chercher(m3, \"123abc456\")\n"
        "  si r3 != rien { afficher(\"neg class match: \" + r3.texte()) } sinon { afficher(\"neg class: aucun\") }\n"
        "\n"
        "  soit m4 = Regex.analyser(\"a{2,3}\") ou propager\n"
        "  soit r4 = Regex.chercher(m4, \"aaaaa\")\n"
        "  si r4 != rien { afficher(\"bounded match: \" + r4.texte()) } sinon { afficher(\"bounded: aucun\") }\n"
        "\n"
        "  soit m5 = Regex.analyser(\"colou?r|chat\") ou propager\n"
        "  afficher(\"alt1: \" + Regex.correspond(m5, \"color\"))\n"
        "  afficher(\"alt2: \" + Regex.correspond(m5, \"colour\"))\n"
        "  afficher(\"alt3: \" + Regex.correspond(m5, \"chat\"))\n"
        "  afficher(\"alt4: \" + Regex.correspond(m5, \"dog\"))\n"
        "\n"
        "  soit m6 = Regex.analyser(\"\\\\d+\\\\s\\\\w+\") ou propager\n"
        "  soit r6 = Regex.chercher(m6, \"xx 42 salut99 yy\")\n"
        "  si r6 != rien { afficher(\"shorthand match: \" + r6.texte()) } sinon { afficher(\"shorthand: aucun\") }\n"
        "\n"
        "  agir selon Regex.analyser(\"a(b\") { Succès(_) -> afficher(\"inattendu succès\") Échec(e) -> afficher(\"erreur motif: \" + e.cause) }\n"
        "  agir selon Regex.analyser(\"a**\") { Succès(_) -> afficher(\"inattendu succès\") Échec(e) -> afficher(\"erreur motif2: \" + e.cause) }\n"
        "\n"
        "  soit m8 = Regex.analyser(\"é+\") ou propager\n"
        "  soit r8 = Regex.chercher(m8, \"cafés très bééé chic\")\n"
        "  si r8 != rien { afficher(\"unicode match: \" + r8.texte() + \" début=\" + r8.début() + \" fin=\" + r8.fin()) } sinon { afficher(\"unicode: aucun\") }\n"
        "\n"
        "  retourne Succès(rien)\n"
        "}\n"
        "fonction principal() {\n"
        "  agir selon executer() {\n"
        "    Succès(_) -> rien\n"
        "    Échec(e) -> afficher(\"echec executer: \" + e.cause)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "anchors abc: vrai\n"
        "anchors xabc: faux\n"
        "class match: abccba\n"
        "neg class match: abc\n"
        "bounded match: aaa\n"
        "alt1: vrai\n"
        "alt2: vrai\n"
        "alt3: vrai\n"
        "alt4: faux\n"
        "shorthand match: 42 salut99\n"
        "erreur motif: parenthèse fermante ')' attendue\n"
        "erreur motif2: quantificateur sans opérande\n"
        "unicode match: é début=3 fin=4\n");
}

TEST(InterpreterBuiltinModules, RegexTrouverTousHandlesEmptyMatchesAcrossMultipleRunCalls)
{
    // Regression test for a bug found during manual verification: the NFA
    // simulation's per-step instruction-dedup generation counter used to be
    // local to RegexMatcher::run(), reset to 0 on every call, while the
    // dedup marks array (m_visited) is a member that persists across calls.
    // trouver_tous() constructs one RegexMatcher and calls run() repeatedly
    // to scan forward, so the second call's fresh generation 0 collided with
    // marks the first call had already left behind, and add_thread()
    // wrongly treated the start instruction as "already visited this step",
    // silently finding nothing after the first match. Fixed by making the
    // generation counter a persistent member (m_gen). "a*" against "bab"
    // must find all four matches: "", "a", "", "".
    const auto [output, completed] = execute_program(
        "importer Regex\n"
        "fonction executer() -> Résultat[Rien, Regex.ErreurRegex] {\n"
        "  soit motif = Regex.analyser(\"a*\") ou propager\n"
        "  soit tous = Regex.trouver_tous(motif, \"bab\")\n"
        "  afficher(\"empty-match count: \" + tous.taille())\n"
        "  pour chaque m dans tous { afficher(\"  [\" + m.texte() + \"] \" + m.début() + \"-\" + m.fin()) }\n"
        "  retourne Succès(rien)\n"
        "}\n"
        "fonction principal() {\n"
        "  agir selon executer() {\n"
        "    Succès(_) -> rien\n"
        "    Échec(e) -> afficher(\"echec: \" + e.cause)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "empty-match count: 4\n"
        "  [] 0-0\n"
        "  [a] 1-2\n"
        "  [] 2-2\n"
        "  [] 3-3\n");
}

TEST(InterpreterBuiltinModules, RegexSupportsCaptureGroupAccessAndNonParticipatingGroupsAndEmptyPattern)
{
    const auto [output, completed] = execute_program(
        "importer Regex\n"
        "fonction executer() -> Résultat[Rien, Regex.ErreurRegex] {\n"
        "  soit m1 = Regex.analyser(\"(a)(b)(c)\") ou propager\n"
        "  soit r1 = Regex.chercher(m1, \"xabcx\")\n"
        "  si r1 != rien {\n"
        "    afficher(\"g0: \" + r1.groupe(0))\n"
        "    afficher(\"g1: \" + r1.groupe(1))\n"
        "    afficher(\"g2: \" + r1.groupe(2))\n"
        "    afficher(\"g3: \" + r1.groupe(3))\n"
        "    afficher(\"groupes: \" + r1.groupes())\n"
        "  }\n"
        "\n"
        "  soit m2 = Regex.analyser(\"(a)|(b)\") ou propager\n"
        "  soit r2 = Regex.chercher(m2, \"b\")\n"
        "  si r2 != rien {\n"
        "    afficher(\"g1 non participant: \" + r2.groupe(1))\n"
        "    afficher(\"g2 participant: \" + r2.groupe(2))\n"
        "  }\n"
        "\n"
        "  soit m3 = Regex.analyser(\"\") ou propager\n"
        "  afficher(\"motif vide correspond vide: \" + Regex.correspond(m3, \"\"))\n"
        "  afficher(\"motif vide correspond a: \" + Regex.correspond(m3, \"a\"))\n"
        "\n"
        "  retourne Succès(rien)\n"
        "}\n"
        "fonction principal() {\n"
        "  agir selon executer() {\n"
        "    Succès(_) -> rien\n"
        "    Échec(e) -> afficher(\"echec: \" + e.cause)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "g0: abc\n"
        "g1: a\n"
        "g2: b\n"
        "g3: c\n"
        "groupes: [a, b, c]\n"
        "g1 non participant: rien\n"
        "g2 participant: b\n"
        "motif vide correspond vide: vrai\n"
        "motif vide correspond a: faux\n");
}

TEST(InterpreterBuiltinModules, RegexRaisesRuntimeErrorsForOutOfRangeGroupAccess)
{
    // Correspondance.groupe(N) and remplacer's "$N" placeholders both treat
    // an out-of-range capture-group number as a caller contract violation
    // (a direct runtime error), not a Résultat failure — mirroring how the
    // rest of the stdlib distinguishes programmer errors from expected
    // failure modes. This also exercises the CALL_MEMBER-on-native-function
    // path on both backends, whose call-site tracking (source path,
    // line/column) was fixed alongside this module.
    {
        const auto [output, completed, error] = execute_program_with_error(
            "importer Regex\n"
            "fonction executer() -> Résultat[Rien, Regex.ErreurRegex] {\n"
            "  soit motif = Regex.analyser(\"(a)(b)\") ou propager\n"
            "  soit r = Regex.chercher(motif, \"ab\")\n"
            "  si r != rien {\n"
            "    afficher(r.groupe(5))\n"
            "  }\n"
            "  retourne Succès(rien)\n"
            "}\n"
            "fonction principal() {\n"
            "  agir selon executer() {\n"
            "    Succès(_) -> rien\n"
            "    Échec(e) -> afficher(e.cause)\n"
            "  }\n"
            "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Correspondance.groupe: numéro de groupe invalide"), std::string::npos);
    }
    {
        const auto [output, completed, error] = execute_program_with_error(
            "importer Regex\n"
            "fonction executer() -> Résultat[Rien, Regex.ErreurRegex] {\n"
            "  soit motif = Regex.analyser(\"(a)\") ou propager\n"
            "  afficher(Regex.remplacer(motif, \"abc\", \"[$5]\"))\n"
            "  retourne Succès(rien)\n"
            "}\n"
            "fonction principal() {\n"
            "  agir selon executer() {\n"
            "    Succès(_) -> rien\n"
            "    Échec(e) -> afficher(e.cause)\n"
            "  }\n"
            "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Regex.remplacer: numéro de groupe invalide dans le remplacement: $5"), std::string::npos);
    }
}

TEST(InterpreterBuiltinModules, TempsSupportsIso8601ParsingAndFormattingRoundTrip)
{
    // Mirrors what was manually verified against both engines on the CLI:
    // Z and numeric-offset inputs that name the same instant parse to the
    // same Instant, sub-second fractions truncate to milliseconds, and
    // formater_iso8601's output is always UTC/Z and round-trips exactly.
    const auto [output, completed] = execute_program(
        "importer Temps\n"
        "fonction executer() -> Résultat[Rien, Temps.ErreurTemps] {\n"
        "  soit i1 = Temps.analyser_iso8601(\"2024-07-01T12:00:00Z\") ou propager\n"
        "  afficher(\"formater: \" + Temps.formater_iso8601(i1))\n"
        "  soit i2 = Temps.analyser_iso8601(\"2024-07-01T14:00:00+02:00\") ou propager\n"
        "  afficher(\"memes horodatages: \" + (i1.en_horodatage() == i2.en_horodatage()))\n"
        "  soit i3 = Temps.analyser_iso8601(\"2024-07-01T14:30:00.250+0200\") ou propager\n"
        "  afficher(\"fraction: \" + Temps.formater_iso8601(i3))\n"
        "  soit roundtrip = Temps.analyser_iso8601(Temps.formater_iso8601(i3)) ou propager\n"
        "  afficher(\"aller-retour: \" + (roundtrip.en_horodatage() == i3.en_horodatage()))\n"
        "  retourne Succès(rien)\n"
        "}\n"
        "fonction principal() {\n"
        "  agir selon executer() {\n"
        "    Succès(_) -> rien\n"
        "    Échec(e) -> afficher(\"echec: \" + e.cause)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "formater: 2024-07-01T12:00:00.000Z\n"
        "memes horodatages: vrai\n"
        "fraction: 2024-07-01T12:30:00.250Z\n"
        "aller-retour: vrai\n");
}

TEST(InterpreterBuiltinModules, TempsSupportsNamedTimezoneConversionAcrossDstBoundaries)
{
    // Mirrors what was manually verified against both engines on the CLI,
    // and independently against Python's zoneinfo reading the same host
    // tzdata: dans_fuseau derives correct calendar fields and UTC offsets in
    // both a Northern-hemisphere zone (Europe/Paris, CEST in July, CET in
    // January) and a Southern-hemisphere one (Australia/Sydney, whose DST
    // months are inverted relative to Paris's).
    const auto [output, completed] = execute_program(
        "importer Temps\n"
        "fonction executer() -> Résultat[Rien, Temps.ErreurTemps] {\n"
        "  soit paris = Temps.fuseau(\"Europe/Paris\") ou propager\n"
        "  afficher(\"nom: \" + paris.nom())\n"
        "\n"
        "  soit été = Temps.analyser_iso8601(\"2024-07-01T12:00:00Z\") ou propager\n"
        "  soit dh_été = Temps.dans_fuseau(été, paris)\n"
        "  afficher(\"été: \" + dh_été.année() + \"-\" + dh_été.mois() + \"-\" + dh_été.jour() + \" \" + dh_été.heure() + \":\" + dh_été.minute() + \":\" + dh_été.seconde())\n"
        "  afficher(\"été décalage: \" + dh_été.décalage_utc_secondes())\n"
        "  afficher(\"dh.fuseau().nom(): \" + dh_été.fuseau().nom())\n"
        "  afficher(\"dh.instant(): \" + (dh_été.instant().en_horodatage() == été.en_horodatage()))\n"
        "\n"
        "  soit hiver = Temps.analyser_iso8601(\"2024-01-15T12:00:00Z\") ou propager\n"
        "  soit dh_hiver = Temps.dans_fuseau(hiver, paris)\n"
        "  afficher(\"hiver décalage: \" + dh_hiver.décalage_utc_secondes())\n"
        "\n"
        "  soit sydney = Temps.fuseau(\"Australia/Sydney\") ou propager\n"
        "  soit sydney_été = Temps.analyser_iso8601(\"2024-01-15T00:00:00Z\") ou propager\n"
        "  soit dh_sydney_été = Temps.dans_fuseau(sydney_été, sydney)\n"
        "  afficher(\"sydney janvier décalage: \" + dh_sydney_été.décalage_utc_secondes())\n"
        "  soit sydney_hiver = Temps.analyser_iso8601(\"2024-07-01T00:00:00Z\") ou propager\n"
        "  soit dh_sydney_hiver = Temps.dans_fuseau(sydney_hiver, sydney)\n"
        "  afficher(\"sydney juillet décalage: \" + dh_sydney_hiver.décalage_utc_secondes())\n"
        "\n"
        "  retourne Succès(rien)\n"
        "}\n"
        "fonction principal() {\n"
        "  agir selon executer() {\n"
        "    Succès(_) -> rien\n"
        "    Échec(e) -> afficher(\"echec: \" + e.cause)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "nom: Europe/Paris\n"
        "été: 2024-7-1 14:0:0\n"
        "été décalage: 7200\n"
        "dh.fuseau().nom(): Europe/Paris\n"
        "dh.instant(): vrai\n"
        "hiver décalage: 3600\n"
        "sydney janvier décalage: 39600\n"
        "sydney juillet décalage: 36000\n");
}

TEST(InterpreterBuiltinModules, TempsRejectsUnknownAndUnsafeTimezoneNamesAndAmbiguousIso8601)
{
    // Temps.fuseau must reject both a name that isn't a real IANA zone and
    // one crafted to escape the zoneinfo directory (path traversal), and
    // analyser_iso8601 must reject a timestamp with no Z/offset rather than
    // silently treating it as local time -- all three explicit Échecs, not
    // runtime errors, since a caller-supplied string is expected to fail
    // sometimes.
    const auto [output, completed] = execute_program(
        "importer Temps\n"
        "fonction principal() {\n"
        "  agir selon Temps.fuseau(\"Pas/UnFuseau\") {\n"
        "    Succès(_) -> afficher(\"inattendu: succès\")\n"
        "    Échec(e) -> afficher(\"erreur fuseau: \" + e.cause)\n"
        "  }\n"
        "  agir selon Temps.fuseau(\"../../../etc/passwd\") {\n"
        "    Succès(_) -> afficher(\"inattendu: succès traversal\")\n"
        "    Échec(e) -> afficher(\"erreur traversal: \" + e.cause)\n"
        "  }\n"
        "  agir selon Temps.analyser_iso8601(\"2024-07-01T12:00:00\") {\n"
        "    Succès(_) -> afficher(\"inattendu: succès sans fuseau\")\n"
        "    Échec(e) -> afficher(\"erreur sans fuseau: \" + e.cause)\n"
        "  }\n"
        "  agir selon Temps.analyser_iso8601(\"2024-13-01T12:00:00Z\") {\n"
        "    Succès(_) -> afficher(\"inattendu: succès mois invalide\")\n"
        "    Échec(e) -> afficher(\"erreur mois: \" + e.cause)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "erreur fuseau: fuseau horaire introuvable: Pas/UnFuseau\n"
        "erreur traversal: nom de fuseau horaire invalide: \"../../../etc/passwd\"\n"
        "erreur sans fuseau: fuseau UTC requis (Z ou décalage numérique)\n"
        "erreur mois: valeurs de date/heure invalides\n");
}

TEST(InterpreterBuiltinModules, TempsMonotonicClockMeasuresRealElapsedTimeAndNeverGoesNegative)
{
    // repère()/écoulé() use steady_clock, independent of Temps.horodatage's
    // wall clock. Sleeping ~30ms between the mark and the read must show up
    // as elapsed time bounded well below a full second (guards against a
    // unit mixup, e.g. nanoseconds mistaken for milliseconds), and écoulé
    // must never be negative even for a repère taken this instant.
    const auto [output, completed] = execute_program(
        "importer Temps\n"
        "fonction principal() {\n"
        "  soit r1 = Temps.repère()\n"
        "  Temps.attendre(Temps.millisecondes(30))\n"
        "  soit d1 = Temps.écoulé(r1)\n"
        "  afficher(\"au moins 25ms: \" + (d1.en_millisecondes() >= 25))\n"
        "  afficher(\"moins de 2000ms: \" + (d1.en_millisecondes() < 2000))\n"
        "  soit r2 = Temps.repère()\n"
        "  soit d2 = Temps.écoulé(r2)\n"
        "  afficher(\"jamais negatif: \" + (d2.en_millisecondes() >= 0))\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "au moins 25ms: vrai\n"
        "moins de 2000ms: vrai\n"
        "jamais negatif: vrai\n");
}

TEST(InterpreterBuiltinModules, LumiDessinSupportsColorConstructionAndComponentAccess)
{
    // couleur builds straight-alpha Couleur values (alpha defaults to
    // 255), couleur_hex accepts both #RRGGBB and #RRGGBBAA case-insensitively
    // and reports a structured Échec(ErreurCouleur) otherwise, and the
    // Couleurs.* constants match the table in docs/stdlib-lumidessin.md.
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction executer() -> Résultat[Rien, LumiDessin.ErreurCouleur] {\n"
        "  soit c1 = LumiDessin.couleur(10, 20, 30)\n"
        "  afficher(\"couleur: \" + c1.rouge() + \",\" + c1.vert() + \",\" + c1.bleu() + \",\" + c1.alpha())\n"
        "  soit c2 = LumiDessin.couleur(10, 20, 30, 40)\n"
        "  afficher(\"couleur+alpha: \" + c2.rouge() + \",\" + c2.vert() + \",\" + c2.bleu() + \",\" + c2.alpha())\n"
        "  soit c3 = LumiDessin.couleur_hex(\"#a1B2c3\") ou propager\n"
        "  afficher(\"hex6: \" + c3.rouge() + \",\" + c3.vert() + \",\" + c3.bleu() + \",\" + c3.alpha())\n"
        "  soit c4 = LumiDessin.couleur_hex(\"#000000FF\") ou propager\n"
        "  afficher(\"hex8: \" + c4.rouge() + \",\" + c4.vert() + \",\" + c4.bleu() + \",\" + c4.alpha())\n"
        "  afficher(\"blanc: \" + LumiDessin.Couleurs.blanc.rouge() + \",\" + LumiDessin.Couleurs.blanc.alpha())\n"
        "  afficher(\"transparent alpha: \" + LumiDessin.Couleurs.transparent.alpha())\n"
        "  retourne Succès(rien)\n"
        "}\n"
        "fonction principal() {\n"
        "  agir selon executer() {\n"
        "    Succès(_) -> rien\n"
        "    Échec(e) -> afficher(\"echec: \" + e.cause)\n"
        "  }\n"
        "  agir selon LumiDessin.couleur_hex(\"pas-hex\") {\n"
        "    Succès(_) -> afficher(\"inattendu: succès\")\n"
        "    Échec(e) -> afficher(\"echec attendu: \" + e.opération + \" / \" + e.valeur)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "couleur: 10,20,30,255\n"
        "couleur+alpha: 10,20,30,40\n"
        "hex6: 161,178,195,255\n"
        "hex8: 0,0,0,255\n"
        "blanc: 255,255\n"
        "transparent alpha: 0\n"
        "echec attendu: couleur_hex / pas-hex\n");
}

TEST(InterpreterBuiltinModules, LumiDessinSupportsOffScreenCanvasLifecycleClearPixelReadWriteAndCapture)
{
    // canevas starts opaque white and invisible; effacer replaces
    // every pixel including alpha; lire_pixel and capturer both observe the
    // replaced pixels; fermer is idempotent and est_ouvert reflects it.
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit c = LumiDessin.canevas(3, 2)\n"
        "  afficher(\"taille: \" + c.largeur() + \"x\" + c.hauteur())\n"
        "  afficher(\"visible: \" + c.est_visible())\n"
        "  soit blanc_initial = c.lire_pixel(0, 0)\n"
        "  afficher(\"blanc initial: \" + blanc_initial.rouge() + \",\" + blanc_initial.alpha())\n"
        "  c.effacer(LumiDessin.couleur(1, 2, 3, 4))\n"
        "  soit p = c.lire_pixel(2, 1)\n"
        "  afficher(\"apres effacer: \" + p.rouge() + \",\" + p.vert() + \",\" + p.bleu() + \",\" + p.alpha())\n"
        "  soit image = c.capturer()\n"
        "  afficher(\"capture: \" + image.largeur() + \"x\" + image.hauteur())\n"
        "  afficher(\"ouvert: \" + c.est_ouvert())\n"
        "  c.fermer()\n"
        "  c.fermer()\n"
        "  afficher(\"ouvert apres fermer x2: \" + c.est_ouvert())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "taille: 3x2\n"
        "visible: faux\n"
        "blanc initial: 255,255\n"
        "apres effacer: 1,2,3,4\n"
        "capture: 3x2\n"
        "ouvert: vrai\n"
        "ouvert apres fermer x2: faux\n");
}

TEST(InterpreterBuiltinModules, LumiDessinSupportsPointConstruction)
{
    // Point exposes x/y as plain readable fields (no parentheses), per
    // docs/stdlib-lumidessin.md's "Public types" section, and accepts
    // integer literals coerced to Décimal like every other Décimal
    // parameter in this stdlib.
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit p1 = LumiDessin.point(1.5, -2.25)\n"
        "  afficher(\"p1: \" + p1.x + \",\" + p1.y)\n"
        "  soit p2 = LumiDessin.point(3, 4)\n"
        "  afficher(\"p2: \" + p2.x + \",\" + p2.y)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "p1: 1.5,-2.25\np2: 3.0,4.0\n");
}

TEST(InterpreterBuiltinModules, LumiDessinRaisesRuntimeErrorsForInvalidDimensionsColorComponentsPixelBoundsAndClosedCanvas)
{
    // Invalid canvas dimensions, out-of-range color components, an
    // out-of-bounds pixel read, and any method but est_ouvert()/fermer() on
    // a closed canvas are all programmer-error runtime errors, not
    // Résultat failures -- docs/stdlib-lumidessin.md's Failure policy.
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() { LumiDessin.canevas(0, 5) }\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("LumiDessin.canevas attend des dimensions entre 1 et 16384"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() { LumiDessin.canevas(16384, 16384) }\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("LumiDessin.canevas attend un canevas d'au plus 16777216 pixels"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() { LumiDessin.couleur(0, 0, 0, 256) }\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("LumiDessin.couleur attend une composante de couleur entre 0 et 255"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(4, 4)\n"
                                       "  c.lire_pixel(4, 0)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.lire_pixel attend des coordonnées à l'intérieur du canevas"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(4, 4)\n"
                                       "  c.fermer()\n"
                                       "  c.effacer(LumiDessin.Couleurs.noir)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.effacer ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
}

TEST(InterpreterBuiltinModules, LumiDessinDrawingPrimitivesClipAndComposite)
{
    // dessiner_pixel, tracer_ligne, remplir_rectangle, remplir_cercle and
    // remplir_polygone all land where geometry says they should and leave
    // untouched pixels alone; a translucent fill over an opaque background
    // composites with the exact source-over formula from
    // docs/stdlib-lumidessin.md's "Compositing" section (verified against
    // both engines on the CLI before being pinned here); drawing past the
    // canvas edge is clipped rather than an error.
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit c = LumiDessin.canevas(10, 10)\n"
        "  c.effacer(LumiDessin.Couleurs.blanc)\n"
        "  c.dessiner_pixel(3, 3, LumiDessin.Couleurs.noir)\n"
        "  soit px = c.lire_pixel(3, 3)\n"
        "  afficher(\"pixel: \" + px.rouge() + \",\" + px.alpha())\n"
        "  c.tracer_ligne(-5.0, 5.0, 20.0, 5.0, LumiDessin.Couleurs.rouge, 3.0)\n"
        "  soit ligne = c.lire_pixel(5, 5)\n"
        "  afficher(\"ligne: \" + ligne.rouge() + \",\" + ligne.vert() + \",\" + ligne.bleu())\n"
        "  c.remplir_cercle(8.0, 8.0, 3.0, LumiDessin.Couleurs.vert)\n"
        "  soit cercle = c.lire_pixel(8, 8)\n"
        "  afficher(\"cercle: \" + cercle.rouge() + \",\" + cercle.vert() + \",\" + cercle.bleu())\n"
        "  soit coin = c.lire_pixel(0, 0)\n"
        "  afficher(\"coin intact: \" + coin.rouge() + \",\" + coin.vert() + \",\" + coin.bleu())\n"
        "  soit triangle = [LumiDessin.point(0.0, 0.0), LumiDessin.point(9.0, 0.0), LumiDessin.point(4.0, 9.0)]\n"
        "  soit c2 = LumiDessin.canevas(10, 10)\n"
        "  c2.effacer(LumiDessin.Couleurs.blanc)\n"
        "  c2.remplir_polygone(triangle, LumiDessin.Couleurs.noir)\n"
        "  soit dedans = c2.lire_pixel(4, 2)\n"
        "  soit dehors = c2.lire_pixel(9, 9)\n"
        "  afficher(\"polygone: \" + dedans.rouge() + \",\" + dehors.rouge())\n"
        "  soit c3 = LumiDessin.canevas(4, 4)\n"
        "  c3.effacer(LumiDessin.couleur(255, 0, 0))\n"
        "  c3.remplir_rectangle(0.0, 0.0, 4.0, 4.0, LumiDessin.couleur(0, 0, 255, 128))\n"
        "  soit fondu = c3.lire_pixel(2, 2)\n"
        "  afficher(\"fondu: \" + fondu.rouge() + \",\" + fondu.vert() + \",\" + fondu.bleu() + \",\" + fondu.alpha())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "pixel: 0,255\n"
        "ligne: 255,0,0\n"
        "cercle: 0,255,0\n"
        "coin intact: 255,255,255\n"
        "polygone: 0,255\n"
        "fondu: 127,0,128,255\n");
}

TEST(InterpreterBuiltinModules, LumiDessinTreatsZeroThicknessAndZeroAreaAsNoOpsAndArcSweepsClockwiseFromTheRight)
{
    // A zero-thickness stroke and a zero-area fill both leave the canvas
    // untouched (symmetric no-ops, not errors) per the Drawing primitives
    // section; tracer_arc's angle convention is 0 degrees pointing right,
    // sweeping clockwise (matching direct-drawing's y-down coordinates), and
    // it stops exactly at its swept endpoints rather than drawing a full ring.
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit c = LumiDessin.canevas(10, 10)\n"
        "  c.effacer(LumiDessin.Couleurs.blanc)\n"
        "  c.tracer_ligne(0.0, 0.0, 9.0, 9.0, LumiDessin.Couleurs.noir, 0.0)\n"
        "  c.remplir_rectangle(0.0, 0.0, 0.0, 5.0, LumiDessin.Couleurs.noir)\n"
        "  c.remplir_cercle(5.0, 5.0, 0.0, LumiDessin.Couleurs.noir)\n"
        "  soit intact = c.lire_pixel(5, 5)\n"
        "  afficher(\"toujours blanc: \" + intact.rouge() + \",\" + intact.vert() + \",\" + intact.bleu())\n"
        "\n"
        "  soit arc_c = LumiDessin.canevas(40, 40)\n"
        "  arc_c.effacer(LumiDessin.Couleurs.blanc)\n"
        "  arc_c.tracer_arc(20.0, 20.0, 10.0, 0.0, 90.0, LumiDessin.Couleurs.noir, 2.0)\n"
        "  soit droite = arc_c.lire_pixel(30, 20)\n"
        "  soit bas = arc_c.lire_pixel(20, 30)\n"
        "  soit gauche = arc_c.lire_pixel(10, 20)\n"
        "  soit haut = arc_c.lire_pixel(20, 10)\n"
        "  afficher(\"arc droite(0deg): \" + droite.rouge())\n"
        "  afficher(\"arc bas(90deg): \" + bas.rouge())\n"
        "  afficher(\"arc gauche(hors sweep): \" + gauche.rouge())\n"
        "  afficher(\"arc haut(hors sweep): \" + haut.rouge())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "toujours blanc: 255,255,255\n"
        "arc droite(0deg): 3\n"
        "arc bas(90deg): 3\n"
        "arc gauche(hors sweep): 255\n"
        "arc haut(hors sweep): 255\n");
}

TEST(InterpreterBuiltinModules, LumiDessinDrawingPrimitivesRaiseRuntimeErrorsForInvalidGeometry)
{
    // Negative fill dimensions, negative stroke thickness, a non-finite
    // coordinate, and too few points for a polyline/polygon are all
    // programmer errors (direct runtime errors), matching the same
    // Failure-policy distinction the rest of the module makes.
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.remplir_rectangle(0.0, 0.0, -1.0, 5.0, LumiDessin.Couleurs.noir)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.remplir_rectangle attend une valeur non négative"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.tracer_ligne(0.0, 0.0, 5.0, 5.0, LumiDessin.Couleurs.noir, -1.0)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.tracer_ligne attend une valeur non négative"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "importer Maths\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.tracer_ligne(Maths.infini, 0.0, 5.0, 5.0, LumiDessin.Couleurs.noir, 1.0)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.tracer_ligne attend une valeur numérique finie"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.tracer_polyligne([LumiDessin.point(0.0, 0.0)], faux, LumiDessin.Couleurs.noir, 1.0)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.tracer_polyligne attend au moins 2 points"), std::string::npos);
    }
    {
        const auto [output, completed, error] = execute_program_with_error(
            "importer LumiDessin\n"
            "fonction principal() {\n"
            "  soit c = LumiDessin.canevas(10, 10)\n"
            "  c.remplir_polygone([LumiDessin.point(0.0, 0.0), LumiDessin.point(1.0, 1.0)], LumiDessin.Couleurs.noir)\n"
            "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.remplir_polygone attend au moins 3 points"), std::string::npos);
    }
}

TEST(InterpreterBuiltinModules, LumiDessinMeasuresAndDrawsTextConsistentlyAcrossLinesTabsAndMissingGlyphs)
{
    // mesurer_texte and dessiner_texte share one layout path
    // (docs/stdlib-lumidessin.md, "Text"), so their agreement is checked
    // structurally here rather than by hard-coding font-metric pixel
    // values that would just be re-deriving stb_truetype's own output:
    // three lines measure exactly 3x one line's height, a tab advances
    // further than no tab, and an empty string is a single zero-width
    // line. A codepoint the bundled font has no glyph for (an emoji) must
    // not crash and must still paint something -- Inter's .notdef glyph is
    // a drawn box, not empty (see text.cpp's "Missing glyphs" comment).
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit c = LumiDessin.canevas(200, 120)\n"
        "  soit une_ligne = c.mesurer_texte(\"Bonjour\", 20)\n"
        "  soit trois_lignes = c.mesurer_texte(\"a\n"
        "b\n"
        "c\", 20)\n"
        "  afficher(\"hauteur x3: \" + (trois_lignes.hauteur == une_ligne.hauteur * 3))\n"
        "  soit vide = c.mesurer_texte(\"\", 20)\n"
        "  afficher(\"vide: \" + vide.largeur + \"x\" + (vide.hauteur == une_ligne.hauteur))\n"
        "  soit sans_tab = c.mesurer_texte(\"a\", 20)\n"
        "  soit avec_tab = c.mesurer_texte(\"a	b\", 20)\n"
        "  afficher(\"tab avance: \" + (avec_tab.largeur > sans_tab.largeur))\n"
        "  soit accents = c.mesurer_texte(\"éèàçùâêîôû\", 20)\n"
        "  afficher(\"accents mesurables: \" + (accents.largeur > 0))\n"
        "  c.effacer(LumiDessin.Couleurs.blanc)\n"
        "  c.dessiner_texte(\"Ai\", 10.0, 10.0, 40, LumiDessin.Couleurs.noir)\n"
        "  soit encre = faux\n"
        "  soit y = 0\n"
        "  tant que y < 120 {\n"
        "    soit x = 0\n"
        "    tant que x < 200 {\n"
        "      soit p = c.lire_pixel(x, y)\n"
        "      si p.rouge() != 255 { encre = vrai }\n"
        "      x = x + 1\n"
        "    }\n"
        "    y = y + 1\n"
        "  }\n"
        "  afficher(\"encre deposee: \" + encre)\n"
        "  c.effacer(LumiDessin.Couleurs.blanc)\n"
        "  c.dessiner_texte(\"🎉\", 10.0, 10.0, 60, LumiDessin.Couleurs.noir)\n"
        "  soit notdef_encre = faux\n"
        "  y = 0\n"
        "  tant que y < 120 {\n"
        "    soit x = 0\n"
        "    tant que x < 200 {\n"
        "      soit p = c.lire_pixel(x, y)\n"
        "      si p.rouge() != 255 { notdef_encre = vrai }\n"
        "      x = x + 1\n"
        "    }\n"
        "    y = y + 1\n"
        "  }\n"
        "  afficher(\"glyphe manquant dessine quelque chose: \" + notdef_encre)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "hauteur x3: vrai\n"
        "vide: 0xvrai\n"
        "tab avance: vrai\n"
        "accents mesurables: vrai\n"
        "encre deposee: vrai\n"
        "glyphe manquant dessine quelque chose: vrai\n");
}

TEST(InterpreterBuiltinModules, LumiDessinTextRaisesRuntimeErrorsForInvalidSizesAndClosedCanvas)
{
    // taille outside 1..1024 and any text method on a closed canvas are
    // programmer errors, matching every other Canevas method
    // (docs/stdlib-lumidessin.md's Failure policy).
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.mesurer_texte(\"x\", 0)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.mesurer_texte attend une taille entre 1 et 1024"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.dessiner_texte(\"x\", 0.0, 0.0, 1025, LumiDessin.Couleurs.noir)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.dessiner_texte attend une taille entre 1 et 1024"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.fermer()\n"
                                       "  c.mesurer_texte(\"x\", 20)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.mesurer_texte ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.fermer()\n"
                                       "  c.dessiner_texte(\"x\", 0.0, 0.0, 20, LumiDessin.Couleurs.noir)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.dessiner_texte ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
}

TEST(InterpreterBuiltinModules, LumiDessinSavesAndLoadsPngRoundTripAndDrawsImages)
{
    // enregistrer_png/charger_image round-trip losslessly; dessiner_image
    // copies 1:1, dessiner_image_redimensionnée/nette sample a scaled copy,
    // and opacité scales the composite -- all through the same blend_pixel
    // path raster.cpp's shapes use (docs/stdlib-lumidessin.md, "Images").
    const std::filesystem::path png_root = std::filesystem::temp_directory_path() / "lumiere_lumidessin_png_test";
    std::filesystem::create_directories(png_root);
    const std::filesystem::path png_path = png_root / "carre.png";
    const std::filesystem::path non_png_path = png_root / "pas_une_image.png";
    {
        std::ofstream junk(non_png_path, std::ios::binary);
        junk << "ceci n'est pas un PNG";
    }

    const std::string program =
        "importer LumiDessin\nfonction principal() {\n  soit source = LumiDessin.canevas(4, 4)\n  source.effacer(LumiDessin.Couleurs.blanc)\n  source.remplir_rectangle(1.0, 1.0, 2.0, 2.0, LumiDessin.Couleurs.rouge)\n  agir selon source.enregistrer_png(\""
        + lumiere_string_literal_text(png_path.string())
        + "\") {\n    Succès(_) -> afficher(\"sauvegarde: ok\")\n    Échec(e) -> afficher(\"BUG sauvegarde: \" + e.cause)\n  }\n  agir selon LumiDessin.charger_image(\""
        + lumiere_string_literal_text(png_path.string())
        + "\") {\n    Succès(img) -> {\n      afficher(\"chargee: \" + img.largeur() + \"x\" + img.hauteur())\n      soit copie = LumiDessin.canevas(4, 4)\n      copie.effacer(LumiDessin.Couleurs.blanc)\n      copie.dessiner_image(img, 0.0, 0.0)\n      soit coin = copie.lire_pixel(0, 0)\n      soit centre = copie.lire_pixel(2, 2)\n      afficher(\"copie coin: \" + coin.rouge() + \",\" + coin.vert() + \",\" + coin.bleu())\n      afficher(\"copie centre: \" + centre.rouge() + \",\" + centre.vert() + \",\" + centre.bleu())\n      soit agrandie = LumiDessin.canevas(8, 8)\n      agrandie.effacer(LumiDessin.Couleurs.blanc)\n      agrandie.dessiner_image_redimensionnée(img, 0.0, 0.0, 8.0, 8.0, 1.0)\n      soit p_agrandie = agrandie.lire_pixel(4, 4)\n      afficher(\"redim centre: \" + p_agrandie.rouge() + \",\" + p_agrandie.vert() + \",\" + p_agrandie.bleu())\n      soit demi_opacite = LumiDessin.canevas(8, 8)\n      demi_opacite.effacer(LumiDessin.Couleurs.blanc)\n      demi_opacite.dessiner_image_nette(img, 0.0, 0.0, 8.0, 8.0, 0.5)\n      soit p_demi = demi_opacite.lire_pixel(4, 4)\n      afficher(\"nette demi-opacite centre: \" + p_demi.rouge() + \",\" + p_demi.vert() + \",\" + p_demi.bleu())\n    }\n    Échec(e) -> afficher(\"BUG chargement: \" + e.cause)\n  }\n  agir selon LumiDessin.charger_image(\""
        + lumiere_string_literal_text((png_root / "absent.png").string())
        + "\") {\n    Succès(_) -> afficher(\"BUG: fichier absent charge\")\n    Échec(e) -> afficher(\"fichier absent: \" + e.cause)\n  }\n  agir selon LumiDessin.charger_image(\""
        + lumiere_string_literal_text(non_png_path.string())
        + "\") {\n    Succès(_) -> afficher(\"BUG: non-png charge\")\n    Échec(e) -> afficher(\"non-png: \" + e.cause)\n  }\n}\n";

    const auto [output, completed] = execute_program(program);

    std::filesystem::remove_all(png_root);

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "sauvegarde: ok\n"
        "chargee: 4x4\n"
        "copie coin: 255,255,255\n"
        "copie centre: 255,0,0\n"
        "redim centre: 255,0,0\n"
        "nette demi-opacite centre: 255,128,128\n"
        "fichier absent: impossible d'ouvrir le fichier\n"
        "non-png: format non pris en charge\n");
}

TEST(InterpreterBuiltinModules, LumiDessinImageRaisesRuntimeErrorsForInvalidGeometryOpacityAndClosedCanvas)
{
    // Negative width/height, opacité outside 0.0..1.0, and any image method
    // on a closed canvas are programmer errors, matching the rest of the
    // module's Failure policy.
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  soit img = c.capturer()\n"
                                       "  c.dessiner_image_redimensionnée(img, 0.0, 0.0, -1.0, 5.0, 1.0)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.dessiner_image_redimensionnée attend une valeur non négative"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  soit img = c.capturer()\n"
                                       "  c.dessiner_image_nette(img, 0.0, 0.0, 5.0, 5.0, 1.5)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.dessiner_image_nette attend une opacité entre 0.0 et 1.0"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  soit img = c.capturer()\n"
                                       "  c.fermer()\n"
                                       "  c.dessiner_image(img, 0.0, 0.0)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.dessiner_image ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.fermer()\n"
                                       "  c.enregistrer_png(\"/tmp/inaccessible.png\")\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.enregistrer_png ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
}

TEST(InterpreterBuiltinModules, LumiDessinCrayonMovesRotatesAndReportsStateConsistently)
{
    // Mirrors what was manually verified byte-identical against both
    // engines on the CLI: default state, avancer/reculer/tourner_*,
    // aller_à, recentrer (which also resets heading), lever/baisser,
    // régler_couleur/épaisseur/cap, montrer/cacher, and heading
    // normalization for a negative angle.
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit toile = LumiDessin.canevas(100, 100)\n"
        "  soit t = toile.crayon()\n"
        "  afficher(\"pos0: \" + t.position().x + \",\" + t.position().y)\n"
        "  afficher(\"cap0: \" + t.cap())\n"
        "  afficher(\"baisse0: \" + t.est_baissé())\n"
        "  afficher(\"visible0: \" + t.est_visible())\n"
        "  t.avancer(10.0)\n"
        "  afficher(\"apres avancer: \" + t.position().x + \",\" + t.position().y)\n"
        "  t.tourner_gauche(90.0)\n"
        "  afficher(\"cap apres gauche90: \" + t.cap())\n"
        "  t.avancer(5.0)\n"
        "  afficher(\"apres avancer2: \" + t.position().x + \",\" + t.position().y)\n"
        "  t.tourner_droite(180.0)\n"
        "  afficher(\"cap apres droite180: \" + t.cap())\n"
        "  t.reculer(5.0)\n"
        "  afficher(\"apres reculer x: \" + t.position().x)\n"
        "  t.aller_à(20.0, -30.0)\n"
        "  afficher(\"apres aller_a: \" + t.position().x + \",\" + t.position().y)\n"
        "  t.recentrer()\n"
        "  afficher(\"apres recentrer: \" + t.position().x + \",\" + t.position().y + \" cap=\" + t.cap())\n"
        "  t.lever()\n"
        "  afficher(\"baisse apres lever: \" + t.est_baissé())\n"
        "  t.avancer(50.0)\n"
        "  afficher(\"apres avancer leve: \" + t.position().x + \",\" + t.position().y)\n"
        "  t.baisser()\n"
        "  afficher(\"baisse apres baisser: \" + t.est_baissé())\n"
        "  t.régler_couleur(LumiDessin.couleur(255, 0, 0))\n"
        "  t.régler_épaisseur(3.0)\n"
        "  t.régler_cap(45.0)\n"
        "  afficher(\"cap apres regler_cap: \" + t.cap())\n"
        "  t.cacher()\n"
        "  afficher(\"visible apres cacher: \" + t.est_visible())\n"
        "  t.montrer()\n"
        "  afficher(\"visible apres montrer: \" + t.est_visible())\n"
        "  t.régler_cap(-30.0)\n"
        "  afficher(\"cap negatif normalise: \" + t.cap())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "pos0: 0.0,0.0\n"
        "cap0: 0.0\n"
        "baisse0: vrai\n"
        "visible0: vrai\n"
        "apres avancer: 10.0,0.0\n"
        "cap apres gauche90: 90.0\n"
        "apres avancer2: 10.0,5.0\n"
        "cap apres droite180: 270.0\n"
        "apres reculer x: 10.000000000000002\n"
        "apres aller_a: 20.0,-30.0\n"
        "apres recentrer: 0.0,0.0 cap=0.0\n"
        "baisse apres lever: faux\n"
        "apres avancer leve: 50.0,0.0\n"
        "baisse apres baisser: vrai\n"
        "cap apres regler_cap: 45.0\n"
        "visible apres cacher: faux\n"
        "visible apres montrer: vrai\n"
        "cap negatif normalise: 330.0\n");
}

TEST(InterpreterBuiltinModules, LumiDessinCrayonDrawsOnlyWhenPenIsDownAndIndependentlyOfOtherCrayons)
{
    // A crayon's trail is a real capsule stroke on the shared framebuffer
    // (raster_capsule, the same primitive tracer_ligne uses): its own color
    // and thickness, only while the pen is down. Two crayons created from
    // the same canvas move independently -- crayon() docs/stdlib-lumidessin
    // .md, "Each call creates independent crayon state".
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit toile = LumiDessin.canevas(20, 20)\n"
        "  toile.effacer(LumiDessin.Couleurs.blanc)\n"
        "  soit a = toile.crayon()\n"
        "  a.avancer(5.0)\n"
        "  soit trait = toile.lire_pixel(12, 10)\n"
        "  afficher(\"trait: \" + trait.rouge() + \",\" + trait.vert() + \",\" + trait.bleu())\n"
        "  soit loin = toile.lire_pixel(2, 2)\n"
        "  afficher(\"loin: \" + loin.rouge() + \",\" + loin.vert() + \",\" + loin.bleu())\n"
        "  soit b = toile.crayon()\n"
        "  b.tourner_gauche(90.0)\n"
        "  b.avancer(5.0)\n"
        "  afficher(\"a pos: \" + a.position().x + \",\" + a.position().y)\n"
        "  afficher(\"b pos y: \" + b.position().y)\n"
        "  afficher(\"a cap: \" + a.cap() + \" b cap: \" + b.cap())\n"
        "  soit c = toile.crayon()\n"
        "  c.lever()\n"
        "  c.tourner_droite(90.0)\n"
        "  c.avancer(3.0)\n"
        "  soit vide_sous_c = toile.lire_pixel(10, 13)\n"
        "  afficher(\"pixel sous c leve reste blanc: \" + vide_sous_c.rouge() + \",\" + vide_sous_c.vert() + \",\" +\n"
        "    vide_sous_c.bleu())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "trait: 128,128,128\n"
        "loin: 255,255,255\n"
        "a pos: 5.0,0.0\n"
        "b pos y: 5.0\n"
        "a cap: 0.0 b cap: 90.0\n"
        "pixel sous c leve reste blanc: 255,255,255\n");
}

TEST(InterpreterBuiltinModules, LumiDessinCrayonRaisesRuntimeErrorsForClosedCanvas)
{
    // A crayon operation after its canvas is closed raises a runtime error
    // at the crayon call site (docs/stdlib-lumidessin.md, "Crayon API"),
    // including crayon() itself, since it is a Canevas method like any
    // other that requires an open canvas.
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  c.fermer()\n"
                                       "  c.crayon()\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.crayon ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  soit t = c.crayon()\n"
                                       "  c.fermer()\n"
                                       "  t.avancer(1.0)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Crayon.avancer ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  soit t = c.crayon()\n"
                                       "  c.fermer()\n"
                                       "  t.position()\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Crayon.position ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
    {
        // The crayon itself keeps the canvas alive past fermer() (a strong
        // Ref<CanvasState>), so this is a documented use-after-close error,
        // never a use-after-free.
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit c = LumiDessin.canevas(10, 10)\n"
                                       "  soit t = c.crayon()\n"
                                       "  c.fermer()\n"
                                       "  t.régler_couleur(LumiDessin.Couleurs.rouge)\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Crayon.régler_couleur ne peut pas utiliser un canevas fermé"), std::string::npos);
    }
}

TEST(InterpreterBuiltinModules, LumiDessinCanevasFrameMethodsPaceAndReportElapsedTime)
{
    // Off-screen frame lifecycle (docs/stdlib-lumidessin.md, "Frame
    // lifecycle"): régler_cadence takes effect, présenter is a
    // valid no-op without a window, and écart_image is zero on the
    // first call and nonnegative thereafter -- exercised for real, at 240
    // fps (the shortest legal interval, so the real sleep this performs
    // stays small), since an off-screen canvas's cadence timing is a real
    // sleep by design (docs/stdlib-lumidessin.md, "Frame lifecycle": "For
    // an open off-screen canvas, prochaine_image performs cadence timing").
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit toile = LumiDessin.canevas(4, 4)\n"
        "  toile.régler_cadence(240)\n"
        "  soit premiere = toile.prochaine_image()\n"
        "  afficher(\"premiere: \" + premiere)\n"
        "  afficher(\"ecart1: \" + toile.écart_image())\n"
        "  soit deuxieme = toile.prochaine_image()\n"
        "  afficher(\"deuxieme: \" + deuxieme)\n"
        "  afficher(\"ecart2 non negatif: \" + (toile.écart_image() >= 0.0))\n"
        "  toile.présenter()\n"
        "  afficher(\"presente sans fenetre: ok\")\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "premiere: vrai\n"
        "ecart1: 0.0\n"
        "deuxieme: vrai\n"
        "ecart2 non negatif: vrai\n"
        "presente sans fenetre: ok\n");
}

TEST(InterpreterBuiltinModules, LumiDessinCanevasRejectsCadenceOutsideOneToTwoHundredForty)
{
    for (const char *bad : {"0", "241", "-1"})
    {
        const std::string source = std::string("importer LumiDessin\n"
                                                "fonction principal() {\n"
                                                "  soit toile = LumiDessin.canevas(4, 4)\n"
                                                "  toile.régler_cadence(") +
                                    bad + ")\n}\n";
        const auto [output, completed, error] = execute_program_with_error(source);
        EXPECT_FALSE(completed) << bad;
        EXPECT_TRUE(output.empty()) << bad;
        EXPECT_NE(error.find("Canevas.régler_cadence attend une cadence entre 1 et 240"), std::string::npos)
            << bad << ": " << error;
    }
}

TEST(InterpreterBuiltinModules, LumiDessinCanevasAttendreFermetureRaisesOffScreen)
{
    // docs/stdlib-lumidessin.md, "Frame lifecycle": "Calling it on an
    // off-screen canvas is a runtime error."
    const auto [output, completed, error] =
        execute_program_with_error("importer LumiDessin\n"
                                   "fonction principal() {\n"
                                   "  soit toile = LumiDessin.canevas(4, 4)\n"
                                   "  toile.attendre_fermeture()\n"
                                   "}\n");
    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("Canevas.attendre_fermeture attend un canevas visible"), std::string::npos);
}

TEST(InterpreterBuiltinModules, LumiDessinCanevasInputMethodsAreNeutralOffScreen)
{
    // docs/stdlib-lumidessin.md, "Input": "Input methods on an off-screen
    // canvas return neutral state. They do not raise."
    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit toile = LumiDessin.canevas(4, 4)\n"
        "  afficher(\"touche: \" + toile.touche_enfoncée(\"a\"))\n"
        "  afficher(\"pressee: \" + toile.touche_pressée(\"espace\"))\n"
        "  afficher(\"relachee: \" + toile.touche_relâchée(\"entrée\"))\n"
        "  afficher(\"texte: [\" + toile.texte_saisi() + \"]\")\n"
        "  afficher(\"souris: \" + toile.position_souris().x + \",\" + toile.position_souris().y)\n"
        "  afficher(\"presente: \" + toile.souris_présente())\n"
        "  afficher(\"bouton: \" + toile.bouton_enfoncé(\"gauche\"))\n"
        "  afficher(\"defilement: \" + toile.défilement().x + \",\" + toile.défilement().y)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(
        output,
        "touche: faux\n"
        "pressee: faux\n"
        "relachee: faux\n"
        "texte: []\n"
        "souris: 0.0,0.0\n"
        "presente: faux\n"
        "bouton: faux\n"
        "defilement: 0.0,0.0\n");
}

TEST(InterpreterBuiltinModules, LumiDessinCanevasInputRejectsUnknownNames)
{
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit toile = LumiDessin.canevas(4, 4)\n"
                                       "  toile.touche_enfoncée(\"xyz\")\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.touche_enfoncée ne reconnaît pas le nom de touche"), std::string::npos) << error;
    }
    {
        const auto [output, completed, error] =
            execute_program_with_error("importer LumiDessin\n"
                                       "fonction principal() {\n"
                                       "  soit toile = LumiDessin.canevas(4, 4)\n"
                                       "  toile.bouton_enfoncé(\"haut\")\n"
                                       "}\n");
        EXPECT_FALSE(completed);
        EXPECT_TRUE(output.empty());
        EXPECT_NE(error.find("Canevas.bouton_enfoncé ne reconnaît pas le nom de bouton"), std::string::npos) << error;
    }
}

#if LUMIERE_ENABLE_LUMIDESSIN_WINDOW

TEST(InterpreterBuiltinModules, LumiDessinFenetreOpensPresentsAndClosesUnderTheDummyDriver)
{
    // Bounded visible-window smoke test (docs/stdlib-lumidessin.md,
    // "Platform tests": "creates, presents, injects or receives a close
    // event, and exits"). SDL's dummy video driver needs no real display,
    // so this runs in ordinary headless CI; it is only compiled when
    // window support is built in. set_env_if_unset only sets it when unset,
    // so a CI environment that already configured SDL_VIDEODRIVER is respected.
    set_env_if_unset("SDL_VIDEODRIVER", "dummy");

    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit f = LumiDessin.fenêtre(64, 48, \"Test\")\n"
        "  afficher(\"visible: \" + f.est_visible())\n"
        "  f.effacer(LumiDessin.Couleurs.rouge)\n"
        "  f.présenter()\n"
        "  soit suite = f.prochaine_image()\n"
        "  afficher(\"suite: \" + suite)\n"
        "  f.fermer()\n"
        "  afficher(\"ouvert apres fermer: \" + f.est_ouvert())\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "visible: vrai\nsuite: vrai\nouvert apres fermer: faux\n");
}

TEST(InterpreterBuiltinModules, LumiDessinFenetreRaisesWhenAlreadyOpen)
{
    set_env_if_unset("SDL_VIDEODRIVER", "dummy");

    const auto [output, completed, error] =
        execute_program_with_error("importer LumiDessin\n"
                                   "fonction principal() {\n"
                                   "  soit a = LumiDessin.fenêtre(32, 32, \"A\")\n"
                                   "  soit b = LumiDessin.fenêtre(32, 32, \"B\")\n"
                                   "}\n");
    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("LumiDessin.fenêtre : une fenêtre visible est déjà ouverte"), std::string::npos) << error;
}

TEST(InterpreterBuiltinModules, LumiDessinFenetreRespondsToAnInjectedCloseEvent)
{
    // The real SDL close path: a genuine SDL_EVENT_QUIT is pushed onto
    // SDL's own event queue from a separate thread while the interpreter
    // thread blocks inside attendre_fermeture()'s event pump, exercising
    // the "injects or receives a close event" half of the platform smoke
    // test the doc describes, rather than only the fermer()-from-Lumière
    // path the previous test covers.
    set_env_if_unset("SDL_VIDEODRIVER", "dummy");
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO));

    std::thread closer([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    });

    const auto [output, completed] = execute_program(
        "importer LumiDessin\n"
        "fonction principal() {\n"
        "  soit f = LumiDessin.fenêtre(64, 48, \"Test\")\n"
        "  f.attendre_fermeture()\n"
        "  afficher(\"ouvert apres attendre_fermeture: \" + f.est_ouvert())\n"
        "}\n");

    closer.join();
    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "ouvert apres attendre_fermeture: faux\n");
}

#endif // LUMIERE_ENABLE_LUMIDESSIN_WINDOW

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
        "  afficher(\"42\".en_entier() ou propager)\n"
        "  afficher(\"3.14\".en_decimal() ou propager)\n"
        "  afficher(\"vrai\".en_logique() ou propager)\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Bonjour Monde\nBONJOUR MONDE\nlumiere\nfaux\nvrai\n3\nvrai\nvrai\nruojnob\nababab\nbonsoir\nbbb\nbonjour monde\n monde!\njour\nmonde!\n3\nb\na|b|c\n42\n3.14\nvrai\n");
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
    EXPECT_EQ(output, "19\nvrai\nvrai\n11\nvrai\nvrai\nBonjour, monde!\nbonjour\nBONJOUR\na|b|c\na|b\nbonjour lumiere\nbonjour, monde\n42\n3.14\nvrai\n");
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
    // Texte.convertir_decimal(2) is a Décimal, so it is written "2.0": that is
    // the language's one way of writing a Décimal, and the "2" pinned here was
    // a stream's default formatting, which also turned 123456789.125 into
    // "1.23457e+08".
    EXPECT_EQ(output, "0\nvrai\n-1\nvrai\nvrai\nvrai\n\n-abc\nabc-\nabc\na\nabc\n\n\n\nabc\nabc\nbaa\nbb\n1\n2\n1\n\n-42\n2.0\nfaux\n");
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
        "  afficher(\"peut-etre\".en_logique() ou propager)\n"
        "}\n");

    EXPECT_FALSE(completed3);
    EXPECT_TRUE(output3.empty());
    EXPECT_NE(error3.find("principal a échoué"), std::string::npos);

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
    EXPECT_NE(error7.find("longueur négative interdite"), std::string::npos);

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
            "n'accepte pas d'arguments nommés"
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
            "Texte.convertir_decimal attend une valeur numérique"
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
            "Texte.separer attend un séparateur non vide"
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
        "  afficher(\"Nom:\")\n"
        "  soit nom = lire() ou propager\n"
        "  soit age = lire_entier() ou propager\n"
        "  soit taille = lire_décimal() ou propager\n"
        "  soit actif = lire_logique() ou propager\n"
        "  afficher(nom)\n"
        "  afficher(age)\n"
        "  afficher(taille)\n"
        "  afficher(actif)\n"
        "}\n",
        "Ada\n36\n1.75\nvrai\n");

    EXPECT_TRUE(completed);
    EXPECT_EQ(output, "Nom:\nAda\n36\n1.75\nvrai\n");
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

    auto [output2, completed2, error2] = execute_program_with_input_and_error(
        "fonction principal() {\n"
        "  afficher(lire_logique() ou propager)\n"
        "}\n", "non_booleen\n");

    EXPECT_FALSE(completed2);
    EXPECT_TRUE(output2.empty());
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
    EXPECT_NE(error.find("aucun paramètre nommé"), std::string::npos);
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
        "  afficher(trio[1])\n"
        "  notes[1] = 9\n"
        "  afficher(trio[1])\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_TRUE(error.empty());
    // The conversion copies, so later writes to the source list do not reach the
    // fixed list.
    EXPECT_EQ(output, "3\n2\n2\n");
}

TEST(InterpreterFunctions, SupportsFixedListFactoryAndIteration)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit zeros = ListeFixe.remplir(Entier, 3, 0)\n"
        "  pour chaque valeur dans zeros {\n"
        "    afficher(valeur)\n"
        "  }\n"
        "}\n");

    EXPECT_TRUE(completed);
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(output, "0\n0\n0\n");
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

TEST(InterpreterFunctions, RejectsFixedListElementAssignment)
{
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit notes: Liste[Entier] = [1, 2, 3]\n"
        "  soit fixe trio = notes.en_liste_fixe(3)\n"
        "  trio[0] = 9\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("immuable"), std::string::npos);
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

TEST(InterpreterFunctions, RejectsAnUnrepresentableFixedListSizeInsteadOfLeakingACppException)
{
    // INT64_MAX elements can never fit in a std::vector<Value> on any real
    // machine: std::vector::assign throws std::length_error the moment the
    // request exceeds max_size(), before attempting any allocation, which
    // makes this deterministic and safe regardless of how much memory the
    // machine actually has.
    const auto [output, completed, error] = execute_program_with_error(
        "fonction principal() {\n"
        "  soit zeros = ListeFixe.remplir(Entier, 9223372036854775807, 0)\n"
        "}\n");

    EXPECT_FALSE(completed);
    EXPECT_TRUE(output.empty());
    EXPECT_NE(error.find("la taille demandée dépasse ce que cette opération peut représenter"), std::string::npos) << error;
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
    EXPECT_NE(error.find("paramètre 'n'"), std::string::npos);
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
