
#include <cmath>
#include <filesystem>
#include <limits>
#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

namespace lumiere
{

    namespace
    {

        std::size_t required_parameter_count(const std::vector<Parameter> &params)
        {
            std::size_t required = 0;
            for (const Parameter &parameter : params)
            {
                if (!parameter.default_value)
                {
                    ++required;
                }
            }
            return required;
        }

    } // namespace

    // Backend payload for a user-defined function value.
    //
    // The current tree-walker scope chain (m_env -> parent -> ...) only tells
    // us where execution is happening *right now*. That is enough while we are
    // still inside a block, but not for closures:
    //
    //     ```fonction fabriquer() {
    //       soit x = 42
    //       retourne fonction() -> Entier {
    //         retourne x
    //       }
    //     }```
    //
    //
    // After fabriquer() returns, its local scope is no longer the active m_env.
    // The returned function still needs that old scope so it can resolve x when
    // called later. This payload is where the tree walker stores the function's
    // lexical context, separate from the interpreter's current execution frame.
    //
    // On call, the tree walker creates a fresh environment for the callee's
    // parameters and locals, then chains that new frame to the saved closure.
    // Name lookup therefore sees:
    //   1. the call's locals/parameters
    //   2. the function's captured outer scope(s)
    //   3. enclosing parents beyond that
    //
    // Exactly one of decl or expr is expected to be set.
    struct TreeWalker::TreeWalkerFunctionBody : RuntimeFunctionBody
    {
        // AST node for a named declaration such as `fonction f(...) ...`.
        // The tree walker needs this later to re-enter the declared body and
        // inspect its parameters/defaults when the function is invoked.
        FunctionDeclStmt *decl = nullptr;
        // AST node for an anonymous / expression-form function, for example
        // `soit f = fonction(x: Entier) -> Entier { retourne x + 1 }`.
        // This plays the same role as decl, but for inline function values.
        FunctionExpr *expr = nullptr;
        // Points to the Environment that was current when this function value
        // was created. In:
        // `soit f = fonction() -> Entier { retourne x }`
        // this points at the environment that contains `x`, not at the future
        // call-time environment for `f()`. When `f` is invoked, the interpreter
        // allocates a new Environment for the call's parameters and locals, and
        // sets that new frame's parent to this saved environment.
        Environment *closure = nullptr;
        // Owning reference that keeps the captured environment alive even after
        // the defining scope has otherwise been exited. closure is a raw pointer
        // because the environment-walking code wants direct pointer access;
        // closure_owner exists so that raw pointer never dangles.
        std::shared_ptr<Environment> closure_owner;
    };

    struct TreeWalker::TreeWalkerClassBody : RuntimeClassBody
    {
        ClassDeclStmt *decl = nullptr;
    };

    struct TreeWalker::TreeWalkerInterfaceBody : RuntimeInterfaceBody
    {
        InterfaceDeclStmt *decl = nullptr;
    };

    TreeWalker::TreeWalker() = default;

    TreeWalker::~TreeWalker()
    {
        m_env = nullptr;
        m_env_owner.reset();
    }

    void TreeWalker::execute(Program &program)
    {
        m_env_owner = std::make_shared<Environment>();
        m_env = m_env_owner.get();
        m_result = Value::rien();
        m_self = Value::rien();
        m_current_source_path = program.source_path;
        m_current_source_text = program.source_text;
        m_stack_trace.clear();

        try
        {
            // principal is the entry point but there might be some top level
            //  statements that set up the world, so we make sure to execute those first
            //  before entering principal
            for (auto &statement : program.statements)
            {
                execute(*statement);
            }

            if (m_env->contains("principal"))
            {
                const Value principal = m_env->get("principal");
                if (principal.is_fonction())
                {
                    const std::vector<RuntimeArgument> principal_args;
                    call(principal, NativeArgs{nullptr, &principal_args, RuntimeSite{m_current_source_path, 0, 0}});
                }
            }
        }
        catch (const ThrownSignal &signal)
        {
            throw RuntimeError(
                "exception non attrapee: " + to_texte(signal.value),
                signal.source_path,
                signal.source_text,
                signal.line,
                signal.column,
                signal.stack_trace);
        }
    }

