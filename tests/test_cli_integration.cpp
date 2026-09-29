#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

#ifndef LUMIERE_CLI_PATH
#error "LUMIERE_CLI_PATH must be defined for CLI integration tests"
#endif

#ifndef LUMIERE_VERSION
#error "LUMIERE_VERSION must be defined for CLI integration tests"
#endif

struct CommandResult
{
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
};

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// A path embedded in Lumière *source* text (not a shell command line --
// that is shell_quote's job) has to escape the Lumière lexer's own
// \n/\t/\\/\" syntax: on Windows, path.string() contains raw
// backslashes ("C:\Users\...") that the lexer otherwise tries to parse
// as escape sequences (producing "échappement invalide" errors).
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

std::string shell_quote(const std::string &text)
{
#ifdef _WIN32
    std::string quoted = "\"";
    for (char ch : text)
    {
        if (ch == '"')
        {
            quoted += "\\\"";
        }
        else
        {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : text)
    {
        if (ch == '\'')
        {
            quoted += "'\\''";
        }
        else
        {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

CommandResult run_cli(const std::string &args,
                      const std::filesystem::path &working_dir,
                      const std::string &stdin_text = {})
{
    // Repository examples share a directory when CTest runs tests in parallel.
    const std::string test_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    const std::filesystem::path stdout_path = working_dir / (test_name + "_stdout.txt");
    const std::filesystem::path stderr_path = working_dir / (test_name + "_stderr.txt");
    const std::filesystem::path stdin_path = working_dir / (test_name + "_stdin.txt");
    if (!stdin_text.empty())
    {
        std::ofstream input(stdin_path);
        input << stdin_text;
    }
#ifdef _WIN32
    const std::string command =
        "cmd /C \"\"" + std::string(LUMIERE_CLI_PATH) + "\" " + args +
        (stdin_text.empty() ? "" : " < \"" + stdin_path.string() + "\"") +
        " > \"" + stdout_path.string() +
        "\" 2> \"" + stderr_path.string() + "\"\"";
#else
    const std::string command =
        shell_quote(LUMIERE_CLI_PATH) + " " + args +
        (stdin_text.empty() ? "" : " < " + shell_quote(stdin_path.string())) +
        " > " + shell_quote(stdout_path.string()) +
        " 2> " + shell_quote(stderr_path.string());
#endif

    const int system_code = std::system(command.c_str());

    CommandResult result;
    result.exit_code = system_code;
    result.stdout_text = read_file(stdout_path);
    result.stderr_text = read_file(stderr_path);
    std::filesystem::remove(stdout_path);
    std::filesystem::remove(stderr_path);
    if (!stdin_text.empty())
        std::filesystem::remove(stdin_path);
    return result;
}

void write_source(const std::filesystem::path &path, const std::string &source)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << source;
}

std::filesystem::path repo_examples_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "examples";
}

TEST(CliIntegration, BothBackendsTrapNumericOverflowAndInvalidCasts)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_numeric_boundaries";
    const auto file = root / "main.lum";
    for (const auto *expression : {
             "9223372036854775807 + 1",
             "(-9223372036854775807 - 1) - 1",
             "9223372036854775807 * 2",
             "(-9223372036854775807 - 1) / -1",
             "-(-9223372036854775807 - 1)",
             "Maths.non_nombre en Entier",
             "Maths.infini en Entier",
             "9223372036854775808.0 en Entier",
             "Maths.plancher(Maths.non_nombre)",
             "Maths.arrondir(9223372036854775808.0)",
             "Maths.plafond(Maths.non_nombre)",
             "Maths.tronquer(Maths.non_nombre)",
             "\"12abc\" en Entier",
             "\"1.5abc\" en Décimal",
             "55296 en Symbole",
             "57343 en Symbole"})
    {
        write_source(file, "importer Maths\nfonction principal() {\n afficher(" +
                               std::string(expression) + ")\n}\n");
        for (const auto *backend : {"--vm", "--tw"})
        {
            SCOPED_TRACE(std::string(backend) + " " + expression);
            const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
            EXPECT_NE(result.exit_code, 0);
            EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos);
        }
    }
}

TEST(CliIntegration, BothBackendsPreserveRepresentableNumericBoundaries)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_numeric_valid";
    const auto file = root / "main.lum";
    write_source(file,
                 "importer Maths\nfonction principal() {\n"
                 " afficher((-9223372036854775807 - 1) % -1)\n"
                 " afficher((-9223372036854775808.0) en Entier)\n"
                 " afficher(Maths.racine_n(-8, 3))\n"
                 " afficher(Maths.racine_n(-8, -3))\n}\n");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "0\n-9223372036854775808\n-2.0\n-0.5\n");
    }
}

TEST(CliIntegration, ExecutesTreeWalkerProgramFromFile)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_exec_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  afficher(\"bonjour\")\n"
        "}\n");

    const CommandResult result = run_cli("--tw " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("bonjour"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty()) << result.stderr_text;
}

TEST(CliIntegration, ExecutesBareFileWithVmByDefault)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_default_vm_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(main_file,
                 "fonction principal() {\n"
                 "  afficher(\"vm par defaut\")\n"
                 "}\n");

    const CommandResult result = run_cli(shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "vm par defaut\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, VmReadBuiltinsSupportOptionalPrompts)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_read_prompt_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(main_file,
                 "fonction principal() -> Résultat[Rien, ErreurEntrée] {\n"
                 "  soit champ = \"Nom\"\n"
                 "  soit nom = lire(champ + \": \") ou propager\n"
                 "  soit age = lire_entier(\"\") ou propager\n"
                 "  afficher(nom, age)\n"
                 "  retourne Succès(rien)\n"
                 "}\n");

    const CommandResult result = run_cli(
        "--vm --run " + shell_quote(main_file.string()), root, "Ada\n36\n");
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "Nom: Ada 36\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, VmReadBuiltinsRejectInvalidPrompts)
{
    struct InvalidPromptCase
    {
        std::string expression;
        std::string expected_error;
    };

    const std::vector<InvalidPromptCase> cases = {
        {"lire(\"x\", \"y\")", "trop d'arguments positionnels"},
        {"lire_entier(42)", "l'argument 'invite' attend Texte"},
        {"lire(invite: \"Nom: \")", "cette fonction native n'accepte pas d'arguments nommés"},
    };

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_invalid_read_prompt_test";
    const std::filesystem::path main_file = root / "main.lum";
    for (const InvalidPromptCase &test_case : cases)
    {
        write_source(main_file,
                     "fonction principal() {\n  " + test_case.expression + "\n}\n");

        const CommandResult result = run_cli(
            "--vm --run " + shell_quote(main_file.string()), root);

        EXPECT_NE(result.exit_code, 0) << test_case.expression;
        EXPECT_TRUE(result.stdout_text.empty()) << test_case.expression;
        EXPECT_NE(result.stderr_text.find(test_case.expected_error), std::string::npos)
            << test_case.expression;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, ExecutesVmProgramWithMoreThan256Constants)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_constant_test";
    const std::filesystem::path main_file = root / "main.lum";

    std::ostringstream source;
    source << "fonction principal() {\n";
    for (int i = 0; i < 260; ++i)
    {
        source << "  \"" << i << "\"\n";
    }
    source << "}\n";

    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmProgramWithGlobalCallAfterMoreThan256Constants)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_constant_global_test";
    const std::filesystem::path main_file = root / "main.lum";

    std::ostringstream source;
    source << "fonction principal() {\n";
    for (int i = 0; i < 260; ++i)
    {
        source << "  \"" << i << "\"\n";
    }
    source << "  afficher(\"ok\")\n";
    source << "}\n";

    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "ok\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmProgramWithMoreThan256GlobalCalls)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_global_call_test";
    const std::filesystem::path main_file = root / "main.lum";

    std::ostringstream source;
    for (int i = 0; i < 260; ++i)
    {
        source << "fonction f" << i << "() {}\n";
    }
    source << "fonction principal() {\n";
    for (int i = 0; i < 260; ++i)
    {
        source << "  f" << i << "()\n";
    }
    source << "  afficher(\"globals longs\")\n";
    source << "}\n";

    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "globals longs\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmLongGlobalStoresAndLoads)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_global_store_test";
    const std::filesystem::path main_file = root / "main.lum";
    std::ostringstream source;
    for (int i = 0; i < 260; ++i)
    {
        source << "soit global" << i << ": Entier = " << i << "\n";
    }
    source << "fonction principal() {\n"
           << "  global259 = global259 + 1\n"
           << "  afficher(global259)\n"
           << "}\n";
    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "260\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, RejectsUnknownVmGlobalReference)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_global_value_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  afficher(inconnue)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    // The analyzer now refuses the name before either engine starts, so this
    // never reaches the VM's own lookup. That is the point: the two engines
    // used to report it from different code, with their carets a character
    // apart.
    EXPECT_NE(result.stderr_text.find("LUM-S0057"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("le symbole 'inconnue' n'est déclaré nulle part"),
              std::string::npos);
}

TEST(CliIntegration, ExecutesVmModuleGlobalsAndFirstClassFunctions)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_globals_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "soit compteur: Entier = 40\n"
        "fonction incrementer() { compteur = compteur + 1 }\n"
        "fonction principal() {\n"
        "  incrementer()\n"
        "  soit appelable = incrementer\n"
        "  appelable()\n"
        "  afficher(compteur)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "42\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmBranchAfterMoreThan256Constants)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_constant_branch_test";
    const std::filesystem::path main_file = root / "main.lum";

    std::ostringstream source;
    source << "fonction principal() {\n";
    for (int i = 0; i < 260; ++i)
    {
        source << "  \"" << i << "\"\n";
    }
    source << "  si (vrai) {\n";
    source << "    afficher(\"branche longue\")\n";
    source << "  }\n";
    source << "}\n";

    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "branche longue\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmArithmeticExpressions)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_arithmetic_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  afficher(1 + 2 * 3)\n"
        "  afficher((8 - 2) / 3)\n"
        "  afficher(17 % 5)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "7\n2\n2\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmLocalDeclarationsAndReads)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_locals_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x = 1 + 2\n"
        "  soit y = x * 4\n"
        "  afficher(y)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "12\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmUnaryMinusAndLocalAssignment)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_assignment_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x = 5\n"
        "  x = -x + 3\n"
        "  afficher(x)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "-2\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmIfElseBranches)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_if_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x = 0\n"
        "  si (vrai) {\n"
        "    x = 10\n"
        "  } sinon {\n"
        "    x = 20\n"
        "  }\n"
        "  afficher(x)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "10\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmComparisonsAndLogicalNot)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_comparisons_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x = 3\n"
        "  soit y = 5\n"
        "  si (non (x >= y)) {\n"
        "    afficher(1)\n"
        "  } sinon {\n"
        "    afficher(0)\n"
        "  }\n"
        "  afficher(x == 3)\n"
        "  afficher(x != y)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "1\nvrai\nvrai\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmShortCircuitLogicalOperators)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_logic_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x = 0\n"
        "  si (faux et (x = 1)) {\n"
        "    afficher(99)\n"
        "  } sinon {\n"
        "    afficher(1)\n"
        "  }\n"
        "  si (vrai ou (x = 2)) {\n"
        "    afficher(2)\n"
        "  } sinon {\n"
        "    afficher(99)\n"
        "  }\n"
        "  afficher(x)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "1\n2\n0\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmWhileLoops)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_while_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x = 0\n"
        "  soit somme = 0\n"
        "  tant que (x < 4) {\n"
        "    somme = somme + x\n"
        "    x = x + 1\n"
        "  }\n"
        "  afficher(somme)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "6\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmContinueInsideWhileLoop)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_continue_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x = 0\n"
        "  soit somme = 0\n"
        "  tant que (x < 5) {\n"
        "    x = x + 1\n"
        "    si (x == 3) {\n"
        "      continuer\n"
        "    }\n"
        "    somme = somme + x\n"
        "  }\n"
        "  afficher(somme)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "12\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmBreakInsideWhileLoop)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_break_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x = 0\n"
        "  soit somme = 0\n"
        "  tant que (vrai) {\n"
        "    x = x + 1\n"
        "    si (x > 3) {\n"
        "      arreter\n"
        "    }\n"
        "    somme = somme + x\n"
        "  }\n"
        "  afficher(somme)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "6\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmUserDefinedFunctionCalls)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_function_call_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction somme4(a: Entier, b: Entier, c: Entier, d: Entier) {\n"
        "  retourne a + b + c + d\n"
        "}\n"
        "\n"
        "fonction principal() {\n"
        "  afficher(somme4(1, 2, 3, 4))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "10\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmDefaultAndNamedArguments)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_default_named_args_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction calculer(a: Entier, b: Entier = a + 1, c: Entier = b + 1) {\n"
        "  retourne a + b + c\n"
        "}\n"
        "\n"
        "fonction paire(a: Entier, b: Entier) {\n"
        "  retourne a * 10 + b\n"
        "}\n"
        "\n"
        "fonction principal() {\n"
        "  afficher(calculer(10))\n"
        "  afficher(calculer(10, c: 30))\n"
        "  soit trace = 0\n"
        "  afficher(paire(b: (trace = trace * 10 + 2), a: (trace = trace * 10 + 1)))\n"
        "  afficher(trace)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "33\n51\n212\n21\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, RejectsInvalidVmNamedArguments)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_invalid_named_args_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction identite(valeur: Entier) {\n"
        "  retourne valeur\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(identite(inconnu: 1))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("paramètre nommé inconnu: 'inconnu'"), std::string::npos);
}

TEST(CliIntegration, EnforcesVmParameterAndReturnTypes)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_function_type_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction identite(valeur: Entier) -> Entier {\n"
        "  retourne valeur\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(identite(\"incorrect\"))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("attend Entier; reçu Texte"), std::string::npos);
}

TEST(CliIntegration, EnforcesVmLocalAssignmentTypes)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_local_type_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit valeur: Entier = 1\n"
        "  valeur = \"incorrect\"\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("attend Entier; reçu Texte"), std::string::npos);
}

