#include "lumiere/analysis/semantic_analysis.hpp"
#include "lumiere/analysis/native_signatures.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"

#include <map>
#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace lumiere
{
namespace
{

SourceRange source_range(const Token &token)
{
    return {token.start_offset, token.end_offset, token.line, token.column};
}

} // namespace

class SemanticAnalyzer final
{
public:
    SemanticAnalyzer(std::string source_path,
                     const SemanticImportEnvironment &imports,
                     const SemanticAnalysisOptions options)
        : m_source_path(std::move(source_path)),
          m_options(options),
          m_imports(imports)
    {
        for (const char *name : {
                 "Entier", "Logique", "Symbole",
                 "Texte", "Rien", "Universel", "Liste", "ListeFixe",
                 "Dictionnaire", "Ensemble", "Classe", "Interface", "Résultat"})
        {
            m_analysis.model.m_type_symbols.emplace(name, m_analysis.model.types.builtin(name));
        }
        m_analysis.model.m_type_symbols.emplace(
            "Erreur", m_analysis.model.types.interface_type("Erreur"));
        m_analysis.model.m_value_symbols.emplace(
            "Erreur",
            SemanticSymbol{
                SemanticSymbolKind::INTERFACE,
                "Erreur",
                nullptr,
                m_analysis.model.types.interface_type(
                    "Erreur")});
        m_error_types.insert("Erreur");
        const SemanticTypeRef decimal = m_analysis.model.types.builtin("Décimal");
        m_analysis.model.m_type_symbols.emplace("Décimal", decimal);
        m_analysis.model.m_type_symbols.emplace("Decimal", decimal);
        m_analysis.model.m_type_symbols.emplace(
            "ErreurEntrée",
            m_analysis.model.types.class_type("ErreurEntrée"));
        m_error_types.insert("ErreurEntrée");
        for (const NativeCallableSpec &native : core_native_signatures())
        {
            CallableSignature signature;
            signature.has_explicit_return_type = true;
            signature.accepts_named_arguments = false;
            for (const NativeParameterSpec &parameter : native.parameters)
            {
                signature.parameter_names.push_back(parameter.name);
                signature.parameter_types.push_back(
                    resolve_type(parameter.type));
                signature.optional_parameters.push_back(parameter.optional);
            }
            signature.return_type = resolve_type(native.return_type);
            signature.variadic = native.variadic;
            if (native.variadic)
            {
                signature.variadic_type =
                    resolve_type(native.variadic_type);
            }
            m_analysis.model.m_named_signatures.emplace(native.name, std::move(signature));
            m_analysis.model.m_value_symbols.emplace(
                native.name,
                SemanticSymbol{SemanticSymbolKind::FUNCTION, native.name, nullptr, nullptr});
        }
        for (const std::string name : {"Succès", "Échec"})
        {
            CallableSignature signature;
            signature.parameter_names = {"valeur"};
            signature.parameter_types = {*m_analysis.model.find_type("Universel")};
            signature.optional_parameters = {false};
            signature.return_type = m_analysis.model.types.generic(
                "Résultat",
                {m_analysis.model.types.bottom(),
                 m_analysis.model.types.bottom()});
            signature.has_explicit_return_type = true;
            signature.accepts_named_arguments = false;
            m_analysis.model.m_named_signatures.emplace(name, std::move(signature));
            m_analysis.model.m_value_symbols.emplace(
                name, SemanticSymbol{SemanticSymbolKind::FUNCTION, name, nullptr, nullptr});
        }
    }

    SemanticAnalysis analyze(const StmtList &statements)
    {
        if (m_options.consume_last_expression &&
            !statements.empty() &&
            dynamic_cast<const ExprStmt *>(
                statements.back().get()) != nullptr)
        {
            m_consumed_expression_statement =
                statements.back().get();
        }
        collect_module_declarations(statements);
        for (const StmtPtr &statement : statements)
        {
            if (const auto *import = dynamic_cast<const ImportStmt *>(statement.get()))
            {
                resolve_import(*import, true);
            }
        }
        collect_error_types(statements);
        resolve_aliases();
        resolve_constructors(statements);
        collect_module_callable_aliases(statements);
        resolve_signatures_and_annotations(statements);
        diagnose_uncontextualized_result_constructors();
        validate_entry_point(statements);
        return std::move(m_analysis);
    }

private:
    struct LocalBinding
    {
        SemanticSymbolKind kind;
        Token name;
        const Stmt *declaration = nullptr;
        SemanticTypeRef type;
        std::optional<CallableSignature> callable;
        std::optional<std::size_t> obligation;
    };

    struct ResultObligation
    {
        Token origin;
        bool active = true;
    };

    void diagnose(const Token &token, std::string code, std::string message)
    {
        m_analysis.diagnostics.push_back({
            std::move(code),
            DiagnosticSeverity::ERROR_LEVEL,
            std::move(message),
            m_source_path,
            source_range(token),
        });
    }

    void declare_value(const Token &name,
                       const SemanticSymbolKind kind,
                       const Stmt &declaration)
    {
        if (!m_analysis.model.m_value_symbols.emplace(
                name.lexeme,
                SemanticSymbol{kind, name.lexeme, &declaration, nullptr}).second)
        {
            diagnose(name, "LUM-S0001", "le nom '" + name.lexeme + "' est déjà déclaré dans ce module");
        }
    }

    void declare_type(const Token &name,
                      const SemanticTypeKind kind)
    {
        SemanticTypeRef type = kind == SemanticTypeKind::CLASS
                                   ? m_analysis.model.types.class_type(name.lexeme)
                                   : m_analysis.model.types.interface_type(name.lexeme);
        if (!m_analysis.model.m_type_symbols.emplace(name.lexeme, std::move(type)).second)
        {
            diagnose(name, "LUM-S0002", "le type '" + name.lexeme + "' est déjà déclaré");
        }
    }

    void push_scope()
    {
        m_scopes.emplace_back();
        m_type_scopes.emplace_back();
        m_signature_scopes.emplace_back();
    }

    void pop_scope()
    {
        std::unordered_set<std::size_t> checked;
        for (const auto &[name, binding] : m_scopes.back())
        {
            if (binding.obligation.has_value() &&
                m_obligations.at(*binding.obligation).active &&
                checked.insert(*binding.obligation).second &&
                !obligation_has_owner_before_last_scope(
                    *binding.obligation))
            {
                if (m_obligations_diagnosed_on_exit
                        .insert(*binding.obligation)
                        .second)
                {
                    diagnose(m_obligations.at(*binding.obligation).origin,
                             "LUM-S0029",
                             "le résultat confié à '" + name +
                                 "' quitte sa portée sans être utilisé");
                }
                m_obligations.at(*binding.obligation).active = false;
            }
        }
        m_scopes.pop_back();
        m_type_scopes.pop_back();
        m_signature_scopes.pop_back();
    }

    bool obligation_has_other_owner(
        const std::size_t obligation,
        const LocalBinding *excluded) const
    {
        for (const auto &scope : m_scopes)
        {
            for (const auto &[_, binding] : scope)
            {
                if (&binding != excluded &&
                    binding.obligation == obligation)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool obligation_has_owner_before_last_scope(
        const std::size_t obligation) const
    {
        if (m_scopes.size() < 2)
        {
            return false;
        }
        return std::any_of(
            m_scopes.begin(),
            std::prev(m_scopes.end()),
            [&](const auto &scope) {
                return std::any_of(
                    scope.begin(),
                    scope.end(),
                    [&](const auto &entry) {
                        return entry.second.obligation == obligation;
                    });
            });
    }

    SemanticTypeRef imported_type(const std::string &module_name,
                                  const std::string &name,
                                  const SemanticTypeKind kind)
    {
        const std::string qualified_name = module_name + '.' + name;
        return kind == SemanticTypeKind::INTERFACE
                   ? m_analysis.model.types.interface_type(qualified_name)
                   : m_analysis.model.types.class_type(qualified_name);
    }

    SemanticTypeRef resolve_imported_type(const TypeExpr &syntax,
                                          const std::string &module_name,
                                          const SemanticModuleExports &exports)
    {
        if (syntax.empty())
        {
            return m_analysis.model.types.builtin("Rien");
        }
        if (syntax.kind == TypeExprKind::INTEGER_ARGUMENT)
        {
            return m_analysis.model.types.integer_argument(syntax.integer);
        }
        if (syntax.kind == TypeExprKind::NAMED)
        {
            if (const auto alias = exports.resolved_aliases.find(syntax.name);
                alias != exports.resolved_aliases.end())
            {
                return qualify_imported_type(alias->second, module_name);
            }
            if (const auto alias = exports.aliases.find(syntax.name);
                alias != exports.aliases.end())
            {
                return resolve_imported_type(alias->second, module_name, exports);
            }
            if (const auto exported = exports.types.find(syntax.name);
                exported != exports.types.end())
            {
                return imported_type(module_name, syntax.name, exported->second);
            }
            if (const SemanticTypeRef *builtin = m_analysis.model.find_type(syntax.name);
                builtin != nullptr &&
                ((*builtin)->kind() ==
                     SemanticTypeKind::BUILTIN ||
                 (*builtin)->name() == "Erreur"))
            {
                return *builtin;
            }
            return m_analysis.model.types.bottom();
        }

        std::vector<SemanticTypeRef> arguments;
        arguments.reserve(syntax.children.size());
        for (const TypeExpr &argument : syntax.children)
        {
            arguments.push_back(resolve_imported_type(argument, module_name, exports));
        }
        if (syntax.kind == TypeExprKind::UNION)
        {
            return m_analysis.model.types.union_type(
                std::move(arguments));
        }
        SemanticTypeRef resolved =
            m_analysis.model.types.generic(
                syntax.name, std::move(arguments));
        if (syntax.name == "Résultat" &&
            resolved->arguments().size() == 2 &&
            !is_error_type(resolved->arguments()[1]))
        {
            diagnose(
                syntax.source,
                "LUM-S0049",
                "le second paramètre de Résultat doit être un type d'erreur réalisant Erreur");
        }
        return resolved;
    }

    SemanticTypeRef qualify_imported_type(const SemanticTypeRef &type, const std::string &module_name)
    {
        // Type equality is pointer-based: every imported node must be re-interned.
        if (type->kind() == SemanticTypeKind::CLASS || type->kind() == SemanticTypeKind::INTERFACE)
        {
            if (type->name().find('.') != std::string_view::npos || type->name() == "Erreur")
                return type->kind() == SemanticTypeKind::CLASS
                           ? m_analysis.model.types.class_type(std::string(type->name()))
                           : m_analysis.model.types.interface_type(std::string(type->name()));
            return imported_type(module_name, std::string(type->name()), type->kind());
        }
        if (type->kind() == SemanticTypeKind::BUILTIN)
            return m_analysis.model.types.builtin(std::string(type->name()));
        if (type->kind() == SemanticTypeKind::INTEGER_ARGUMENT)
            return m_analysis.model.types.integer_argument(type->integer());
        if (type->kind() == SemanticTypeKind::TYPE_PARAMETER)
            return m_analysis.model.types.type_parameter(std::string(type->name()));
        if (type->kind() == SemanticTypeKind::BOTTOM)
            return m_analysis.model.types.bottom();
        std::vector<SemanticTypeRef> arguments;
        for (const auto &argument : type->arguments())
            arguments.push_back(qualify_imported_type(argument, module_name));
        return type->kind() == SemanticTypeKind::UNION
                   ? m_analysis.model.types.union_type(std::move(arguments))
                   : m_analysis.model.types.generic(std::string(type->name()), std::move(arguments));
    }

    void bind_imported_signature(const std::string &binding,
                                 const SemanticModuleExports::Callable &exported,
                                 const std::string &module_name,
                                 const SemanticModuleExports &exports,
                                 const bool module_level)
    {
        CallableSignature signature;
        signature.parameter_names = exported.parameter_names;
        signature.optional_parameters = exported.optional_parameters;
        signature.has_explicit_return_type = exported.has_explicit_return_type;
        for (const auto &parameter : exported.resolved_parameter_types)
            signature.parameter_types.push_back(qualify_imported_type(parameter, module_name));
        for (const TypeExpr &parameter : exported.parameter_types)
        {
            signature.parameter_types.push_back(
                resolve_imported_type(parameter, module_name, exports));
        }
        signature.return_type =
            resolve_imported_type(exported.return_type, module_name, exports);
        if (module_level)
        {
            m_analysis.model.m_named_signatures.emplace(binding, std::move(signature));
        }
        else
        {
            m_signature_scopes.back().emplace(binding, std::move(signature));
        }
    }

    void bind_imported_type(const Token &site,
                            const std::string &binding,
                            SemanticTypeRef type,
                            const bool module_level)
    {
        auto &types = module_level
                          ? m_analysis.model.m_type_symbols
                          : m_type_scopes.back();
        if (!types.emplace(binding, std::move(type)).second)
        {
            diagnose(site, "LUM-S0002", "le type '" + binding + "' est déjà déclaré");
        }
    }

    void bind_imported_value(const Token &site,
                             const std::string &binding,
                             const SemanticSymbolKind kind,
                             const ImportStmt &import,
                             const bool module_level,
                             SemanticTypeRef type = nullptr)
    {
        if (module_level)
        {
            if (!m_analysis.model.m_value_symbols.emplace(
                    binding, SemanticSymbol{kind, binding, &import, std::move(type)}).second)
            {
                diagnose(site, "LUM-S0001",
                         "le nom '" + binding + "' est déjà déclaré dans ce module");
            }
            return;
        }
        Token binding_token = site;
        binding_token.type = TokenType::IDENT;
        binding_token.lexeme = binding;
        declare_local(binding_token, kind);
        if (type != nullptr)
        {
            set_local_type(binding, std::move(type));
        }
    }

    static std::string default_module_alias(const std::string &module_name)
    {
        const std::size_t dot = module_name.rfind('.');
        return dot == std::string::npos ? module_name : module_name.substr(dot + 1);
    }

    void resolve_import(const ImportStmt &import, const bool module_level)
    {
        const auto module = m_imports.find(import.module_name.lexeme);
        if (module == m_imports.end())
        {
            diagnose(import.module_name, "LUM-S0012",
                     "module introuvable: '" + import.module_name.lexeme + "'");
            return;
        }
        for (const std::string &name : module->second.error_types)
        {
            m_error_types.insert(import.module_name.lexeme + "." + name);
        }

        if (import.imported_members.empty())
        {
            const Token &site = import.alias.lexeme.empty() ? import.module_name : import.alias;
            const std::string alias = import.alias.lexeme.empty()
                                          ? default_module_alias(import.module_name.lexeme)
                                          : import.alias.lexeme;
            bind_imported_value(site, alias, SemanticSymbolKind::MODULE, import, module_level);
            for (const auto &[name, kind] : module->second.types)
            {
                bind_imported_type(
                    site,
                    alias + '.' + name,
                    imported_type(import.module_name.lexeme, name, kind),
                    module_level);
            }
            for (const auto &[name, target] : module->second.aliases)
            {
                const auto resolved = module->second.resolved_aliases.find(name);
                bind_imported_type(
                    site,
                    alias + '.' + name,
                    resolved == module->second.resolved_aliases.end()
                        ? resolve_imported_type(target, import.module_name.lexeme,
                                                module->second)
                        : qualify_imported_type(resolved->second,
                                                import.module_name.lexeme),
                    module_level);
            }
            for (const auto &[name, callable] : module->second.callables)
            {
                bind_imported_signature(
                    alias + '.' + name,
                    callable,
                    import.module_name.lexeme,
                    module->second,
                    module_level);
            }
            return;
        }

        for (const ImportStmt::ImportedMember &member : import.imported_members)
        {
            const Token &site = member.alias.lexeme.empty() ? member.name : member.alias;
            const std::string binding = member.alias.lexeme.empty()
                                            ? member.name.lexeme
                                            : member.alias.lexeme;
            const auto type = module->second.types.find(member.name.lexeme);
            const auto alias = module->second.aliases.find(member.name.lexeme);
            const auto value = module->second.values.find(member.name.lexeme);
            if (type == module->second.types.end() &&
                alias == module->second.aliases.end() &&
                value == module->second.values.end())
            {
                diagnose(member.name, "LUM-S0013",
                         "membre non exporté ou introuvable: '" + member.name.lexeme + "'");
                continue;
            }
            if (type != module->second.types.end())
            {
                bind_imported_type(
                    site,
                    binding,
                    imported_type(import.module_name.lexeme, member.name.lexeme, type->second),
                    module_level);
            }
            if (alias != module->second.aliases.end())
            {
                const auto resolved =
                    module->second.resolved_aliases.find(member.name.lexeme);
                bind_imported_type(
                    site,
                    binding,
                    resolved == module->second.resolved_aliases.end()
                        ? resolve_imported_type(alias->second,
                                                import.module_name.lexeme,
                                                module->second)
                        : qualify_imported_type(resolved->second,
                                                import.module_name.lexeme),
                    module_level);
            }
            if (value != module->second.values.end())
            {
                SemanticTypeRef value_type;
                if (const auto syntax =
                        module->second.value_types.find(member.name.lexeme);
                    syntax != module->second.value_types.end())
                {
                    value_type = resolve_imported_type(
                        syntax->second, import.module_name.lexeme,
                        module->second);
                }
                bind_imported_value(site, binding, value->second, import,
                                    module_level, std::move(value_type));
            }
            if (const auto callable = module->second.callables.find(member.name.lexeme);
                callable != module->second.callables.end())
            {
                bind_imported_signature(
                    binding,
                    callable->second,
                    import.module_name.lexeme,
                    module->second,
                    module_level);
            }
        }
    }

    const SemanticTypeRef *find_type(const std::string &name) const
    {
        for (auto scope = m_type_scopes.rbegin(); scope != m_type_scopes.rend(); ++scope)
        {
            if (const auto found = scope->find(name); found != scope->end())
            {
                return &found->second;
            }
        }
        return m_analysis.model.find_type(name);
    }

    void declare_local(const Token &name,
                       const SemanticSymbolKind kind,
                       const Stmt *declaration = nullptr)
    {
        if (m_scopes.empty())
        {
            return;
        }
        if (const auto existing = m_analysis.model.m_value_symbols.find(name.lexeme);
            existing != m_analysis.model.m_value_symbols.end() &&
            dynamic_cast<const ImportStmt *>(existing->second.declaration) != nullptr)
        {
            diagnose(name, "LUM-S0044",
                     "le nom '" + name.lexeme + "' provient d'une importation et ne peut pas être redéclaré");
        }
        if (!m_scopes.back().emplace(
                name.lexeme,
                LocalBinding{kind, name, declaration, nullptr, std::nullopt,
                             std::nullopt}).second)
        {
            diagnose(name, "LUM-S0007",
                     "le nom '" + name.lexeme + "' est déjà déclaré dans cette portée");
        }
    }

    void set_local_type(const std::string &name, SemanticTypeRef type)
    {
        if (!m_scopes.empty())
        {
            if (const auto found = m_scopes.back().find(name);
                found != m_scopes.back().end())
            {
                found->second.type = std::move(type);
            }
        }
    }

    LocalBinding *find_local_value_mutable(const std::string &name)
    {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope)
        {
            if (const auto found = scope->find(name); found != scope->end())
            {
                return &found->second;
            }
        }
        return nullptr;
    }

    static bool is_result_type(const SemanticTypeRef &type)
    {
        return type != nullptr &&
               type->kind() == SemanticTypeKind::GENERIC &&
               type->name() == "Résultat" &&
               type->arguments().size() == 2;
    }

    bool is_error_type(const SemanticTypeRef &type) const
    {
        if (type == nullptr ||
            type->kind() == SemanticTypeKind::BOTTOM)
        {
            return true;
        }
        if (type->kind() == SemanticTypeKind::UNION)
        {
            return !type->arguments().empty() &&
                   std::all_of(
                       type->arguments().begin(),
                       type->arguments().end(),
                       [&](const SemanticTypeRef &alternative) {
                           return is_error_type(alternative);
                       });
        }
        if ((type->kind() != SemanticTypeKind::CLASS &&
             type->kind() != SemanticTypeKind::INTERFACE) ||
            type->name().empty())
        {
            return false;
        }
        if (m_error_types.contains(std::string(type->name())))
        {
            return true;
        }
        const ClassDeclStmt *klass =
            class_declaration(type);
        std::unordered_set<const ClassDeclStmt *> visited;
        while (klass != nullptr &&
               visited.insert(klass).second &&
               !klass->parent.empty())
        {
            const SemanticTypeRef *parent =
                find_type(klass->parent.name);
            if (parent != nullptr &&
                m_error_types.contains(
                    std::string((*parent)->name())))
            {
                return true;
            }
            klass = parent == nullptr
                        ? nullptr
                        : class_declaration(*parent);
        }
        return false;
    }

    const CallableSignature *current_callable_signature()
    {
        if (m_callable_stack.empty())
        {
            return nullptr;
        }
        const CallableOwner &owner = m_callable_stack.back();
        return owner.declaration != nullptr
                   ? &ensure_signature(*owner.declaration)
                   : m_analysis.model.signature(*owner.expression);
    }

    void validate_propagation(const SemanticTypeRef &error_type,
                              const Token &site)
    {
        const CallableSignature *boundary = current_callable_signature();
        if (boundary == nullptr ||
            !boundary->has_explicit_return_type ||
            !is_result_type(boundary->return_type))
        {
            diagnose(
                site,
                "LUM-S0045",
                "propager exige que la fonction englobante déclare explicitement un retour Résultat[U,F]");
            return;
        }
        const SemanticTypeRef accepted_error =
            boundary->return_type->arguments()[1];
        if (!is_assignable(error_type, accepted_error))
        {
            diagnose(
                site,
                "LUM-S0046",
                "l'erreur " + std::string(error_type->display()) +
                    " ne peut pas être propagée vers " +
                    std::string(accepted_error->display()));
        }
        diagnose_active_obligations_on_possible_exit(site);
    }

    static bool is_union_containing_result(
        const SemanticTypeRef &type)
    {
        return type != nullptr &&
               type->kind() == SemanticTypeKind::UNION &&
               std::any_of(
                   type->arguments().begin(),
                   type->arguments().end(),
                   [](const SemanticTypeRef &alternative) {
                       return is_result_type(alternative);
                   });
    }

    static bool contains_incomplete_result(
        const SemanticTypeRef &type)
    {
        if (type == nullptr)
        {
            return false;
        }
        if (is_result_type(type) &&
            std::any_of(
                type->arguments().begin(),
                type->arguments().end(),
                [](const SemanticTypeRef &argument) {
                    return argument->kind() ==
                           SemanticTypeKind::BOTTOM;
                }))
        {
            return true;
        }
        return std::any_of(
            type->arguments().begin(),
            type->arguments().end(),
            [](const SemanticTypeRef &argument) {
                return contains_incomplete_result(argument);
            });
    }

    static const CallExpr *result_constructor_call(
        const Expr &expression)
    {
        const auto *call =
            dynamic_cast<const CallExpr *>(&expression);
        if (call == nullptr || call->args.size() != 1)
        {
            return nullptr;
        }
        const auto *identifier =
            dynamic_cast<const IdentifierExpr *>(
                call->callee.get());
        return identifier != nullptr &&
                       (identifier->name.lexeme == "Succès" ||
                        identifier->name.lexeme == "Échec")
                   ? call
                   : nullptr;
    }

    void contextualize_result_construction(
        const Expr &expression,
        const SemanticTypeRef &expected,
        const Token &site)
    {
        const CallExpr *call =
            result_constructor_call(expression);
        if (call == nullptr)
        {
            if (const auto *list =
                    dynamic_cast<const ListExpr *>(&expression);
                list != nullptr &&
                expected != nullptr &&
                expected->kind() == SemanticTypeKind::GENERIC &&
                (expected->name() == "Liste" ||
                 expected->name() == "ListeFixe") &&
                !expected->arguments().empty())
            {
                for (const ExprPtr &element : list->elements)
                {
                    contextualize_result_construction(
                        *element,
                        expected->arguments()[0],
                        site);
                }
                m_analysis.model.m_expression_types[
                    &expression] = expected;
            }
            else if (const auto *dictionary =
                         dynamic_cast<const DictionaryExpr *>(
                             &expression);
                     dictionary != nullptr &&
                     expected != nullptr &&
                     expected->kind() ==
                         SemanticTypeKind::GENERIC &&
                     expected->name() == "Dictionnaire" &&
                     expected->arguments().size() == 2)
            {
                for (const DictionaryEntryExpr &entry :
                     dictionary->entries)
                {
                    contextualize_result_construction(
                        *entry.key,
                        expected->arguments()[0],
                        site);
                    contextualize_result_construction(
                        *entry.value,
                        expected->arguments()[1],
                        site);
                }
                m_analysis.model.m_expression_types[
                    &expression] = expected;
            }
            else if (const auto *match =
                         dynamic_cast<const AgirSelonStmt *>(
                             &expression))
            {
                for (const AgirSelonBranch &branch :
                     match->branches)
                {
                    if (const auto *body =
                            dynamic_cast<const ExprStmt *>(
                                branch.body.get()))
                    {
                        contextualize_result_construction(
                            *body->expr, expected, site);
                    }
                }
                if (const auto *body =
                        dynamic_cast<const ExprStmt *>(
                            match->else_branch.get()))
                {
                    contextualize_result_construction(
                        *body->expr, expected, site);
                }
                m_analysis.model.m_expression_types[
                    &expression] = expected;
            }
            return;
        }
        const auto &identifier =
            static_cast<const IdentifierExpr &>(*call->callee);
        if (!is_result_type(expected))
        {
            return;
        }

        const std::size_t parameter =
            identifier.name.lexeme == "Succès" ? 0 : 1;
        contextualize_result_construction(
            *call->args.front().value,
            expected->arguments()[parameter],
            call->paren);
        const SemanticTypeRef &payload_type =
            m_analysis.model.m_expression_types.at(
                call->args.front().value.get());
        require_assignable(
            payload_type,
            expected->arguments()[parameter],
            call->paren,
            identifier.name.lexeme == "Succès"
                ? "la valeur de succès"
                : "la valeur d'échec");
        m_analysis.model.m_expression_types[&expression] = expected;
    }

    void diagnose_uncontextualized_result_constructors()
    {
        for (const auto &[expression, type] :
             m_analysis.model.m_expression_types)
        {
            const CallExpr *call =
                result_constructor_call(*expression);
            if (call == nullptr ||
                !contains_incomplete_result(type))
            {
                continue;
            }
            const auto &identifier =
                static_cast<const IdentifierExpr &>(
                    *call->callee);
            diagnose(
                call->paren,
                "LUM-S0050",
                identifier.name.lexeme +
                    " ne peut être construit que dans un contexte attendant Résultat[T,E]");
        }
    }

    void create_obligation(LocalBinding &binding, const Token &origin)
    {
        const std::size_t id = m_next_obligation++;
        m_obligations.emplace(id, ResultObligation{origin, true});
        binding.obligation = id;
    }

    void consume_obligation(const std::string &name)
    {
        if (LocalBinding *binding = find_local_value_mutable(name);
            binding != nullptr && binding->obligation.has_value())
        {
            m_obligations.at(*binding->obligation).active = false;
        }
    }

    using ObligationState = std::unordered_map<std::size_t, bool>;

    ObligationState obligation_state() const
    {
        ObligationState state;
        for (const auto &[id, obligation] : m_obligations)
        {
            state.emplace(id, obligation.active);
        }
        return state;
    }

    void restore_obligation_state(const ObligationState &state)
    {
        for (const auto &[id, active] : state)
        {
            m_obligations.at(id).active = active;
        }
    }

    void merge_obligation_states(const ObligationState &baseline,
                                 const std::vector<ObligationState> &paths)
    {
        for (const auto &[id, active] : baseline)
        {
            bool active_on_any_path = false;
            for (const ObligationState &path : paths)
            {
                const auto found = path.find(id);
                active_on_any_path |= found == path.end() ? active : found->second;
            }
            m_obligations.at(id).active = active_on_any_path;
        }
    }

    void diagnose_active_obligations(const Token &exit)
    {
        for (const auto &scope : m_scopes)
        {
            for (const auto &[name, binding] : scope)
            {
                if (!binding.obligation.has_value())
                {
                    continue;
                }
                ResultObligation &obligation =
                    m_obligations.at(*binding.obligation);
                if (obligation.active)
                {
                    if (m_obligations_diagnosed_on_exit
                            .insert(*binding.obligation)
                            .second)
                    {
                        diagnose(exit, "LUM-S0029",
                                 "le résultat confié à '" + name +
                                     "' n'est pas utilisé avant cette sortie");
                    }
                    obligation.active = false;
                }
            }
        }
    }

    void diagnose_active_obligations_on_possible_exit(
        const Token &exit)
    {
        for (const auto &scope : m_scopes)
        {
            for (const auto &[name, binding] : scope)
            {
                if (!binding.obligation.has_value() ||
                    !m_obligations.at(
                         *binding.obligation).active ||
                    !m_obligations_diagnosed_on_exit
                         .insert(*binding.obligation)
                         .second)
                {
                    continue;
                }
                diagnose(
                    exit,
                    "LUM-S0029",
                    "le résultat confié à '" + name +
                        "' resterait inutilisé si cette propagation quittait la fonction");
            }
        }
    }

    void diagnose_new_loop_obligations(
        const Token &exit)
    {
        if (m_loop_obligation_baselines.empty())
        {
            return;
        }
        const ObligationState &baseline =
            m_loop_obligation_baselines.back();
        for (auto &[id, obligation] : m_obligations)
        {
            const auto initial = baseline.find(id);
            const bool initially_active =
                initial != baseline.end() && initial->second;
            if (obligation.active && !initially_active)
            {
                diagnose(
                    exit,
                    "LUM-S0039",
                    "un résultat produit dans cette itération doit être utilisé avant 'continuer'");
                obligation.active = false;
            }
        }
    }

    void collect_module_declarations(const StmtList &statements)
    {
        for (const StmtPtr &statement : statements)
        {
            if (const auto *variable = dynamic_cast<const VarDeclStmt *>(statement.get()))
            {
                declare_value(variable->name, SemanticSymbolKind::VARIABLE, *variable);
            }
            else if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(statement.get()))
            {
                declare_value(function->name, SemanticSymbolKind::FUNCTION, *function);
            }
            else if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(statement.get()))
            {
                declare_type(klass->name, SemanticTypeKind::CLASS);
                declare_value(klass->name, SemanticSymbolKind::CLASS, *klass);
                if (std::any_of(
                        klass->interfaces.begin(), klass->interfaces.end(),
                        [](const TypeExpr &interface) {
                            return interface.kind == TypeExprKind::NAMED &&
                                   interface.name == "Erreur";
                        }))
                {
                    m_error_types.insert(klass->name.lexeme);
                }
                if (!klass->is_public)
                {
                    m_private_type_names.insert(klass->name.lexeme);
                }
            }
            else if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(statement.get()))
            {
                declare_type(interface->name, SemanticTypeKind::INTERFACE);
                declare_value(interface->name, SemanticSymbolKind::INTERFACE, *interface);
                if (!interface->is_public)
                {
                    m_private_type_names.insert(interface->name.lexeme);
                }
            }
            else if (const auto *alias = dynamic_cast<const TypeAliasDeclStmt *>(statement.get()))
            {
                if (m_analysis.model.m_type_symbols.contains(alias->name.lexeme) ||
                    !m_aliases.emplace(alias->name.lexeme, alias).second)
                {
                    diagnose(alias->name, "LUM-S0002",
                             "le type '" + alias->name.lexeme + "' est déjà déclaré");
                }
                else
                {
                    m_alias_order.push_back(alias->name.lexeme);
                }
            }
        }
    }

    SemanticTypeRef resolve_alias(const std::string &name, const Token &site)
    {
        if (const auto complete = m_resolved_aliases.find(name);
            complete != m_resolved_aliases.end())
        {
            return complete->second;
        }
        const auto declaration = m_aliases.find(name);
        if (declaration == m_aliases.end())
        {
            return nullptr;
        }
        const auto active = std::find(m_alias_stack.begin(), m_alias_stack.end(), name);
        if (active != m_alias_stack.end())
        {
            std::string cycle;
            for (auto item = active; item != m_alias_stack.end(); ++item)
            {
                if (!cycle.empty())
                {
                    cycle += " -> ";
                }
                cycle += *item;
            }
            cycle += " -> " + name;
            diagnose(site, "LUM-S0020", "cycle d'alias de type: " + cycle);
            return m_analysis.model.types.bottom();
        }

        m_alias_stack.push_back(name);
        SemanticTypeRef resolved = resolve_type(declaration->second->target);
        m_alias_stack.pop_back();
        m_resolved_aliases.emplace(name, resolved);
        m_analysis.model.m_type_symbols.emplace(name, resolved);
        return resolved;
    }

    void resolve_aliases()
    {
        for (const std::string &name : m_alias_order)
        {
            const TypeAliasDeclStmt *declaration = m_aliases.at(name);
            const SemanticTypeRef resolved =
                resolve_alias(name, declaration->name);
            if (declaration->is_public &&
                exposes_private_type(resolved))
            {
                diagnose(
                    declaration->name,
                    "LUM-S0038",
                    "l'alias public '" + name +
                        "' expose un type privé");
            }
        }
    }

    void collect_error_types(const StmtList &statements)
    {
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const StmtPtr &statement : statements)
            {
                const auto *klass =
                    dynamic_cast<const ClassDeclStmt *>(
                        statement.get());
                if (klass == nullptr ||
                    m_error_types.contains(
                        klass->name.lexeme))
                {
                    continue;
                }
                const bool realizes_error =
                    std::any_of(
                        klass->interfaces.begin(),
                        klass->interfaces.end(),
                        [&](const TypeExpr &interface) {
                            const SemanticTypeRef resolved =
                                resolve_type(interface);
                            return resolved->kind() ==
                                       SemanticTypeKind::INTERFACE &&
                                   resolved->name() == "Erreur";
                        });
                bool derives_from_error = false;
                if (!klass->parent.empty())
                {
                    const SemanticTypeRef parent =
                        resolve_type(klass->parent);
                    derives_from_error =
                        m_error_types.contains(
                            std::string(parent->name()));
                }
                if (realizes_error ||
                    derives_from_error)
                {
                    m_error_types.insert(
                        klass->name.lexeme);
                    changed = true;
                }
            }
        }
    }

    bool exposes_private_type(const SemanticTypeRef &type) const
    {
        if (type == nullptr)
        {
            return false;
        }
        if ((type->kind() == SemanticTypeKind::CLASS ||
             type->kind() == SemanticTypeKind::INTERFACE) &&
            m_private_type_names.contains(std::string(type->name())))
        {
            return true;
        }
        return std::any_of(
            type->arguments().begin(),
            type->arguments().end(),
            [&](const SemanticTypeRef &argument) {
                return exposes_private_type(argument);
            });
    }

    SemanticTypeRef resolve_type(const TypeExpr &syntax)
    {
        if (syntax.empty())
        {
            return m_analysis.model.types.builtin("Rien");
        }
        if (syntax.kind == TypeExprKind::INTEGER_ARGUMENT)
        {
            return m_analysis.model.types.integer_argument(syntax.integer);
        }
        if (syntax.kind == TypeExprKind::NAMED)
        {
            if (SemanticTypeRef alias = resolve_alias(syntax.name, syntax.source))
            {
                return alias;
            }
            if (const SemanticTypeRef *type = find_type(syntax.name))
            {
                return *type;
            }
            diagnose(syntax.source, "LUM-S0003", "type inconnu: '" + syntax.name + "'");
            return m_analysis.model.types.bottom();
        }
        if (syntax.kind == TypeExprKind::UNION)
        {
            std::vector<SemanticTypeRef> alternatives;
            alternatives.reserve(syntax.children.size());
            for (const TypeExpr &alternative : syntax.children)
            {
                alternatives.push_back(resolve_type(alternative));
            }
            return m_analysis.model.types.union_type(std::move(alternatives));
        }

        std::vector<SemanticTypeRef> arguments;
        arguments.reserve(syntax.children.size());
        for (const TypeExpr &argument : syntax.children)
        {
            arguments.push_back(resolve_type(argument));
        }

        const SemanticTypeRef *constructor = find_type(syntax.name);
        if (m_aliases.contains(syntax.name) || m_resolved_aliases.contains(syntax.name))
        {
            diagnose(syntax.source, "LUM-S0021",
                     "un alias concret n'accepte pas d'arguments génériques");
            return m_analysis.model.types.bottom();
        }
        if (constructor == nullptr)
        {
            diagnose(syntax.source, "LUM-S0003", "type générique inconnu: '" + syntax.name + "'");
            return m_analysis.model.types.bottom();
        }

        const std::size_t expected_arity =
            syntax.name == "Liste" || syntax.name == "Ensemble" ? 1 :
            syntax.name == "ListeFixe" || syntax.name == "Dictionnaire" ||
                    syntax.name == "Résultat" ? 2 : 0;
        if (expected_arity == 0)
        {
            diagnose(syntax.source, "LUM-S0004",
                     "le type '" + syntax.name + "' n'accepte pas d'arguments");
            return m_analysis.model.types.bottom();
        }
        if (arguments.size() != expected_arity)
        {
            diagnose(syntax.source, "LUM-S0005",
                     "le type '" + syntax.name + "' attend " +
                         std::to_string(expected_arity) + " argument(s)");
            return m_analysis.model.types.bottom();
        }
        if (syntax.name == "ListeFixe")
        {
            if (syntax.children[0].kind == TypeExprKind::INTEGER_ARGUMENT ||
                syntax.children[1].kind != TypeExprKind::INTEGER_ARGUMENT)
            {
                diagnose(syntax.source, "LUM-S0006",
                         "ListeFixe attend un type puis une taille entière");
                return m_analysis.model.types.bottom();
            }
        }
        else if (std::any_of(syntax.children.begin(), syntax.children.end(), [](const TypeExpr &argument) {
                     return argument.kind == TypeExprKind::INTEGER_ARGUMENT;
                 }))
        {
            diagnose(syntax.source, "LUM-S0006",
                     "un entier n'est pas permis comme argument de '" + syntax.name + "'");
            return m_analysis.model.types.bottom();
        }
        SemanticTypeRef resolved =
            m_analysis.model.types.generic(syntax.name, std::move(arguments));
        if (syntax.name == "Résultat" &&
            !is_error_type(resolved->arguments()[1]))
        {
            diagnose(
                syntax.children[1].source,
                "LUM-S0049",
                "le second paramètre de Résultat doit être un type d'erreur réalisant Erreur");
        }
        return resolved;
    }

    const CallableSignature &ensure_signature(const FunctionDeclStmt &function)
    {
        if (const auto found = m_analysis.model.m_signatures.find(&function);
            found != m_analysis.model.m_signatures.end())
        {
            return found->second;
        }
        CallableSignature signature;
        signature.parameter_types.reserve(function.params.size());
        for (const Parameter &parameter : function.params)
        {
            signature.parameter_names.push_back(parameter.name);
            signature.parameter_types.push_back(resolve_type(parameter.type));
            signature.optional_parameters.push_back(parameter.default_value != nullptr);
        }
        signature.has_explicit_return_type = !function.return_type.empty();
        signature.return_type = resolve_type(function.return_type);
        return m_analysis.model.m_signatures.emplace(&function, std::move(signature)).first->second;
    }

    const CallableSignature &ensure_signature(const FunctionExpr &function)
    {
        if (const auto found =
                m_analysis.model.m_expression_signatures.find(&function);
            found != m_analysis.model.m_expression_signatures.end())
        {
            return found->second;
        }
        CallableSignature signature;
        signature.parameter_types.reserve(function.params.size());
        for (const Parameter &parameter : function.params)
        {
            signature.parameter_names.push_back(parameter.name);
            signature.parameter_types.push_back(resolve_type(parameter.type));
            signature.optional_parameters.push_back(
                parameter.default_value != nullptr);
        }
        signature.has_explicit_return_type = !function.return_type.empty();
        signature.return_type = resolve_type(function.return_type);
        return m_analysis.model.m_expression_signatures
            .emplace(&function, std::move(signature))
            .first->second;
    }

    void resolve_function(const FunctionDeclStmt &function)
    {
        const CallableSignature &resolved_signature = ensure_signature(function);
        if (is_union_containing_result(
                resolved_signature.return_type))
        {
            diagnose(
                function.name,
                "LUM-S0036",
                "Résultat ne peut pas être une alternative du type de retour; placez l'union dans son type de succès ou d'erreur");
        }
        // A method is a function declared directly in the class being resolved.
        // Nothing else counts: a closure written inside a method is not one, and
        // 'parent' does not work there at run time either.
        const bool is_method =
            !m_class_stack.empty() &&
            std::any_of(m_class_stack.back()->members.begin(),
                        m_class_stack.back()->members.end(),
                        [&function](const StmtPtr &member) { return member.get() == &function; });
        const std::size_t enclosing_loops = m_loop_depth;
        const std::size_t enclosing_methods = m_method_depth;
        m_loop_depth = 0;
        m_method_depth = is_method ? 1 : 0;
        m_callable_stack.push_back(CallableOwner{&function, nullptr});
        push_scope();
        for (std::size_t i = 0; i < function.params.size(); ++i)
        {
            const Parameter &parameter = function.params[i];
            declare_local(parameter.name_token, SemanticSymbolKind::PARAMETER);
            set_local_type(parameter.name, resolved_signature.parameter_types[i]);
            if (is_result_type(resolved_signature.parameter_types[i]))
            {
                create_obligation(*find_local_value_mutable(parameter.name),
                                  parameter.name_token);
            }
            if (parameter.default_value != nullptr)
            {
                resolve_expression(*parameter.default_value);
                contextualize_result_construction(
                    *parameter.default_value,
                    resolved_signature.parameter_types[i],
                    parameter.name_token);
            }
        }
        if (function.body != nullptr)
        {
            if (const auto *body =
                    dynamic_cast<const BlockStmt *>(
                        function.body.get()))
            {
                resolve_block(*body, false);
            }
            else
            {
                resolve_statement(*function.body);
            }
        }
        if (resolved_signature.has_explicit_return_type &&
            is_result_type(resolved_signature.return_type) &&
            function.body != nullptr &&
            !statement_always_returns(*function.body))
        {
            diagnose(function.name, "LUM-S0033",
                     "tous les chemins d'une fonction Résultat doivent retourner explicitement un résultat");
        }
        pop_scope();
        m_callable_stack.pop_back();
        m_loop_depth = enclosing_loops;
        m_method_depth = enclosing_methods;
    }

    void resolve_function(const FunctionExpr &function)
    {
        const CallableSignature &resolved_signature =
            ensure_signature(function);
        if (is_union_containing_result(
                resolved_signature.return_type))
        {
            diagnose(
                function.keyword,
                "LUM-S0036",
                "Résultat ne peut pas être une alternative du type de retour; placez l'union dans son type de succès ou d'erreur");
        }

        const std::size_t enclosing_loops = m_loop_depth;
        const std::size_t enclosing_methods = m_method_depth;
        m_loop_depth = 0;
        m_method_depth = 0;
        m_callable_stack.push_back(CallableOwner{nullptr, &function});
        push_scope();
        for (std::size_t i = 0; i < function.params.size(); ++i)
        {
            const Parameter &parameter = function.params[i];
            declare_local(parameter.name_token, SemanticSymbolKind::PARAMETER);
            set_local_type(parameter.name, resolved_signature.parameter_types[i]);
            if (is_result_type(resolved_signature.parameter_types[i]))
            {
                create_obligation(*find_local_value_mutable(parameter.name),
                                  parameter.name_token);
            }
        }
        for (const Parameter &parameter : function.params)
        {
            if (parameter.default_value != nullptr)
            {
                resolve_expression(*parameter.default_value);
                const std::size_t index =
                    static_cast<std::size_t>(
                        &parameter - function.params.data());
                contextualize_result_construction(
                    *parameter.default_value,
                    resolved_signature.parameter_types[index],
                    parameter.name_token);
            }
        }
        if (function.body != nullptr)
        {
            if (const auto *body = dynamic_cast<const BlockStmt *>(function.body.get()))
            {
                resolve_block(*body, false);
            }
            else
            {
                resolve_statement(*function.body);
            }
        }
        if (resolved_signature.has_explicit_return_type &&
            is_result_type(resolved_signature.return_type) &&
            function.body != nullptr &&
            !statement_always_returns(*function.body))
        {
            diagnose(function.keyword, "LUM-S0033",
                     "tous les chemins d'une fonction Résultat doivent retourner explicitement un résultat");
        }
        pop_scope();
        m_callable_stack.pop_back();
        m_loop_depth = enclosing_loops;
        m_method_depth = enclosing_methods;
    }

    void collect_module_callable_aliases(const StmtList &statements)
    {
        for (const StmtPtr &statement : statements)
        {
            if (const auto *function =
                    dynamic_cast<const FunctionDeclStmt *>(statement.get()))
            {
                static_cast<void>(ensure_signature(*function));
            }
        }

        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const StmtPtr &statement : statements)
            {
                const auto *variable =
                    dynamic_cast<const VarDeclStmt *>(statement.get());
                if (variable == nullptr ||
                    variable->initializer == nullptr ||
                    m_analysis.model.m_named_signatures.contains(
                        variable->name.lexeme))
                {
                    continue;
                }
                if (const CallableSignature *callable =
                        callable_signature(*variable->initializer))
                {
                    m_analysis.model.m_named_signatures.emplace(
                        variable->name.lexeme,
                        *callable);
                    changed = true;
                }
            }
        }
    }

    const LocalBinding *find_local_value(const std::string &name) const
    {
        for (auto scope = m_scopes.rbegin(); scope != m_scopes.rend(); ++scope)
        {
            if (const auto found = scope->find(name); found != scope->end())
            {
                return &found->second;
            }
        }
        return nullptr;
    }

    static bool statement_always_returns(const Stmt &statement)
    {
        if (dynamic_cast<const ReturnStmt *>(&statement) != nullptr)
        {
            return true;
        }
        if (const auto *block = dynamic_cast<const BlockStmt *>(&statement))
        {
            return std::any_of(
                block->statements.begin(), block->statements.end(),
                [](const StmtPtr &child) {
                    return statement_always_returns(*child);
                });
        }
        if (const auto *conditional = dynamic_cast<const IfStmt *>(&statement))
        {
            return conditional->else_branch != nullptr &&
                   statement_always_returns(*conditional->then_branch) &&
                   statement_always_returns(*conditional->else_branch);
        }
        if (const auto *expression = dynamic_cast<const ExprStmt *>(&statement))
        {
            if (const auto *match =
                    dynamic_cast<const AgirSelonStmt *>(expression->expr.get()))
            {
                return match->else_branch != nullptr &&
                       std::all_of(
                           match->branches.begin(), match->branches.end(),
                           [](const AgirSelonBranch &branch) {
                               return statement_always_returns(*branch.body);
                           }) &&
                       statement_always_returns(*match->else_branch);
            }
        }
        return false;
    }

    const CallableSignature *find_named_signature(const std::string &name) const
    {
        for (auto scope = m_signature_scopes.rbegin();
             scope != m_signature_scopes.rend();
             ++scope)
        {
            if (const auto found = scope->find(name); found != scope->end())
            {
                return &found->second;
            }
        }
        return m_analysis.model.signature(name);
    }

    const CallableSignature *text_member_signature(const std::string &name)
    {
        const std::string key = "Texte." + name;
        if (const auto cached = m_builtin_member_signatures.find(key);
            cached != m_builtin_member_signatures.end())
            return &cached->second;

        // Reuse the module contract, omitting its explicit text receiver.
        // This works without an import and keeps methods out of module scope.
        static const auto exports = native_module_exports("Texte");
        if (!exports)
            return nullptr;
        const auto found = exports->callables.find(name);
        if (found == exports->callables.end() || found->second.parameter_types.empty() ||
            found->second.parameter_types.front().name != "Texte")
            return nullptr;

        for (const auto &error : exports->error_types)
            m_error_types.insert("Texte." + error);
        const auto &method = found->second;
        CallableSignature signature;
        signature.has_explicit_return_type = method.has_explicit_return_type;
        signature.accepts_named_arguments = false;
        for (std::size_t i = 1; i < method.parameter_types.size(); ++i)
        {
            signature.parameter_names.push_back(method.parameter_names[i]);
            signature.parameter_types.push_back(resolve_imported_type(method.parameter_types[i], "Texte", *exports));
            signature.optional_parameters.push_back(method.optional_parameters[i]);
        }
        signature.return_type = resolve_imported_type(method.return_type, "Texte", *exports);
        return &m_builtin_member_signatures.emplace(key, std::move(signature)).first->second;
    }

    /**
     * @brief Signature of a member call on a builtin collection.
     *
     * The runtime already carries these result types — `Dictionnaire.clés()` hands
     * back a `Liste[K]` and `Liste.en_ensemble()` an `Ensemble[T]` — but the
     * analyzer knew only `taille`, so every other member call was `Universel` and
     * could not initialize a declared collection type. Even the idiom the overview
     * documents, `soit t: ListeFixe[Entier, 3] = notes.en_liste_fixe(3)`, was
     * rejected.
     *
     * Parameters stay `Universel` on purpose. Element and key types are enforced at
     * run time, against the constraint carried by the allocation, which sees through
     * aliases that the analyzer cannot follow. Declaring them here would reject
     * programs the runtime accepts.
     */
    const CallableSignature *collection_member_signature(const SemanticTypeRef &receiver,
                                                         const std::string &member)
    {
        if (receiver == nullptr || receiver->kind() != SemanticTypeKind::GENERIC)
        {
            return nullptr;
        }
        const std::string family(receiver->name());
        if (family != "Liste" && family != "ListeFixe" &&
            family != "Dictionnaire" && family != "Ensemble")
        {
            return nullptr;
        }

        const std::pair<const SemanticType *, std::string> key{receiver.get(), member};
        if (const auto cached = m_collection_member_signatures.find(key);
            cached != m_collection_member_signatures.end())
        {
            return &cached->second;
        }

        const SemanticTypeRef universel = *m_analysis.model.find_type("Universel");
        const SemanticTypeRef entier = *m_analysis.model.find_type("Entier");
        const SemanticTypeRef logique = *m_analysis.model.find_type("Logique");
        const SemanticTypeRef texte = *m_analysis.model.find_type("Texte");
        const auto &arguments = receiver->arguments();
        // Liste[T] and Ensemble[T] carry one argument; Dictionnaire[K, V] and
        // ListeFixe[T, N] carry two, the second of which is a size for ListeFixe.
        const SemanticTypeRef first = arguments.empty() ? universel : arguments[0];
        const SemanticTypeRef second = arguments.size() < 2 ? universel : arguments[1];
        const auto liste_of = [&](const SemanticTypeRef &element) {
            return m_analysis.model.types.generic("Liste", {element});
        };

        CallableSignature signature;
        signature.accepts_named_arguments = false;
        signature.has_explicit_return_type = true;
        const auto takes = [&](const std::size_t count) {
            for (std::size_t i = 0; i < count; ++i)
            {
                signature.parameter_names.push_back("argument" + std::to_string(i + 1));
                signature.parameter_types.push_back(universel);
                signature.optional_parameters.push_back(false);
            }
        };

        if (member == "taille")
        {
            signature.return_type = entier;
        }
        else if (member == "vide")
        {
            signature.return_type = logique;
        }
        else if (member == "contient")
        {
            takes(1);
            signature.return_type = logique;
        }
        else if (member == "joindre" && family != "Dictionnaire")
        {
            takes(1);
            signature.return_type = texte;
        }
        else if (family == "Liste" && member == "ajouter")
        {
            takes(1);
            signature.return_type = entier;
        }
        else if (family == "Liste" && member == "inserer")
        {
            takes(2);
            signature.return_type = entier;
        }
        else if (family == "Liste" && member == "retirer_a")
        {
            takes(1);
            signature.return_type = first;
        }
        else if (family == "Liste" && member == "en_liste_fixe")
        {
            // The size comes from the argument, so the call site refines this.
            takes(1);
            signature.return_type = m_analysis.model.types.bottom();
        }
        else if (family == "Liste" && member == "en_ensemble")
        {
            signature.return_type = m_analysis.model.types.generic("Ensemble", {first});
        }
        else if (family == "ListeFixe" && member == "en_liste")
        {
            signature.return_type = liste_of(first);
        }
        else if (family == "Dictionnaire" && (member == "clés" || member == "cles"))
        {
            signature.return_type = liste_of(first);
        }
        else if (family == "Dictionnaire" && member == "valeurs")
        {
            signature.return_type = liste_of(second);
        }
        else if (family == "Dictionnaire" && member == "paires")
        {
            // A pair keeps a shared key/value type and falls back to Universel.
            const SemanticTypeRef element = same_type(first, second) ? first : universel;
            signature.return_type = liste_of(m_analysis.model.types.generic(
                "ListeFixe", {element, m_analysis.model.types.integer_argument(2)}));
        }
        else if (family == "Dictionnaire" && member == "retirer")
        {
            takes(1);
            signature.return_type = second;
        }
        else if (family == "Ensemble" && (member == "ajouter" || member == "retirer" ||
                                          member == "sous_ensemble_de"))
        {
            takes(1);
            signature.return_type = logique;
        }
        else if (family == "Ensemble" && member == "en_liste")
        {
            signature.return_type = liste_of(first);
        }
        else if (family == "Ensemble" && (member == "union" || member == "intersection" ||
                                          member == "difference" || member == "différence"))
        {
            takes(1);
            signature.return_type = m_analysis.model.types.generic("Ensemble", {first});
        }
        else
        {
            return nullptr;
        }

        return &m_collection_member_signatures.emplace(key, std::move(signature)).first->second;
    }

    const CallableSignature *callable_signature(const Expr &callee)
    {
        if (const auto *function = dynamic_cast<const FunctionExpr *>(&callee))
        {
            return &ensure_signature(*function);
        }
        if (const auto *identifier = dynamic_cast<const IdentifierExpr *>(&callee))
        {
            if (const LocalBinding *binding = find_local_value(identifier->name.lexeme))
            {
                if (binding->callable.has_value())
                {
                    return &*binding->callable;
                }
                if (binding->kind == SemanticSymbolKind::FUNCTION)
                {
                    if (const auto *function =
                            dynamic_cast<const FunctionDeclStmt *>(binding->declaration))
                    {
                        return &ensure_signature(*function);
                    }
                }
                if (binding->kind == SemanticSymbolKind::CLASS)
                {
                    if (const auto *klass =
                            dynamic_cast<const ClassDeclStmt *>(binding->declaration))
                    {
                        return m_analysis.model.constructor(*klass);
                    }
                }
                return find_named_signature(identifier->name.lexeme);
            }
            if (const SemanticSymbol *symbol =
                    m_analysis.model.find_value(identifier->name.lexeme))
            {
                if (symbol->kind == SemanticSymbolKind::FUNCTION)
                {
                    if (const auto *function =
                            dynamic_cast<const FunctionDeclStmt *>(symbol->declaration))
                    {
                        return &ensure_signature(*function);
                    }
                }
                if (symbol->kind == SemanticSymbolKind::CLASS)
                {
                    if (const auto *klass =
                            dynamic_cast<const ClassDeclStmt *>(symbol->declaration))
                    {
                        return m_analysis.model.constructor(*klass);
                    }
                }
            }
            return find_named_signature(identifier->name.lexeme);
        }
        if (const auto *member = dynamic_cast<const MemberAccessExpr *>(&callee))
        {
            if (const std::optional<std::string> qualified =
                    qualified_expression_name(callee);
                qualified.has_value())
            {
                if (const CallableSignature *signature =
                        find_named_signature(*qualified))
                {
                    return signature;
                }
            }
            if (const auto *object = dynamic_cast<const IdentifierExpr *>(member->object.get()))
            {
                if (object->name.lexeme == "parent" &&
                    !m_class_stack.empty() &&
                    !m_class_stack.back()->parent.empty())
                {
                    if (const ClassDeclStmt *parent_class =
                            class_declaration(
                                m_class_stack.back()->parent.name))
                    {
                        if (const FunctionDeclStmt *method =
                                find_method(*parent_class,
                                            member->member.lexeme))
                        {
                            return &ensure_signature(*method);
                        }
                    }
                }
                if (const CallableSignature *signature =
                        find_named_signature(
                            object->name.lexeme + '.' +
                            member->member.lexeme))
                {
                    return signature;
                }
            }
            const auto object_type =
                m_analysis.model.m_expression_types.find(member->object.get());
            if (object_type != m_analysis.model.m_expression_types.end())
            {
                const std::string &member_name =
                    member->member.lexeme;
                // A builtin type and a module can share a name (e.g. Texte).
                // Module functions take an explicit receiver; methods do not.
                if (object_type->second != nullptr &&
                    object_type->second->kind() != SemanticTypeKind::BUILTIN)
                {
                    if (const CallableSignature *signature =
                            find_named_signature(
                                std::string(
                                    object_type->second->name()) +
                                "." + member_name))
                    {
                        return signature;
                    }
                }
                if (object_type->second != nullptr &&
                    object_type->second->kind() ==
                        SemanticTypeKind::BUILTIN &&
                    object_type->second->name() == "Texte")
                {
                    return text_member_signature(member_name);
                }
                if (const CallableSignature *builtin =
                        collection_member_signature(object_type->second, member_name))
                {
                    return builtin;
                }
                if (const ClassDeclStmt *klass =
                        class_declaration(object_type->second))
                {
                    if (const FunctionDeclStmt *method =
                            find_method(*klass, member->member.lexeme))
                    {
                        return &ensure_signature(*method);
                    }
                }
            }
        }
        return nullptr;
    }

    static std::optional<std::string>
    qualified_expression_name(const Expr &expression)
    {
        if (const auto *identifier =
                dynamic_cast<const IdentifierExpr *>(&expression))
        {
            return identifier->name.lexeme;
        }
        if (const auto *member =
                dynamic_cast<const MemberAccessExpr *>(&expression))
        {
            std::optional<std::string> object =
                qualified_expression_name(*member->object);
            if (object.has_value())
            {
                return *object + "." + member->member.lexeme;
            }
        }
        return std::nullopt;
    }

    const ClassDeclStmt *class_declaration(const SemanticTypeRef &type) const
    {
        if (type == nullptr || type->kind() != SemanticTypeKind::CLASS)
        {
            return nullptr;
        }
        std::string name(type->name());
        if (const std::size_t dot = name.rfind('.'); dot != std::string::npos)
        {
            name = name.substr(dot + 1);
        }
        const SemanticSymbol *symbol = m_analysis.model.find_value(name);
        return symbol == nullptr
                   ? nullptr
                   : dynamic_cast<const ClassDeclStmt *>(symbol->declaration);
    }

    const ClassDeclStmt *class_declaration(const std::string &name) const
    {
        if (const LocalBinding *local = find_local_value(name);
            local != nullptr)
        {
            if (const auto *klass =
                    dynamic_cast<const ClassDeclStmt *>(local->declaration))
            {
                return klass;
            }
        }
        const SemanticSymbol *symbol = m_analysis.model.find_value(name);
        return symbol == nullptr
                   ? nullptr
                   : dynamic_cast<const ClassDeclStmt *>(symbol->declaration);
    }

    const FunctionDeclStmt *find_method(const ClassDeclStmt &klass,
                                        const std::string &name)
    {
        for (const StmtPtr &member : klass.members)
        {
            if (const auto *method =
                    dynamic_cast<const FunctionDeclStmt *>(member.get());
                method != nullptr && method->name.lexeme == name)
            {
                return method;
            }
        }
        if (!klass.parent.empty())
        {
            if (const ClassDeclStmt *parent_class =
                    class_declaration(klass.parent.name))
            {
                return find_method(*parent_class, name);
            }
        }
        return nullptr;
    }

    SemanticTypeRef member_type(const ClassDeclStmt &klass,
                                const std::string &name)
    {
        for (const StmtPtr &member : klass.members)
        {
            if (const auto *field =
                    dynamic_cast<const VarDeclStmt *>(member.get());
                field != nullptr && field->name.lexeme == name)
            {
                return resolve_type(field->type);
            }
            if (const auto *method =
                    dynamic_cast<const FunctionDeclStmt *>(member.get());
                method != nullptr && method->name.lexeme == name)
            {
                return ensure_signature(*method).return_type;
            }
        }
        if (!klass.parent.empty())
        {
            if (const ClassDeclStmt *parent_class =
                    class_declaration(klass.parent.name))
            {
                return member_type(*parent_class, name);
            }
        }
        return *m_analysis.model.find_type("Universel");
    }

    void validate_call(const CallExpr &call, const CallableSignature &signature)
    {
        if (!signature.accepts_named_arguments &&
            std::any_of(call.args.begin(), call.args.end(), [](const Argument &argument) {
                return !argument.name.empty();
            }))
        {
            diagnose(call.paren, "LUM-S0018",
                     "cette fonction native n'accepte pas d'arguments nommés");
            return;
        }
        std::vector<bool> provided(signature.parameter_names.size(), false);
        std::size_t next_positional = 0;
        for (const Argument &argument : call.args)
        {
            std::size_t target = signature.parameter_names.size();
            if (argument.name.empty())
            {
                while (next_positional < provided.size() && provided[next_positional])
                {
                    ++next_positional;
                }
                target = next_positional;
            }
            else
            {
                const auto found = std::find(signature.parameter_names.begin(),
                                             signature.parameter_names.end(),
                                             argument.name);
                if (found != signature.parameter_names.end())
                {
                    target = static_cast<std::size_t>(
                        std::distance(signature.parameter_names.begin(), found));
                }
            }

            if (target >= provided.size())
            {
                if (argument.name.empty() && signature.variadic)
                {
                    require_assignable(
                        m_analysis.model.m_expression_types.at(argument.value.get()),
                        signature.variadic_type,
                        call.paren,
                        "l'argument variadique");
                    continue;
                }
                diagnose(call.paren,
                         argument.name.empty() ? "LUM-S0015" : "LUM-S0014",
                         argument.name.empty()
                             ? "trop d'arguments positionnels"
                             : "paramètre nommé inconnu: '" + argument.name + "'");
                continue;
            }
            if (provided[target])
            {
                diagnose(call.paren, "LUM-S0016",
                         "le paramètre '" + signature.parameter_names[target] +
                             "' est fourni plusieurs fois");
                continue;
            }
            provided[target] = true;
            require_assignable(
                m_analysis.model.m_expression_types.at(argument.value.get()),
                signature.parameter_types[target],
                call.paren,
                "l'argument '" + signature.parameter_names[target] + "'");
            if (argument.name.empty())
            {
                next_positional = target + 1;
            }
        }

        for (std::size_t i = 0; i < provided.size(); ++i)
        {
            const bool optional = i < signature.optional_parameters.size() &&
                                  signature.optional_parameters[i];
            if (!provided[i] && !optional)
            {
                diagnose(call.paren, "LUM-S0017",
                         "argument requis manquant: '" + signature.parameter_names[i] + "'");
            }
        }
    }

    bool is_assignable(const SemanticTypeRef &source,
                       const SemanticTypeRef &target) const
    {
        if (source == nullptr || target == nullptr ||
            source->kind() == SemanticTypeKind::BOTTOM ||
            target->kind() == SemanticTypeKind::BOTTOM ||
            same_type(source, target))
        {
            return true;
        }
        if (target->kind() == SemanticTypeKind::INTERFACE &&
            target->name() == "Erreur" &&
            is_error_type(source))
        {
            return true;
        }
        if (target->kind() == SemanticTypeKind::BUILTIN &&
            target->name() == "Universel")
        {
            return true;
        }
        if (source->kind() == SemanticTypeKind::BUILTIN &&
            source->name() == "Entier" &&
            target->kind() == SemanticTypeKind::BUILTIN &&
            target->name() == "Décimal")
        {
            return true;
        }
        if (target->kind() == SemanticTypeKind::UNION)
        {
            return std::any_of(
                target->arguments().begin(),
                target->arguments().end(),
                [&](const SemanticTypeRef &alternative) {
                    return is_assignable(source, alternative);
                });
        }
        if (source->kind() == SemanticTypeKind::UNION)
        {
            return std::all_of(
                source->arguments().begin(),
                source->arguments().end(),
                [&](const SemanticTypeRef &alternative) {
                    return is_assignable(alternative, target);
                });
        }
        if (source->kind() == SemanticTypeKind::GENERIC &&
            target->kind() == SemanticTypeKind::GENERIC &&
            source->name() == "Résultat" && target->name() == "Résultat" &&
            source->arguments().size() == 2 && target->arguments().size() == 2)
        {
            return is_assignable(source->arguments()[0], target->arguments()[0]) &&
                   is_assignable(source->arguments()[1], target->arguments()[1]);
        }
        if (source->kind() == SemanticTypeKind::CLASS &&
            (target->kind() == SemanticTypeKind::CLASS ||
             target->kind() == SemanticTypeKind::INTERFACE))
        {
            const auto unqualified = [](const std::string_view name) {
                const std::size_t dot = name.rfind('.');
                return std::string(
                    dot == std::string_view::npos
                        ? name
                        : name.substr(dot + 1));
            };
            const std::string target_name =
                unqualified(target->name());
            const ClassDeclStmt *current =
                class_declaration(source);
            std::unordered_set<const ClassDeclStmt *> visited;
            while (current != nullptr && visited.insert(current).second)
            {
                if (target->kind() == SemanticTypeKind::CLASS &&
                    current->name.lexeme == target_name)
                {
                    return true;
                }
                if (target->kind() == SemanticTypeKind::INTERFACE &&
                    std::any_of(
                        current->interfaces.begin(),
                        current->interfaces.end(),
                        [&](const TypeExpr &interface) {
                            return unqualified(interface.name) ==
                                   target_name;
                        }))
                {
                    return true;
                }
                if (current->parent.empty())
                {
                    break;
                }
                const SemanticSymbol *parent =
                    m_analysis.model.find_value(
                        current->parent.name);
                current =
                    parent == nullptr
                        ? nullptr
                        : dynamic_cast<const ClassDeclStmt *>(
                              parent->declaration);
            }
        }
        return false;
    }

    void require_assignable(const SemanticTypeRef &source,
                            const SemanticTypeRef &target,
                            const Token &site,
                            const std::string &context)
    {
        if (!is_assignable(source, target))
        {
            diagnose(site, "LUM-S0019",
                     context + " attend " + std::string(target->display()) +
                         "; reçu " + std::string(source->display()));
        }
    }

    SemanticTypeRef inferred_type(const Expr &expression)
    {
        if (const auto *literal = dynamic_cast<const LiteralExpr *>(&expression))
        {
            switch (literal->token.type)
            {
            case TokenType::ENTIER_LIT:
                return *m_analysis.model.find_type("Entier");
            case TokenType::DECIMAL_LIT:
                return *m_analysis.model.find_type("Décimal");
            case TokenType::TEXTE_LIT:
                return *m_analysis.model.find_type("Texte");
            case TokenType::SYMBOLE_LIT:
                return *m_analysis.model.find_type("Symbole");
            case TokenType::VRAI:
            case TokenType::FAUX:
                return *m_analysis.model.find_type("Logique");
            case TokenType::RIEN:
                return *m_analysis.model.find_type("Rien");
            default:
                break;
            }
        }
        if (const auto *identifier = dynamic_cast<const IdentifierExpr *>(&expression))
        {
            if (const LocalBinding *local = find_local_value(identifier->name.lexeme);
                local != nullptr && local->type != nullptr)
            {
                return local->type;
            }
            if (const SemanticSymbol *symbol =
                    m_analysis.model.find_value(identifier->name.lexeme);
                symbol != nullptr && symbol->type != nullptr)
            {
                return symbol->type;
            }
        }
        if (const auto *call = dynamic_cast<const CallExpr *>(&expression))
        {
            if (const auto *identifier =
                    dynamic_cast<const IdentifierExpr *>(call->callee.get());
                identifier != nullptr && call->args.size() == 1 &&
                (identifier->name.lexeme == "Succès" ||
                 identifier->name.lexeme == "Échec"))
            {
                const SemanticTypeRef payload =
                    m_analysis.model.m_expression_types.at(call->args.front().value.get());
                const SemanticTypeRef bottom = m_analysis.model.types.bottom();
                return m_analysis.model.types.generic(
                    "Résultat",
                    identifier->name.lexeme == "Succès"
                        ? std::vector<SemanticTypeRef>{payload, bottom}
                        : std::vector<SemanticTypeRef>{bottom, payload});
            }
            // ListeFixe carries its length in its type, and the length is the
            // argument, so this one result can only be read at the call.
            if (const auto *member =
                    dynamic_cast<const MemberAccessExpr *>(call->callee.get());
                member != nullptr && member->member.lexeme == "en_liste_fixe" &&
                call->args.size() == 1)
            {
                const auto receiver =
                    m_analysis.model.m_expression_types.find(member->object.get());
                const auto *length_literal =
                    dynamic_cast<const LiteralExpr *>(call->args.front().value.get());
                if (receiver != m_analysis.model.m_expression_types.end() &&
                    receiver->second != nullptr &&
                    receiver->second->kind() == SemanticTypeKind::GENERIC &&
                    receiver->second->name() == "Liste" &&
                    length_literal != nullptr &&
                    length_literal->token.type == TokenType::ENTIER_LIT)
                {
                    if (const auto length =
                            numeric::parse_integer_literal(length_literal->token.lexeme);
                        length.has_value() && *length >= 0)
                    {
                        const auto &arguments = receiver->second->arguments();
                        return m_analysis.model.types.generic(
                            "ListeFixe",
                            {arguments.empty() ? *m_analysis.model.find_type("Universel")
                                               : arguments[0],
                             m_analysis.model.types.integer_argument(
                                 static_cast<std::uint64_t>(*length))});
                    }
                }
            }
            if (const CallableSignature *signature = callable_signature(*call->callee);
                signature != nullptr)
            {
                return signature->return_type;
            }
        }
        if (const auto *member =
                dynamic_cast<const MemberAccessExpr *>(&expression))
        {
            if (const auto *object =
                    dynamic_cast<const IdentifierExpr *>(member->object.get());
                object != nullptr && object->name.lexeme == "ici" &&
                !m_class_stack.empty())
            {
                return member_type(*m_class_stack.back(),
                                   member->member.lexeme);
            }
            if (const auto *object =
                    dynamic_cast<const IdentifierExpr *>(member->object.get());
                object != nullptr && object->name.lexeme == "parent" &&
                !m_class_stack.empty() &&
                !m_class_stack.back()->parent.empty())
            {
                if (const ClassDeclStmt *parent_class =
                        class_declaration(
                            m_class_stack.back()->parent.name))
                {
                    return member_type(*parent_class,
                                       member->member.lexeme);
                }
            }
            const auto object_type =
                m_analysis.model.m_expression_types.find(member->object.get());
            if (object_type != m_analysis.model.m_expression_types.end())
            {
                if (member->member.lexeme == "taille")
                {
                    return *m_analysis.model.find_type("Entier");
                }
                if (const ClassDeclStmt *klass =
                        class_declaration(object_type->second))
                {
                    return member_type(*klass, member->member.lexeme);
                }
            }
            if (const auto *object =
                    dynamic_cast<const IdentifierExpr *>(member->object.get()))
            {
                const ImportStmt *import = nullptr;
                if (const LocalBinding *local =
                        find_local_value(object->name.lexeme))
                {
                    import = dynamic_cast<const ImportStmt *>(
                        local->declaration);
                }
                if (import == nullptr)
                {
                    if (const SemanticSymbol *symbol =
                            m_analysis.model.find_value(
                                object->name.lexeme))
                    {
                        import = dynamic_cast<const ImportStmt *>(
                            symbol->declaration);
                    }
                }
                if (import != nullptr)
                {
                    const auto module =
                        m_imports.find(import->module_name.lexeme);
                    if (module != m_imports.end())
                    {
                        const auto value_type =
                            module->second.value_types.find(
                                member->member.lexeme);
                        if (value_type !=
                            module->second.value_types.end())
                        {
                            return resolve_imported_type(
                                value_type->second,
                                import->module_name.lexeme,
                                module->second);
                        }
                    }
                }
            }
        }
        if (const auto *cast = dynamic_cast<const CastExpr *>(&expression))
        {
            return resolve_type(cast->target_type);
        }
        if (dynamic_cast<const TypeCheckExpr *>(&expression) != nullptr)
        {
            return *m_analysis.model.find_type("Logique");
        }
        if (const auto *list = dynamic_cast<const ListExpr *>(&expression))
        {
            std::vector<SemanticTypeRef> element_types;
            for (const ExprPtr &element : list->elements)
            {
                const auto found = m_analysis.model.m_expression_types.find(element.get());
                if (found != m_analysis.model.m_expression_types.end() &&
                    std::none_of(element_types.begin(), element_types.end(),
                                 [&](const SemanticTypeRef &existing) {
                                     return same_type(existing, found->second);
                                 }))
                {
                    element_types.push_back(found->second);
                }
            }
            SemanticTypeRef element_type = *m_analysis.model.find_type("Universel");
            if (element_types.size() == 1)
            {
                element_type = element_types.front();
            }
            else if (element_types.size() > 1)
            {
                element_type = m_analysis.model.types.union_type(std::move(element_types));
            }
            return m_analysis.model.types.generic("Liste", {element_type});
        }
        if (const auto *set = dynamic_cast<const SetExpr *>(&expression))
        {
            std::vector<SemanticTypeRef> element_types;
            for (const ExprPtr &element : set->elements)
            {
                const auto found = m_analysis.model.m_expression_types.find(element.get());
                if (found != m_analysis.model.m_expression_types.end() &&
                    std::none_of(element_types.begin(), element_types.end(),
                                 [&](const SemanticTypeRef &existing) {
                                     return same_type(existing, found->second);
                                 }))
                {
                    element_types.push_back(found->second);
                }
            }
            SemanticTypeRef element_type = *m_analysis.model.find_type("Universel");
            if (element_types.size() == 1)
            {
                element_type = element_types.front();
            }
            else if (element_types.size() > 1)
            {
                element_type = m_analysis.model.types.union_type(std::move(element_types));
            }
            return m_analysis.model.types.generic("Ensemble", {element_type});
        }
        if (const auto *dictionary = dynamic_cast<const DictionaryExpr *>(&expression))
        {
            std::vector<SemanticTypeRef> key_types;
            std::vector<SemanticTypeRef> value_types;
            auto add_distinct = [](std::vector<SemanticTypeRef> &types,
                                   const SemanticTypeRef &type) {
                if (std::none_of(types.begin(), types.end(),
                                 [&](const SemanticTypeRef &existing) {
                                     return same_type(existing, type);
                                 }))
                {
                    types.push_back(type);
                }
            };
            for (const DictionaryEntryExpr &entry : dictionary->entries)
            {
                add_distinct(key_types, m_analysis.model.m_expression_types.at(entry.key.get()));
                add_distinct(value_types, m_analysis.model.m_expression_types.at(entry.value.get()));
            }
            auto aggregate = [&](std::vector<SemanticTypeRef> types) {
                if (types.empty())
                {
                    return *m_analysis.model.find_type("Universel");
                }
                return types.size() == 1
                           ? types.front()
                           : m_analysis.model.types.union_type(std::move(types));
            };
            return m_analysis.model.types.generic(
                "Dictionnaire",
                {aggregate(std::move(key_types)), aggregate(std::move(value_types))});
        }
        if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expression))
        {
            if (unary->op.type == TokenType::NON)
            {
                return *m_analysis.model.find_type("Logique");
            }
            return m_analysis.model.m_expression_types.at(unary->operand.get());
        }
        if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expression))
        {
            if (binary->op.type == TokenType::EGAL)
            {
                return m_analysis.model.m_expression_types.at(binary->right.get());
            }
            switch (binary->op.type)
            {
            case TokenType::EGAL_EGAL:
            case TokenType::BANG_EGAL:
            case TokenType::INFERIEUR:
            case TokenType::INFERIEUR_EGAL:
            case TokenType::SUPERIEUR:
            case TokenType::SUPERIEUR_EGAL:
            case TokenType::ET:
            case TokenType::OU:
                return *m_analysis.model.find_type("Logique");
            default:
                return m_analysis.model.m_expression_types.at(binary->left.get());
            }
        }
        if (const auto *match = dynamic_cast<const AgirSelonStmt *>(&expression))
        {
            if (const auto found = m_analysis.model.m_expression_types.find(match);
                found != m_analysis.model.m_expression_types.end())
            {
                return found->second;
            }
        }
        if (const auto *propagation = dynamic_cast<const PropagationExpr *>(&expression))
        {
            const SemanticTypeRef inner =
                m_analysis.model.m_expression_types.at(propagation->operand.get());
            if (is_result_type(inner) && inner->arguments().size() >= 1)
            {
                return inner->arguments()[0];
            }
            return *m_analysis.model.find_type("Universel");
        }
        return *m_analysis.model.find_type("Universel");
    }

    void resolve_match(const AgirSelonStmt &match)
    {
        resolve_expression(*match.expression);
        const SemanticTypeRef matched =
            m_analysis.model.m_expression_types.at(match.expression.get());
        const bool is_result =
            matched->kind() == SemanticTypeKind::GENERIC &&
            matched->name() == "Résultat" &&
            matched->arguments().size() == 2;
        const bool has_result_patterns = std::any_of(
            match.branches.begin(), match.branches.end(),
            [](const AgirSelonBranch &branch) {
                return std::any_of(
                    branch.patterns.begin(), branch.patterns.end(),
                    [](const Pattern &pattern) {
                        return pattern.kind == PatternKind::RESULT_SUCCESS ||
                               pattern.kind == PatternKind::RESULT_FAILURE;
                    });
            });
        if (has_result_patterns && !is_result)
        {
            diagnose(match.keyword, "LUM-S0023",
                     "les motifs Succès et Échec exigent une valeur Résultat");
        }

        const SemanticTypeRef success_type =
            is_result ? matched->arguments()[0] : m_analysis.model.types.bottom();
        const SemanticTypeRef error_type =
            is_result ? matched->arguments()[1] : m_analysis.model.types.bottom();
        bool covers_success = false;
        bool covers_all_failures = false;
        const std::vector<SemanticTypeRef> error_alternatives =
            is_result && error_type->kind() == SemanticTypeKind::UNION
                ? error_type->arguments()
                : std::vector<SemanticTypeRef>{error_type};
        std::vector<bool> covered_errors(error_alternatives.size(), false);
        std::vector<SemanticTypeRef>
            covered_open_error_patterns;
        std::vector<SemanticTypeRef> branch_types;
        const ObligationState baseline = obligation_state();
        std::vector<ObligationState> branch_obligations;

        for (const AgirSelonBranch &branch : match.branches)
        {
            restore_obligation_state(baseline);
            push_scope();
            bool propagates_only_failures = !branch.patterns.empty();
            bool contains_only_result_patterns = !branch.patterns.empty();
            std::vector<SemanticTypeRef> propagated_error_types;
            for (const Pattern &pattern : branch.patterns)
            {
                if (pattern.kind == PatternKind::RESULT_SUCCESS ||
                    pattern.kind == PatternKind::RESULT_FAILURE)
                {
                    if (!is_result)
                    {
                        continue;
                    }
                    SemanticTypeRef binding_type =
                        pattern.kind == PatternKind::RESULT_SUCCESS
                            ? success_type
                            : error_type;
                    if (pattern.kind == PatternKind::RESULT_SUCCESS)
                    {
                        propagates_only_failures = false;
                    }
                    if (pattern.kind == PatternKind::RESULT_SUCCESS)
                    {
                        if (!pattern.type.empty())
                        {
                            diagnose(pattern.constructor, "LUM-S0024",
                                     "un motif Succès ne peut pas préciser un type d'erreur");
                        }
                        if (covers_success)
                        {
                            diagnose(pattern.constructor, "LUM-S0025",
                                     "motif Succès inaccessible ou dupliqué");
                        }
                        covers_success = true;
                    }
                    else if (!pattern.type.empty())
                    {
                        binding_type = resolve_type(pattern.type);
                        if (!is_assignable(binding_type, error_type))
                        {
                            diagnose(pattern.constructor, "LUM-S0026",
                                     "ce type ne fait pas partie des erreurs du résultat");
                        }
                        bool covers_new_error = false;
                        if (error_type->kind() ==
                            SemanticTypeKind::INTERFACE)
                        {
                            const bool already_covered =
                                std::any_of(
                                    covered_open_error_patterns.begin(),
                                    covered_open_error_patterns.end(),
                                    [&](const SemanticTypeRef &previous) {
                                        return is_assignable(
                                            binding_type,
                                            previous);
                                    });
                            if (!already_covered)
                            {
                                covers_new_error = true;
                                covered_open_error_patterns.push_back(
                                    binding_type);
                            }
                        }
                        else
                        {
                            for (std::size_t i = 0;
                                 i < error_alternatives.size();
                                 ++i)
                            {
                                if (is_assignable(
                                        error_alternatives[i],
                                        binding_type))
                                {
                                    covers_new_error |=
                                        !covered_errors[i];
                                    covered_errors[i] = true;
                                }
                            }
                        }
                        if (!covers_new_error)
                        {
                            diagnose(pattern.constructor, "LUM-S0025",
                                     "motif Échec inaccessible ou dupliqué");
                        }
                    }
                    else
                    {
                        if (covers_all_failures)
                        {
                            diagnose(pattern.constructor, "LUM-S0025",
                                     "motif Échec inaccessible ou dupliqué");
                        }
                        covers_all_failures = true;
                        std::fill(covered_errors.begin(),
                                  covered_errors.end(), true);
                    }
                    if (pattern.name.lexeme != "_")
                    {
                        declare_local(pattern.name, SemanticSymbolKind::VARIABLE);
                        set_local_type(pattern.name.lexeme, binding_type);
                    }
                    if (pattern.kind == PatternKind::RESULT_FAILURE)
                    {
                        propagated_error_types.push_back(binding_type);
                    }
                }
                else if (pattern.kind == PatternKind::TYPE_BINDING)
                {
                    propagates_only_failures = false;
                    contains_only_result_patterns = false;
                    const SemanticTypeRef type = resolve_type(pattern.type);
                    declare_local(pattern.name, SemanticSymbolKind::VARIABLE);
                    set_local_type(pattern.name.lexeme, type);
                }
                else if (pattern.literal != nullptr)
                {
                    propagates_only_failures = false;
                    contains_only_result_patterns = false;
                    resolve_expression(*pattern.literal);
                }
                else
                {
                    propagates_only_failures = false;
                    contains_only_result_patterns = false;
                }
            }

            if (branch.terminator == BranchTerminator::PROPAGER)
            {
                if (!is_result || !propagates_only_failures ||
                    propagated_error_types.empty())
                {
                    diagnose(
                        branch.terminator_token,
                        "LUM-S0047",
                        "'propager' n'est permis que pour une branche composée exclusivement de motifs Échec");
                }
                else
                {
                    validate_propagation(
                        m_analysis.model.types.union_type(
                            std::move(propagated_error_types)),
                        branch.terminator_token);
                }
                branch_types.push_back(m_analysis.model.types.bottom());
            }
            else if (branch.terminator == BranchTerminator::IGNORER)
            {
                if (!is_result || !contains_only_result_patterns)
                {
                    diagnose(
                        branch.terminator_token,
                        "LUM-S0048",
                        "'ignorer' en fin de branche exige uniquement des motifs Succès ou Échec");
                }
                branch_types.push_back(*m_analysis.model.find_type("Rien"));
            }
            else if (const auto *body = dynamic_cast<const ExprStmt *>(branch.body.get()))
            {
                resolve_expression(*body->expr);
                branch_types.push_back(
                    m_analysis.model.m_expression_types.at(body->expr.get()));
            }
            else if (const auto *block = dynamic_cast<const BlockStmt *>(branch.body.get()))
            {
                resolve_block(*block, false);
            }
            else
            {
                resolve_statement(*branch.body);
            }
            pop_scope();
            branch_obligations.push_back(obligation_state());
        }
        if (match.else_branch != nullptr)
        {
            restore_obligation_state(baseline);
            if (const auto *body = dynamic_cast<const ExprStmt *>(match.else_branch.get()))
            {
                resolve_expression(*body->expr);
                branch_types.push_back(
                    m_analysis.model.m_expression_types.at(body->expr.get()));
            }
            else
            {
                resolve_statement(*match.else_branch);
            }
            covers_success = true;
            covers_all_failures = true;
            branch_obligations.push_back(obligation_state());
        }
        if (has_result_patterns && is_result &&
            (!covers_success || !covers_all_failures) &&
            error_type->kind() != SemanticTypeKind::INTERFACE)
        {
            covers_all_failures =
                std::all_of(covered_errors.begin(), covered_errors.end(),
                            [](const bool covered) { return covered; });
        }
        if (has_result_patterns && is_result &&
            (!covers_success || !covers_all_failures))
        {
            diagnose(match.keyword, "LUM-S0027",
                     "agir selon n'est pas exhaustif pour Succès et Échec");
        }
        if (match.else_branch == nullptr && !is_result)
        {
            branch_obligations.push_back(baseline);
        }
        if (!branch_obligations.empty())
        {
            merge_obligation_states(baseline, branch_obligations);
        }

        SemanticTypeRef result_type = *m_analysis.model.find_type("Rien");
        if (!branch_types.empty())
        {
            result_type = branch_types.front();
            for (std::size_t i = 1; i < branch_types.size(); ++i)
            {
                if (!is_assignable(branch_types[i], result_type) &&
                    !is_assignable(result_type, branch_types[i]))
                {
                    result_type = m_analysis.model.types.union_type(
                        {result_type, branch_types[i]});
                }
                else if (is_assignable(result_type, branch_types[i]))
                {
                    result_type = branch_types[i];
                }
            }
        }
        m_analysis.model.m_expression_types[&match] = result_type;
    }

    /**
     * @brief Checks that a name being assigned to is one that can be assigned.
     *
     * Narrower than resolving every name that is read: an assignment target is
     * always a plain identifier, and it must name a declared, non-fixed
     * variable. Reads are checked too now, in diagnose_value_read.
     */
    void diagnose_assignment_target(const IdentifierExpr &target)
    {
        if (const LocalBinding *binding = find_local_value(target.name.lexeme))
        {
            if (const auto *declaration =
                    dynamic_cast<const VarDeclStmt *>(binding->declaration);
                declaration != nullptr && declaration->is_fixe)
            {
                diagnose(target.name, "LUM-S0056",
                         "'" + target.name.lexeme + "' est fixe et ne peut pas être réaffecté");
            }
            return;
        }

        if (const SemanticSymbol *symbol = m_analysis.model.find_value(target.name.lexeme))
        {
            if (const auto *declaration =
                    dynamic_cast<const VarDeclStmt *>(symbol->declaration);
                declaration != nullptr && declaration->is_fixe)
            {
                diagnose(target.name, "LUM-S0056",
                         "'" + target.name.lexeme + "' est fixe et ne peut pas être réaffecté");
            }
            return;
        }

        if (m_options.incremental_submission)
        {
            // An earlier line may have declared it; this buffer cannot tell.
            return;
        }
        diagnose(target.name, "LUM-S0055",
                 "affectation à '" + target.name.lexeme + "', qui n'est déclaré nulle part");
    }

    /**
     * @brief Checks that a name being read is one this buffer declares.
     *
     * A read resolves against locals and parameters first, then everything the
     * module level knows: its own declarations, what it imported, the class,
     * interface and type names, and the builtins. A name none of those hold is
     * a name nothing can supply, and saying so here means both engines are
     * told the same thing before either one starts -- the two used to fail at
     * run time, with their carets one character apart.
     *
     * Only plain identifiers are checked. `ici` and `parent` are keywords with
     * their own rules, and they arrive here wearing their own token types.
     */
    void diagnose_value_read(const IdentifierExpr &read)
    {
        const Token &name = read.name;
        if (m_options.incremental_submission || name.type != TokenType::IDENT)
        {
            return;
        }
        if (find_local_value(name.lexeme) != nullptr ||
            m_analysis.model.find_value(name.lexeme) != nullptr ||
            m_analysis.model.find_type(name.lexeme) != nullptr)
        {
            return;
        }
        diagnose(name, "LUM-S0057",
                 "le symbole '" + name.lexeme + "' n'est déclaré nulle part");
    }

    void resolve_expression(
        const Expr &expression,
        const bool consumes_result_binding = true)
    {
        if (const auto *match = dynamic_cast<const AgirSelonStmt *>(&expression))
        {
            resolve_match(*match);
            return;
        }
        if (const auto *parent_use = dynamic_cast<const IdentifierExpr *>(&expression);
            parent_use != nullptr && parent_use->name.type == TokenType::PARENT &&
            m_method_depth == 0)
        {
            diagnose(parent_use->name, "LUM-S0054",
                     "'parent' doit être placé dans une méthode");
        }
        if (const auto *read = dynamic_cast<const IdentifierExpr *>(&expression))
        {
            diagnose_value_read(*read);
        }
        if (const auto *identifier =
                dynamic_cast<const IdentifierExpr *>(&expression);
            identifier != nullptr && consumes_result_binding)
        {
            consume_obligation(identifier->name.lexeme);
        }
        else if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expression);
                 binary != nullptr && binary->op.type == TokenType::EGAL)
        {
            resolve_expression(*binary->right);
            if (const auto *identifier =
                    dynamic_cast<const IdentifierExpr *>(binary->left.get()))
            {
                diagnose_assignment_target(*identifier);
                if (LocalBinding *binding =
                        find_local_value_mutable(identifier->name.lexeme))
                {
                    if (binding->obligation.has_value() &&
                        m_obligations.at(*binding->obligation).active)
                    {
                        if (!obligation_has_other_owner(
                                *binding->obligation,
                                binding))
                        {
                            diagnose(binary->op, "LUM-S0030",
                                     "l'affectation écraserait un résultat non utilisé");
                            m_obligations.at(*binding->obligation).active =
                                false;
                        }
                    }
                    if (binding->type != nullptr)
                    {
                        contextualize_result_construction(
                            *binary->right,
                            binding->type,
                            binary->op);
                        const SemanticTypeRef contextual_type =
                            m_analysis.model.m_expression_types.at(
                                binary->right.get());
                        require_assignable(contextual_type, binding->type, binary->op,
                                           "la valeur affectée");
                    }
                    const SemanticTypeRef assigned =
                        m_analysis.model.m_expression_types.at(binary->right.get());
                    binding->obligation.reset();
                    binding->callable.reset();
                    if (const CallableSignature *assigned_callable =
                            callable_signature(*binary->right))
                    {
                        binding->callable = *assigned_callable;
                    }
                    if (is_result_type(assigned))
                    {
                        if (const auto *source =
                                dynamic_cast<const IdentifierExpr *>(binary->right.get());
                            source != nullptr)
                        {
                            if (LocalBinding *source_binding =
                                    find_local_value_mutable(source->name.lexeme))
                            {
                                binding->obligation = source_binding->obligation;
                                if (binding->obligation.has_value())
                                {
                                    m_obligations.at(*binding->obligation).active = true;
                                }
                            }
                        }
                        if (!binding->obligation.has_value())
                        {
                            create_obligation(*binding, binary->op);
                        }
                    }
                }
            }
        }
        else if (const auto *binary = dynamic_cast<const BinaryExpr *>(&expression))
        {
            resolve_expression(*binary->left);
            resolve_expression(*binary->right);
            if (binary->op.type != TokenType::EGAL_EGAL &&
                binary->op.type != TokenType::BANG_EGAL &&
                (is_result_type(
                     m_analysis.model.m_expression_types.at(
                         binary->left.get())) ||
                 is_result_type(
                     m_analysis.model.m_expression_types.at(
                         binary->right.get()))))
            {
                diagnose(
                    binary->op,
                    "LUM-S0041",
                    "Résultat ne participe pas implicitement aux opérations logiques, numériques ou textuelles");
            }
        }
        else if (const auto *set = dynamic_cast<const SetExpr *>(&expression))
        {
            for (const ExprPtr &element : set->elements)
            {
                resolve_expression(*element);
            }
        }
        else if (const auto *dictionary = dynamic_cast<const DictionaryExpr *>(&expression))
        {
            for (const DictionaryEntryExpr &entry : dictionary->entries)
            {
                resolve_expression(*entry.key);
                resolve_expression(*entry.value);
            }
        }
        else if (const auto *unary = dynamic_cast<const UnaryExpr *>(&expression))
        {
            resolve_expression(*unary->operand);
            if (is_result_type(
                    m_analysis.model.m_expression_types.at(
                        unary->operand.get())))
            {
                diagnose(
                    unary->op,
                    "LUM-S0041",
                    "Résultat ne possède pas de conversion logique ou numérique implicite");
            }
        }
        else if (const auto *cast = dynamic_cast<const CastExpr *>(&expression))
        {
            resolve_expression(*cast->operand, false);
        }
        else if (const auto *type_check = dynamic_cast<const TypeCheckExpr *>(&expression))
        {
            resolve_expression(*type_check->operand, false);
            static_cast<void>(resolve_type(type_check->type));
        }
        else if (const auto *function = dynamic_cast<const FunctionExpr *>(&expression))
        {
            resolve_function(*function);
        }
        else if (const auto *call = dynamic_cast<const CallExpr *>(&expression))
        {
            resolve_expression(*call->callee);
            for (const Argument &argument : call->args)
            {
                resolve_expression(*argument.value);
            }
            if (const CallableSignature *signature = callable_signature(*call->callee))
            {
                validate_call(*call, *signature);
                std::size_t positional = 0;
                for (const Argument &argument : call->args)
                {
                    std::size_t parameter = positional;
                    if (argument.name.empty())
                    {
                        ++positional;
                    }
                    else
                    {
                        const auto found = std::find(
                            signature->parameter_names.begin(),
                            signature->parameter_names.end(),
                            argument.name);
                        if (found == signature->parameter_names.end())
                        {
                            continue;
                        }
                        parameter = static_cast<std::size_t>(
                            std::distance(
                                signature->parameter_names.begin(),
                                found));
                    }
                    if (parameter >=
                        signature->parameter_types.size())
                    {
                        if (signature->variadic &&
                            signature->variadic_type != nullptr)
                        {
                            contextualize_result_construction(
                                *argument.value,
                                signature->variadic_type,
                                call->paren);
                        }
                        continue;
                    }
                    contextualize_result_construction(
                        *argument.value,
                        signature->parameter_types[parameter],
                        call->paren);
                }
            }
        }
        else if (const auto *list = dynamic_cast<const ListExpr *>(&expression))
        {
            for (const ExprPtr &element : list->elements)
            {
                resolve_expression(*element);
            }
        }
        else if (const auto *member = dynamic_cast<const MemberAccessExpr *>(&expression))
        {
            resolve_expression(*member->object);
        }
        else if (const auto *index = dynamic_cast<const IndexAccessExpr *>(&expression))
        {
            resolve_expression(*index->object);
            resolve_expression(*index->index);
        }
        else if (const auto *propagation = dynamic_cast<const PropagationExpr *>(&expression))
        {
            resolve_expression(*propagation->operand, true);
            const SemanticTypeRef inner =
                m_analysis.model.m_expression_types.at(propagation->operand.get());
            if (!is_result_type(inner))
            {
                diagnose(propagation->keyword, "LUM-S0031",
                         "propager exige une valeur dont le type est Résultat[T,E]");
            }
            else
            {
                validate_propagation(inner->arguments()[1],
                                     propagation->keyword);
            }
        }
        m_analysis.model.m_expression_types[&expression] = inferred_type(expression);
    }

    void resolve_block(const BlockStmt &block, const bool creates_scope)
    {
        if (creates_scope)
        {
            push_scope();
        }
        for (const StmtPtr &child : block.statements)
        {
            resolve_statement(*child);
        }
        if (creates_scope)
        {
            pop_scope();
        }
    }

    void resolve_statement(const Stmt &statement)
    {
        if (const auto *expression = dynamic_cast<const ExprStmt *>(&statement))
        {
            resolve_expression(
                *expression->expr,
                dynamic_cast<const IdentifierExpr *>(
                    expression->expr.get()) == nullptr);
            if (result_constructor_call(*expression->expr) != nullptr &&
                contains_incomplete_result(
                    m_analysis.model.m_expression_types.at(
                        expression->expr.get())))
            {
                contextualize_result_construction(
                    *expression->expr,
                    *m_analysis.model.find_type("Rien"),
                    static_cast<const CallExpr &>(
                        *expression->expr).paren);
            }
            if (&statement !=
                    m_consumed_expression_statement &&
                is_result_type(
                    m_analysis.model.m_expression_types.at(
                        expression->expr.get())) &&
                !is_assignment_expression(
                    *expression->expr))
            {
                diagnose(
                    expression_site(*expression->expr),
                    "LUM-S0028",
                    "cette valeur Résultat ne peut pas être ignorée");
            }
        }
        else if (const auto *ignorer = dynamic_cast<const IgnorerStmt *>(&statement))
        {
            resolve_expression(*ignorer->expr);
            const SemanticTypeRef inner_type =
                m_analysis.model.m_expression_types.at(ignorer->expr.get());
            if (!is_result_type(inner_type))
            {
                diagnose(ignorer->keyword, "LUM-S0031",
                         "ignorer exige une valeur dont le type est Résultat[T,E]");
            }
        }
        else if (const auto *block = dynamic_cast<const BlockStmt *>(&statement))
        {
            resolve_block(*block, true);
        }
        else if (const auto *variable = dynamic_cast<const VarDeclStmt *>(&statement))
        {
            declare_local(variable->name, SemanticSymbolKind::VARIABLE, variable);
            SemanticTypeRef declared_type;
            if (!variable->type.empty())
            {
                declared_type = resolve_type(variable->type);
            }
            SemanticTypeRef initializer_type;
            if (variable->initializer != nullptr)
            {
                resolve_expression(*variable->initializer);
                if (declared_type != nullptr)
                {
                    contextualize_result_construction(
                        *variable->initializer,
                        declared_type,
                        variable->name);
                }
                initializer_type =
                    m_analysis.model.m_expression_types.at(variable->initializer.get());
            }
            if (declared_type != nullptr && initializer_type != nullptr)
            {
                require_assignable(initializer_type,
                                   declared_type,
                                   variable->name,
                                   "l'initialiseur de '" + variable->name.lexeme + "'");
            }
            if (declared_type == nullptr &&
                contains_incomplete_result(initializer_type))
            {
                diagnose(
                    variable->name,
                    "LUM-S0042",
                    "le type complet de Succès ou Échec ne peut pas être déduit; ajoutez une annotation Résultat[T,E]");
            }
            SemanticTypeRef variable_type = declared_type != nullptr
                                                ? declared_type
                                                : initializer_type;
            if (variable_type == nullptr)
            {
                variable_type = *m_analysis.model.find_type("Rien");
            }
            if (m_scopes.empty())
            {
                if (auto found = m_analysis.model.m_value_symbols.find(variable->name.lexeme);
                    found != m_analysis.model.m_value_symbols.end() &&
                    found->second.declaration == variable)
                {
                    found->second.type = variable_type;
                }
                if (variable->initializer != nullptr)
                {
                    if (const CallableSignature *callable =
                            callable_signature(*variable->initializer))
                    {
                        m_analysis.model.m_named_signatures[variable->name.lexeme] =
                            *callable;
                    }
                }
            }
            else
            {
                set_local_type(variable->name.lexeme, variable_type);
                LocalBinding *binding =
                    find_local_value_mutable(variable->name.lexeme);
                if (variable->initializer != nullptr)
                {
                    if (const CallableSignature *callable =
                            callable_signature(*variable->initializer))
                    {
                        binding->callable = *callable;
                    }
                }
                if (is_result_type(variable_type) &&
                    variable->initializer != nullptr)
                {
                    if (const auto *source = dynamic_cast<const IdentifierExpr *>(
                            variable->initializer.get());
                        source != nullptr)
                    {
                        if (LocalBinding *source_binding =
                                find_local_value_mutable(source->name.lexeme))
                        {
                            binding->obligation = source_binding->obligation;
                            if (binding->obligation.has_value())
                            {
                                m_obligations.at(*binding->obligation).active = true;
                            }
                        }
                    }
                    if (!binding->obligation.has_value())
                    {
                        create_obligation(*binding, variable->name);
                    }
                }
            }
        }
        else if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(&statement))
        {
            declare_local(function->name, SemanticSymbolKind::FUNCTION, function);
            resolve_function(*function);
        }
        else if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(&statement))
        {
            declare_local(klass->name, SemanticSymbolKind::CLASS, klass);
            if (std::any_of(
                    klass->interfaces.begin(),
                    klass->interfaces.end(),
                    [](const TypeExpr &interface) {
                        return interface.kind == TypeExprKind::NAMED &&
                               interface.name == "Erreur";
                    }))
            {
                m_error_types.insert(klass->name.lexeme);
            }
            if (!m_type_scopes.empty())
            {
                if (!m_type_scopes.back()
                         .emplace(klass->name.lexeme,
                                  m_analysis.model.types.class_type(
                                      klass->name.lexeme))
                         .second)
                {
                    diagnose(klass->name, "LUM-S0002",
                             "le type '" + klass->name.lexeme +
                                 "' est déjà déclaré dans cette portée");
                }
            }
            static_cast<void>(resolve_constructor(*klass));
            if (!klass->parent.empty())
            {
                const SemanticTypeRef parent = resolve_type(klass->parent);
                if (parent->kind() != SemanticTypeKind::CLASS &&
                    parent->kind() != SemanticTypeKind::BOTTOM)
                {
                    diagnose(klass->parent.source, "LUM-S0009",
                             "une classe ne peut hériter que d'une autre classe");
                }
            }
            for (const TypeExpr &interface : klass->interfaces)
            {
                const SemanticTypeRef resolved = resolve_type(interface);
                if (resolved->kind() != SemanticTypeKind::INTERFACE &&
                    resolved->kind() != SemanticTypeKind::BOTTOM)
                {
                    diagnose(interface.source, "LUM-S0010",
                             "'réalise' attend un type interface");
                }
            }
            m_class_stack.push_back(klass);
            push_scope();
            for (const StmtPtr &member : klass->members)
            {
                resolve_statement(*member);
            }
            pop_scope();
            m_class_stack.pop_back();
            validate_class_result_contracts(*klass);
        }
        else if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(&statement))
        {
            declare_local(interface->name, SemanticSymbolKind::INTERFACE, interface);
            if (!m_type_scopes.empty())
            {
                if (!m_type_scopes.back()
                         .emplace(interface->name.lexeme,
                                  m_analysis.model.types.interface_type(
                                      interface->name.lexeme))
                         .second)
                {
                    diagnose(interface->name, "LUM-S0002",
                             "le type '" + interface->name.lexeme +
                                 "' est déjà déclaré dans cette portée");
                }
            }
            push_scope();
            for (const StmtPtr &member : interface->methods)
            {
                resolve_statement(*member);
            }
            pop_scope();
        }
        else if (const auto *alias = dynamic_cast<const TypeAliasDeclStmt *>(&statement))
        {
            if (!m_scopes.empty())
            {
                diagnose(alias->name, "LUM-S0022",
                         "un alias de type ne peut être déclaré qu'au niveau du module");
            }
        }
        else if (const auto *import = dynamic_cast<const ImportStmt *>(&statement))
        {
            if (!m_scopes.empty())
            {
                resolve_import(*import, false);
            }
        }
        else if (const auto *conditional = dynamic_cast<const IfStmt *>(&statement))
        {
            resolve_expression(*conditional->condition);
            reject_result_condition(
                *conditional->condition,
                condition_site(*conditional->condition));
            const ObligationState baseline = obligation_state();
            resolve_statement(*conditional->then_branch);
            const ObligationState then_state = obligation_state();
            restore_obligation_state(baseline);
            if (conditional->else_branch != nullptr)
            {
                resolve_statement(*conditional->else_branch);
            }
            const ObligationState else_state = obligation_state();
            merge_obligation_states(baseline, {then_state, else_state});
        }
        else if (const auto *loop = dynamic_cast<const ForStmt *>(&statement))
        {
            resolve_expression(*loop->iterable);
            const ObligationState baseline = obligation_state();
            m_loop_obligation_baselines.push_back(baseline);
            ++m_loop_depth;
            push_scope();
            declare_local(loop->variable, SemanticSymbolKind::VARIABLE);
            if (const auto *body = dynamic_cast<const BlockStmt *>(loop->body.get()))
            {
                resolve_block(*body, false);
            }
            else
            {
                resolve_statement(*loop->body);
            }
            pop_scope();
            --m_loop_depth;
            m_loop_obligation_baselines.pop_back();
            const ObligationState body_state = obligation_state();
            merge_obligation_states(baseline, {baseline, body_state});
        }
        else if (const auto *loop = dynamic_cast<const WhileStmt *>(&statement))
        {
            resolve_expression(*loop->condition);
            reject_result_condition(
                *loop->condition,
                condition_site(*loop->condition));
            const ObligationState baseline = obligation_state();
            m_loop_obligation_baselines.push_back(baseline);
            ++m_loop_depth;
            resolve_statement(*loop->body);
            --m_loop_depth;
            m_loop_obligation_baselines.pop_back();
            const ObligationState body_state = obligation_state();
            merge_obligation_states(baseline, {baseline, body_state});
        }
        else if (const auto *continuation =
                     dynamic_cast<const ContinueStmt *>(&statement))
        {
            // Outside a loop these used to reach the runtime, where the tree
            // walker threw a signal nothing caught: the process aborted with
            // "terminate called after throwing an instance of
            // 'lumiere::ContinueSignal'". A C++ exception name is not a
            // diagnostic, and an abort is not a way to reject a program.
            if (m_loop_depth == 0)
            {
                diagnose(continuation->keyword, "LUM-S0053",
                         "'continuer' doit être placé dans une boucle");
            }
            diagnose_new_loop_obligations(
                continuation->keyword);
        }
        else if (const auto *interruption =
                     dynamic_cast<const BreakStmt *>(&statement))
        {
            if (m_loop_depth == 0)
            {
                diagnose(interruption->keyword, "LUM-S0052",
                         "'arrêter' doit être placé dans une boucle");
            }
        }
        else if (const auto *return_statement = dynamic_cast<const ReturnStmt *>(&statement))
        {
            if (m_callable_stack.empty())
            {
                diagnose(return_statement->keyword, "LUM-S0008",
                         "'retourne' doit être placé dans une fonction");
            }
            else
            {
                m_analysis.model.m_return_owners.emplace(return_statement, m_callable_stack.back());
            }
            SemanticTypeRef returned_type = *m_analysis.model.find_type("Rien");
            if (return_statement->value != nullptr)
            {
                resolve_expression(*return_statement->value);
                if (const CallableSignature *expected =
                        current_callable_signature();
                    expected != nullptr &&
                    expected->has_explicit_return_type)
                {
                    contextualize_result_construction(
                        *return_statement->value,
                        expected->return_type,
                        return_statement->keyword);
                }
                returned_type =
                    m_analysis.model.m_expression_types.at(return_statement->value.get());
            }
            if (!m_callable_stack.empty())
            {
                const CallableOwner &owner = m_callable_stack.back();
                CallableSignature *signature =
                    owner.declaration != nullptr
                        ? &m_analysis.model.m_signatures.at(owner.declaration)
                        : &m_analysis.model.m_expression_signatures.at(owner.expression);
                if (signature != nullptr)
                {
                    if (!signature->has_explicit_return_type)
                    {
                        if (is_result_type(returned_type))
                        {
                            diagnose(return_statement->keyword, "LUM-S0032",
                                     "une fonction retournant Résultat doit déclarer ce type explicitement");
                        }
                        if (same_type(signature->return_type,
                                      *m_analysis.model.find_type("Rien")))
                        {
                            signature->return_type = returned_type;
                        }
                        else if (!is_assignable(returned_type,
                                                signature->return_type))
                        {
                            signature->return_type =
                                m_analysis.model.types.union_type(
                                    {signature->return_type, returned_type});
                        }
                    }
                    else
                    {
                        if (is_result_type(returned_type) &&
                            signature->return_type->kind() ==
                                SemanticTypeKind::BUILTIN &&
                            signature->return_type->name() ==
                                "Universel")
                        {
                            diagnose(
                                return_statement->keyword,
                                "LUM-S0037",
                                "un résultat ne peut pas être effacé dans Universel au retour");
                        }
                        require_assignable(returned_type,
                                           signature->return_type,
                                           return_statement->keyword,
                                           "la valeur retournée");
                    }
                }
            }
            diagnose_active_obligations(return_statement->keyword);
        }
        else if (const auto *match = dynamic_cast<const AgirSelonStmt *>(&statement))
        {
            resolve_match(*match);
        }
    }

    void reject_result_condition(
        const Expr &condition,
        const Token &site)
    {
        if (is_result_type(
                m_analysis.model.m_expression_types.at(
                    &condition)))
        {
            diagnose(
                site,
                "LUM-S0041",
                "Résultat ne peut pas être utilisé comme condition; traitez-le avec 'agir selon'");
        }
    }

    static Token condition_site(
        const Expr &condition)
    {
        if (const auto *identifier =
                dynamic_cast<const IdentifierExpr *>(
                    &condition))
        {
            return identifier->name;
        }
        if (const auto *call =
                dynamic_cast<const CallExpr *>(&condition))
        {
            return call->paren;
        }
        if (const auto *unary =
                dynamic_cast<const UnaryExpr *>(&condition))
        {
            return unary->op;
        }
        if (const auto *binary =
                dynamic_cast<const BinaryExpr *>(&condition))
        {
            return binary->op;
        }
        return Token(
            TokenType::IDENT,
            "<condition>",
            0,
            0);
    }

    static Token expression_site(
        const Expr &expression)
    {
        if (const auto *call =
                dynamic_cast<const CallExpr *>(&expression))
        {
            return call->paren;
        }
        if (const auto *propagation =
                dynamic_cast<const PropagationExpr *>(
                    &expression))
        {
            return propagation->keyword;
        }
        if (const auto *match =
                dynamic_cast<const AgirSelonStmt *>(
                    &expression))
        {
            return match->keyword;
        }
        if (const auto *identifier =
                dynamic_cast<const IdentifierExpr *>(
                    &expression))
        {
            return identifier->name;
        }
        return condition_site(expression);
    }

    static bool is_assignment_expression(
        const Expr &expression)
    {
        const auto *binary =
            dynamic_cast<const BinaryExpr *>(
                &expression);
        return binary != nullptr &&
               binary->op.type == TokenType::EGAL;
    }

    const CallExpr *transparent_result_call(
        const Expr &expression) const
    {
        if (const auto *call =
                dynamic_cast<const CallExpr *>(&expression))
        {
            return call;
        }
        if (const auto *cast =
                dynamic_cast<const CastExpr *>(&expression))
        {
            return transparent_result_call(
                *cast->operand);
        }
        if (const auto *check =
                dynamic_cast<const TypeCheckExpr *>(&expression))
        {
            return transparent_result_call(
                *check->operand);
        }
        return nullptr;
    }

    void validate_class_result_contracts(
        const ClassDeclStmt &klass)
    {
        const auto validate =
            [&](const FunctionDeclStmt &implementation,
                const FunctionDeclStmt &contract) {
                const CallableSignature &actual =
                    ensure_signature(implementation);
                const CallableSignature &required =
                    ensure_signature(contract);
                if (is_result_type(required.return_type) &&
                    (!actual.has_explicit_return_type ||
                     !is_result_type(actual.return_type) ||
                     !is_assignable(
                         actual.return_type,
                         required.return_type)))
                {
                    diagnose(
                        implementation.name,
                        "LUM-S0040",
                        "la méthode '" +
                            implementation.name.lexeme +
                            "' doit préserver le retour " +
                            std::string(
                                required.return_type->display()) +
                            " déclaré par son contrat");
                }
            };

        if (!klass.parent.empty())
        {
            const SemanticSymbol *parent_symbol =
                m_analysis.model.find_value(
                    klass.parent.name);
            const auto *parent =
                parent_symbol == nullptr
                    ? nullptr
                    : dynamic_cast<const ClassDeclStmt *>(
                          parent_symbol->declaration);
            if (parent != nullptr)
            {
                for (const StmtPtr &member : klass.members)
                {
                    const auto *method =
                        dynamic_cast<const FunctionDeclStmt *>(
                            member.get());
                    if (method == nullptr)
                    {
                        continue;
                    }
                    if (const FunctionDeclStmt *contract =
                            find_method(
                                *parent,
                                method->name.lexeme))
                    {
                        validate(*method, *contract);
                    }
                }
            }
        }

        for (const TypeExpr &interface_type :
             klass.interfaces)
        {
            const SemanticSymbol *interface_symbol =
                m_analysis.model.find_value(
                    interface_type.name);
            const auto *interface =
                interface_symbol == nullptr
                    ? nullptr
                    : dynamic_cast<const InterfaceDeclStmt *>(
                          interface_symbol->declaration);
            if (interface == nullptr)
            {
                continue;
            }
            for (const StmtPtr &member :
                 interface->methods)
            {
                const auto *contract =
                    dynamic_cast<const FunctionDeclStmt *>(
                        member.get());
                if (contract == nullptr)
                {
                    continue;
                }
                if (const FunctionDeclStmt *implementation =
                        find_method(
                            klass,
                            contract->name.lexeme))
                {
                    validate(*implementation, *contract);
                }
            }
        }
    }

    void resolve_signatures_and_annotations(const StmtList &statements)
    {
        for (const StmtPtr &statement : statements)
        {
            resolve_statement(*statement);
        }
    }

    void validate_entry_point(const StmtList &statements)
    {
        bool found = false;
        for (const StmtPtr &statement : statements)
        {
            const auto *function =
                dynamic_cast<const FunctionDeclStmt *>(statement.get());
            if (function == nullptr ||
                function->name.lexeme != "principal")
            {
                continue;
            }

            found = true;
            const CallableSignature &signature =
                ensure_signature(*function);
            if (!signature.parameter_types.empty())
            {
                diagnose(
                    function->name,
                    "LUM-S0034",
                    "'principal' ne peut pas déclarer de paramètres");
            }
            const SemanticTypeRef rien =
                *m_analysis.model.find_type("Rien");
            if (!same_type(signature.return_type, rien) &&
                !is_result_type(signature.return_type))
            {
                diagnose(
                    function->name,
                    "LUM-S0035",
                    "'principal' doit retourner Rien ou Résultat[Rien, E]");
            }
            if (is_result_type(signature.return_type) &&
                !is_assignable(
                    signature.return_type->arguments()[0],
                    rien))
            {
                diagnose(
                    function->name,
                    "LUM-S0035",
                    "le succès de 'principal' doit être de type Rien");
            }
            return;
        }

        // Only when the file is about to be run. Both engines execute a file's
        // top-level code, but only the VM used to insist on an entry point
        // afterwards, so the same file ran under one engine and was refused by
        // the other. Deciding it here means they are told the same thing at the
        // same moment, before either of them starts.
        if (found || !m_options.require_entry_point)
        {
            return;
        }
        m_analysis.diagnostics.push_back({
            "LUM-S0051",
            DiagnosticSeverity::ERROR_LEVEL,
            "ce programme n'a pas de point d'entrée : ajoutez une fonction 'principal'",
            m_source_path,
            SourceRange{},
        });
    }

    const CallableSignature &resolve_constructor(const ClassDeclStmt &klass)
    {
        if (const auto found = m_analysis.model.m_constructors.find(&klass);
            found != m_analysis.model.m_constructors.end())
        {
            return found->second;
        }
        if (!m_constructor_stack.insert(&klass).second)
        {
            diagnose(klass.name, "LUM-S0011",
                     "cycle d'héritage détecté pour la classe '" + klass.name.lexeme + "'");
            CallableSignature invalid;
            invalid.return_type = m_analysis.model.types.bottom();
            return m_analysis.model.m_constructors.emplace(&klass, std::move(invalid)).first->second;
        }

        CallableSignature signature;
        if (!klass.parent.empty())
        {
            if (const ClassDeclStmt *parent_decl =
                    class_declaration(klass.parent.name))
            {
                signature = resolve_constructor(*parent_decl);
            }
            else if (const CallableSignature *parent = find_named_signature(klass.parent.name))
            {
                // Imported classes expose their constructor contract, not their AST.
                signature = *parent;
            }
        }

        for (const StmtPtr &member : klass.members)
        {
            const auto *field = dynamic_cast<const VarDeclStmt *>(member.get());
            if (field == nullptr)
            {
                continue;
            }
            const SemanticTypeRef field_type = resolve_type(field->type);
            const auto existing = std::find(signature.parameter_names.begin(),
                                            signature.parameter_names.end(),
                                            field->name.lexeme);
            if (existing == signature.parameter_names.end())
            {
                signature.parameter_names.push_back(field->name.lexeme);
                signature.parameter_types.push_back(field_type);
                signature.optional_parameters.push_back(field->initializer != nullptr);
            }
            else
            {
                const std::size_t index = static_cast<std::size_t>(
                    std::distance(signature.parameter_names.begin(), existing));
                signature.parameter_types[index] = field_type;
                signature.optional_parameters[index] = field->initializer != nullptr;
            }
        }

        signature.has_explicit_return_type = true;
        if (const SemanticTypeRef *class_type = find_type(klass.name.lexeme))
        {
            signature.return_type = *class_type;
        }
        else
        {
            signature.return_type = m_analysis.model.types.bottom();
        }
        m_constructor_stack.erase(&klass);
        return m_analysis.model.m_constructors.emplace(&klass, std::move(signature)).first->second;
    }

    void resolve_constructors(const StmtList &statements)
    {
        for (const StmtPtr &statement : statements)
        {
            if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(statement.get()))
            {
                static_cast<void>(resolve_constructor(*klass));
            }
        }
    }

    std::string m_source_path;
    SemanticAnalysis m_analysis;
    std::vector<std::unordered_map<std::string, LocalBinding>> m_scopes;
    std::vector<std::unordered_map<std::string, SemanticTypeRef>> m_type_scopes;
    std::vector<std::unordered_map<std::string, CallableSignature>> m_signature_scopes;
    std::vector<CallableOwner> m_callable_stack;
    // How many loops and how many methods enclose the statement being resolved.
    // Both reset across a function boundary; see enter_callable_body.
    std::size_t m_loop_depth = 0;
    std::size_t m_method_depth = 0;
    std::vector<const ClassDeclStmt *> m_class_stack;
    std::unordered_set<const ClassDeclStmt *> m_constructor_stack;
    std::unordered_map<std::string, CallableSignature>
        m_builtin_member_signatures;
    /** Keyed by receiver type and member, since the result type follows the receiver. */
    std::map<std::pair<const SemanticType *, std::string>, CallableSignature>
        m_collection_member_signatures;
    SemanticAnalysisOptions m_options;
    const Stmt *m_consumed_expression_statement = nullptr;
    std::unordered_map<std::size_t, ResultObligation> m_obligations;
    std::unordered_set<std::size_t>
        m_obligations_diagnosed_on_exit;
    std::size_t m_next_obligation = 0;
    std::unordered_map<std::string, const TypeAliasDeclStmt *> m_aliases;
    std::vector<std::string> m_alias_order;
    std::unordered_map<std::string, SemanticTypeRef> m_resolved_aliases;
    std::vector<std::string> m_alias_stack;
    std::unordered_set<std::string> m_private_type_names;
    std::unordered_set<std::string> m_error_types;
    std::vector<ObligationState> m_loop_obligation_baselines;
    const SemanticImportEnvironment &m_imports;
};

