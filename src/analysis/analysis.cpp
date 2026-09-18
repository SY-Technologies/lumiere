#include "lumiere/analysis/analysis.hpp"

#include "lumiere/analysis/native_signatures.hpp"
#include "lumiere/analysis/semantic_analysis.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"
#include "lumiere/source_file.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace lumiere
{
namespace
{

void collect_imports(const Stmt &statement, std::vector<const ImportStmt *> &imports)
{
    if (const auto *import = dynamic_cast<const ImportStmt *>(&statement))
    {
        imports.push_back(import);
    }
    else if (const auto *block = dynamic_cast<const BlockStmt *>(&statement))
    {
        for (const StmtPtr &child : block->statements)
        {
            collect_imports(*child, imports);
        }
    }
    else if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(&statement);
             function != nullptr && function->body != nullptr)
    {
        collect_imports(*function->body, imports);
    }
    else if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(&statement))
    {
        for (const StmtPtr &member : klass->members)
        {
            collect_imports(*member, imports);
        }
    }
    else if (const auto *conditional = dynamic_cast<const IfStmt *>(&statement))
    {
        collect_imports(*conditional->then_branch, imports);
        if (conditional->else_branch != nullptr)
        {
            collect_imports(*conditional->else_branch, imports);
        }
    }
    else if (const auto *loop = dynamic_cast<const ForStmt *>(&statement))
    {
        collect_imports(*loop->body, imports);
    }
    else if (const auto *loop = dynamic_cast<const WhileStmt *>(&statement))
    {
        collect_imports(*loop->body, imports);
    }
    else if (const auto *match = dynamic_cast<const AgirSelonStmt *>(&statement))
    {
        for (const AgirSelonBranch &branch : match->branches)
        {
            collect_imports(*branch.body, imports);
        }
        if (match->else_branch != nullptr)
        {
            collect_imports(*match->else_branch, imports);
        }
    }
}

std::filesystem::path module_path(const std::filesystem::path &source_path,
                                  const std::string &module_name)
{
    std::filesystem::path candidate = source_path.parent_path();
    std::size_t start = 0;
    while (start < module_name.size())
    {
        const std::size_t end = module_name.find('.', start);
        const bool last = end == std::string::npos;
        candidate /= module_name.substr(start, last ? std::string::npos : end - start);
        if (last)
        {
            candidate += SOURCE_FILE_EXTENSION;
            break;
        }
        start = end + 1;
    }
    return std::filesystem::exists(candidate) ? candidate : std::filesystem::path{};
}

void append_diagnostics(
    std::vector<Diagnostic> &destination,
    std::vector<Diagnostic> source,
    const std::string &source_path)
{
    for (Diagnostic &diagnostic : source)
    {
        if (diagnostic.source_path.empty())
        {
            diagnostic.source_path = source_path;
        }
        destination.push_back(std::move(diagnostic));
    }
}