TEST(CliIntegration, RejectsVmAssignmentToFixedLocal)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_fixed_local_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit fixe valeur = 1\n"
        "  valeur = 2\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    // Caught by the analyzer now, so both engines are told the same thing before
    // either of them starts, and the message names the binding.
    EXPECT_NE(result.stderr_text.find("LUM-S0056"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("'valeur' est fixe et ne peut pas être réaffecté"),
              std::string::npos);
}

TEST(CliIntegration, EnforcesVmExplicitAndImplicitReturnTypes)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_return_type_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction incorrecte() -> Entier {\n"
        "  retourne \"incorrect\"\n"
        "}\n"
        "fonction principal() {\n"
        "  incorrecte()\n"
        "}\n");

    const CommandResult explicit_result = run_cli("--vm --run " + shell_quote(main_file.string()), root);

    write_source(
        main_file,
        "fonction incomplete() -> Entier {}\n"
        "fonction principal() {\n"
        "  incomplete()\n"
        "}\n");
    const CommandResult implicit_result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(explicit_result.exit_code, 0);
    EXPECT_NE(explicit_result.stderr_text.find("attend Entier; reçu Texte"), std::string::npos);
    EXPECT_NE(implicit_result.exit_code, 0);
    // Both engines now word this the same way, and both name the function whose
    // declared return was not met rather than only the types involved.
    EXPECT_NE(implicit_result.stderr_text.find(
                  "la fonction 'incomplete' attend une valeur de type Entier; type reçu : Rien"),
              std::string::npos);
}

TEST(CliIntegration, ExecutesVmRecursiveFunctionCalls)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_recursive_call_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction factorielle(n: Entier) {\n"
        "  si (n <= 1) {\n"
        "    retourne 1\n"
        "  }\n"
        "  retourne n * factorielle(n - 1)\n"
        "}\n"
        "\n"
        "fonction principal() {\n"
        "  afficher(factorielle(5))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "120\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmDeepRecursionWithExplicitFrames)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_deep_recursion_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction descendre(n: Entier) {\n"
        "  si (n == 0) {\n"
        "    retourne 0\n"
        "  }\n"
        "  retourne descendre(n - 1)\n"
        "}\n"
        "\n"
        "fonction incrementer(n: Entier) {\n"
        "  retourne n + 1\n"
        "}\n"
        "\n"
        "fonction additionner(a: Entier, b: Entier) {\n"
        "  retourne a + b\n"
        "}\n"
        "\n"
        "fonction principal() {\n"
        "  afficher(descendre(10000))\n"
        "  afficher(additionner(incrementer(1), incrementer(4)))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "0\n7\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmAnonymousFunctionsAndMutableClosures)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_closure_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction fabriquer(depart: Entier) {\n"
        "  soit compteur = depart\n"
        "  retourne fonction(pas: Entier = 1) -> Entier {\n"
        "    compteur = compteur + pas\n"
        "    retourne compteur\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  soit premier = fabriquer(10)\n"
        "  soit second = fabriquer(100)\n"
        "  afficher(premier())\n"
        "  afficher(premier(5))\n"
        "  afficher(second())\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "11\n16\n101\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, BothBackendsShareCapturedLocalsBeforeAndAfterReturn)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_shared_capture_cells";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
fonction fabriquer() {
    soit valeur = 10
    soit lire = fonction() { retourne valeur }
    valeur = 20
    soit augmenter = fonction() {
        valeur = valeur + 1
        retourne valeur
    }
    afficher(lire())
    augmenter()
    afficher(valeur)
    retourne [lire, augmenter]
}
fonction principal() {
    soit actions = fabriquer()
    afficher(actions[0]())
    afficher(actions[1]())
    afficher(actions[0]())
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "20\n21\n21\n22\n22\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, ExecutesVmTransitiveClosureCaptures)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_transitive_closure_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction fabriquer() {\n"
        "  soit valeur = 40\n"
        "  fonction milieu() {\n"
        "    fonction interieur() {\n"
        "      valeur = valeur + 1\n"
        "      retourne valeur\n"
        "    }\n"
        "    retourne interieur\n"
        "  }\n"
        "  retourne milieu()\n"
        "}\n"
        "fonction principal() {\n"
        "  soit suivant = fabriquer()\n"
        "  afficher(suivant())\n"
        "  afficher(suivant())\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "41\n42\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmRecursiveNestedFunctions)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_nested_recursion_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  fonction factorielle(n: Entier) -> Entier {\n"
        "    si (n <= 1) { retourne 1 }\n"
        "    retourne n * factorielle(n - 1)\n"
        "  }\n"
        "  afficher(factorielle(5))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "120\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmListLiterals)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_list_literal_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  afficher([1, 2, 3])\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "[1, 2, 3]\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmDictionaryLiteralsAndIndexReads)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_dictionary_read_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit valeurs = {\"nom\": \"Lumiere\", 7: 42, 'é': vrai}\n"
        "  afficher(valeurs[\"nom\"])\n"
        "  afficher(valeurs[7])\n"
        "  afficher(valeurs['é'])\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "Lumiere\n42\nvrai\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmIndexAssignments)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_index_assignment_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit valeurs = [10, 20]\n"
        "  afficher(valeurs[1] = 99)\n"
        "  afficher(valeurs)\n"
        "  soit options = {\"mode\": \"lent\"}\n"
        "  options[\"mode\"] = \"rapide\"\n"
        "  options[\"niveau\"] = 3\n"
        "  afficher(options)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "99\n[10, 99]\n{mode: rapide, niveau: 3}\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmListAndDictionaryMemberCalls)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_collection_members_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit xs = [1, 2]\n"
        "  afficher(xs.taille())\n"
        "  afficher(xs.vide())\n"
        "  afficher(xs.contient(2))\n"
        "  afficher(xs.ajouter(3))\n"
        "  xs.inserer(1, 99)\n"
        "  afficher(xs.joindre(\",\"))\n"
        "  afficher(xs.retirer_a(1))\n"
        "  afficher(xs.en_liste_fixe(3).en_liste().joindre(\"-\"))\n"
        "  soit d = {\"a\": 1, \"b\": 2}\n"
        "  afficher(d.taille())\n"
        "  afficher(d.contient(\"a\"))\n"
        "  afficher(d.cles().joindre(\"|\"))\n"
        "  afficher(d.valeurs().joindre(\"|\"))\n"
        "  afficher(d.paires()[0].taille())\n"
        "  afficher(d.retirer(\"a\"))\n"
        "  afficher(d.taille())\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "2\nfaux\nvrai\n3\n1,99,2,3\n99\n1-2-3\n2\nvrai\na|b\n1|2\n2\n1\n1\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmTextMemberCalls)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_text_members_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit texte = \"  Bonjour Monde  \"\n"
        "  afficher(texte.elaguer().majuscules())\n"
        "  afficher(\"LUMIERE\".minuscules())\n"
        "  afficher(\"bonjour\".contient(\"jour\"))\n"
        "  afficher(\"bonjour\".index_de(\"jour\"))\n"
        "  afficher(\"ab\".repeter(3))\n"
        "  afficher(\"bonjour\".remplacer(\"jour\", \"soir\"))\n"
        "  afficher(\"a,b,c\".separer(\",\").joindre(\"|\"))\n"
        "  afficher(\"42\".en_entier())\n"
        "  afficher(\"3.14\".en_decimal())\n"
        "  afficher(\"vrai\".en_logique())\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "BONJOUR MONDE\nlumiere\nvrai\n3\nababab\nbonsoir\na|b|c\nSuccès(42)\nSuccès(3.14)\nSuccès(vrai)\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmProgramWithMoreThan256MemberNames)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_member_test";
    const std::filesystem::path main_file = root / "main.lum";
    std::ostringstream source;
    source << "fonction principal() {\n";
    source << "  si (faux) {\n";
    for (int i = 0; i < 260; ++i)
    {
        source << "    [1].m" << i << "()\n";
    }
    source << "  }\n";
    source << "  afficher([1, 2, 3].taille())\n";
    source << "}\n";
    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "3\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmLongObjectMemberStores)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_object_member_store_test";
    const std::filesystem::path main_file = root / "main.lum";
    std::ostringstream source;
    source << "classe Large {\n";
    for (int i = 0; i < 259; ++i)
    {
        source << "  fonction membre" << i << "() {}\n";
    }
    source << "  champ259: Universel\n";
    source << "}\nfonction principal() {\n"
           << "  soit objet = Large(champ259: rien)\n"
           << "  objet.champ259 = 42\n"
           << "  afficher(objet.champ259)\n"
           << "}\n";
    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "42\n");
    EXPECT_TRUE(result.stderr_text.empty()) << result.stderr_text;
}

TEST(CliIntegration, ExecutesVmBoundNativeMemberValues)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_bound_member_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit valeurs: Liste[Entier] = [1, 2]\n"
        "  soit taille = valeurs.taille\n"
        "  soit ajouter = valeurs.ajouter\n"
        "  afficher(taille())\n"
        "  afficher(ajouter(3))\n"
        "  afficher(valeurs)\n"
        "  soit majuscules = \"bonjour\".majuscules\n"
        "  afficher(majuscules())\n"
        "  soit cles = {\"a\": 1}.cles\n"
        "  afficher(cles().joindre(\"|\"))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "2\n3\n[1, 2, 3]\nBONJOUR\na\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmLongBoundMemberReference)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_bound_member_test";
    const std::filesystem::path main_file = root / "main.lum";
    std::ostringstream source;
    source << "fonction principal() {\n";
    source << "  si (faux) {\n";
    for (int i = 0; i < 260; ++i)
    {
        source << "    [1].m" << i << "()\n";
    }
    source << "  }\n";
    source << "  soit taille = [1, 2].taille\n";
    source << "  afficher(taille())\n";
    source << "}\n";
    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "2\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, EnforcesVmGenericListMutationsThroughAliases)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_generic_list_mutation_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit valeurs: Liste[Entier] = [1, 2]\n"
        "  soit alias = valeurs\n"
        "  alias.ajouter(\"incorrect\")\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("Liste.ajouter attend une valeur de type Entier"), std::string::npos);
}

TEST(CliIntegration, EnforcesVmGenericIndexedMutations)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_generic_index_mutation_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit valeurs: Dictionnaire[Texte, Entier] = {\"un\": 1}\n"
        "  valeurs[\"deux\"] = \"incorrect\"\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("l'entrée du dictionnaire (valeur) attend une valeur de type Entier"), std::string::npos);
}

TEST(CliIntegration, PreservesVmGenericTypesFromNativeMemberResults)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_native_result_type_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit morceaux = \"a,b\".separer(\",\")\n"
        "  morceaux.ajouter(3)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("Liste.ajouter attend une valeur de type Texte"), std::string::npos);
}

TEST(CliIntegration, RejectsVmDictionaryLookupForMissingKey)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_dictionary_missing_key_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit valeurs = {\"present\": 1}\n"
        "  afficher(valeurs[\"absent\"])\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("clé introuvable"), std::string::npos);
}

TEST(CliIntegration, BothBackendsUseUnicodeScalarTextPositions)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_unicode_positions";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
importer Texte
fonction principal() {
    soit texte = "aé中😀"
    afficher(texte.taille())
    afficher(Texte.taille(texte))
    afficher(texte.index_de("😀"))
    afficher(texte.index_de("absent"))
    afficher(texte.inverser())
    afficher(texte.inserer(2, "界"))
    afficher(texte.inserer(4, "!"))
    afficher(texte.supprimer(1, 2))
    afficher(texte.supprimer(1, 9223372036854775807))
    afficher(texte.sous_texte(1, 2))
    afficher(texte.sous_texte(3))
    afficher(texte.sous_texte(4, 0))
    afficher("".repeter(9223372036854775807))
    afficher("é".taille())
    pour chaque ch dans texte { afficher(ch) }
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "4\n4\n3\n-1\n😀中éa\naé界中😀\naé中😀!\na😀\na\né中\n😀\n\n\n2\na\né\n中\n😀\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsRejectInvalidTextPositions)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_unicode_bounds";
    const auto file = root / "main.lum";
    for (const auto *expression : {"\"é😀\".inserer(3, \"x\")",
                                   "\"é😀\".supprimer(-1, 1)",
                                   "\"é😀\".sous_texte(1, 9223372036854775807)",
                                   "\"é😀\".sous_texte(3)"})
    {
        write_source(file, "fonction principal() { afficher(" + std::string(expression) + ") }\n");
        for (const auto *backend : {"--vm", "--tw"})
        {
            SCOPED_TRACE(std::string(backend) + " " + expression);
            const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
            EXPECT_NE(result.exit_code, 0);
            EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos);
        }
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsIterateASnapshotWhenListChanges)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_iteration_snapshot";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
fonction principal() {
    soit valeurs = [1, 2, 3]
    pour chaque valeur dans valeurs {
        afficher(valeur)
        si valeur == 1 {
            valeurs[1] = 99
            valeurs.ajouter(4)
        }
    }
    afficher(valeurs)
    pour chaque ch dans "" { afficher("incorrect") }
    pour chaque valeur dans [] { afficher("incorrect") }
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "1\n2\n3\n[1, 99, 3, 4]\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, ExecutesVmForEachLoopsOverLists)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_for_list_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit somme = 0\n"
        "  pour chaque valeur dans [4, 7, 10] {\n"
        "    somme = somme + valeur\n"
        "  }\n"
        "  afficher(somme)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "21\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmForEachLoopsOverText)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_for_text_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  pour chaque ch dans \"été\" {\n"
        "    afficher(ch)\n"
        "  }\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "é\nt\né\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmIndexAccessReads)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_index_read_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit valeurs = [10, 20, 30]\n"
        "  afficher(valeurs[1])\n"
        "  afficher(\"été\"[2])\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "20\né\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmTruthinessSemantics)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_truthiness_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  si (1) {\n"
        "    afficher(non rien)\n"
        "  }\n"
        "  si (non faux) {\n"
        "    afficher(non 42)\n"
        "  }\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "vrai\nfaux\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmSymbolLiterals)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_symbol_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  afficher('é')\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "é\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmExplicitCasts)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_cast_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  afficher(42 en Décimal)\n"
        "  afficher(\"123\" en Entier)\n"
        "  afficher(\"vrai\" en Logique)\n"
        "  afficher('A' en Entier)\n"
        "  afficher(66 en Symbole)\n"
        "  afficher(9 en Texte)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "42.0\n123\nvrai\n65\nB\n9\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmRuntimeTypeChecks)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_type_check_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  afficher(1 est Entier)\n"
        "  afficher(1 est Décimal)\n"
        "  afficher([1, 2] est Liste[Entier])\n"
        "  afficher([1, \"deux\"] est Liste[Entier])\n"
        "  afficher({\"un\": 1} est Dictionnaire[Texte, Entier])\n"
        "  afficher(rien est Rien)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "vrai\nvrai\nvrai\nfaux\nvrai\nvrai\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmProgramWithMoreThan256TypeReferences)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_long_type_test";
    const std::filesystem::path main_file = root / "main.lum";

    std::ostringstream source;
    for (int i = 0; i < 260; ++i)
    {
        source << "classe Type" << i << " {}\n";
    }
    source << "fonction principal() {\n";
    for (int i = 0; i < 260; ++i)
    {
        source << "  1 est Type" << i << "\n";
    }
    source << "  si (vrai) {\n";
    source << "    afficher(\"types longs\")\n";
    source << "  }\n";
    source << "}\n";

    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "types longs\n");
    EXPECT_TRUE(result.stderr_text.empty()) << result.stderr_text;
}