const SemanticTypeRef *SemanticModel::find_type(const std::string_view name) const
{
    const auto found = m_type_symbols.find(std::string(name));
    return found == m_type_symbols.end() ? nullptr : &found->second;
}

const SemanticSymbol *SemanticModel::find_value(const std::string_view name) const
{
    const auto found = m_value_symbols.find(std::string(name));
    return found == m_value_symbols.end() ? nullptr : &found->second;
}

const CallableSignature *SemanticModel::signature(const FunctionDeclStmt &function) const
{
    const auto found = m_signatures.find(&function);
    return found == m_signatures.end() ? nullptr : &found->second;
}

const CallableSignature *SemanticModel::signature(const FunctionExpr &function) const
{
    const auto found = m_expression_signatures.find(&function);
    return found == m_expression_signatures.end() ? nullptr : &found->second;
}

const CallableSignature *SemanticModel::constructor(const ClassDeclStmt &klass) const
{
    const auto found = m_constructors.find(&klass);
    return found == m_constructors.end() ? nullptr : &found->second;
}

const CallableSignature *SemanticModel::signature(const std::string_view qualified_name) const
{
    const auto found = m_named_signatures.find(std::string(qualified_name));
    return found == m_named_signatures.end() ? nullptr : &found->second;
}

const CallableOwner *SemanticModel::enclosing_callable(const ReturnStmt &statement) const
{
    const auto found = m_return_owners.find(&statement);
    return found == m_return_owners.end() ? nullptr : &found->second;
}