    void TreeWalker::add_import_path(std::filesystem::path path)
    {
        m_import_paths.push_back(std::move(path));
    }

    void TreeWalker::configure_lumitest(LumiTestRuntimeOptions options)
    {
        m_lumitest_options = std::move(options);
    }

    LumiTestRunSummary TreeWalker::lumitest_summary() const
    {
        const auto it = m_modules.find("builtin:LumiTest");
        if (it == m_modules.end() || it->second == nullptr || it->second->state == nullptr)
        {
            return {};
        }

        // casting because it->second->state is std::shared_ptr<RuntimeModuleState>
        const auto state = std::dynamic_pointer_cast<LumiTestModuleState>(it->second->state);
        if (state == nullptr)
        {
            return {};
        }

        return state->summary;
    }

    void TreeWalker::raise_runtime_error(const Token &token, const std::string &message) const
    {
        throw_runtime_error(token, message);
    }

    Value TreeWalker::call(Value callee, const NativeArgs &args)
    {
        if (!callee.is_fonction())
        {
            raise_runtime_error(args.site, "la valeur appelée n'est pas une fonction");
        }

        const auto function = callee.as_fonction();
        if (function == nullptr)
        {
            raise_runtime_error(args.site, "fonction invalide");
        }

        if (function->is_native())
        {
            return function->native_handler(*this, args);
        }

        if (args.arguments == nullptr)
        {
            raise_runtime_error(args.site, "arguments d'appel absents");
        }

        return call_user_function(function, *args.arguments, args.site);
    }

    [[noreturn]] void TreeWalker::raise_runtime_error(const RuntimeSite &site, const std::string &message) const
    {
        throw RuntimeError(message,
                           site.source_path.empty() ? m_current_source_path : site.source_path,
                           m_current_source_text,
                           static_cast<uint32_t>(site.line),
                           static_cast<uint32_t>(site.column),
                           m_stack_trace);
    }

    std::string TreeWalker::to_text(const Value &value) const
    {
        return to_texte(value);
    }

    void TreeWalker::annotate_value(const Value &value, std::string_view type_name, const RuntimeSite &site) const
    {
        register_value_annotation(value,
                                  Token(TokenType::IDENT,
                                        std::string(type_name),
                                        static_cast<uint32_t>(site.line),
                                        static_cast<uint32_t>(site.column)));
    }

    std::string TreeWalker::require_texte_value(const Value &value, const Token &token) const
    {
        return assert_texte(value, token);
    }

    int64_t TreeWalker::require_entier_value(const Value &value, const Token &token) const
    {
        return assert_entier(value, token);
    }

    double TreeWalker::require_decimal_value(const Value &value, const Token &token) const
    {
        return assert_decimal(value, token);
    }

    void TreeWalker::annotate_runtime_value(const Value &value, const Token &annotation) const
    {
        register_value_annotation(value, annotation);
    }

    std::vector<RuntimeArgument> TreeWalker::evaluate_runtime_arguments(const std::vector<Argument> &args)
    {
        std::vector<RuntimeArgument> runtime_args;
        runtime_args.reserve(args.size());

        for (const auto &arg : args)
        {
            runtime_args.push_back(RuntimeArgument{
                arg.name,
                // some args values will be expressions that need to be evaluated
                evaluate(*arg.value),
            });
        }

        return runtime_args;
    }

    std::shared_ptr<LumiereFunction> TreeWalker::make_native_function(LumiereFunction::NativeHandler handler) const
    {
        auto function = std::make_shared<LumiereFunction>();
        function->name = "<native>";
        function->native_handler = std::move(handler);
        function->receiver = Value::rien();
        function->min_arity = 0;
        function->max_arity = 0;
        return function;
    }

    std::shared_ptr<LumiereFunction> TreeWalker::make_native_method(Value receiver,
                                                                    LumiereFunction::NativeHandler handler) const
    {
        auto function = make_native_function(std::move(handler));
        function->receiver = std::move(receiver);
        return function;
    }