TEST(CliIntegration, ExecutesVmAgirSelonLiteralAndElseBranches)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_agir_literal_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction choisir(valeur: Entier) {\n"
        "  agir selon valeur {\n"
        "    1 -> afficher(\"un\")\n"
        "    2, 3 -> afficher(\"petit\")\n"
        "    sinon -> afficher(\"autre\")\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  choisir(2)\n"
        "  choisir(9)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "petit\nautre\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesVmAgirSelonTypedAndRienPatterns)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_agir_typed_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction choisir(valeur: Universel) {\n"
        "  agir selon valeur {\n"
        "    n: Entier -> afficher(n)\n"
        "    rien -> afficher(\"vide\")\n"
        "    sinon -> afficher(\"autre\")\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  choisir(7)\n"
        "  choisir(rien)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "7\nvide\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, RejectsVmAgirSelonWithoutMatchingBranch)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_agir_no_match_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  agir selon 9 {\n"
        "    1 -> afficher(\"un\")\n"
        "  }\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("aucune branche de 'agir selon' ne correspond"), std::string::npos);
}

TEST(CliIntegration, ExecutesRepositoryBonjourExample)
{
    const std::filesystem::path example_file = repo_examples_dir() / "bonjour.lum";
    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(example_file.string()),
                                         example_file.parent_path());

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Bonjour, monde!"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("La moyenne des nombres est:"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesRepositoryCollectionsExample)
{
    const std::filesystem::path example_file = repo_examples_dir() / "analyse_collections.lum";
    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(example_file.string()),
                                         example_file.parent_path());

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("notes=4"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("Ada depuis Paris"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("vrai"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesRepositoryModuleExample)
{
    const std::filesystem::path example_file = repo_examples_dir() / "modules" / "main.lum";
    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(example_file.string()),
                                         example_file.parent_path());

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("statistiques"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("52"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("13"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesRepositoryObjectsExample)
{
    const std::filesystem::path example_file = repo_examples_dir() / "objets.lum";
    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(example_file.string()),
                                         example_file.parent_path());

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "compteur=3\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesRepositoryContractsExample)
{
    const std::filesystem::path example_file = repo_examples_dir() / "contrats" / "main.lum";
    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(example_file.string()),
                                         example_file.parent_path());

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("trace:presenter"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("[DOC:RAPPORT]"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("tags=2"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ResolvesExampleNameWithoutExtension)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_examples_test";
    const std::filesystem::path examples_dir = root / "examples";
    const std::filesystem::path example_file = examples_dir / "hello.lum";
    write_source(
        example_file,
        "fonction principal() {\n"
        "  afficher(\"depuis-examples\")\n"
        "}\n");

    const auto previous_cwd = std::filesystem::current_path();
    std::filesystem::current_path(root);
    const CommandResult result = run_cli("--tree-walker --run hello", root);
    std::filesystem::current_path(previous_cwd);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("depuis-examples"), std::string::npos);
}

TEST(CliIntegration, ReportsMissingInputFile)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_missing_file_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote((root / "absent.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("impossible d'ouvrir"), std::string::npos);
}

TEST(CliIntegration, ReportsUnknownOption)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_bad_option_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli("--pas-une-option", root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("option inconnue"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("usage: lumiere"), std::string::npos);
}

TEST(CliIntegration, RejectsConflictingBackendSelectors)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_backend_conflict_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(main_file, "fonction principal() {}\n");

    const CommandResult result = run_cli("--vm --tw " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("un seul backend"), std::string::npos);
}

TEST(CliIntegration, ReportsMissingFileArgument)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_missing_arg_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli("--tree-walker --run", root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("aucun fichier .lum fourni"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("usage: lumiere"), std::string::npos);
}

TEST(CliIntegration, PrintsVersion)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_version_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli("--version", root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find(std::string("Lumiere ") + LUMIERE_VERSION), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, PrintsLinkedIrAndBytecode)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_inspection_test";
    const std::filesystem::path source_file = root / "main.lum";
    const std::filesystem::path library_file = root / "Calculs.lum";
    write_source(source_file,
                 "fonction principal() {\n"
                 "  afficher(1 + 2)\n"
                 "}\n");
    write_source(library_file, "public fonction doubler(x: Entier) { retourne x * 2 }\n");

    const CommandResult ir = run_cli("ir " + shell_quote(source_file.string()), root);
    const CommandResult bytecode = run_cli("bytecode " + shell_quote(source_file.string()), root);
    const CommandResult library_ir = run_cli("ir " + shell_quote(library_file.string()), root);
    const CommandResult library_bytecode = run_cli("bytecode " + shell_quote(library_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(ir.exit_code, 0);
    EXPECT_NE(ir.stdout_text.find("function principal"), std::string::npos);
    EXPECT_NE(ir.stdout_text.find("IR_OP_ADD"), std::string::npos);
    EXPECT_TRUE(ir.stderr_text.empty());

    EXPECT_EQ(bytecode.exit_code, 0);
    EXPECT_NE(bytecode.stdout_text.find("function 0 principal"), std::string::npos);
    EXPECT_NE(bytecode.stdout_text.find("0004  ADD"), std::string::npos);
    EXPECT_TRUE(bytecode.stderr_text.empty());

    EXPECT_EQ(library_ir.exit_code, 0);
    EXPECT_NE(library_ir.stdout_text.find("function doubler"), std::string::npos);
    EXPECT_EQ(library_bytecode.exit_code, 0);
    EXPECT_NE(library_bytecode.stdout_text.find("function 0 doubler"), std::string::npos);
}

/**
 * @brief The shell's analysis carries what earlier lines declared.
 *
 * Nothing typed two dependent lines until this test, which is how LUM-S0055
 * shipped refusing `base = 60` after `soit base = 40`: the assignment was
 * rejected as being to an undeclared name, the line never ran, and the shell
 * then printed the old value as though nothing had happened.
 */
TEST(CliIntegration, ReplResolvesNamesDeclaredByEarlierSubmissions)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_repl_names_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli("", root,
                                         "soit base = 40\n"
                                         "base = 60\n"
                                         "base\n"
                                         "soit base = 99\n"
                                         "fonction ajouter(x: Entier) -> Entier {\n"
                                         "  retourne base + x\n"
                                         "}\n"
                                         "ajouter(2)\n"
                                         "classe Boite { valeur: Entier }\n"
                                         "Boite(valeur: 7).valeur\n"
                                         "soit fantome = 1 / 0\n"
                                         "fantome\n"
                                         "nom_absent\n"
                                         ":quitter\n");
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    // The assignment ran, so the read that follows sees the new value.
    EXPECT_NE(result.stdout_text.find("60\n"), std::string::npos);
    // A function declared on one line closes over a name from an earlier one.
    EXPECT_NE(result.stdout_text.find("62\n"), std::string::npos);
    // So does a class, and its constructor's named argument.
    EXPECT_NE(result.stdout_text.find("7\n"), std::string::npos);
    // The carried model also makes a declaration from an earlier submission a
    // real duplicate; the rejected declaration leaves the old binding intact.
    EXPECT_NE(result.stderr_text.find("LUM-S0001"), std::string::npos);
    // A submission that fails while evaluating its initializer never creates
    // the binding, so its model must not leak that declaration into the next
    // analysis either.
    EXPECT_NE(result.stderr_text.find("fantome"), std::string::npos);
    const std::size_t failed_binding_diagnostic = result.stderr_text.find("LUM-S0057");
    ASSERT_NE(failed_binding_diagnostic, std::string::npos);
    EXPECT_NE(result.stderr_text.find("LUM-S0057", failed_binding_diagnostic + 1),
              std::string::npos);
    // A name nothing declared is still caught, and now by the analyzer rather
    // than by whichever engine reached it.
    EXPECT_NE(result.stderr_text.find("nom_absent"), std::string::npos);
}

TEST(CliIntegration, ReplPreservesDefinitionsAndPrintsExpressionResults)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_repl_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli("", root,
                                         "soit base = 40\n"
                                         "fonction ajouter(x: Entier) {\n"
                                         "  retourne base + x\n"
                                         "}\n"
                                         "ajouter(2)\n"
                                         "Succès(7)\n"
                                         "fonction creerA() {\n"
                                         "  classe Objet {\n"
                                         "    fonction meme(autre: Universel) -> Logique { retourne autre est Objet }\n"
                                         "  }\n"
                                         "  retourne Objet()\n"
                                         "}\n"
                                         "fonction creerB() {\n"
                                         "  classe Objet {\n"
                                         "    fonction meme(autre: Universel) -> Logique { retourne autre est Objet }\n"
                                         "  }\n"
                                         "  retourne Objet()\n"
                                         "}\n"
                                         "soit a = creerA()\n"
                                         "soit b = creerB()\n"
                                         "a.meme(b)\n"
                                         ":quitter\n");
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Lumiere "), std::string::npos);
    EXPECT_NE(result.stdout_text.find("42\n"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("faux\n"), std::string::npos);
    EXPECT_EQ(result.stdout_text.find("Succès(7)\n"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("LUM-S0050"), std::string::npos);
}

TEST(CliIntegration, ReplDoesNotEchoRienResultsFromStatements)
{
    // afficher(...) returns rien; the REPL used to echo every incremental
    // result including rien, printing a stray "rien" line after every bare
    // statement. This is a separate script from the test above so this
    // one's "clean stderr" expectation doesn't collide with that test's
    // own deliberately-triggered LUM-S0050 diagnostic.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_repl_rien_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli("", root,
                                         "soit base = 40\n"
                                         "fonction ajouter(x: Entier) {\n"
                                         "  retourne base + x\n"
                                         "}\n"
                                         "ajouter(2)\n"
                                         "afficher(\"visible\")\n"
                                         ":quitter\n");
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Lumiere "), std::string::npos);
    EXPECT_NE(result.stdout_text.find("42\n"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("visible\n"), std::string::npos);
    EXPECT_EQ(result.stdout_text.find("visible\nrien\n"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, PrintsHelp)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_help_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli("--help", root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("usage: lumiere"), std::string::npos);
}

TEST(CliIntegration, ReportsMultipleInputFiles)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_multiple_files_test";
    const std::filesystem::path a_file = root / "a.lum";
    const std::filesystem::path b_file = root / "b.lum";
    write_source(a_file, "fonction principal() {}\n");
    write_source(b_file, "fonction principal() {}\n");

    const CommandResult result = run_cli(
        shell_quote(a_file.string()) + " " + shell_quote(b_file.string()),
        root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("plus d'un fichier a été fourni"), std::string::npos);
}

TEST(CliIntegration, ReportsRuntimeErrorsToStderr)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_runtime_error_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit xs = [1]\n"
        "  afficher(xs[2])\n"
        "}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("Traceback (most recent call last):"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("in principal"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("File \"" + main_file.string() + "\""), std::string::npos);
    EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("indice hors limites"), std::string::npos);
}

TEST(CliIntegration, ReportsParseErrorsToStderr)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_parse_error_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  soit x =\n"
        "}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("erreur"), std::string::npos);
}

TEST(CliIntegration, CheckReportsTerminalDiagnostics)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_check_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(main_file, "soit = 1\n");

    const CommandResult result = run_cli("check " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.stdout_text.empty());
    EXPECT_NE(result.stderr_text.find("erreur[LUM-P0001]"), std::string::npos);
    EXPECT_NE(result.stderr_text.find(main_file.string() + ":1:6"), std::string::npos);
}

TEST(CliIntegration, CheckReadsEditorBufferFromStdinAsJson)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_check_stdin_test";
    std::filesystem::create_directories(root);

    const CommandResult result = run_cli(
        "check --format=json --stdin --source-path src/main.lum",
        root,
        "soit = 1\nsoit = 2\n");
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_NE(result.stdout_text.find("\"protocolVersion\":1"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("\"source\":\"src/main.lum\""), std::string::npos);
    EXPECT_NE(result.stdout_text.find("\"line\":2"), std::string::npos);
}

TEST(CliIntegration, InspectReadsEditorBufferFromStdinAsJson)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_inspect_stdin_test";
    std::filesystem::create_directories(root);
    const std::string source =
        "fonction doubler(x: Entier) -> Entier { retourne x * 2 }\n"
        "doubler(4)\n";

    const CommandResult result = run_cli(
        "inspect --format=json --stdin --source-path src/main.lum --offset " +
            std::to_string(source.rfind("doubler")),
        root,
        source);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_TRUE(result.stderr_text.empty());
    EXPECT_NE(result.stdout_text.find("\"protocolVersion\":2"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("fonction doubler(x: Entier) -> Entier"), std::string::npos);
}

TEST(CliIntegration, ReportsParseErrorsInsideImportedModules)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_import_parse_error_test";
    write_source(
        root / "Cassé.lum",
        "public fonction casser( {\n"
        "  retourne 1\n"
        "}\n");
    write_source(
        root / "main.lum",
        "importer Cassé\n"
        "fonction principal() {\n"
        "  afficher(1)\n"
        "}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote((root / "main.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("Cassé.lum"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("LUM-P0001"), std::string::npos);
}