const SemanticTypeRef *SemanticModel::type_of(const Expr &expression) const
{
    const auto found = m_expression_types.find(&expression);
    return found == m_expression_types.end() ? nullptr : &found->second;
}

bool SemanticAnalysis::has_errors() const noexcept
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::ERROR_LEVEL;
    });
}

SemanticAnalysis analyze_semantics(const StmtList &statements,
                                   std::string source_path,
                                   const SemanticImportEnvironment &imports,
                                   const SemanticAnalysisOptions options)
{
    return SemanticAnalyzer(
               std::move(source_path),
               imports,
               options)
        .analyze(statements);
}

SemanticModuleExports collect_semantic_exports(const StmtList &statements, const SemanticModel &model)
{
    SemanticModuleExports exports;
    for (const StmtPtr &statement : statements)
    {
        if (const auto *variable = dynamic_cast<const VarDeclStmt *>(statement.get()))
        {
            if (variable->is_public)
            {
                exports.values.emplace(variable->name.lexeme, SemanticSymbolKind::VARIABLE);
                if (!variable->type.empty())
                {
                    exports.value_types.emplace(variable->name.lexeme,
                                                variable->type);
                }
                else if (const auto *literal =
                             dynamic_cast<const LiteralExpr *>(
                                 variable->initializer.get()))
                {
                    Token type_token = variable->name;
                    switch (literal->token.type)
                    {
                    case TokenType::ENTIER_LIT: type_token.lexeme = "Entier"; break;
                    case TokenType::DECIMAL_LIT: type_token.lexeme = "Décimal"; break;
                    case TokenType::TEXTE_LIT: type_token.lexeme = "Texte"; break;
                    case TokenType::SYMBOLE_LIT: type_token.lexeme = "Symbole"; break;
                    case TokenType::VRAI:
                    case TokenType::FAUX: type_token.lexeme = "Logique"; break;
                    case TokenType::RIEN: type_token.lexeme = "Rien"; break;
                    default: type_token.lexeme.clear(); break;
                    }
                    if (!type_token.lexeme.empty())
                    {
                        exports.value_types.emplace(
                            variable->name.lexeme,
                            TypeExpr::named(std::move(type_token)));
                    }
                }
            }
        }
        else if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(statement.get()))
        {
            if (function->is_public)
            {
                exports.values.emplace(function->name.lexeme, SemanticSymbolKind::FUNCTION);
                SemanticModuleExports::Callable callable;
                callable.has_explicit_return_type = !function->return_type.empty();
                callable.return_type = function->return_type;
                for (const Parameter &parameter : function->params)
                {
                    callable.parameter_names.push_back(parameter.name);
                    callable.parameter_types.push_back(parameter.type);
                    callable.optional_parameters.push_back(parameter.default_value != nullptr);
                }
                exports.callables.emplace(function->name.lexeme, std::move(callable));
            }
        }
        else if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(statement.get()))
        {
            if (klass->is_public)
            {
                exports.types.emplace(klass->name.lexeme, SemanticTypeKind::CLASS);
                exports.values.emplace(klass->name.lexeme, SemanticSymbolKind::CLASS);
                if (const CallableSignature *signature = model.constructor(*klass))
                {
                    SemanticModuleExports::Callable callable;
                    callable.parameter_names = signature->parameter_names;
                    callable.resolved_parameter_types = signature->parameter_types;
                    callable.optional_parameters = signature->optional_parameters;
                    callable.return_type = TypeExpr::named(klass->name);
                    callable.has_explicit_return_type = true;
                    exports.callables.emplace(klass->name.lexeme, std::move(callable));
                }
                if (std::any_of(
                        klass->interfaces.begin(), klass->interfaces.end(),
                        [](const TypeExpr &interface) {
                            return interface.kind == TypeExprKind::NAMED &&
                                   interface.name == "Erreur";
                        }))
                {
                    exports.error_types.insert(klass->name.lexeme);
                }
            }
        }
        else if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(statement.get()))
        {
            if (interface->is_public)
            {
                exports.types.emplace(interface->name.lexeme, SemanticTypeKind::INTERFACE);
                exports.values.emplace(interface->name.lexeme, SemanticSymbolKind::INTERFACE);
            }
        }
        else if (const auto *alias = dynamic_cast<const TypeAliasDeclStmt *>(statement.get()))
        {
            if (alias->is_public)
            {
                exports.aliases.emplace(alias->name.lexeme, alias->target);
                if (const SemanticTypeRef *resolved =
                        model.find_type(alias->name.lexeme))
                {
                    exports.resolved_aliases.emplace(
                        alias->name.lexeme,
                        *resolved);
                }
            }
        }
    }
    std::unordered_set<std::string> error_markers{"Erreur"};
    bool marker_changed = true;
    while (marker_changed)
    {
        marker_changed = false;
        for (const StmtPtr &statement : statements)
        {
            const auto *alias =
                dynamic_cast<const TypeAliasDeclStmt *>(
                    statement.get());
            if (alias != nullptr &&
                alias->target.kind == TypeExprKind::NAMED &&
                error_markers.contains(
                    alias->target.name) &&
                error_markers.insert(
                    alias->name.lexeme).second)
            {
                marker_changed = true;
            }
        }
    }
    for (const StmtPtr &statement : statements)
    {
        const auto *klass =
            dynamic_cast<const ClassDeclStmt *>(
                statement.get());
        if (klass != nullptr && klass->is_public &&
            std::any_of(
                klass->interfaces.begin(),
                klass->interfaces.end(),
                [&](const TypeExpr &interface) {
                    return interface.kind ==
                               TypeExprKind::NAMED &&
                           error_markers.contains(
                               interface.name);
                }))
        {
            exports.error_types.insert(
                klass->name.lexeme);
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const StmtPtr &statement : statements)
        {
            const auto *klass =
                dynamic_cast<const ClassDeclStmt *>(
                    statement.get());
            if (klass == nullptr || !klass->is_public ||
                klass->parent.empty() ||
                exports.error_types.contains(
                    klass->name.lexeme))
            {
                continue;
            }
            if (exports.error_types.contains(
                    klass->parent.name))
            {
                exports.error_types.insert(
                    klass->name.lexeme);
                changed = true;
            }
        }
    }
    return exports;
}

} // namespace lumiere
