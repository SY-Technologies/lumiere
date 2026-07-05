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
                      const std::filesystem::path &working_dir)
{
    const std::filesystem::path stdout_path = working_dir / "stdout.txt";
    const std::filesystem::path stderr_path = working_dir / "stderr.txt";
#ifdef _WIN32
    const std::string command =
        "cmd /C \"\"" + std::string(LUMIERE_CLI_PATH) + "\" " + args +
        " > \"" + stdout_path.string() +
        "\" 2> \"" + stderr_path.string() + "\"\"";
#else
    const std::string command =
        shell_quote(LUMIERE_CLI_PATH) + " " + args +
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

    const CommandResult result = run_cli("--tree-walker --run " + shell_quote(main_file.string()), root);
    std::filesystem::remove_all(root);

    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_text.find("bonjour"), std::string::npos);
    EXPECT_TRUE(result.stderr_text.empty());
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

TEST(CliIntegration, VmBackendFailsClearlyWhileUnimplemented)
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

    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(result.stderr_text.find("VM"), std::string::npos);
    EXPECT_NE(result.stderr_text.find("pas encore implemente"), std::string::npos);
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