TEST(CliIntegration, ExecutesModuleImportsEndToEnd)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_import_test";
    write_source(
        root / "Calculs.lum",
        "public fonction doubler(x: Entier) {\n"
        "  retourne x * 2\n"
        "}\n"
        "public soit base = 21\n");
    write_source(
        root / "main.lum",
        "importer Calculs.{doubler, base}\n"
        "fonction principal() {\n"
        "  afficher(base)\n"
        "  afficher(doubler(base))\n"
        "}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote((root / "main.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("21\n42\n"), std::string::npos);
}

TEST(CliIntegration, BothBackendsPreserveImportedInterfaceIdentity)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_interface_identity";
    const auto file = root / "main.lum";
    const std::string module =
        "public interface Contrat { fonction valeur() -> Entier }\n"
        "public classe Objet réalise Contrat { fonction valeur() -> Entier { retourne 7 } }\n"
        "public fonction accepter(valeur: Contrat) -> Contrat { retourne valeur }\n";
    write_source(root / "PremierModule.lum", module);
    write_source(root / "SecondModule.lum", module);
    for (const std::string mutation : {"", "valeurs.ajouter(B.Objet())", "appels[0](B.Objet())",
                                      "classe Invalide réalise A.Contrat {}"})
    {
        write_source(file,
            "importer PremierModule comme A\nimporter SecondModule comme B\n"
            "importer PremierModule.{Contrat comme Premier}\n"
            "importer SecondModule.{Contrat comme Second}\ntype Alias = Premier\n"
            "classe Locale réalise Alias { fonction valeur() -> Entier { retourne 8 } }\n"
            "classe Enfant : Locale {}\n"
            "classe Qualifiee réalise A.Contrat { fonction valeur() -> Entier { retourne 9 } }\n"
            "fonction principal() {\nsoit objet = A.Objet()\n"
            "afficher(objet est Premier)\nafficher(objet est Second)\n"
            "afficher(objet est A.Contrat)\nafficher(Locale() est Premier)\n"
            "afficher(Qualifiee() est Premier)\n"
            "afficher(Enfant() est Premier)\n"
            "soit valeurs: Liste[Premier] = []\nvaleurs.ajouter(objet)\n"
            "soit appels = [A.accepter]\nappels[0](objet)\n" + mutation + "\n}\n");
        for (const auto *backend : {"--vm", "--tw"})
        {
            SCOPED_TRACE(std::string(backend) + " " + mutation);
            const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
            EXPECT_EQ(result.stdout_text, "vrai\nfaux\nvrai\nvrai\nvrai\nvrai\n") << result.stderr_text;
            EXPECT_EQ(result.stderr_text.find("Contrat@"), std::string::npos) << result.stderr_text;
            if (mutation.empty())
                EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
            else
            {
                EXPECT_NE(result.exit_code, 0);
                EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos) << result.stderr_text;
            }
        }
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsImplementInterfacesThroughTypeOnlyAliases)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_interface_type_alias";
    const auto file = root / "main.lum";
    const std::string module =
        "public interface Contrat { fonction valeur() -> Entier }\n"
        "public type Alias = Contrat\n";
    write_source(root / "Base.lum", module);
    write_source(root / "Autre.lum", module);
    write_source(root / "Relais.lum",
                 "importer Base.{Alias}\npublic type ContratRelaye = Alias\n");
    write_source(
        file,
        "importer Base.{Alias comme Direct}\n"
        "importer Base comme Espace\n"
        "importer Autre.{Alias comme AutreContrat}\n"
        "importer Relais.{ContratRelaye}\n"
        "classe Directe réalise Direct { fonction valeur() -> Entier { retourne 1 } }\n"
        "classe Qualifiee réalise Espace.Alias { fonction valeur() -> Entier { retourne 2 } }\n"
        "classe Relayee réalise ContratRelaye { fonction valeur() -> Entier { retourne 3 } }\n"
        "fonction principal() {\n"
        "  importer Base.{Alias comme Local}\n"
        "  classe Locale réalise Local { fonction valeur() -> Entier { retourne 4 } }\n"
        "  afficher(Directe() est Direct)\n"
        "  afficher(Qualifiee() est Espace.Alias)\n"
        "  afficher(Relayee() est ContratRelaye)\n"
        "  afficher(Locale() est Local)\n"
        "  afficher(Directe() est AutreContrat)\n"
        "}\n");

    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "vrai\nvrai\nvrai\nvrai\nfaux\n");
        EXPECT_EQ(result.stderr_text.find("Contrat@"), std::string::npos) << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsPreserveImportedClassIdentity)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_nominal_identity";
    const auto file = root / "main.lum";
    write_source(root / "PremierModule.lum",
                 "public classe Objet {}\npublic type Alias = Objet\n"
                 "public classe Boite { valeur: Objet }\n"
                 "public fonction accepter(valeur: Objet) -> Objet { retourne valeur }\n"
                 "public fonction creer() -> Objet { retourne Objet() }\n");
    write_source(root / "SecondModule.lum", "public classe Objet {}\n");
    const std::string declarations =
        "importer PremierModule.{Objet comme Premier, Alias, creer}\n"
        "importer SecondModule.{Objet comme Second}\nimporter PremierModule comme Espace\n"
        "classe Enfant : Premier {}\n"
        "fonction identite(valeur: Premier) -> Premier { retourne valeur }\n";
    for (const std::string mutation : {"", "valeurs.ajouter(Second())",
                                      "boite.valeur = Second()",
                                      "soit appels = [Espace.accepter]\nappels[0](Second())"})
    {
        write_source(file, declarations + "fonction principal() {\n"
                     "soit objet: Premier = identite(creer())\n"
                     "afficher(objet est Premier)\nafficher(objet est Second)\n"
                     "afficher(objet est Espace.Objet)\nafficher(objet est Alias)\n"
                     "soit enfant = Enfant()\nafficher(enfant est Premier)\n"
                     "soit boite = Espace.Boite(valeur: objet)\n"
                     "soit valeurs: Liste[Premier] = [objet]\nvaleurs.ajouter(enfant)\n" +
                     mutation + "\n}\n");
        for (const auto *backend : {"--vm", "--tw"})
        {
            SCOPED_TRACE(std::string(backend) + " " + mutation);
            const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
            EXPECT_EQ(result.stdout_text, "vrai\nfaux\nvrai\nvrai\nvrai\n") << result.stderr_text;
            EXPECT_EQ(result.stderr_text.find("Objet@"), std::string::npos) << result.stderr_text;
            if (!mutation.empty())
            {
                EXPECT_NE(result.exit_code, 0);
                EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos) << result.stderr_text;
            }
            else
                EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        }
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, SemanticAnalysisPreservesTransitiveNominalOwners)
{
    const auto root = std::filesystem::temp_directory_path() /
                      "lumiere_semantic_nominal_owner";
    const auto file = root / "main.lum";
    write_source(root / "Origine.lum", "public classe Objet {}\n");
    write_source(root / "Relais.lum",
                 "importer Origine.{Objet}\npublic type Alias = Objet\n");
    write_source(root / "Autre.lum", "public classe Objet {}\n");
    write_source(file,
                 "importer Relais.{Alias}\n"
                 "importer Autre.{Objet comme AutreObjet}\n"
                 "soit valeur: Alias = AutreObjet()\n");

    const auto result = run_cli("check " + shell_quote(file.string()), root);
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("LUM-S0019"), std::string::npos)
        << result.stderr_text;
    EXPECT_NE(result.stderr_text.find("Origine.Objet"), std::string::npos)
        << result.stderr_text;
    EXPECT_NE(result.stderr_text.find("Autre.Objet"), std::string::npos)
        << result.stderr_text;
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsKeepLocalClassIdentitiesScoped)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_local_nominal_identity";
    const auto file = root / "main.lum";
    write_source(root / "Types.lum", "public classe Objet {}\n");
    write_source(file,
                 "classe Objet {}\nfonction principal() {\nsoit origine = Objet()\n"
                 "{ classe Objet {}\nsoit autre = Objet()\n"
                 "afficher(origine est Objet)\nafficher(autre est Objet)\n}\n"
                 "afficher(origine est Objet)\n"
                 "{ importer Types.{Objet comme Externe}\nsoit autre = Externe()\n"
                 "afficher(autre est Externe)\nafficher(origine est Externe)\n}\n}\n");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "faux\nvrai\nvrai\nvrai\nfaux\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsBindInheritedMethodsInTheirDeclaringModule)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_method_declaration_context";
    const auto file = root / "main.lum";
    write_source(root / "Services.lum",
                 "type Nombre = Entier\nsoit decalage = 3\n"
                 "public classe Service {\n"
                 "fonction calculer(valeur: Nombre) -> Nombre { retourne valeur + decalage }\n}\n");
    write_source(file,
                 "importer Services.{Service}\ntype Nombre = Texte\nsoit decalage = 100\n"
                 "classe Enfant : Service {}\nfonction principal() {\n"
                 "soit objet = Enfant()\nsoit methode = objet.calculer\n"
                 "afficher(objet.calculer(4))\nafficher(methode(5))\n}\n");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "7\n8\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsConstructSubclassesOfImportedClasses)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_imported_constructor";
    const auto file = root / "main.lum";
    write_source(root / "Parents.lum",
                 "type Nombre = Entier\npublic classe Base { valeur: Nombre }\n");
    write_source(root / "Milieu.lum",
                 "importer Parents.{Base comme Origine}\npublic classe Base : Origine {}\n");
    for (const auto &import : {std::string("importer Parents.{Base}"),
                               std::string("importer Parents.{Base comme Origine}"),
                               std::string("importer Milieu.{Base}")})
    {
        const std::string parent = import.find("comme") == std::string::npos ? "Base" : "Origine";
        for (const auto *argument : {"valeur: 7", "7", "valeur: \"incorrect\"", "inconnu: 7"})
        {
            write_source(file, import + "\ntype Nombre = Texte\nclasse Enfant : " + parent +
                         " {}\nclasse Petit : Enfant {}\nfonction principal() {\n"
                         "soit objet = Petit(" + argument + ")\nafficher(objet.valeur)\n}\n");
            for (const auto *backend : {"--vm", "--tw"})
            {
                SCOPED_TRACE(import + " " + backend + " " + argument);
                const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
                if (std::string(argument) == "7" || std::string(argument) == "valeur: 7")
                {
                    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
                    EXPECT_EQ(result.stdout_text, "7\n");
                }
                else
                {
                    EXPECT_NE(result.exit_code, 0);
                    EXPECT_NE(result.stderr_text.find("LUM-S"), std::string::npos) << result.stderr_text;
                }
            }
        }
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsPreserveImportedConstructorGenericContracts)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_imported_constructor_generics";
    const auto file = root / "main.lum";
    write_source(root / "Parents.lum",
                 "type Elements = Liste[Entier | Texte]\npublic classe Base { valeurs: Elements }\n");
    for (const auto *argument : {"[1, \"texte\"]", "[vrai]"})
    {
        write_source(file,
                     "importer Parents.{Base}\nclasse Enfant : Base {}\nfonction principal() {\n"
                     "soit objet = Enfant(valeurs: " + std::string(argument) + ")\n}\n");
        for (const auto *backend : {"--vm", "--tw"})
        {
            SCOPED_TRACE(std::string(backend) + " " + argument);
            const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
            if (std::string(argument) == "[vrai]")
            {
                EXPECT_NE(result.exit_code, 0);
                EXPECT_NE(result.stderr_text.find("LUM-S0019"), std::string::npos) << result.stderr_text;
            }
            else
                EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        }
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsResolveImportedFunctionAliasesInTheirDefiningModule)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_function_alias_scope";
    const auto file = root / "main.lum";
    write_source(root / "Fonctions.lum",
                 "public fonction identite(valeur: Nombre) -> Nombre { retourne valeur }\n"
                 "type Nombre = Entier\n");
    write_source(file,
                 "importer Fonctions.{identite}\ntype Nombre = Texte\n"
                 "fonction principal() { afficher(identite(7)) }\n");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "7\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsAllowAssignmentsThroughTypeAliases)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_alias_assignments";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
type Nombre = Entier
type Nombres = Liste[Nombre]
classe Boite {}
type Contenant = Boite
fonction incrementer(valeur: Nombre) -> Nombre {
    valeur = valeur + 1
    retourne valeur
}
fonction principal() {
    soit valeur: Nombre = 1
    valeur = incrementer(valeur)
    soit valeurs: Nombres = [1]
    valeurs = [valeur]
    soit boite: Contenant = Boite()
    boite = Boite()
    afficher(valeurs)
    afficher(boite est Contenant)
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "[2]\nvrai\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsImportClosedCollectionTypeAliases)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_imported_type_aliases";
    const auto file = root / "main.lum";
    write_source(root / "Types.lum",
                 "type Interne = Entier\npublic type Nombres = Liste[Interne]\n");
    write_source(root / "Relais.lum",
                 "importer Types.{Nombres}\npublic type Exportés = Nombres\n");
    for (const auto &[import, type] : {
             std::pair{"importer Types.{Nombres}", "Nombres"},
             std::pair{"importer Types.{Nombres comme Entiers}", "Entiers"},
             std::pair{"importer Types comme t", "t.Nombres"},
             std::pair{"importer Relais.{Exportés}", "Exportés"}})
    {
        for (bool local : {false, true})
        for (bool invalid : {false, true})
        {
            const std::string import_line = std::string(import) + "\n";
            write_source(file, (local ? "" : import_line) + "type Interne = Texte\nfonction principal() {\n" +
                         (local ? import_line : "") +
                         "soit valeurs: " + type + " = [1]\nvaleurs = [1]\nvaleurs.ajouter(2)\nafficher(valeurs)\n" +
                         (invalid ? "valeurs.ajouter(\"incorrect\")\n" : "") + "}\n");
            for (const auto *backend : {"--vm", "--tw"})
            {
                SCOPED_TRACE(std::string(backend) + " " + import);
                SCOPED_TRACE(local ? "function-local import" : "module-level import");
                const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
                EXPECT_EQ(result.stdout_text, "[1, 2]\n") << result.stderr_text;
                if (invalid)
                {
                    EXPECT_NE(result.exit_code, 0);
                    EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos);
                }
                else
                {
                    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
                    EXPECT_TRUE(result.stderr_text.empty());
                }
            }
        }
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, PreservesErrorContractsAcrossModuleBoundaries)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_imported_error_test";
    write_source(
        root / "Erreurs.lum",
        "public type MarqueurErreur = Erreur\n"
        "public classe ErreurPartagée réalise MarqueurErreur {}\n"
        "public fonction produire() -> Résultat[Entier, ErreurPartagée] {\n"
        "  retourne Échec(ErreurPartagée())\n"
        "}\n");
    write_source(
        root / "main.lum",
        "importer Erreurs.{produire, ErreurPartagée}\n"
        "fonction principal() {\n"
        "  agir selon produire() {\n"
        "    Succès(_) -> afficher(\"inattendu\")\n"
        "    Échec(erreur: ErreurPartagée) -> afficher(erreur)\n"
        "  }\n"
        "}\n");

    for (const std::string backend :
         {"--tree-walker", "--vm"})
    {
        const CommandResult result =
            run_cli(
                backend + " --run " +
                    shell_quote(
                        (root / "main.lum").string()),
                root);
        EXPECT_EQ(result.exit_code, 0)
            << backend << ": " << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "ErreurPartagée\n")
            << backend;
        EXPECT_TRUE(result.stderr_text.empty())
            << backend << ": " << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, AcceptsGenericTypeAnnotationsEndToEnd)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_generic_param_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction taille_de(nombs: Liste[Entier]) -> Entier {\n"
        "  retourne nombs.taille()\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(taille_de([1, 2, 3]))\n"
        "}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("3\n"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty()) << result.stderr_text;
}

