#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/interpreter/tree_walker/runtime.hpp"
#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/interpreter/vm/vm.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/ast.hpp"
#include "lumiere/parser/parser.hpp"

namespace
{

struct CliOptions
{
    std::string backend;
    std::string file_argument;
    bool execute = false;
    bool print_help = false;
    bool print_version = false;
};

struct TestCliOptions
{
    std::string path_argument;
    std::string filter;
    bool verbose = false;
    bool stop_on_failure = false;
};

void print_usage()
{
    std::cerr << "usage: lumiere [--vm] [--run] <fichier.lum>\n";
    std::cerr << "       lumiere tester [chemin] [--filtre motif] [--verbeux] [--arrêter-sur-échec]\n";
    std::cerr << "       lumiere --help\n";
    std::cerr << "       lumiere --version\n";
}

void print_version()
{
    std::cout << "Lumiere " << LUMIERE_VERSION << '\n';
}

std::filesystem::path resolve_input_file(const std::string &file_argument)
{
    const std::string extension = ".lum";
    const std::filesystem::path input_path(file_argument);
    if (std::filesystem::exists(input_path))
    {
        return input_path;
    }

    std::string file_name = file_argument;
    if (input_path.extension() != extension)
    {
        file_name = input_path.stem().string() + extension;
    }

    return std::filesystem::current_path() / "examples" / file_name;
}

std::string read_file_text(const std::filesystem::path &path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("impossible d'ouvrir '" + path.string() + "'");
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

CliOptions parse_run_args(int argc, char *argv[])
{
    CliOptions options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--vm")
        {
            options.backend = "vm";
            options.execute = true;
            continue;
        }

        if (arg == "--tree-walker")
        {
            options.backend = "tree-walker";
            options.execute = true;
            continue;
        }

        if (arg == "--run")
        {
            options.execute = true;
            continue;
        }

        if (arg == "--help" || arg == "-h")
        {
            options.print_help = true;
            continue;
        }

        if (arg == "--version" || arg == "-V")
        {
            options.print_version = true;
            continue;
        }

        if (!arg.empty() && arg[0] == '-')
        {
            print_usage();
            throw std::runtime_error("option inconnue: " + arg);
        }

        if (!options.file_argument.empty())
        {
            print_usage();
            throw std::runtime_error("plus d'un fichier a ete fourni");
        }

        options.file_argument = arg;
    }

    if (options.print_help || options.print_version)
    {
        if (!options.file_argument.empty() || options.execute || !options.backend.empty())
        {
            print_usage();
            throw std::runtime_error("--help et --version ne prennent pas d'autre argument");
        }

        return options;
    }

    if (options.file_argument.empty())
    {
        print_usage();
        throw std::runtime_error("aucun fichier .lum fourni");
    }

    if (options.backend.empty())
    {
        options.backend = "tree-walker";
    }

    return options;
}

TestCliOptions parse_test_args(int argc, char *argv[])
{
    TestCliOptions options;

    for (int i = 2; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--verbeux")
        {
            options.verbose = true;
            continue;
        }

        if (arg == "--arrêter-sur-échec" || arg == "--arreter-sur-echec")
        {
            options.stop_on_failure = true;
            continue;
        }

        if (arg == "--filtre")
        {
            if (i + 1 >= argc)
            {
                print_usage();
                throw std::runtime_error("--filtre requiert une valeur");
            }
            options.filter = argv[++i];
            continue;
        }

        if (!arg.empty() && arg[0] == '-')
        {
            print_usage();
            throw std::runtime_error("option inconnue: " + arg);
        }

        if (!options.path_argument.empty())
        {
            print_usage();
            throw std::runtime_error("plus d'un chemin de test a ete fourni");
        }

        options.path_argument = arg;
    }

    return options;
}

std::unique_ptr<lumiere::Backend> make_backend(const std::string &backend_name)
{
    if (backend_name == "tree-walker")
    {
        return std::make_unique<lumiere::TreeWalker>();
    }

    if (backend_name == "vm")
    {
        return std::make_unique<lumiere::VM>();
    }

    throw std::runtime_error("backend inconnu: " + backend_name);
}

std::vector<std::filesystem::path> discover_test_files(const TestCliOptions &options)
{
    const std::filesystem::path root = options.path_argument.empty()
                                           ? std::filesystem::current_path()
                                           : std::filesystem::path(options.path_argument);

    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(root))
    {
        throw std::runtime_error("chemin introuvable: " + root.string());
    }