SemanticImportEnvironment build_import_environment(
    const StmtList &statements,
    const std::filesystem::path &source_path,
    std::vector<Diagnostic> &diagnostics,
    std::unordered_set<std::string> &modules_in_progress,
    std::vector<std::pair<std::string, std::string>> &module_stack,
    std::unordered_map<std::string, SemanticModuleExports> &module_cache)
{
    SemanticImportEnvironment environment;
    std::vector<const ImportStmt *> imports;
    for (const StmtPtr &statement : statements)
    {
        collect_imports(*statement, imports);
    }
    for (const ImportStmt *import : imports)
    {
        if (environment.contains(import->module_name.lexeme))
        {
            continue;
        }
        const std::filesystem::path path =
            module_path(source_path, import->module_name.lexeme);
        if (path.empty())
        {
            environment.emplace(
                import->module_name.lexeme,
                native_module_exports(import->module_name.lexeme)
                    .value_or(SemanticModuleExports{}));
            continue;
        }

        const std::string module_key =
            std::filesystem::absolute(path).lexically_normal().string();
        if (const auto cached = module_cache.find(module_key);
            cached != module_cache.end())
        {
            environment.emplace(import->module_name.lexeme, cached->second);
            continue;
        }
        if (!modules_in_progress.insert(module_key).second)
        {
            const auto cycle_start = std::find_if(
                module_stack.begin(),
                module_stack.end(),
                [&](const auto &module) { return module.first == module_key; });
            std::ostringstream cycle;
            for (auto module = cycle_start; module != module_stack.end(); ++module)
            {
                if (module != cycle_start)
                {
                    cycle << " -> ";
                }
                cycle << module->second;
            }
            if (cycle_start != module_stack.end())
            {
                cycle << " -> ";
            }
            cycle << import->module_name.lexeme;
            diagnostics.push_back(Diagnostic{
                "LUM-S0043",
                DiagnosticSeverity::ERROR_LEVEL,
                "cycle d'import: " + cycle.str(),
                source_path.string(),
                SourceRange{
                    import->module_name.start_offset,
                    import->module_name.end_offset,
                    import->module_name.line,
                    import->module_name.column}});
            environment.emplace(import->module_name.lexeme,
                                SemanticModuleExports{});
            continue;
        }
        module_stack.emplace_back(module_key, import->module_name.lexeme);

        std::ifstream input(path);
        std::ostringstream source;
        source << input.rdbuf();
        Lexer lexer(source.str());
        Parser parser(lexer.tokenise());
        StmtList module_statements = parser.parse();
        append_diagnostics(
            diagnostics,
            lexer.diagnostics(),
            path.string());
        append_diagnostics(
            diagnostics,
            parser.diagnostics(),
            path.string());
        if (!lexer.diagnostics().empty() || parser.had_error())
        {
            environment.emplace(import->module_name.lexeme,
                                SemanticModuleExports{});
            module_stack.pop_back();
            modules_in_progress.erase(module_key);
            continue;
        }

        const SemanticImportEnvironment child_imports =
            build_import_environment(
                module_statements,
                path,
                diagnostics,
                modules_in_progress,
                module_stack,
                module_cache);
        SemanticAnalysis module_analysis =
            analyze_semantics(
                module_statements,
                path.string(),
                child_imports);
        append_diagnostics(
            diagnostics,
            std::move(module_analysis.diagnostics),
            path.string());

        SemanticModuleExports exports =
            collect_semantic_exports(module_statements, module_analysis.model);
        module_cache.emplace(module_key, exports);
        environment.emplace(import->module_name.lexeme,
                            std::move(exports));
        module_stack.pop_back();
        modules_in_progress.erase(module_key);
    }
    return environment;
}

} // namespace

bool AnalysisResult::has_errors() const noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::ERROR_LEVEL;
    });
}

AnalysisResult analyze_source(std::string source,
                              std::string source_path,
                              const AnalysisOptions options)
{
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenise();

    AnalysisResult result;
    result.diagnostics = lexer.diagnostics();
    for (Diagnostic &diagnostic : result.diagnostics)
    {
        diagnostic.source_path = source_path;
    }

    if (!result.has_errors())
    {
        Parser parser(std::move(tokens));
        result.statements = parser.parse();
        for (Diagnostic diagnostic : parser.diagnostics())
        {
            diagnostic.source_path = source_path;
            result.diagnostics.push_back(std::move(diagnostic));
        }
        if (!result.has_errors())
        {
            std::unordered_set<std::string> modules_in_progress;
            std::vector<std::pair<std::string, std::string>> module_stack;
            std::unordered_map<std::string, SemanticModuleExports> module_cache;
            const SemanticImportEnvironment imports =
                build_import_environment(
                    result.statements,
                    std::filesystem::path(source_path),
                    result.diagnostics,
                    modules_in_progress,
                    module_stack,
                    module_cache);
            SemanticAnalysis semantics =
                analyze_semantics(
                    result.statements,
                    source_path,
                    imports,
                    SemanticAnalysisOptions{
                        options.consume_last_expression,
                        options.require_entry_point});
            result.diagnostics.insert(
                result.diagnostics.end(),
                std::make_move_iterator(semantics.diagnostics.begin()),
                std::make_move_iterator(semantics.diagnostics.end()));
        }
    }
    return result;
}

} // namespace lumiere