TEST(CliIntegration, ExecutesBuiltinModulesEndToEnd)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_builtin_test";
    const std::filesystem::path note_file = root / "note.txt";
    write_source(note_file, "bonjour");
    write_source(
        root / "main.lum",
        "importer Fichier\n"
        "fonction valeur_fichier("
        "résultat: Résultat[Universel, Fichier.ErreurFichier]"
        ") -> Universel {\n"
        "  retourne agir selon résultat {\n"
        "    Succès(valeur) -> valeur\n"
        "    Échec(erreur) -> erreur\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(valeur_fichier(Fichier.existe(\"" + lumiere_string_literal_text(note_file.string()) + "\")))\n"
        "  afficher(valeur_fichier(Fichier.lire_texte(\"" + lumiere_string_literal_text(note_file.string()) + "\")))\n"
        "}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote((root / "main.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
    EXPECT_NE(result.stdout_text.find("vrai\nbonjour\n"), std::string::npos) << result.stderr_text;
}

TEST(CliIntegration, BothBackendsPreserveNativeNominalTypeIdentity)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_native_nominal_identity";
    const auto file = root / "main.lum";
    const auto missing = root / "absent.txt";
    write_source(
        file,
        "importer Temps.{Instant comme Moment, Durée comme Intervalle, maintenant, secondes}\n"
        "importer Temps comme T\n"
        "importer Fichier.{ErreurFichier comme ErreurLecture, lire_texte}\n"
        "classe Instant {}\n"
        "fonction identite(valeur: Moment) -> Moment { retourne valeur }\n"
        "fonction principal() {\n"
        "  soit instant: Moment = identite(maintenant())\n"
        "  soit intervalle: Intervalle = secondes(1)\n"
        "  afficher(instant est Moment)\n"
        "  afficher(instant est T.Instant)\n"
        "  afficher(intervalle est Intervalle)\n"
        "  afficher(Instant() est Moment)\n"
        "  soit lecture = lire_texte(\"" + lumiere_string_literal_text(missing.string()) + "\")\n"
        "  agir selon lecture {\n"
        "    Succès(_) -> afficher(faux)\n"
        "    Échec(erreur) -> afficher(erreur est ErreurLecture)\n"
        "  }\n"
        "}\n");

    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "vrai\nvrai\nvrai\nfaux\nvrai\n");
        EXPECT_EQ(result.stderr_text.find("@"), std::string::npos) << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, ImportsCycleFailsEndToEnd)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_cycle_test";
    write_source(root / "A.lum", "importer B\npublic soit a = 1\n");
    write_source(root / "B.lum", "importer A\npublic soit b = 2\n");
    write_source(root / "main.lum", "importer A\nfonction principal() {\n  afficher(1)\n}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote((root / "main.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("cycle d'import"), std::string::npos);
}

TEST(CliIntegration, VmBackendExecutesSimpleProgram)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  afficher(\"bonjour\")\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("bonjour"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, BothBackendsExecuteFormerExceptionKeywordsAsIdentifiers)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       "lumiere_cli_former_exception_identifiers_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction essayer(attraper: Entier) -> Entier {\n"
        "  soit finalement = attraper + 1\n"
        "  retourne finalement\n"
        "}\n"
        "fonction lancer() { afficher(essayer(attraper: 41)) }\n"
        "fonction principal() { lancer() }\n");

    const CommandResult tree_walker = run_cli(
        "--tree-walker --run " + shell_quote(main_file.string()), root);
    const CommandResult vm = run_cli(
        "--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(tree_walker.exit_code, 0) << tree_walker.stderr_text;
    EXPECT_EQ(vm.exit_code, 0) << vm.stderr_text;
    EXPECT_EQ(tree_walker.stdout_text, "42\n");
    EXPECT_EQ(vm.stdout_text, tree_walker.stdout_text);
    EXPECT_TRUE(tree_walker.stderr_text.empty());
    EXPECT_TRUE(vm.stderr_text.empty());
}

TEST(CliIntegration, BothBackendsHonorResultReturningPrincipal)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_result_principal_test";
    const std::filesystem::path main_file = root / "main.lum";

    write_source(
        main_file,
        "classe ErreurTest réalise Erreur {} type Sortie = Résultat[Rien, ErreurTest]\n"
        "fonction principal() -> Sortie {\n"
        "  retourne Succès(rien)\n"
        "}\n");
    for (const std::string backend : {"--tree-walker", "--vm"})
    {
        const CommandResult success =
            run_cli(backend + " --run " +
                        shell_quote(main_file.string()),
                    root);
        EXPECT_EQ(success.exit_code, 0) << backend;
        EXPECT_TRUE(success.stderr_text.empty())
            << backend << ": " << success.stderr_text;
    }

    write_source(
        main_file,
        "classe ErreurTest réalise Erreur {} fonction principal() -> Résultat[Rien, ErreurTest] {\n"
        "  retourne Échec(ErreurTest())\n"
        "}\n");
    for (const std::string backend : {"--tree-walker", "--vm"})
    {
        const CommandResult failure =
            run_cli(backend + " --run " +
                        shell_quote(main_file.string()),
                    root);
        EXPECT_NE(failure.exit_code, 0) << backend;
        EXPECT_NE(
            failure.stderr_text.find(
                "principal a échoué: ErreurTest"),
            std::string::npos)
            << backend << ": " << failure.stderr_text;
        EXPECT_NE(
            failure.stderr_text.find("line 2, column 12"),
            std::string::npos)
            << backend << ": " << failure.stderr_text;
    }

    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsPropagateOnlyCompatibleFailures)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_result_propagation_test";
    const std::filesystem::path main_file = root / "main.lum";

    write_source(
        main_file,
        "classe ErreurTest réalise Erreur {} classe ErreurAutre réalise Erreur {} fonction charger_valeur(active: Logique) -> Résultat[Entier, ErreurTest] {\n"
        "  si active { retourne Succès(41) }\n"
        "  retourne Échec(ErreurTest())\n"
        "}\n"
        "fonction calculer(active: Logique) -> Résultat[Entier, ErreurTest | ErreurAutre] {\n"
        "  soit valeur = charger_valeur(active) ou propager\n"
        "  retourne Succès(valeur + 1)\n"
        "}\n"
        "fonction principal() -> Résultat[Rien, ErreurTest | ErreurAutre] {\n"
        "  afficher(calculer(vrai) ou propager)\n"
        "  calculer(faux) ou propager\n"
        "  retourne Succès(rien)\n"
        "}\n");

    for (const std::string backend : {"--tree-walker", "--vm"})
    {
        const CommandResult result =
            run_cli(backend + " --run " +
                        shell_quote(main_file.string()),
                    root);
        EXPECT_NE(result.exit_code, 0) << backend;
        EXPECT_EQ(result.stdout_text, "42\n") << backend;
        EXPECT_NE(result.stderr_text.find("ErreurTest"),
                  std::string::npos)
            << backend << ": " << result.stderr_text;
    }

    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsPropagateExactlyOnceWithoutReplacingErrors)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_propagation_identity_test";
    const std::filesystem::path main_file =
        root / "main.lum";
    write_source(
        main_file,
        "classe ErreurTest réalise Erreur {}\n"
        "soit compteur = 0\n"
        "soit erreur_attendue = ErreurTest()\n"
        "fonction opération(ok: Logique) -> Résultat[Entier, ErreurTest] {\n"
        "  compteur = compteur + 1\n"
        "  si ok { retourne Succès(7) }\n"
        "  retourne Échec(erreur_attendue)\n"
        "}\n"
        "fonction intermédiaire(ok: Logique) -> Résultat[Entier, ErreurTest] {\n"
        "  soit valeur = opération(ok) ou propager\n"
        "  retourne Succès(valeur)\n"
        "}\n"
        "fonction avec_défaut(valeur: Entier = opération(vrai) ou propager) -> Résultat[Entier, ErreurTest] {\n"
        "  retourne Succès(valeur)\n"
        "}\n"
        "fonction principal() {\n"
        "  agir selon intermédiaire(vrai) {\n"
        "    Succès(valeur) -> afficher(valeur)\n"
        "    Échec(_) -> afficher(\"inattendu\")\n"
        "  }\n"
        "  agir selon avec_défaut() {\n"
        "    Succès(valeur) -> afficher(valeur)\n"
        "    Échec(_) -> afficher(\"inattendu\")\n"
        "  }\n"
        "  agir selon intermédiaire(faux) {\n"
        "    Succès(_) -> afficher(\"inattendu\")\n"
        "    Échec(erreur) -> {\n"
        "      afficher(compteur)\n"
        "      afficher(erreur == erreur_attendue)\n"
        "    }\n"
        "  }\n"
        "}\n");

    for (const std::string backend :
         {"--tree-walker", "--vm"})
    {
        const CommandResult result =
            run_cli(
                backend + " --run " +
                    shell_quote(main_file.string()),
                root);
        EXPECT_EQ(result.exit_code, 0)
            << backend << ": " << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "7\n7\n3\nvrai\n")
            << backend;
        EXPECT_TRUE(result.stderr_text.empty())
            << backend << ": " << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsPreserveOriginsAcrossPropagationChains)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_propagation_origin_test";
    const std::filesystem::path main_file =
        root / "main.lum";
    write_source(
        main_file,
        "classe ErreurTest réalise Erreur {} fonction source() -> Résultat[Rien, Erreur] {\n"
        "  retourne Échec(ErreurTest())\n"
        "}\n"
        "fonction milieu() -> Résultat[Rien, ErreurTest] {\n"
        "  agir selon source() {\n"
        "    Succès(_) -> ignorer\n"
        "    Échec(_: ErreurTest) -> propager\n"
        "    Échec(_) -> ignorer\n"
        "  }\n"
        "  retourne Succès(rien)\n"
        "}\n"
        "fonction principal() -> Résultat[Rien, ErreurTest] {\n"
        "  milieu() ou propager\n"
        "  retourne Succès(rien)\n"
        "}\n");

    for (const std::string backend :
         {"--tree-walker", "--vm"})
    {
        const CommandResult result =
            run_cli(
                backend + " --run " +
                    shell_quote(main_file.string()),
                root);
        EXPECT_NE(result.exit_code, 0) << backend;
        EXPECT_NE(
            result.stderr_text.find(
                "line 2, column 12"),
            std::string::npos)
            << backend << ": " << result.stderr_text;
        EXPECT_EQ(
            result.stderr_text.find(
                "line 6, column"),
            std::string::npos)
            << backend << ": " << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsNeverConvertTrapsIntoPropagatedFailures)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_propagation_trap_test";
    const std::filesystem::path main_file =
        root / "main.lum";
    write_source(
        main_file,
        "classe ErreurTest réalise Erreur {}\n"
        "fonction source() -> Résultat[Entier, ErreurTest] {\n"
        "  soit valeurs = [1]\n"
        "  retourne Succès(valeurs[2] en Entier)\n"
        "}\n"
        "fonction principal() -> Résultat[Rien, ErreurTest] {\n"
        "  afficher(source() ou propager)\n"
        "  retourne Succès(rien)\n"
        "}\n");

    for (const std::string backend :
         {"--tree-walker", "--vm"})
    {
        const CommandResult result =
            run_cli(
                backend + " --run " +
                    shell_quote(main_file.string()),
                root);
        EXPECT_NE(result.exit_code, 0) << backend;
        EXPECT_NE(
            result.stderr_text.find(
                "indice hors limites"),
            std::string::npos)
            << backend << ": " << result.stderr_text;
        EXPECT_EQ(
            result.stderr_text.find(
                "principal a échoué"),
            std::string::npos)
            << backend << ": " << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsPreserveAndReplaceFailureOrigins)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_result_origin_test";
    const std::filesystem::path main_file = root / "main.lum";

    write_source(
        main_file,
        "classe ErreurSource réalise Erreur {} fonction source() -> Résultat[Rien, ErreurSource] {\n"
        "  retourne Échec(ErreurSource())\n"
        "}\n"
        "fonction principal() -> Résultat[Rien, ErreurSource] {\n"
        "  retourne source()\n"
        "}\n");
    for (const std::string backend : {"--tree-walker", "--vm"})
    {
        const CommandResult forwarded =
            run_cli(
                backend + " --run " +
                    shell_quote(main_file.string()),
                root);
        EXPECT_NE(forwarded.exit_code, 0);
        EXPECT_NE(
            forwarded.stderr_text.find("line 2, column 12"),
            std::string::npos)
            << backend << ": " << forwarded.stderr_text;
    }

    write_source(
        main_file,
        "classe ErreurSource réalise Erreur {} classe ErreurTraduite réalise Erreur {} fonction source() -> Résultat[Rien, ErreurSource] {\n"
        "  retourne Échec(ErreurSource())\n"
        "}\n"
        "fonction principal() -> Résultat[Rien, ErreurSource | ErreurTraduite] {\n"
        "  soit résultat = source()\n"
        "  retourne agir selon résultat {\n"
        "    Succès(_) -> Succès(rien)\n"
        "    Échec(_) -> retourne Échec(ErreurTraduite())\n"
        "  }\n"
        "}\n");
    for (const std::string backend : {"--tree-walker", "--vm"})
    {
        const CommandResult translated =
            run_cli(
                backend + " --run " +
                    shell_quote(main_file.string()),
                root);
        EXPECT_NE(translated.exit_code, 0);
        EXPECT_NE(
            translated.stderr_text.find("line 8, column 27"),
            std::string::npos)
            << backend << ": " << translated.stderr_text;
    }

    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsRenderStackTraceForUnhandledPropagatedResults)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_propagated_traceback_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "classe ErreurTest réalise Erreur {}\n"
        "fonction source() -> Résultat[Entier, ErreurTest] {\n"
        "    retourne Échec(ErreurTest())\n"
        "}\n"
        "fonction milieu() -> Résultat[Entier, ErreurTest] {\n"
        "  source() ou propager\n"
        "  retourne Succès(0)\n"
        "}\n"
        "fonction principal() -> Résultat[Rien, ErreurTest] {\n"
        "  milieu() ou propager\n"
        "  retourne Succès(rien)\n"
        "}\n");

    for (const std::string backend :
         {"--tree-walker", "--vm"})
    {
        const CommandResult result =
            run_cli(
                backend + " --run " +
                    shell_quote(main_file.string()),
                root);
        EXPECT_NE(result.exit_code, 0) << backend;
        EXPECT_NE(
            result.stderr_text.find("Traceback (most recent call last):"),
            std::string::npos)
            << backend << ": " << result.stderr_text;

        const std::size_t source_frame =
            result.stderr_text.find("in source (");
        const std::size_t milieu_frame =
            result.stderr_text.find("in milieu (");
        const std::size_t principal_frame =
            result.stderr_text.find("in principal");
        EXPECT_NE(source_frame, std::string::npos) << backend;
        EXPECT_NE(milieu_frame, std::string::npos) << backend;
        EXPECT_NE(principal_frame, std::string::npos) << backend;
        EXPECT_LT(source_frame, milieu_frame) << backend;
        EXPECT_LT(milieu_frame, principal_frame) << backend;

        // The failure originates in the Échec call, so the callable token owns
        // the caret rather than its opening parenthesis.
        EXPECT_NE(
            result.stderr_text.find("line 3, column 14"),
            std::string::npos)
            << backend << ": " << result.stderr_text;
        EXPECT_NE(
            result.stderr_text.find(
                "principal a échoué: ErreurTest"),
            std::string::npos)
            << backend << ": " << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsHandleNativeNetworkFailuresAsResults)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_native_network_result_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "importer LumiNet\n"
        "fonction principal() {\n"
        "  agir selon LumiNet.Adresse.analyser(\"invalide\") {\n"
        "    Succès(_) -> afficher(\"inattendu\")\n"
        "    Échec(erreur) -> afficher(erreur)\n"
        "  }\n"
        "}\n");

    for (const std::string backend : {"--tree-walker", "--vm"})
    {
        const CommandResult result =
            run_cli(
                backend + " --run " +
                    shell_quote(main_file.string()),
                root);
        EXPECT_EQ(result.exit_code, 0)
            << backend << ": " << result.stderr_text;
        EXPECT_NE(
            result.stdout_text.find("LumiNet.ErreurAdresse("),
            std::string::npos)
            << backend << ": " << result.stdout_text;
        EXPECT_EQ(
            result.stdout_text.find("inattendu"),
            std::string::npos);
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsEnforceUnionAnnotationsInsideNestedGenerics)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       "lumiere_cli_union_annotations_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction afficher_premier(valeurs: Liste[Entier | Texte]) {\n"
        "  afficher(valeurs[0])\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher_premier([42, \"lumière\"])\n"
        "}\n");

    const CommandResult tree_walker = run_cli(
        "--tree-walker --run " + shell_quote(main_file.string()), root);
    const CommandResult vm = run_cli(
        "--vm --run " + shell_quote(main_file.string()), root);

    EXPECT_EQ(tree_walker.exit_code, 0) << tree_walker.stderr_text;
    EXPECT_EQ(vm.exit_code, 0) << vm.stderr_text;
    EXPECT_EQ(tree_walker.stdout_text, "42\n");
    EXPECT_EQ(vm.stdout_text, tree_walker.stdout_text);

    write_source(
        main_file,
        "fonction accepter(valeurs: Liste[Entier | Texte]) {}\n"
        "fonction principal() { accepter([vrai]) }\n");
    const CommandResult rejected_tree_walker = run_cli(
        "--tree-walker --run " + shell_quote(main_file.string()), root);
    const CommandResult rejected_vm = run_cli(
        "--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(rejected_tree_walker.exit_code, 0);
    EXPECT_NE(rejected_vm.exit_code, 0);
    EXPECT_NE(rejected_tree_walker.stderr_text.find("Liste[Entier | Texte]"), std::string::npos);
    EXPECT_NE(rejected_vm.stderr_text.find("Liste[Entier | Texte]"), std::string::npos);
}