    std::shared_ptr<LumiereFunction> TreeWalker::make_declared_function(FunctionDeclStmt &decl,
                                                                        Value receiver,
                                                                        Environment *closure) const
    {
        auto function = std::make_shared<LumiereFunction>();
        function->name = decl.name.lexeme;
        auto body = std::make_shared<TreeWalkerFunctionBody>();
        body->decl = &decl;
        body->closure = closure;
        // Keep the environment chain alive after the declaring scope exits so
        // later calls can still resolve captured names.
        body->closure_owner = m_env_owner;
        function->body = std::move(body);
        function->receiver = std::move(receiver);
        function->min_arity = required_parameter_count(decl.params);
        function->max_arity = decl.params.size();
        return function;
    }

    std::shared_ptr<LumiereFunction> TreeWalker::make_declared_function(FunctionExpr &expr,
                                                                        Value receiver,
                                                                        Environment *closure) const
    {
        auto function = std::make_shared<LumiereFunction>();
        function->name = "<anonyme>";
        auto body = std::make_shared<TreeWalkerFunctionBody>();
        body->expr = &expr;
        body->closure = closure;
        // Same lifetime rule as named functions: the closure may outlive the
        // scope that created this anonymous function value.
        body->closure_owner = m_env_owner;
        function->body = std::move(body);
        function->receiver = std::move(receiver);
        function->min_arity = required_parameter_count(expr.params);
        function->max_arity = expr.params.size();
        return function;
    }

    std::shared_ptr<LumiereClass> TreeWalker::make_runtime_class(ClassDeclStmt &decl) const
    {
        auto klass = std::make_shared<LumiereClass>();
        klass->name = decl.name.lexeme;
        auto body = std::make_shared<TreeWalkerClassBody>();
        body->decl = &decl;
        klass->body = std::move(body);
        return klass;
    }

    std::shared_ptr<LumiereInterface> TreeWalker::make_runtime_interface(InterfaceDeclStmt &decl) const
    {
        auto iface = std::make_shared<LumiereInterface>();
        iface->name = decl.name.lexeme;
        auto body = std::make_shared<TreeWalkerInterfaceBody>();
        body->decl = &decl;
        iface->body = std::move(body);
        return iface;
    }

    FunctionDeclStmt *TreeWalker::function_decl(const LumiereFunction &function) const
    {
        auto body = std::dynamic_pointer_cast<TreeWalkerFunctionBody>(function.body);
        return body ? body->decl : nullptr;
    }

    FunctionExpr *TreeWalker::function_expr(const LumiereFunction &function) const
    {
        auto body = std::dynamic_pointer_cast<TreeWalkerFunctionBody>(function.body);
        return body ? body->expr : nullptr;
    }

    Environment *TreeWalker::function_closure(const LumiereFunction &function) const
    {
        auto body = std::dynamic_pointer_cast<TreeWalkerFunctionBody>(function.body);
        return body ? body->closure : nullptr;
    }

    std::shared_ptr<Environment> TreeWalker::function_closure_owner(const LumiereFunction &function) const
    {
        auto body = std::dynamic_pointer_cast<TreeWalkerFunctionBody>(function.body);
        return body ? body->closure_owner : nullptr;
    }

    ClassDeclStmt *TreeWalker::class_decl(const std::shared_ptr<LumiereClass> &klass) const
    {
        if (klass == nullptr)
        {
            return nullptr;
        }

        auto body = std::dynamic_pointer_cast<TreeWalkerClassBody>(klass->body);
        return body ? body->decl : nullptr;
    }

    InterfaceDeclStmt *TreeWalker::interface_decl(const std::shared_ptr<LumiereInterface> &iface) const
    {
        if (iface == nullptr)
        {
            return nullptr;
        }

        auto body = std::dynamic_pointer_cast<TreeWalkerInterfaceBody>(iface->body);
        return body ? body->decl : nullptr;
    }

    Environment *TreeWalker::module_environment(const std::shared_ptr<Module> &module) const
    {
        if (module == nullptr)
        {
            return nullptr;
        }

        auto state = std::dynamic_pointer_cast<TreeWalkerModuleState>(module->state);
        return state ? state->environment.get() : nullptr;
    }

