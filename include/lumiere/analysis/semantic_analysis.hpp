#pragma once

#include "lumiere/analysis/semantic_type.hpp"
#include "lumiere/diagnostics/diagnostic.hpp"
#include "lumiere/parser/ast.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lumiere
{

class SemanticAnalyzer;
struct SemanticAnalysis;
struct SemanticAnalysisOptions;

enum class SemanticSymbolKind
{
    VARIABLE,
    PARAMETER,
    FUNCTION,
    CLASS,
    INTERFACE,
    MODULE,
};

struct SemanticModuleExports
{
    struct Callable
    {
        std::vector<std::string> parameter_names;
        std::vector<TypeExpr> parameter_types;
        std::vector<bool> optional_parameters;
        TypeExpr return_type;
        bool has_explicit_return_type = false;
    };

    std::unordered_map<std::string, SemanticTypeKind> types;
    std::unordered_map<std::string, TypeExpr> aliases;
    std::unordered_map<std::string, SemanticSymbolKind> values;
    std::unordered_map<std::string, TypeExpr> value_types;
    std::unordered_map<std::string, Callable> callables;
    std::unordered_set<std::string> error_types;
};

using SemanticImportEnvironment =
    std::unordered_map<std::string, SemanticModuleExports>;

struct SemanticSymbol
{
    SemanticSymbolKind kind;
    std::string name;
    const Stmt *declaration = nullptr;
    SemanticTypeRef type;
};

struct CallableSignature
{
    std::vector<std::string> parameter_names;
    std::vector<SemanticTypeRef> parameter_types;
    std::vector<bool> optional_parameters;
    SemanticTypeRef return_type;
    bool has_explicit_return_type = false;
    bool variadic = false;
    SemanticTypeRef variadic_type;
    bool accepts_named_arguments = true;
};

struct CallableOwner
{
    const FunctionDeclStmt *declaration = nullptr;
    const FunctionExpr *expression = nullptr;
};

class SemanticModel final
{
public:
    [[nodiscard]] const SemanticTypeRef *find_type(std::string_view name) const;
    [[nodiscard]] const SemanticSymbol *find_value(std::string_view name) const;
    [[nodiscard]] const CallableSignature *signature(const FunctionDeclStmt &function) const;
    [[nodiscard]] const CallableSignature *signature(const FunctionExpr &function) const;
    [[nodiscard]] const CallableSignature *constructor(const ClassDeclStmt &klass) const;
    [[nodiscard]] const CallableSignature *signature(std::string_view qualified_name) const;
    [[nodiscard]] const CallableOwner *enclosing_callable(const ReturnStmt &statement) const;
    [[nodiscard]] const SemanticTypeRef *type_of(const Expr &expression) const;

    TypeInterner types;

private:
    friend class SemanticAnalyzer;
    friend struct SemanticAnalysis;
    friend SemanticAnalysis analyze_semantics(const StmtList &,
                                              std::string,
                                              const SemanticImportEnvironment &,
                                              SemanticAnalysisOptions);

    std::unordered_map<std::string, SemanticTypeRef> m_type_symbols;
    std::unordered_map<std::string, SemanticSymbol> m_value_symbols;
    std::unordered_map<const FunctionDeclStmt *, CallableSignature> m_signatures;
    std::unordered_map<const FunctionExpr *, CallableSignature> m_expression_signatures;
    std::unordered_map<const ClassDeclStmt *, CallableSignature> m_constructors;
    std::unordered_map<std::string, CallableSignature> m_named_signatures;
    std::unordered_map<const ReturnStmt *, CallableOwner> m_return_owners;
    std::unordered_map<const Expr *, SemanticTypeRef> m_expression_types;
};

struct SemanticAnalysis
{
    SemanticModel model;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
};

struct SemanticAnalysisOptions
{
    bool consume_last_expression = false;
};

/**
 * Collects module declarations and resolves every declared type annotation.
 *
 * This pass is intentionally independent from execution until semantic
 * analysis becomes mandatory. It resolves callable signatures before bodies,
 * which permits forward type references.
 */
[[nodiscard]] SemanticAnalysis analyze_semantics(const StmtList &statements,
                                                 std::string source_path = {},
                                                 const SemanticImportEnvironment &imports = {},
                                                 SemanticAnalysisOptions options = {});

/**
 * Builds the public type/value manifest consumed by importing modules.
 */
[[nodiscard]] SemanticModuleExports collect_semantic_exports(const StmtList &statements);

} // namespace lumiere