TEST(CliIntegration, VmBackendSupportsObjectsInheritanceAndParentDispatch)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_objects_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "classe Animal {\n"
        "  nom: Texte\n"
        "  fonction decrire() { retourne \"Animal:\" + ici.nom }\n"
        "}\n"
        "classe Chien : Animal {\n"
        "  remplace fonction decrire() { retourne parent.decrire() + \"/Chien\" }\n"
        "}\n"
        "fonction principal() {\n"
        "  soit chien = Chien(nom: \"Rex\")\n"
        "  soit decrire = chien.decrire\n"
        "  afficher(decrire())\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "Animal:Rex/Chien\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, VmBackendEnforcesInterfacesAndPrivateFields)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_object_contract_test";
    const std::filesystem::path valid_file = root / "valid.lum";
    const std::filesystem::path private_file = root / "private.lum";
    write_source(
        valid_file,
        "interface Nommable { fonction nom() -> Texte }\n"
        "classe Personne réalise Nommable {\n"
        "  privé valeur: Texte\n"
        "  fonction nom() -> Texte { retourne ici.valeur }\n"
        "}\n"
        "fonction principal() { afficher(Personne(valeur: \"Ada\").nom()) }\n");
    write_source(
        private_file,
        "classe Secret { privé valeur: Texte }\n"
        "fonction principal() { afficher(Secret(valeur: \"x\").valeur) }\n");

    const CommandResult valid = run_cli("--vm --run " + shell_quote(valid_file.string()), root);
    const CommandResult private_access = run_cli("--vm --run " + shell_quote(private_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(valid.exit_code, 0);
    EXPECT_EQ(valid.stdout_text, "Ada\n");
    EXPECT_NE(private_access.exit_code, 0);
    EXPECT_NE(private_access.stderr_text.find("champ privé"), std::string::npos);
}

TEST(CliIntegration, VmBackendSupportsBlockScopedClassesAndInterfaces)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_local_classes_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction fabriquer(prefixe: Texte) {\n"
        "  interface Nommable { fonction nom() -> Texte }\n"
        "  classe Base {\n"
        "    valeur: Texte\n"
        "    fonction nom() -> Texte { retourne prefixe + ici.valeur }\n"
        "  }\n"
        "  classe Enfant : Base réalise Nommable {\n"
        "    remplace fonction nom() -> Texte { retourne parent.nom() + \"!\" }\n"
        "  }\n"
        "  retourne Enfant\n"
        "}\n"
        "fonction principal() {\n"
        "  soit Type = fabriquer(\"préfixe:\")\n"
        "  soit objet = Type(valeur: \"ok\")\n"
        "  afficher(objet.nom())\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "préfixe:ok!\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, VmBackendSupportsUserAndBuiltinModuleImports)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_modules_test";
    write_source(
        root / "Calculs.lum",
        "afficher(\"init\")\n"
        "public soit base = 20\n"
        "public fonction doubler(x: Entier) -> Entier { retourne x * 2 }\n"
        "fonction interne() { retourne 0 }\n");
    write_source(
        root / "main.lum",
        "importer Calculs comme calc\n"
        "importer Calculs.{doubler comme fois_deux}\n"
        "importer Maths.{racine}\n"
        "fonction principal() {\n"
        "  afficher(calc.doubler(calc.base))\n"
        "  afficher(fois_deux(3))\n"
        "  afficher(racine(81))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote((root / "main.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "init\n40\n6\n9.0\n");
    EXPECT_TRUE(result.stderr_text.empty()) << result.stderr_text;
}

TEST(CliIntegration, RejectsSemanticErrorsInsideImportedModules)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_imported_semantic_error_test";
    write_source(
        root / "Invalide.lum",
        "public fonction calculer() -> Entier { retourne \"texte\" }\n");
    write_source(
        root / "main.lum",
        "importer Invalide\n"
        "fonction principal() { afficher(Invalide.calculer()) }\n");

    const CommandResult result =
        run_cli("--vm --run " +
                    shell_quote((root / "main.lum").string()),
                root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("Invalide.lum"), std::string::npos)
        << result.stderr_text;
    EXPECT_NE(result.stderr_text.find("Entier"), std::string::npos)
        << result.stderr_text;
}

TEST(CliIntegration, RejectsUnknownSelectiveNativeImports)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "lumiere_cli_unknown_native_import_test";
    write_source(
        root / "main.lum",
        "importer Temps.{fonction_absente}\n"
        "fonction principal() { fonction_absente() }\n");

    const CommandResult result =
        run_cli("--vm --run " +
                    shell_quote((root / "main.lum").string()),
                root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("fonction_absente"), std::string::npos)
        << result.stderr_text;
}