    if (std::filesystem::is_regular_file(root))
    {
        if (root.extension() == ".lum" && root.filename().string().ends_with("_test.lum"))
        {
            files.push_back(root);
        }
        return files;
    }

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        if (entry.path().extension() == ".lum" && filename.ends_with("_test.lum"))
        {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

void print_result_line(const lumiere::LumiTestCaseResult &result)
{
    if (result.passed)
    {
        std::cout << "    ✓ " << result.name << '\n';
        return;
    }

    std::cout << "    ✗ " << result.name << '\n';
    std::cout << "        Échec";
    if (!result.source_path.empty())
    {
        std::cout << " — " << result.source_path;
        if (result.line > 0)
        {
            std::cout << ':' << result.line;
            if (result.column > 0)
            {
                std::cout << ':' << result.column;
            }
        }
    }
    std::cout << '\n';

    std::istringstream details(result.failure_message);
    std::string line;
    while (std::getline(details, line))
    {
        std::cout << "        " << line << '\n';
    }
}

int run_test_file(const std::filesystem::path &file_path,
                  const TestCliOptions &options,
                  lumiere::LumiTestRunSummary &aggregate)
{
    const std::string source = read_file_text(file_path);
    lumiere::Lexer lexer(source);
    std::vector<lumiere::Token> tokens = lexer.tokenise();
    lumiere::Parser parser(tokens);
    auto statements = parser.parse();
    if (parser.had_error())
    {
        std::cerr << "erreur: échec d'analyse du fichier de test " << file_path.string() << '\n';
        return 1;
    }

    lumiere::Program program{
        std::move(statements),
        file_path.string(),
        source,
    };

    lumiere::TreeWalker backend;
    backend.configure_lumitest(lumiere::LumiTestRuntimeOptions{
        options.verbose,
        options.stop_on_failure,
        options.filter,
    });

    try
    {
        backend.execute(program);
    }
    catch (const lumiere::RuntimeError &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }

    const lumiere::LumiTestRunSummary summary = backend.lumitest_summary();
    if (summary.executed == 0)
    {
        return 0;
    }

    std::cout << file_path.string() << '\n';
    for (const auto &result : summary.results)
    {
        print_result_line(result);
    }

    aggregate.executed += summary.executed;
    aggregate.failed += summary.failed;
    aggregate.results.insert(aggregate.results.end(), summary.results.begin(), summary.results.end());
    return 0;
}

int run_tester_command(const TestCliOptions &options)
{
    const std::vector<std::filesystem::path> files = discover_test_files(options);
    std::cout << "--- LumiTest ---\n\n";

    if (files.empty())
    {
        std::cout << "--- RÉUSSI — 0 tests ---\n";
        return 0;
    }

    lumiere::LumiTestRunSummary aggregate;
    for (const auto &file_path : files)
    {
        const int file_status = run_test_file(file_path, options, aggregate);
        if (file_status != 0)
        {
            return file_status;
        }

        if (options.stop_on_failure && aggregate.failed > 0)
        {
            break;
        }
    }

    std::cout << '\n';
    if (aggregate.executed == 0 && !options.filter.empty())
    {
        std::cout << "--- AUCUN TEST NE CORRESPOND AU FILTRE \"" << options.filter << "\" ---\n";
        return 0;
    }

    if (aggregate.failed == 0)
    {
        std::cout << "--- RÉUSSI — " << aggregate.executed << " tests ---\n";
        return 0;
    }

    std::cout << "--- ÉCHOUÉ — " << aggregate.failed << " échec";
    if (aggregate.failed > 1)
    {
        std::cout << "s";
    }
    std::cout << " sur " << aggregate.executed << " tests ---\n";
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    try
    {
        if (argc >= 2 && std::string(argv[1]) == "tester")
        {
            return run_tester_command(parse_test_args(argc, argv));
        }

        const CliOptions options = parse_run_args(argc, argv);
        if (options.print_help)
        {
            print_usage();
            return 0;
        }

        if (options.print_version)
        {
            print_version();
            return 0;
        }

        const std::filesystem::path file_path = resolve_input_file(options.file_argument);
        std::ifstream file(file_path);

        if (!file.is_open())
        {
            std::cerr << "erreur: impossible d'ouvrir '" << options.file_argument << "'\n";
            return 1;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();
        lumiere::Lexer lexer(source);
        lumiere::Parser parser(lexer.tokenise());
        auto statements = parser.parse();

        if (parser.had_error())
        {
            return 1;
        }

        if (options.execute)
        {
            lumiere::Program program{
                std::move(statements),
                file_path.string(),
                std::move(source),
            };
            auto backend = make_backend(options.backend);
            backend->execute(program);
        }

        return 0;
    }
    catch (const lumiere::RuntimeError &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "erreur: " << error.what() << '\n';
        return 1;
    }
}