    Value TreeWalker::make_tree_walker_native_method(
        Value receiver,
        NativeMethodHandler handler) const
    {
        // Build the callback signature that native runtime functions expect.
        LumiereFunction::NativeHandler adapted_handler =
            [handler = std::move(handler)](IRuntime &runtime, const NativeArgs &native_args) -> Value
            {
                // This helper only works for the tree-walker runtime.
                auto *walker = dynamic_cast<TreeWalker *>(&runtime);
                if (walker == nullptr)
                {
                    runtime.raise_runtime_error(native_args.site, "methode native non compatible");
                }
                // Turn the call-site coordinates back into a Token for existing checks.
                const Token site_token(TokenType::IDENT,
                                       "",
                                       static_cast<uint32_t>(native_args.site.line),
                                       static_cast<uint32_t>(native_args.site.column));
                return handler(*walker, *native_args.arguments, site_token);
            };

        // Remember which object this method belongs to.
        auto method = make_native_method(std::move(receiver), std::move(adapted_handler));
        return Value::fonction(std::move(method));
    }

    void TreeWalker::require_positional_args(const std::vector<RuntimeArgument> &args,
                                             std::size_t min_count,
                                             std::size_t max_count,
                                             const std::string &signature,
                                             const Token &call_site) const
    {
        if (args.size() < min_count || args.size() > max_count)
        {
            if (min_count == max_count)
            {
                throw_runtime_error(call_site, signature + " requiert exactement " + std::to_string(min_count) + " argument(s)");
            }
            throw_runtime_error(call_site, signature + " requiert entre " + std::to_string(min_count) +
                                               " et " + std::to_string(max_count) + " arguments");
        }

        for (const auto &arg : args)
        {
            if (!arg.name.empty())
            {
                throw_runtime_error(call_site, signature + " n'accepte pas d'arguments nommés");
            }
        }
    }
    Value TreeWalker::evaluate(Expr &expr)
    {
        expr.accept(*this);
        return m_result;
    }

    void TreeWalker::execute(Stmt &stmt)
    {
        stmt.accept(*this);
    }

    void TreeWalker::execute_branch_with_optional_binding(Stmt &body,
                                                          const Token *binding_name,
                                                          const Value *binding_value)
    {
        ScopeGuard guard(m_env, m_env_owner);
        if (binding_name != nullptr && binding_value != nullptr)
        {
            m_env->define(binding_name->lexeme, *binding_value);
        }
        execute(body);
    }

    void TreeWalker::execute_block(BlockStmt &block)
    {
        ScopeGuard guard(m_env, m_env_owner);
        for (auto &statement : block.statements)
        {
            execute(*statement);
        }
    }

    bool TreeWalker::is_truthy(const Value &value) const
    {
        if (value.is_rien())
        {
            return false;
        }

        if (value.is_logique())
        {
            return value.as_logique();
        }

        
        //any non Null object that is not a falsy logique should be truthy
        return true;
    }

    bool TreeWalker::is_equal(const Value &a, const Value &b) const
    {
        if (a.is_liste_fixe() && b.is_liste_fixe())
        {
            const auto left = a.as_liste_fixe();
            const auto right = b.as_liste_fixe();
            if (left->elements.size() != right->elements.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < left->elements.size(); ++i)
            {
                if (!is_equal(left->elements[i], right->elements[i]))
                {
                    return false;
                }
            }
            return true;
        }

        return a == b;
    }

    bool TreeWalker::is_iterable_value(const Value &value) const
    {
        return value.is_liste() || value.is_liste_fixe() || value.is_ensemble() || value.is_texte();
    }

    bool TreeWalker::supports_index_read(const Value &value) const
    {
        return value.is_liste() || value.is_liste_fixe() || value.is_dictionnaire() || value.is_texte();
    }

    bool TreeWalker::supports_mutable_index_assignment(const Value &value) const
    {
        return value.is_liste() || value.is_liste_fixe() || value.is_dictionnaire();
    }