TEST(CliIntegration, VmBackendSupportsNamedCallableAndMethodArguments)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_named_callable_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "classe Calcul {\n"
        "  fonction somme(a: Entier, b: Entier = 2) { retourne a + b }\n"
        "}\n"
        "fonction principal() {\n"
        "  soit f = fonction(a: Entier, b: Entier = 3) { retourne a * b }\n"
        "  afficher(f(b: 4, a: 5))\n"
        "  afficher(Calcul().somme(b: 7, a: 6))\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "20\n13\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, VmBackendRejectsPrivateImportsAndImportCycles)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_module_errors_test";
    write_source(root / "Cache.lum", "fonction secret() { retourne 1 }\n");
    write_source(root / "private.lum",
                 "importer Cache.{secret}\nfonction principal() { afficher(secret()) }\n");
    write_source(root / "A.lum", "importer B\npublic soit a = 1\n");
    write_source(root / "B.lum", "importer A\npublic soit b = 2\n");
    write_source(root / "cycle.lum", "importer A\nfonction principal() {}\n");

    const CommandResult private_import = run_cli("--vm --run " + shell_quote((root / "private.lum").string()), root);
    const CommandResult cycle = run_cli("--vm --run " + shell_quote((root / "cycle.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(private_import.exit_code, 0);
    EXPECT_NE(private_import.stderr_text.find("non exporté"), std::string::npos);
    EXPECT_NE(cycle.exit_code, 0);
    EXPECT_NE(cycle.stderr_text.find("LUM-S0043"), std::string::npos);
    EXPECT_NE(cycle.stderr_text.find("cycle d'import: A -> B -> A"),
              std::string::npos);
}

TEST(CliIntegration, VmBackendSupportsLazyBlockScopedImports)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_local_import_test";
    write_source(root / "Charge.lum",
                 "afficher(\"initialisation\")\n"
                 "public soit valeur = 7\n");
    write_source(
        root / "main.lum",
        "fonction charger(active: Logique) {\n"
        "  si (active) {\n"
        "    importer Charge.{valeur}\n"
        "    afficher(valeur)\n"
        "  }\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(\"avant\")\n"
        "  charger(faux)\n"
        "  afficher(\"milieu\")\n"
        "  charger(vrai)\n"
        "  charger(vrai)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote((root / "main.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "avant\nmilieu\ninitialisation\n7\n7\n");
    EXPECT_TRUE(result.stderr_text.empty()) << result.stderr_text;
}

TEST(CliIntegration, BothBackendsPreserveCollectionContractsAcrossCallbacks)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_callback_contracts";
    const auto file = root / "main.lum";
    for (const auto *body : {
             "soit valeurs: Liste[Entier] = [1]\n"
             "LumiTest.groupe(\"callback\", fonction() { valeurs.ajouter(\"incorrect\") })",
             "soit valeurs = [1]\n"
             "LumiTest.groupe(\"callback\", fonction() { soit alias: Liste[Entier] = valeurs })\n"
             "valeurs.ajouter(\"incorrect\")"})
    {
        write_source(file, "importer LumiTest\nfonction principal() {\n" + std::string(body) + "\n}\n");
        for (const auto *backend : {"--vm", "--tw"})
        {
            SCOPED_TRACE(std::string(backend) + " " + body);
            const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
            EXPECT_NE(result.exit_code, 0);
            EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos);
        }
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsKeepBoundMethodsFromNestedCallbacksAlive)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_callback_bound_method";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
importer LumiTest
fonction principal() {
    soit valeurs: Liste[Entier] = [1]
    soit ajouter = valeurs.ajouter
    LumiTest.groupe("exterieur", fonction() {
        LumiTest.groupe("interieur", fonction() {
            ajouter = valeurs.ajouter
        })
        ajouter(2)
    })
    ajouter(3)
    afficher(valeurs)
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "[1, 2, 3]\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsProtectNestedCollectionsAndTypeAliases)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_nested_alias_contracts";
    const auto file = root / "main.lum";
    for (const auto *declarations : {
             "soit valeurs = [1]\nsoit conteneur: Liste[Liste[Entier]] = [valeurs]\n",
             "soit valeurs = [1]\nsoit conteneur: Liste[Liste[Entier]] = []\nconteneur.ajouter(valeurs)\n",
             "soit valeurs = [1]\nsoit sortie: Résultat[Liste[Entier], Erreur] = Succès(valeurs)\nagir selon sortie { Succès(_) -> ignorer Échec(_) -> ignorer }\n",
             "soit valeurs: Nombres = [1]\n",
             "soit valeurs: Liste[Nombre] = [1]\n"})
    {
        write_source(file, "type Nombre = Entier\ntype Nombres = Liste[Nombre]\n"
                           "fonction principal() {\n" + std::string(declarations) +
                           "valeurs.ajouter(2)\nafficher(\"pret\")\nvaleurs.ajouter(\"incorrect\")\n}\n");
        for (const auto *backend : {"--vm", "--tw"})
        {
            SCOPED_TRACE(std::string(backend) + " " + declarations);
            const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
            EXPECT_NE(result.exit_code, 0);
            EXPECT_EQ(result.stdout_text, "pret\n");
            EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos);
        }
    }
    std::filesystem::remove_all(root);
}
TEST(CliIntegration, BothBackendsDiscardConstraintsWithDeadCollections)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_dead_collection_constraints";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
fonction annoter() {
    soit valeurs: Liste[Entier] = [1]
}
fonction nouvelle() -> Texte {
    soit valeurs = []
    valeurs.ajouter("libre")
    retourne valeurs[0] en Texte
}
fonction principal() {
    soit index = 0
    tant que index < 1000 {
        annoter()
        afficher(nouvelle())
        index = index + 1
    }
}
)lum");
    std::string expected;
    for (int i = 0; i < 1000; ++i)
        expected += "libre\n";
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, expected);
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsPreserveGlobalCollectionContracts)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_global_contracts";
    const auto file = root / "main.lum";
    write_source(file, "soit valeurs: Liste[Entier] = [1]\n"
                       "fonction principal() { valeurs.ajouter(\"incorrect\") }\n");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_NE(result.exit_code, 0);
        EXPECT_NE(result.stderr_text.find("erreur d'exécution"), std::string::npos);
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, VmBackendSupportsNativeCallbacksIntoBytecode)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_native_callback_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "importer LumiTest\n"
        "fonction principal() {\n"
        "  soit trace = \"avant\"\n"
        "  LumiTest.groupe(\"VM\", fonction() { trace = trace + \"-callback\" })\n"
        "  afficher(trace)\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "avant-callback\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, RunsLumiTestFiles)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_success";
    write_source(
        root / "calcul_test.lum",
        "importer LumiTest\n"
        "LumiTest.groupe(\"Calculs\", fonction() {\n"
        "  LumiTest.test(\"addition\", fonction() {\n"
        "    LumiTest.vérifier_égal(5, 2 + 3)\n"
        "  })\n"
        "  LumiTest.test(\"approx\", fonction() {\n"
        "    LumiTest.vérifier_approx(3.0, 3.001, 0.01)\n"
        "  })\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Calculs > addition"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("Calculs > approx"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("RÉUSSI"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ReportsLumiTestFailures)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_failure";
    write_source(
        root / "calcul_test.lum",
        "importer LumiTest\n"
        "LumiTest.test(\"mauvais résultat\", fonction() {\n"
        "  LumiTest.vérifier_égal(5, 2 + 2)\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("mauvais résultat"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("vérifier_égal"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("attendu: 5"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("reçu: 4"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("ÉCHOUÉ"), std::string::npos);
}

TEST(CliIntegration, ReportsAFailedAvantToutOnEveryTestInTheGroup)
{
    // A failing avant_tout used to mark the group's before-all hooks as
    // already run before actually running them, so only the first test in
    // the group reported the real failure; every later test silently ran
    // with no fixture in place and passed or failed for an unrelated
    // reason instead.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_avant_tout_echec";
    write_source(
        root / "groupe_test.lum",
        "importer LumiTest\n"
        "LumiTest.groupe(\"Config\", fonction() {\n"
        "  LumiTest.avant_tout(fonction() {\n"
        "    LumiTest.vérifier_égal(1, 2)\n"
        "  })\n"
        "  LumiTest.test(\"premier\", fonction() {\n"
        "    LumiTest.vérifier_égal(1, 1)\n"
        "  })\n"
        "  LumiTest.test(\"second\", fonction() {\n"
        "    LumiTest.vérifier_égal(1, 1)\n"
        "  })\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Config > premier"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("Config > second"), std::string::npos);
    // Both tests report the avant_tout failure itself, not a pass and not
    // some unrelated error from running without the fixture.
    const auto first_failure = result.stdout_text.find("vérifier_égal échoué");
    ASSERT_NE(first_failure, std::string::npos);
    const auto second_failure =
        result.stdout_text.find("vérifier_égal échoué", first_failure + 1);
    EXPECT_NE(second_failure, std::string::npos);
    EXPECT_NE(result.stdout_text.find("ÉCHOUÉ — 2 échecs"), std::string::npos);
}

TEST(CliIntegration, RunsLumiTestBeforeAndAfterEachHooks)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_hooks";
    write_source(
        root / "hooks_test.lum",
        "importer LumiTest\n"
        "soit compteur = 0\n"
        "LumiTest.groupe(\"Hooks\", fonction() {\n"
        "  LumiTest.avant_chaque(fonction() {\n"
        "    compteur = compteur + 1\n"
        "  })\n"
        "  LumiTest.après_chaque(fonction() {\n"
        "    compteur = compteur + 10\n"
        "  })\n"
        "  LumiTest.test(\"premier\", fonction() {\n"
        "    LumiTest.vérifier_égal(1, compteur)\n"
        "  })\n"
        "  LumiTest.test(\"second\", fonction() {\n"
        "    LumiTest.vérifier_égal(12, compteur)\n"
        "  })\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Hooks > premier"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("Hooks > second"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("RÉUSSI"), std::string::npos);
}

TEST(CliIntegration, InheritsNestedLumiTestHooks)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_nested_hooks";
    write_source(
        root / "nested_hooks_test.lum",
        "importer LumiTest\n"
        "soit trace = \"\"\n"
        "LumiTest.groupe(\"Parent\", fonction() {\n"
        "  LumiTest.avant_chaque(fonction() {\n"
        "    trace = trace + \"A\"\n"
        "  })\n"
        "  LumiTest.après_chaque(fonction() {\n"
        "    trace = trace + \"Z\"\n"
        "  })\n"
        "  LumiTest.groupe(\"Enfant\", fonction() {\n"
        "    LumiTest.avant_chaque(fonction() {\n"
        "      trace = trace + \"B\"\n"
        "    })\n"
        "    LumiTest.après_chaque(fonction() {\n"
        "      trace = trace + \"Y\"\n"
        "    })\n"
        "    LumiTest.test(\"ordre\", fonction() {\n"
        "      LumiTest.vérifier_égal(\"AB\", trace)\n"
        "    })\n"
        "    LumiTest.test(\"après\", fonction() {\n"
        "      LumiTest.vérifier_égal(\"ABYZAB\", trace)\n"
        "    })\n"
        "  })\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Parent > Enfant > ordre"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("Parent > Enfant > après"), std::string::npos);
}

TEST(CliIntegration, RunsLumiTestBeforeAndAfterAllHooks)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_all_hooks";
    write_source(
        root / "all_hooks_test.lum",
        "importer LumiTest\n"
        "soit trace = \"\"\n"
        "LumiTest.groupe(\"Cycle\", fonction() {\n"
        "  LumiTest.avant_tout(fonction() {\n"
        "    trace = trace + \"S\"\n"
        "  })\n"
        "  LumiTest.test(\"premier\", fonction() {\n"
        "    LumiTest.vérifier_égal(\"S\", trace)\n"
        "  })\n"
        "  LumiTest.test(\"second\", fonction() {\n"
        "    LumiTest.vérifier_égal(\"S\", trace)\n"
        "  })\n"
        "  LumiTest.après_tout(fonction() {\n"
        "    trace = trace + \"E\"\n"
        "  })\n"
        "})\n"
        "LumiTest.test(\"hors groupe\", fonction() {\n"
        "  LumiTest.vérifier_égal(\"SE\", trace)\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Cycle > premier"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("Cycle > second"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("hors groupe"), std::string::npos);
}

TEST(CliIntegration, RunsNestedLumiTestAfterAllHooksInScopeOrder)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_nested_all_hooks";
    write_source(
        root / "nested_all_hooks_test.lum",
        "importer LumiTest\n"
        "soit trace = \"\"\n"
        "LumiTest.groupe(\"Parent\", fonction() {\n"
        "  LumiTest.avant_tout(fonction() {\n"
        "    trace = trace + \"P\"\n"
        "  })\n"
        "  LumiTest.groupe(\"Enfant\", fonction() {\n"
        "    LumiTest.avant_tout(fonction() {\n"
        "      trace = trace + \"C\"\n"
        "    })\n"
        "    LumiTest.test(\"intérieur\", fonction() {\n"
        "      LumiTest.vérifier_égal(\"PC\", trace)\n"
        "    })\n"
        "    LumiTest.après_tout(fonction() {\n"
        "      trace = trace + \"c\"\n"
        "    })\n"
        "  })\n"
        "  LumiTest.test(\"après enfant\", fonction() {\n"
        "    LumiTest.vérifier_égal(\"PCc\", trace)\n"
        "  })\n"
        "  LumiTest.après_tout(fonction() {\n"
        "    trace = trace + \"p\"\n"
        "  })\n"
        "})\n"
        "LumiTest.test(\"final\", fonction() {\n"
        "  LumiTest.vérifier_égal(\"PCcp\", trace)\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Parent > Enfant > intérieur"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("Parent > après enfant"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("final"), std::string::npos);
}

TEST(CliIntegration, SupportsLumiTestContextObjectApi)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_context";
    write_source(
        root / "context_test.lum",
        "importer LumiTest\n"
        "soit compteur = 0\n"
        "LumiTest.groupe(\"Contexte\", fonction(t: Universel) {\n"
        "  t.avant_tout(fonction() {\n"
        "    compteur = 40\n"
        "  })\n"
        "  t.avant_chaque(fonction() {\n"
        "    compteur = compteur + 1\n"
        "  })\n"
        "  t.test(\"premier\", fonction(t: Universel) {\n"
        "    t.vérifier_égal(41, compteur)\n"
        "  })\n"
        "  t.test(\"second\", fonction(t: Universel) {\n"
        "    t.vérifier_égal(42, compteur)\n"
        "  })\n"
        "  t.après_tout(fonction() {\n"
        "    compteur = compteur + 100\n"
        "  })\n"
        "})\n"
        "LumiTest.test(\"hors groupe\", fonction(t: Universel) {\n"
        "  t.vérifier_égal(142, compteur)\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Contexte > premier"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("Contexte > second"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("hors groupe"), std::string::npos);
}

TEST(CliIntegration, RejectsWritingAdHocStateOntoTheLumiTestContextObject)
{
    // The `t` context object is one shared LumiereObject instance, created
    // once and reused across every group and test in a run (see
    // register_lumitest_module in lumitest.cpp). If Lumiere let a program
    // write an arbitrary new field onto it (t.donnees = ...), that field
    // would leak from one test into every later one, since it is the same
    // object every time. It cannot: `t` has no class (it is built directly
    // in native code, only its fixed native methods are bound onto it), and
    // field assignment requires the field to be declared on the receiver's
    // class in both engines (TreeWalker::assign_member, VM SET_MEMBER), so
    // this fails the same way assigning an unknown field on any classless
    // object would. This test locks that in rather than leaving it an
    // unverified assumption.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_context_no_ad_hoc_fields";
    write_source(
        root / "context_field_test.lum",
        "importer LumiTest\n"
        "LumiTest.test(\"écrit un champ ad hoc\", fonction(t: Universel) {\n"
        "  t.donnees = 1\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("champ introuvable"), std::string::npos);
}

TEST(CliIntegration, LumiTestFilterSkipsUnmatchedBeforeAll)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_filter_hooks";
    write_source(
        root / "filter_test.lum",
        "importer LumiTest\n"
        "soit trace = \"\"\n"
        "LumiTest.groupe(\"Cycle\", fonction(t: Universel) {\n"
        "  t.avant_tout(fonction() {\n"
        "    trace = trace + \"S\"\n"
        "  })\n"
        "  t.test(\"premier\", fonction(t: Universel) {\n"
        "    t.vérifier_égal(\"S\", trace)\n"
        "  })\n"
        "})\n"
        "LumiTest.test(\"final\", fonction(t: Universel) {\n"
        "  t.vérifier_égal(\"\", trace)\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()) + " --filtre final", root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text.find("Cycle > premier"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("final"), std::string::npos);
}

TEST(CliIntegration, ReportsWhenNoLumiTestMatchesFilter)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_filter_none";
    write_source(
        root / "none_test.lum",
        "importer LumiTest\n"
        "LumiTest.test(\"alpha\", fonction() {\n"
        "  LumiTest.vérifier(vrai)\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()) + " --filtre omega", root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("AUCUN TEST NE CORRESPOND AU FILTRE"), std::string::npos);
    EXPECT_EQ(result.stdout_text.find("alpha"), std::string::npos);
}

TEST(CliIntegration, RunsAfterEachEvenWhenLumiTestFails)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_lumitest_after_each_failure";
    write_source(
        root / "failure_cleanup_test.lum",
        "importer LumiTest\n"
        "soit trace = \"\"\n"
        "LumiTest.groupe(\"Nettoyage\", fonction(t: Universel) {\n"
        "  t.après_chaque(fonction() {\n"
        "    trace = trace + \"C\"\n"
        "  })\n"
        "  t.test(\"échoue\", fonction(t: Universel) {\n"
        "    t.vérifier(faux)\n"
        "  })\n"
        "})\n"
        "LumiTest.test(\"post état\", fonction(t: Universel) {\n"
        "  t.vérifier_égal(\"C\", trace)\n"
        "})\n");

    const CommandResult result = run_cli("tester " + shell_quote(root.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Nettoyage > échoue"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("post état"), std::string::npos);
}

TEST(CliIntegration, BothBackendsKeepOneEntryPerDictionaryKey)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_dictionary_keys";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
fonction principal() {
    soit d = {"a": 1, "b": 2, "a": 3}
    afficher(d.taille())
    afficher(d["a"])
    afficher(d.cles().joindre(","))

    soit ordre = {"z": 1, "y": 2}
    ordre["z"] = 10
    afficher(ordre.cles().joindre(","))
    afficher(ordre["z"])
    ordre.retirer("y")
    ordre["y"] = 9
    afficher(ordre.cles().joindre(","))

    soit nombres = {1: "entier"}
    nombres[1.0] = "decimal"
    afficher(nombres.taille())

    soit zeros = {}
    zeros[0.0] = "plus"
    zeros[-0.0] = "moins"
    afficher(zeros.taille())
    afficher(zeros[0.0])

    soit gauche = [1]
    soit droite = [1]
    soit refs = {}
    refs[gauche] = "gauche"
    refs[droite] = "droite"
    afficher(refs.taille())
    afficher(refs[gauche])
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        // A repeated key overwrites in place: the dictionary keeps two entries and "a"
        // stays first. Entier and Decimal never compare equal, so 1 and 1.0 are two
        // keys, while 0.0 and -0.0 are one. Lists are compared by identity, so two
        // lists with equal contents remain distinct keys.
        EXPECT_EQ(result.stdout_text, "2\n3\na,b\nz,y\n10\nz,y\n2\n1\nmoins\n2\ngauche\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsRejectNonFiniteNumericText)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_non_finite_text";
    const auto file = root / "main.lum";
    // Every arithmetic path that would reach a non-number or an infinity traps, so
    // text conversion must not be the one door that lets them in.
    for (const auto *source : {"nan", "NaN", "inf", "-inf", "infinity"})
    {
        write_source(file,
                     "fonction principal() {\n"
                     "    soit lu = agir selon \"" + std::string(source) + "\".en_decimal() {\n"
                     "        Succès(valeur) -> valeur\n"
                     "        Échec(_) -> -1.0\n"
                     "    }\n"
                     "    afficher(lu)\n"
                     "}\n");
        for (const auto *backend : {"--vm", "--tw"})
        {
            SCOPED_TRACE(std::string(backend) + " " + source);
            const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
            EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
            EXPECT_EQ(result.stdout_text, "-1.0\n");
        }
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsReadScientificNotation)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_scientific_notation";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
fonction principal() {
    afficher(1.0e3)
    afficher(1e3)
    afficher(1.5E-3)
    afficher(2e+2)
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "1000.0\n1000.0\n0.0015\n200.0\n");
    }

    // A letter touching a number used to split into two tokens, which turned a
    // missing exponent into a runtime "variable introuvable".
    write_source(file, "fonction principal() { afficher(12abc) }\n");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_NE(result.exit_code, 0);
        EXPECT_NE(result.stderr_text.find("nombre invalide"), std::string::npos) << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsUseFixedListsAsDictionaryKeys)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_fixed_list_keys";
    const auto file = root / "main.lum";
    // A fixed list is compared by content and can no longer be reassigned, so it
    // stays equal to the key that was stored: two fixed lists with the same
    // elements are the same key, and a different order is a different key.
    write_source(file, R"lum(
fonction principal() {
    soit fx = {}
    fx[[1, 2].en_liste_fixe(2)] = "paire"
    afficher(fx[[1, 2].en_liste_fixe(2)])
    fx[[1, 2].en_liste_fixe(2)] = "encore"
    afficher(fx.taille())
    afficher(fx[[1, 2].en_liste_fixe(2)])
    fx[[2, 1].en_liste_fixe(2)] = "inverse"
    afficher(fx.taille())
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "paire\n1\nencore\n2\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsRejectFixedListElementAssignment)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_fixed_list_immutable";
    const auto file = root / "main.lum";
    write_source(file, "fonction principal() { soit t = [1, 2].en_liste_fixe(2) t[0] = 9 }\n");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_NE(result.exit_code, 0);
        EXPECT_NE(result.stderr_text.find("immuable"), std::string::npos) << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsWalkDictionaryKeys)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_dictionary_iteration";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
fonction principal() {
    soit d = {"a": 1, "b": 2}
    pour chaque cle dans d {
        afficher(cle + "=" + d[cle])
    }
    afficher(d.clés().joindre(","))
    afficher(d.cles().joindre(","))
    pour chaque cle dans d {
        si cle == "a" { d["z"] = 9 }
        afficher(cle)
    }
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        // Keys in insertion order, and the loop walks a snapshot, so "z" is not visited.
        EXPECT_EQ(result.stdout_text, "a=1\nb=2\na,b\na,b\na\nb\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsBuildAndCombineSets)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_sets";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
fonction principal() {
    soit e = {1, 2, 2, 3}
    afficher(e)
    afficher(e.taille())
    afficher(e.contient(2))
    afficher(e.ajouter(3))
    afficher(e.ajouter(4))
    afficher(e.retirer(1))
    afficher(e.retirer(1))
    afficher(e)

    soit f = [3, 4, 5, 5].en_ensemble()
    afficher(f)
    afficher(e.union(f))
    afficher(e.intersection(f))
    afficher(e.difference(f))
    afficher(e.différence(f))
    afficher(e.sous_ensemble_de(f))
    afficher({3, 4}.sous_ensemble_de(f))

    pour chaque x dans f { afficher(x) }
    afficher(f.en_liste().joindre("-"))
    afficher({}.taille())
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        // Duplicates collapse on construction, elements keep insertion order, and an
        // empty {} is still the empty dictionary.
        EXPECT_EQ(result.stdout_text,
                  "{1, 2, 3}\n3\nvrai\nfaux\nvrai\nvrai\nfaux\n{2, 3, 4}\n"
                  "{3, 4, 5}\n{2, 3, 4, 5}\n{3, 4}\n{2}\n{2}\nfaux\nvrai\n"
                  "3\n4\n5\n3-4-5\n0\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsTypeSetLiterals)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_set_types";
    const auto file = root / "main.lum";
    write_source(file, R"lum(
fonction principal() {
    soit e: Ensemble[Entier] = {1, 2, 3}
    e.ajouter(4)
    afficher(e.taille())
    e.ajouter("oups")
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_NE(result.exit_code, 0);
        EXPECT_NE(result.stdout_text.find("4\n"), std::string::npos) << result.stdout_text;
        EXPECT_NE(result.stderr_text.find("Entier"), std::string::npos) << result.stderr_text;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsReportTheSameRuntimeDiagnostic)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_diagnostic_parity";
    const auto file = root / "main.lum";
    // The two engines detect these conditions in separate code. When each owned its
    // wording they drifted: the same division reported "division par zero" from one
    // and "division par zéro interdite" from the other, and an unknown member named
    // the member in one and blamed the receiver in the other. Every message a
    // program can reach is now written once, in diagnostics/runtime_messages.hpp.
    const std::vector<std::string> programs = {
        "soit a = 1 / 0 afficher(a)",
        "soit a = 1.0 / 0.0 afficher(a)",
        "soit a = 1 % 0 afficher(a)",
        "soit a = 9223372036854775807 + 1 afficher(a)",
        "soit l = [1] afficher(l[5])",
        "soit l = [1] afficher(l[-1])",
        "soit l = [1] afficher(l[\"a\"])",
        "soit d = {\"a\": 1} afficher(d[\"b\"])",
        "soit d = {\"a\": 1} d.retirer(\"b\")",
        "soit d = {\"a\": 1} d.zzz()",
        "soit l = [1] l.zzz()",
        "soit e = {1} e.zzz()",
        "soit t = [1].en_liste_fixe(1) t[0] = 2",
        "afficher(\"ab\"[9])",
        "pour chaque x dans 3 { afficher(x) }",
        "soit x = 1 x()",
        "soit x = 1 afficher(x[0])",
        "soit x = 1 x[0] = 2",
        "afficher(\"zz\" en Entier)",
        "afficher(\"zz\" en Décimal)",
        // Each engine refused an operand with no conversion in its own words,
        // and the VM answered an Entier sent to Logique with a sentence about
        // text. There is one conversion now, in runtime/conversions.cpp.
        "soit v: Universel = vrai afficher(v en Entier)",
        "soit v: Universel = vrai afficher(v en Décimal)",
        "soit v: Universel = 3 afficher(v en Logique)",
        "soit v: Universel = vrai afficher(v en Symbole)",
        "soit e = {1} e.union(3)",
        "soit e: Ensemble[Entier] = {1} e.ajouter(\"x\")",
        "soit l: Liste[Entier] = [1] l.ajouter(\"x\")",
        // The Liste members were implemented twice, and the two wordings had
        // drifted on every one of these: the VM's "indice hors limites" said
        // neither the index nor the size, its en_liste_fixe said neither the
        // length it wanted nor that a negative one is a different mistake, and
        // its argument checks named a type where the tree walker named a value.
        "soit l: Liste[Entier] = [1] afficher(l.retirer_a(5))",
        "soit l: Liste[Entier] = [1] afficher(l.retirer_a(-1))",
        "soit l: Liste[Entier] = [1] afficher(l.inserer(5, 2))",
        "soit l: Liste[Entier] = [1] afficher(l.en_liste_fixe(3))",
        "soit l: Liste[Entier] = [1] afficher(l.en_liste_fixe(-1))",
        "soit l: Liste[Universel] = [1] afficher(l.joindre(2))",
        "soit l: Liste[Universel] = [1] afficher(l.inserer(\"a\", 2))",
        // A ListeFixe answers the shared sequence members, so it drifted with
        // them; and the Liste en_liste hands back has to keep the contract, or
        // a round trip through ListeFixe would launder it away.
        "soit s: Liste[Texte] = [\"a\"] afficher(s.en_liste_fixe(1).joindre(2))",
        "soit s: Liste[Texte] = [\"a\"] s.en_liste_fixe(1).en_liste().ajouter(3)",
        // Every set operation takes a set, and says so the way every other
        // builtin says what it takes.
        "soit e: Ensemble[Entier] = {1} afficher(e.intersection(\"a\"))",
        "soit e: Ensemble[Entier] = {1} afficher(e.différence(1))",
        "soit e: Ensemble[Entier] = {1} afficher(e.sous_ensemble_de(1))",
        "soit l: Liste[Universel] = [1] afficher(l.en_ensemble().joindre(2))",
        // The three views a Dictionnaire hands out carry its contract. The VM
        // gave paires() no contract at all, so a Liste[ListeFixe[Texte, 2]]
        // accepted an Entier there and the tree walker refused it.
        "soit d: Dictionnaire[Texte, Texte] = {\"a\": \"x\"} d.paires().ajouter(1)",
        "soit d: Dictionnaire[Texte, Entier] = {\"a\": 1} d.cles().ajouter(2)",
        "soit d: Dictionnaire[Texte, Entier] = {\"a\": 1} d.valeurs().ajouter(\"z\")",
        "soit d: Dictionnaire[Texte, Entier] = {\"a\": 1} d[\"b\"] = \"x\"",
        "afficher(\"ab\".sous_texte(0, 99))",
    };

    const auto first_error_line = [](const std::string &text) {
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.find("erreur") != std::string::npos && line.find("File \"") == std::string::npos)
            {
                return line;
            }
        }
        return std::string();
    };

    for (const std::string &program : programs)
    {
        write_source(file, "importer Maths\nfonction principal() { " + program + " }\n");
        const auto vm = run_cli("--vm " + shell_quote(file.string()), root);
        const auto tw = run_cli("--tw " + shell_quote(file.string()), root);
        SCOPED_TRACE(program);
        EXPECT_NE(vm.exit_code, 0);
        EXPECT_NE(tw.exit_code, 0);
        const std::string vm_error = first_error_line(vm.stderr_text);
        EXPECT_FALSE(vm_error.empty()) << vm.stderr_text;
        EXPECT_EQ(vm_error, first_error_line(tw.stderr_text));
        // Runtime diagnostics are written in French, accents included.
        EXPECT_NE(vm_error.find("erreur d'exécution"), std::string::npos) << vm_error;
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, BothBackendsTypeBuiltinCollectionMembers)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_collection_member_types";
    const auto file = root / "main.lum";
    // Every one of these declarations used to fail analysis with "reçu Universel":
    // the analyzer typed only `taille`, so a collection member call could not
    // initialize a declared collection type, including the idiom the overview
    // documents for en_liste_fixe.
    write_source(file, R"lum(
fonction principal() {
    soit notes: Liste[Entier] = [1, 2, 3]
    soit trio: ListeFixe[Entier, 3] = notes.en_liste_fixe(3)
    soit revenu: Liste[Entier] = trio.en_liste()
    soit unique: Ensemble[Entier] = notes.en_ensemble()
    soit combine: Ensemble[Entier] = unique.union(unique)
    soit premier: Entier = notes.retirer_a(0)

    soit index: Dictionnaire[Texte, Entier] = {"a": 1, "b": 2}
    soit clés: Liste[Texte] = index.clés()
    soit valeurs: Liste[Entier] = index.valeurs()

    afficher(trio.taille())
    afficher(revenu.joindre(","))
    afficher(combine.taille())
    afficher(premier)
    afficher(clés.joindre(",") + " " + valeurs.joindre(","))
    afficher(notes.en_ensemble().en_liste().joindre("-"))
}
)lum");
    for (const auto *backend : {"--vm", "--tw"})
    {
        SCOPED_TRACE(backend);
        const auto result = run_cli(std::string(backend) + " " + shell_quote(file.string()), root);
        EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
        EXPECT_EQ(result.stdout_text, "3\n1,2,3\n3\n1\na,b 1,2\n2-3\n");
    }
    std::filesystem::remove_all(root);
}

TEST(CliIntegration, RejectsMistypedCollectionMemberResult)
{
    const auto root = std::filesystem::temp_directory_path() / "lumiere_collection_member_mistype";
    const auto file = root / "main.lum";
    // Knowing the result type also means a wrong one is caught before the program runs.
    write_source(file, R"lum(
fonction principal() {
    soit index: Dictionnaire[Texte, Entier] = {"a": 1}
    soit mauvais: Liste[Entier] = index.clés()
    afficher(mauvais.taille())
}
)lum");
    const auto result = run_cli("--vm " + shell_quote(file.string()), root);
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("attend Liste[Entier]; reçu Liste[Texte]"), std::string::npos)
        << result.stderr_text;
    std::filesystem::remove_all(root);
}

} // namespace
