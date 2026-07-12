#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
    const std::filesystem::path stdout_path = working_dir / "stdout.txt";
    const std::filesystem::path stderr_path = working_dir / "stderr.txt";
    const std::filesystem::path stdin_path = working_dir / "stdin.txt";
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
    EXPECT_TRUE(result.stderr_text.empty());
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
    EXPECT_NE(result.stderr_text.find("variable globale introuvable"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("aucun parametre nomme 'inconnu'"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("incompatible avec Entier"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("incompatible avec Entier"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("variable fixe 'valeur'"), std::string::npos);
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
    EXPECT_NE(explicit_result.stderr_text.find("incompatible avec Entier"), std::string::npos);
    EXPECT_NE(implicit_result.exit_code, 0);
    EXPECT_NE(implicit_result.stderr_text.find("incompatible avec Entier"), std::string::npos);
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
    EXPECT_EQ(result.stdout_text, "BONJOUR MONDE\nlumiere\nvrai\n3\nababab\nbonsoir\na|b|c\n42\n3.14\nvrai\n");
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
    for (int i = 0; i < 260; ++i)
    {
        source << "  champ" << i << ": Universel\n";
    }
    source << "}\nfonction principal() {\n"
           << "  soit objet = Large()\n"
           << "  objet.champ259 = 42\n"
           << "  afficher(objet.champ259)\n"
           << "}\n";
    write_source(main_file, source.str());

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "42\n");
    EXPECT_TRUE(result.stderr_text.empty());
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
    EXPECT_NE(result.stderr_text.find("Liste.ajouter attend une valeur Entier"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("attend Texte -> Entier"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("Liste.ajouter attend une valeur Texte"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("cle introuvable"), std::string::npos);
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
    EXPECT_EQ(result.stdout_text, "42\n123\nvrai\n65\nB\n9\n");
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
    EXPECT_TRUE(result.stderr_text.empty());
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

TEST(CliIntegration, ExecutesRepositoryObjectsAndErrorsExample)
{
    const std::filesystem::path example_file = repo_examples_dir() / "objets_et_erreurs.lum";
    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(example_file.string()),
                                         example_file.parent_path());

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("compteur=3"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("capture=vrai"), std::string::npos);
    EXPECT_NE(result.stdout_text.find("fin"), std::string::npos);
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
                                         ":quitter\n");
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("Lumiere "), std::string::npos);
    EXPECT_NE(result.stdout_text.find("42\n"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("plus d'un fichier a ete fourni"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("erreur d'execution"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("indice hors limites"), std::string::npos);
}

TEST(CliIntegration, ReportsUnhandledThrownValuesWithTraceback)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_unhandled_throw_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  lancer 42\n"
        "}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("Traceback (most recent call last):"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("in principal"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("File \"" + main_file.string() + "\""), std::string::npos);
    EXPECT_NE(result.stderr_text.find("erreur d'execution: exception non attrapee: 42"), std::string::npos);
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
    EXPECT_NE(result.stdout_text.find("\"protocolVersion\":1"), std::string::npos);
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
    EXPECT_NE(result.stderr_text.find("erreur de syntaxe"), std::string::npos);
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
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, ExecutesBuiltinModulesEndToEnd)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_builtin_test";
    const std::filesystem::path note_file = root / "note.txt";
    write_source(note_file, "bonjour");
    write_source(
        root / "main.lum",
        "importer Fichier.{existe, lire_texte}\n"
        "fonction principal() {\n"
        "  afficher(existe(\"" + note_file.string() + "\"))\n"
        "  afficher(lire_texte(\"" + note_file.string() + "\"))\n"
        "}\n");

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote((root / "main.lum").string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("vrai\nbonjour\n"), std::string::npos);
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

TEST(CliIntegration, VmBackendSupportsTypedThrowCatchAndFinally)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_exception_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  essayer {\n"
        "    lancer 42\n"
        "  } attraper (e: Entier) {\n"
        "    afficher(e)\n"
        "  } finalement {\n"
        "    afficher(\"fin\")\n"
        "  }\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "42\nfin\n");
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, VmBackendCatchesRuntimeErrorsAsText)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_runtime_exception_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction principal() {\n"
        "  essayer {\n"
        "    soit xs = [1]\n"
        "    afficher(xs[3])\n"
        "  } attraper (e: Texte) {\n"
        "    afficher(e)\n"
        "  }\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("indice hors limites"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
}

TEST(CliIntegration, VmBackendRunsFinallyBeforeNonLocalExits)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "lumiere_cli_vm_finally_exit_test";
    const std::filesystem::path main_file = root / "main.lum";
    write_source(
        main_file,
        "fonction f() {\n"
        "  essayer { retourne 7 } attraper (e: Universel) {} finalement { afficher(\"retour\") }\n"
        "}\n"
        "fonction principal() {\n"
        "  afficher(f())\n"
        "  pour chaque n dans [1, 2] {\n"
        "    essayer { arrêter } attraper (e: Universel) {} finalement { afficher(\"arrêt\") }\n"
        "  }\n"
        "  pour chaque n dans [1, 2] {\n"
        "    essayer { afficher(n) continuer } attraper (e: Universel) {} finalement { afficher(\"suite\") }\n"
        "  }\n"
        "}\n");

    const CommandResult result = run_cli("--vm --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, "retour\n7\narrêt\n1\nsuite\n2\nsuite\n");
    EXPECT_TRUE(result.stderr_text.empty());
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
    EXPECT_NE(private_access.stderr_text.find("champ prive"), std::string::npos);
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
    EXPECT_EQ(result.stdout_text, "init\n40\n6\n9\n");
    EXPECT_TRUE(result.stderr_text.empty());
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
    EXPECT_NE(private_import.stderr_text.find("non exporte"), std::string::npos);
    EXPECT_NE(cycle.exit_code, 0);
    EXPECT_NE(cycle.stderr_text.find("cycle d'import"), std::string::npos);
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
    EXPECT_TRUE(result.stderr_text.empty());
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

} // namespace