    const std::vector<Value> *TreeWalker::sequence_elements(const Value &value) const
    {
        if (value.is_liste())
        {
            return &value.as_liste()->elements;
        }
        if (value.is_liste_fixe())
        {
            return &value.as_liste_fixe()->elements;
        }
        if (value.is_ensemble())
        {
            return &value.as_ensemble()->elements;
        }

        return nullptr;
    }

    std::vector<Value> *TreeWalker::mutable_sequence_elements(const Value &value) const
    {
        if (value.is_liste())
        {
            return &value.as_liste()->elements;
        }
        if (value.is_liste_fixe())
        {
            return &value.as_liste_fixe()->elements;
        }

        return nullptr;
    }

    std::string TreeWalker::sequence_family_name(const Value &value) const
    {
        if (value.is_liste())
        {
            return "Liste";
        }
        if (value.is_liste_fixe())
        {
            return "ListeFixe";
        }
        if (value.is_ensemble())
        {
            return "Ensemble";
        }

        return "Séquence";
    }

    std::string TreeWalker::runtime_type_name(const Value &value) const
    {
        if (value.is_liste())
        {
            if (const auto it = m_list_constraints.find(value.as_liste().get()); it != m_list_constraints.end())
            {
                return "Liste[" + it->second.element_type + "]";
            }
            return "Liste";
        }

        if (value.is_liste_fixe())
        {
            if (const auto it = m_fixed_list_constraints.find(value.as_liste_fixe().get()); it != m_fixed_list_constraints.end())
            {
                return "ListeFixe[" + it->second.element_type + ", " + std::to_string(it->second.length) + "]";
            }
            return "ListeFixe";
        }

        if (value.is_dictionnaire())
        {
            if (const auto it = m_dict_constraints.find(value.as_dictionnaire().get()); it != m_dict_constraints.end())
            {
                return "Dictionnaire[" + it->second.key_type + ", " + it->second.value_type + "]";
            }
            return "Dictionnaire";
        }

        if (value.is_ensemble())
        {
            if (const auto it = m_set_constraints.find(value.as_ensemble().get()); it != m_set_constraints.end())
            {
                return "Ensemble[" + it->second.element_type + "]";
            }
            return "Ensemble";
        }

        if (value.is_objet())
        {
            const auto object = value.as_objet();
            if (object != nullptr && object->klass != nullptr)
            {
                if (ClassDeclStmt *decl = class_decl(object->klass))
                {
                    return decl->name.lexeme;
                }
            }
            return "Objet";
        }

        return value.type_name();
    }

    std::vector<Value> TreeWalker::enumerate_iterable(const Value &iterable, const Token &site) const
    {
        if (!is_iterable_value(iterable))
        {
            throw_runtime_error(site, "cet object n'est pas iterable");
        }

        if (const auto *elements = sequence_elements(iterable))
        {
            return *elements;
        }

        if (iterable.is_texte())
        {
            std::vector<Value> items;
            for (unsigned char ch : iterable.as_texte())
            {
                items.push_back(Value::symbole(static_cast<char32_t>(ch)));
            }
            return items;
        }

        throw_runtime_error(site, "cet object n'est pas iterable");
    }

    bool TreeWalker::matches_catch_clause(const CatchClause &clause, const Value &thrown_value) const
    {
        return matches_type_name(thrown_value, clause.type_token);
    }

    std::string TreeWalker::to_texte(const Value &value) const
    {
        return value.to_string();
    }

    int64_t TreeWalker::assert_entier(const Value &value, const Token &token) const
    {
        if (!value.is_entier())
        {
            throw_runtime_error(token, "une valeur de type Entier est requise");
        }
        return value.as_entier();
    }

    double TreeWalker::assert_decimal(const Value &value, const Token &token) const
    {
        if (value.is_decimal())
        {
            return value.as_decimal();
        }
        if (value.is_entier())
        {
            return static_cast<double>(value.as_entier());
        }

        throw_runtime_error(token, "une valeur numerique est requise");
    }

    std::string TreeWalker::assert_texte(const Value &value, const Token &token) const
    {
        if (!value.is_texte())
        {
            throw_runtime_error(token, "une valeur de type Texte est requise");
        }
        return value.as_texte();
    }

} // namespace lumiere
