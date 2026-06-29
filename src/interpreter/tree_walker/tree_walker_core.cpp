
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"

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

struct TreeWalker::TreeWalkerFunctionBody : RuntimeFunctionBody
{
    FunctionDeclStmt *decl = nullptr;
    FunctionExpr *expr = nullptr;
    Environment *closure = nullptr;
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

struct TreeWalker::TreeWalkerModuleState : RuntimeModuleState
{
    std::shared_ptr<Environment> environment;
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
        raise_runtime_error(args.site, "la valeur appelee n'est pas une fonction");
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

Value TreeWalker::resolve_native_member(const Value &object, const Token &member) const
{
    auto make_method = [&](std::function<Value(TreeWalker &, const std::vector<RuntimeArgument> &, const Token &)> handler) -> Value {
        return Value::fonction(make_native_method(
            object,
            [handler = std::move(handler)](IRuntime &runtime, const NativeArgs &native_args) -> Value {
                auto *walker = dynamic_cast<TreeWalker *>(&runtime);
                if (walker == nullptr)
                {
                    runtime.raise_runtime_error(native_args.site, "methode native non compatible avec ce backend");
                }
                const Token site_token(TokenType::IDENT,
                                       "",
                                       static_cast<uint32_t>(native_args.site.line),
                                       static_cast<uint32_t>(native_args.site.column));
                return handler(*walker, *native_args.arguments, site_token);
            }));
    };

    auto require_positional_args = [&](const std::vector<RuntimeArgument> &args,
                                       std::size_t min_count,
                                       std::size_t max_count,
                                       const std::string &signature,
                                       const Token &call_site) {
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
                throw_runtime_error(call_site, signature + " n'accepte pas d'arguments nommes");
            }
        }
    };

    Value texte_member = Value::rien();
    if (try_resolve_texte_native_member(
            object,
            member.lexeme,
            [this](Value receiver, LumiereFunction::NativeHandler handler) {
                return make_native_method(std::move(receiver), std::move(handler));
            },
            texte_member))
    {
        return texte_member;
    }

    auto make_sequence_common_methods =
        [&make_method, &member, &require_positional_args](const auto &elements_ptr, const std::string &family_name) -> Value {
            if (member.lexeme == "taille")
            {
                return make_method([elements_ptr, require_positional_args, family_name](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                    require_positional_args(args, 0, 0, family_name + ".taille", call_site);
                    return Value::entier(static_cast<int64_t>(elements_ptr->size()));
                });
            }
            if (member.lexeme == "vide")
            {
                return make_method([elements_ptr, require_positional_args, family_name](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                    require_positional_args(args, 0, 0, family_name + ".vide", call_site);
                    return Value::logique(elements_ptr->empty());
                });
            }
            if (member.lexeme == "contient")
            {
                return make_method([elements_ptr, require_positional_args, family_name](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                    require_positional_args(args, 1, 1, family_name + ".contient", call_site);
                    for (const auto &element : *elements_ptr)
                    {
                        if (walker.is_equal(element, args[0].value))
                        {
                            return Value::logique(true);
                        }
                    }
                    return Value::logique(false);
                });
            }
            if (member.lexeme == "joindre")
            {
                return make_method([elements_ptr, require_positional_args, family_name](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                    require_positional_args(args, 1, 1, family_name + ".joindre", call_site);
                    const std::string separator = walker.assert_texte(args[0].value, call_site);
                    std::string result;
                    for (std::size_t i = 0; i < elements_ptr->size(); ++i)
                    {
                        if (i > 0)
                        {
                            result += separator;
                        }
                        result += walker.to_texte((*elements_ptr)[i]);
                    }
                    return Value::texte(std::move(result));
                });
            }

            return Value::rien();
        };

    if (object.is_liste())
    {
        auto list = object.as_liste();
        if (Value common = make_sequence_common_methods(&list->elements, "Liste"); !common.is_rien())
        {
            return common;
        }
        if (member.lexeme == "ajouter")
        {
            return make_method([list, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 1, 1, "Liste.ajouter", call_site);
                walker.enforce_list_element_constraint(list, args[0].value, call_site, "Liste.ajouter");
                list->elements.push_back(args[0].value);
                return Value::entier(static_cast<int64_t>(list->elements.size()));
            });
        }
        if (member.lexeme == "inserer")
        {
            return make_method([list, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 2, 2, "Liste.inserer", call_site);
                const int64_t position = walker.assert_entier(args[0].value, call_site);
                if (position < 0 || static_cast<std::size_t>(position) > list->elements.size())
                {
                    walker.throw_runtime_error(call_site, "indice d'insertion hors limites");
                }
                walker.enforce_list_element_constraint(list, args[1].value, call_site, "Liste.inserer");
                list->elements.insert(list->elements.begin() + position, args[1].value);
                return Value::entier(position);
            });
        }
        if (member.lexeme == "retirer_a")
        {
            return make_method([list, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 1, 1, "Liste.retirer_a", call_site);
                const int64_t position = walker.assert_entier(args[0].value, call_site);
                if (position < 0 || static_cast<std::size_t>(position) >= list->elements.size())
                {
                    walker.throw_runtime_error(call_site, "indice hors limites");
                }
                Value removed = list->elements[static_cast<std::size_t>(position)];
                list->elements.erase(list->elements.begin() + position);
                return removed;
            });
        }
        if (member.lexeme == "en_liste_fixe")
        {
            return make_method([list, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 1, 1, "Liste.en_liste_fixe", call_site);
                const int64_t length = walker.assert_entier(args[0].value, call_site);
                if (length < 0)
                {
                    walker.throw_runtime_error(call_site, "la taille d'une ListeFixe ne peut pas etre negative");
                }
                if (static_cast<std::size_t>(length) != list->elements.size())
                {
                    walker.throw_runtime_error(call_site, "Liste.en_liste_fixe requiert une liste de taille exacte " + std::to_string(length));
                }

                auto fixed = std::make_shared<ListeFixeData>();
                fixed->elements = list->elements;
                Value result = Value::liste_fixe(std::move(fixed));

                std::string element_type = "Universel";
                if (const auto it = walker.m_list_constraints.find(list.get()); it != walker.m_list_constraints.end())
                {
                    element_type = it->second.element_type;
                }

                walker.register_value_annotation(
                    result,
                    Token(TokenType::IDENT,
                          "ListeFixe[" + element_type + ", " + std::to_string(length) + "]",
                          call_site.line,
                          call_site.column));
                return result;
            });
        }
    }

    if (object.is_liste_fixe())
    {
        auto list = object.as_liste_fixe();
        if (Value common = make_sequence_common_methods(&list->elements, "ListeFixe"); !common.is_rien())
        {
            return common;
        }
        if (member.lexeme == "en_liste")
        {
            return make_method([list, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 0, 0, "ListeFixe.en_liste", call_site);
                auto dynamic = std::make_shared<ListeData>();
                dynamic->elements = list->elements;
                Value result = Value::liste(std::move(dynamic));
                if (const auto it = walker.m_fixed_list_constraints.find(list.get()); it != walker.m_fixed_list_constraints.end())
                {
                    walker.register_value_annotation(
                        result,
                        Token(TokenType::IDENT, "Liste[" + it->second.element_type + "]", call_site.line, call_site.column));
                }
                return result;
            });
        }
    }

    if (object.is_dictionnaire())
    {
        auto dict = object.as_dictionnaire();

        if (member.lexeme == "taille")
        {
            return make_method([dict, require_positional_args](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 0, 0, "Dictionnaire.taille", call_site);
                return Value::entier(static_cast<int64_t>(dict->entries.size()));
            });
        }
        if (member.lexeme == "vide")
        {
            return make_method([dict, require_positional_args](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 0, 0, "Dictionnaire.vide", call_site);
                return Value::logique(dict->entries.empty());
            });
        }
        if (member.lexeme == "contient")
        {
            return make_method([dict, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 1, 1, "Dictionnaire.contient", call_site);
                for (const auto &entry : dict->entries)
                {
                    if (walker.is_equal(entry.first, args[0].value))
                    {
                        return Value::logique(true);
                    }
                }
                return Value::logique(false);
            });
        }
        if (member.lexeme == "cles")
        {
            return make_method([dict, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 0, 0, "Dictionnaire.cles", call_site);
                auto keys = std::make_shared<ListeData>();
                std::string key_type = "Universel";
                if (const auto it = walker.m_dict_constraints.find(dict.get()); it != walker.m_dict_constraints.end())
                {
                    key_type = it->second.key_type;
                }
                for (const auto &entry : dict->entries)
                {
                    keys->elements.push_back(entry.first);
                }
                Value result = Value::liste(std::move(keys));
                walker.register_value_annotation(result, Token(TokenType::IDENT, "Liste[" + key_type + "]", call_site.line, call_site.column));
                return result;
            });
        }
        if (member.lexeme == "valeurs")
        {
            return make_method([dict, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 0, 0, "Dictionnaire.valeurs", call_site);
                auto values = std::make_shared<ListeData>();
                std::string value_type = "Universel";
                if (const auto it = walker.m_dict_constraints.find(dict.get()); it != walker.m_dict_constraints.end())
                {
                    value_type = it->second.value_type;
                }
                for (const auto &entry : dict->entries)
                {
                    values->elements.push_back(entry.second);
                }
                Value result = Value::liste(std::move(values));
                walker.register_value_annotation(result, Token(TokenType::IDENT, "Liste[" + value_type + "]", call_site.line, call_site.column));
                return result;
            });
        }
        if (member.lexeme == "paires")
        {
            return make_method([dict, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 0, 0, "Dictionnaire.paires", call_site);
                auto pairs = std::make_shared<ListeData>();
                std::string key_type = "Universel";
                std::string value_type = "Universel";
                if (const auto it = walker.m_dict_constraints.find(dict.get()); it != walker.m_dict_constraints.end())
                {
                    key_type = it->second.key_type;
                    value_type = it->second.value_type;
                }
                const std::string pair_element_type = key_type == value_type ? key_type : "Universel";

                for (const auto &entry : dict->entries)
                {
                    auto pair = std::make_shared<ListeFixeData>();
                    pair->elements.push_back(entry.first);
                    pair->elements.push_back(entry.second);
                    Value pair_value = Value::liste_fixe(std::move(pair));
                    walker.register_value_annotation(
                        pair_value,
                        Token(TokenType::IDENT,
                              "ListeFixe[" + pair_element_type + ", 2]",
                              call_site.line,
                              call_site.column));
                    pairs->elements.push_back(pair_value);
                }

                Value result = Value::liste(std::move(pairs));
                walker.register_value_annotation(
                    result,
                    Token(TokenType::IDENT,
                          "Liste[ListeFixe[" + pair_element_type + ", 2]]",
                          call_site.line,
                          call_site.column));
                return result;
            });
        }
        if (member.lexeme == "retirer")
        {
            return make_method([dict, require_positional_args](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site) {
                require_positional_args(args, 1, 1, "Dictionnaire.retirer", call_site);
                for (auto it = dict->entries.begin(); it != dict->entries.end(); ++it)
                {
                    if (walker.is_equal(it->first, args[0].value))
                    {
                        Value removed = it->second;
                        dict->entries.erase(it);
                        return removed;
                    }
                }
                walker.throw_runtime_error(call_site, "cle introuvable dans le dictionnaire");
                return Value::rien();
            });
        }
    }

    return Value::rien();
}

std::shared_ptr<Module> TreeWalker::load_builtin_module(const std::string &module_name) const
{
    auto module = std::make_shared<Module>();
    module->name = module_name;
    auto state = std::make_shared<TreeWalkerModuleState>();
    state->environment = std::make_shared<Environment>();
    module->state = state;

    if (module_name == "Chemin")
    {
        register_chemin_module(*module, [this](LumiereFunction::NativeHandler handler) {
            return make_native_function(std::move(handler));
        });
        return module;
    }

    if (module_name == "Fichier")
    {
        register_fichier_module(*module, [this](LumiereFunction::NativeHandler handler) {
            return make_native_function(std::move(handler));
        });
        return module;
    }

    if (module_name == "Texte")
    {
        register_texte_module(*module, [this](LumiereFunction::NativeHandler handler) {
            return make_native_function(std::move(handler));
        });
        return module;
    }

    if (module_name == "Maths")
    {
        register_maths_module(*module, [this](LumiereFunction::NativeHandler handler) {
            return make_native_function(std::move(handler));
        });
        return module;
    }

    if (module_name == "Temps")
    {
        register_temps_module(*module, [this](LumiereFunction::NativeHandler handler) {
            return make_native_function(std::move(handler));
        });
        return module;
    }

    if (module_name == "Aléatoire" || module_name == "Aleatoire")
    {
        register_aleatoire_module(*module, [this](LumiereFunction::NativeHandler handler) {
            return make_native_function(std::move(handler));
        });
        return module;
    }

    if (module_name == "LumiNet")
    {
        register_luminet_module(*module, [this](LumiereFunction::NativeHandler handler) {
            return make_native_function(std::move(handler));
        });
        return module;
    }

    if (module_name == "LumiTest")
    {
        auto lumitest_state = std::make_shared<LumiTestModuleState>();
        lumitest_state->options = m_lumitest_options;
        register_lumitest_module(*module,
                                 [this](LumiereFunction::NativeHandler handler) {
                                     return make_native_function(std::move(handler));
                                 },
                                 lumitest_state);
        return module;
    }

    return nullptr;
}

std::filesystem::path TreeWalker::resolve_module_path(const std::string &module_name) const
{
    std::vector<std::filesystem::path> search_roots = m_import_paths;

    if (!m_current_source_path.empty())
    {
        const std::filesystem::path current_path(m_current_source_path);
        if (current_path.has_parent_path())
        {
            search_roots.push_back(current_path.parent_path());
        }
    }

    for (const auto &root : search_roots)
    {
        std::filesystem::path candidate = root;
        std::size_t segment_start = 0;
        while (segment_start < module_name.size())
        {
            const std::size_t segment_end = module_name.find('.', segment_start);
            const bool is_last = segment_end == std::string::npos;
            const std::string segment = module_name.substr(
                segment_start,
                is_last ? std::string::npos : segment_end - segment_start);

            if (is_last)
            {
                candidate /= segment + ".lum";
                break;
            }

            candidate /= segment;
            segment_start = segment_end + 1;
        }

        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }
    }

    return {};
}

std::string TreeWalker::default_module_alias(const std::string &module_name) const
{
    const std::size_t last_dot = module_name.rfind('.');
    if (last_dot == std::string::npos)
    {
        return module_name;
    }

    return module_name.substr(last_dot + 1);
}

std::shared_ptr<Module> TreeWalker::execute_module(const std::string &module_name,
                                                   const std::filesystem::path &path,
                                                   std::shared_ptr<StmtList> statements,
                                                   std::string source_text)
{
    auto module = std::make_shared<Module>();
    module->name = module_name;
    auto state = std::make_shared<TreeWalkerModuleState>();
    state->environment = std::make_shared<Environment>();
    module->state = state;

    Environment *previous_env = m_env;
    std::shared_ptr<Environment> previous_env_owner = m_env_owner;
    const Value previous_self = m_self;
    const std::string previous_source_path = m_current_source_path;
    const std::string previous_source_text = m_current_source_text;

    m_env = module_environment(module);
    m_env_owner = state->environment;
    m_self = Value::rien();
    m_current_source_path = path.string();
    m_current_source_text = std::move(source_text);

    try
    {
        for (auto &statement : *statements)
        {
            execute(*statement);

            if (auto *var_decl = dynamic_cast<VarDeclStmt *>(statement.get()))
            {
                module->members[var_decl->name.lexeme] = m_env->get(var_decl->name.lexeme);
                if (var_decl->is_public)
                {
                    module->public_members.insert(var_decl->name.lexeme);
                }
            }
            else if (auto *function_decl = dynamic_cast<FunctionDeclStmt *>(statement.get()))
            {
                module->members[function_decl->name.lexeme] = m_env->get(function_decl->name.lexeme);
                if (function_decl->is_public)
                {
                    module->public_members.insert(function_decl->name.lexeme);
                }
            }
            else if (auto *class_decl = dynamic_cast<ClassDeclStmt *>(statement.get()))
            {
                module->members[class_decl->name.lexeme] = m_env->get(class_decl->name.lexeme);
                if (class_decl->is_public)
                {
                    module->public_members.insert(class_decl->name.lexeme);
                }
            }
            else if (auto *interface_decl = dynamic_cast<InterfaceDeclStmt *>(statement.get()))
            {
                module->members[interface_decl->name.lexeme] = m_env->get(interface_decl->name.lexeme);
                if (interface_decl->is_public)
                {
                    module->public_members.insert(interface_decl->name.lexeme);
                }
            }
        }
    }
    catch (...)
    {
        m_env = previous_env;
        m_env_owner = previous_env_owner;
        m_self = previous_self;
        m_current_source_path = previous_source_path;
        m_current_source_text = previous_source_text;
        throw;
    }

    m_env = previous_env;
    m_env_owner = previous_env_owner;
    m_self = previous_self;
    m_current_source_path = previous_source_path;
    m_current_source_text = previous_source_text;
    return module;
}

std::shared_ptr<Module> TreeWalker::load_module(const Token &module_name_token)
{
    const std::string &module_name = module_name_token.lexeme;
    const std::string builtin_cache_key = "builtin:" + module_name;
    if (auto it = m_modules.find(builtin_cache_key); it != m_modules.end())
    {
        return it->second;
    }

    if (std::shared_ptr<Module> builtin = load_builtin_module(module_name))
    {
        m_modules[builtin_cache_key] = builtin;
        return builtin;
    }

    const std::filesystem::path path = resolve_module_path(module_name);
    if (path.empty())
    {
        throw_runtime_error(module_name_token, "module introuvable: " + module_name);
    }

    const std::string cache_key = std::filesystem::weakly_canonical(path).string();
    if (auto it = m_modules.find(cache_key); it != m_modules.end())
    {
        return it->second;
    }

    if (m_loading_modules.count(cache_key) != 0)
    {
        throw_runtime_error(module_name_token, "cycle d'import detecte pour le module " + module_name);
    }

    m_loading_modules.insert(cache_key);
    try
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw_runtime_error(module_name_token, "impossible d'ouvrir le module " + module_name);
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        const std::string source_text = buffer.str();

        Lexer lexer(source_text);
        Parser parser(lexer.tokenise());
        auto statements = std::make_shared<StmtList>(parser.parse());
        if (parser.had_error())
        {
            throw_runtime_error(module_name_token, "erreur de syntaxe dans le module " + module_name);
        }
        m_loaded_module_programmes.push_back(statements);

        auto module = execute_module(module_name, path, statements, source_text);
        m_modules[cache_key] = module;
        m_loading_modules.erase(cache_key);
        return module;
    }
    catch (...)
    {
        m_loading_modules.erase(cache_key);
        throw;
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

ClassDeclStmt *TreeWalker::resolve_parent_class(const ClassDeclStmt &klass) const
{
    if (klass.parent.lexeme.empty() || m_env == nullptr || !m_env->contains(klass.parent.lexeme))
    {
        return nullptr;
    }

    const Value parent = m_env->get(klass.parent.lexeme);
    if (!parent.is_classe())
    {
        return nullptr;
    }

    return class_decl(parent.as_classe());
}

VarDeclStmt *TreeWalker::find_field_decl(ClassDeclStmt &klass, const std::string &name) const
{
    for (auto &member : klass.members)
    {
        if (auto *field = dynamic_cast<VarDeclStmt *>(member.get()))
        {
            if (field->name.lexeme == name)
            {
                return field;
            }
        }
    }

    if (ClassDeclStmt *parent = resolve_parent_class(klass))
    {
        return find_field_decl(*parent, name);
    }

    return nullptr;
}

FunctionDeclStmt *TreeWalker::find_method_decl(ClassDeclStmt &klass, const std::string &name) const
{
    for (auto &member : klass.members)
    {
        if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
        {
            if (method->name.lexeme == name)
            {
                return method;
            }
        }
    }

    if (ClassDeclStmt *parent = resolve_parent_class(klass))
    {
        return find_method_decl(*parent, name);
    }

    return nullptr;
}

FunctionDeclStmt *TreeWalker::find_interface_method_decl(InterfaceDeclStmt &iface, const std::string &name) const
{
    for (auto &member : iface.methods)
    {
        if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
        {
            if (method->name.lexeme == name)
            {
                return method;
            }
        }
    }

    return nullptr;
}

void TreeWalker::validate_class_interfaces(ClassDeclStmt &klass) const
{
    if (m_env == nullptr)
    {
        throw_runtime_error(klass.name, "environnement d'execution absent");
    }

    for (const Token &interface_name : klass.interfaces)
    {
        if (!m_env->contains(interface_name.lexeme))
        {
            throw_runtime_error(interface_name, "interface introuvable: " + interface_name.lexeme);
        }

        const Value interface_value = m_env->get(interface_name.lexeme);
        if (!interface_value.is_interface())
        {
            throw_runtime_error(interface_name, "le symbole n'est pas une interface: " + interface_name.lexeme);
        }

        InterfaceDeclStmt *iface_decl = interface_decl(interface_value.as_interface());
        if (iface_decl == nullptr)
        {
            throw_runtime_error(interface_name, "interface non compatible avec ce backend: " + interface_name.lexeme);
        }

        for (auto &member : iface_decl->methods)
        {
            auto *required_method = dynamic_cast<FunctionDeclStmt *>(member.get());
            if (required_method == nullptr)
            {
                continue;
            }

            if (find_method_decl(klass, required_method->name.lexeme) == nullptr)
            {
                throw_runtime_error(
                    klass.name,
                    "la classe " + klass.name.lexeme + " ne realise pas la methode requise " +
                        interface_name.lexeme + "." + required_method->name.lexeme);
            }
        }
    }
}

bool TreeWalker::access_uses_ici(const Expr &expr) const
{
    auto *identifier = dynamic_cast<const IdentifierExpr *>(&expr);
    return identifier != nullptr &&
           (identifier->name.type == TokenType::ICI || identifier->name.type == TokenType::PARENT);
}

bool TreeWalker::matches_type_name(const Value &value, const Token &type_token) const
{
    const std::string &full_type_name = type_token.lexeme;
    const std::string::size_type generic_start = full_type_name.find('[');
    const std::string type_name = generic_start == std::string::npos
        ? full_type_name
        : full_type_name.substr(0, generic_start);
    const std::string generic_spec = generic_start == std::string::npos
        ? std::string{}
        : full_type_name.substr(generic_start + 1, full_type_name.size() - generic_start - 2);

    if (type_name == "Entier")
    {
        return value.is_entier();
    }
    if (type_name == "Décimal" || type_name == "Decimal")
    {
        return value.is_decimal() || value.is_entier();
    }
    if (type_name == "Logique")
    {
        return value.is_logique();
    }
    if (type_name == "Symbole")
    {
        return value.is_symbole();
    }
    if (type_name == "Texte")
    {
        return value.is_texte();
    }
    if (type_name == "Rien")
    {
        return value.is_rien();
    }
    if (type_name == "Universel")
    {
        return true;
    }
    if (type_name == "Liste")
    {
        if (!value.is_liste())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }

        const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);
        if (generic_args.size() != 1)
        {
            return false;
        }

        for (const Value &element : value.as_liste()->elements)
        {
            if (!matches_type_name(element, Token(TokenType::IDENT, generic_args[0], type_token.line, type_token.column)))
            {
                return false;
            }
        }
        return true;
    }
    if (type_name == "ListeFixe")
    {
        if (!value.is_liste_fixe())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }

        const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);
        if (generic_args.size() != 2)
        {
            return false;
        }

        std::size_t expected_length = 0;
        try
        {
            expected_length = static_cast<std::size_t>(std::stoll(generic_args[1]));
        }
        catch (...)
        {
            return false;
        }

        const auto list = value.as_liste_fixe();
        if (list->elements.size() != expected_length)
        {
            return false;
        }

        for (const Value &element : list->elements)
        {
            if (!matches_type_name(element, Token(TokenType::IDENT, generic_args[0], type_token.line, type_token.column)))
            {
                return false;
            }
        }
        return true;
    }
    if (type_name == "Dictionnaire")
    {
        if (!value.is_dictionnaire())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }

        const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);
        if (generic_args.size() != 2)
        {
            return false;
        }

        for (const auto &[key, entry_value] : value.as_dictionnaire()->entries)
        {
            if (!matches_type_name(key, Token(TokenType::IDENT, generic_args[0], type_token.line, type_token.column)) ||
                !matches_type_name(entry_value, Token(TokenType::IDENT, generic_args[1], type_token.line, type_token.column)))
            {
                return false;
            }
        }
        return true;
    }
    if (type_name == "Ensemble")
    {
        if (!value.is_ensemble())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }

        const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);
        if (generic_args.size() != 1)
        {
            return false;
        }

        for (const Value &element : value.as_ensemble()->elements)
        {
            if (!matches_type_name(element, Token(TokenType::IDENT, generic_args[0], type_token.line, type_token.column)))
            {
                return false;
            }
        }
        return true;
    }
    if (type_name == "Classe")
    {
        return value.is_classe();
    }
    if (type_name == "Interface")
    {
        return value.is_interface();
    }

    if (value.is_objet())
    {
        auto object = value.as_objet();
        return object != nullptr &&
               object->klass != nullptr &&
               ([&]() {
                   ClassDeclStmt *object_class = class_decl(object->klass);
                   return object_class != nullptr &&
                          (class_derives_from(*object_class, type_name) ||
                           class_implements_interface(*object_class, type_name));
               })();
    }

    return false;
}

std::vector<std::string> TreeWalker::split_generic_arguments(const std::string &generic_spec) const
{
    std::vector<std::string> result;
    std::string current;
    int depth = 0;

    for (char ch : generic_spec)
    {
        if (ch == '[')
        {
            ++depth;
            current += ch;
            continue;
        }
        if (ch == ']')
        {
            --depth;
            current += ch;
            continue;
        }
        if (ch == ',' && depth == 0)
        {
            if (!current.empty())
            {
                std::string trimmed = current;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
                result.push_back(trimmed);
            }
            current.clear();
            continue;
        }

        current += ch;
    }

    if (!current.empty())
    {
        std::string trimmed = current;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
        result.push_back(trimmed);
    }

    return result;
}

void TreeWalker::register_value_annotation(const Value &value, const Token &annotation) const
{
    if (annotation.lexeme.empty())
    {
        return;
    }

    const std::string &full_type_name = annotation.lexeme;
    const std::string::size_type generic_start = full_type_name.find('[');
    if (generic_start == std::string::npos)
    {
        return;
    }

    const std::string type_name = full_type_name.substr(0, generic_start);
    const std::string generic_spec = full_type_name.substr(generic_start + 1, full_type_name.size() - generic_start - 2);
    const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);

    if (type_name == "Liste" && value.is_liste() && generic_args.size() == 1)
    {
        m_list_constraints[value.as_liste().get()] = ListConstraint{generic_args[0]};
        for (const Value &element : value.as_liste()->elements)
        {
            register_value_annotation(element, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
        }
        return;
    }

    if (type_name == "ListeFixe" && value.is_liste_fixe() && generic_args.size() == 2)
    {
        std::size_t expected_length = 0;
        try
        {
            expected_length = static_cast<std::size_t>(std::stoll(generic_args[1]));
        }
        catch (...)
        {
            return;
        }

        m_fixed_list_constraints[value.as_liste_fixe().get()] = FixedListConstraint{generic_args[0], expected_length};
        for (const Value &element : value.as_liste_fixe()->elements)
        {
            register_value_annotation(element, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
        }
        return;
    }

    if (type_name == "Dictionnaire" && value.is_dictionnaire() && generic_args.size() == 2)
    {
        m_dict_constraints[value.as_dictionnaire().get()] = DictConstraint{generic_args[0], generic_args[1]};
        for (const auto &[key, entry_value] : value.as_dictionnaire()->entries)
        {
            register_value_annotation(key, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
            register_value_annotation(entry_value, Token(TokenType::IDENT, generic_args[1], annotation.line, annotation.column));
        }
        return;
    }

    if (type_name == "Ensemble" && value.is_ensemble() && generic_args.size() == 1)
    {
        m_set_constraints[value.as_ensemble().get()] = SetConstraint{generic_args[0]};
        for (const Value &element : value.as_ensemble()->elements)
        {
            register_value_annotation(element, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
        }
    }
}

void TreeWalker::enforce_list_element_constraint(const std::shared_ptr<ListeData> &list,
                                                 const Value &element,
                                                 const Token &site,
                                                 const std::string &context) const
{
    if (list == nullptr)
    {
        return;
    }

    const auto it = m_list_constraints.find(list.get());
    if (it == m_list_constraints.end())
    {
        return;
    }

    const Token annotation(TokenType::IDENT, it->second.element_type, site.line, site.column);
    ensure_value_matches_annotation(element, annotation, site, context);
}

void TreeWalker::enforce_fixed_list_element_constraint(const std::shared_ptr<ListeFixeData> &list,
                                                       const Value &element,
                                                       const Token &site,
                                                       const std::string &context) const
{
    if (list == nullptr)
    {
        return;
    }

    const auto it = m_fixed_list_constraints.find(list.get());
    if (it == m_fixed_list_constraints.end())
    {
        return;
    }

    const Token annotation(TokenType::IDENT, it->second.element_type, site.line, site.column);
    ensure_value_matches_annotation(element, annotation, site, context);
}

void TreeWalker::enforce_dict_entry_constraint(const std::shared_ptr<DictData> &dict,
                                               const Value &key,
                                               const Value &entry_value,
                                               const Token &site,
                                               const std::string &context) const
{
    if (dict == nullptr)
    {
        return;
    }

    const auto it = m_dict_constraints.find(dict.get());
    if (it == m_dict_constraints.end())
    {
        return;
    }

    const Token key_annotation(TokenType::IDENT, it->second.key_type, site.line, site.column);
    const Token value_annotation(TokenType::IDENT, it->second.value_type, site.line, site.column);
    ensure_value_matches_annotation(key, key_annotation, site, context + " (cle)");
    ensure_value_matches_annotation(entry_value, value_annotation, site, context + " (valeur)");
}

bool TreeWalker::class_derives_from(const ClassDeclStmt &klass, const std::string &ancestor_name) const
{
    if (klass.name.lexeme == ancestor_name)
    {
        return true;
    }

    if (ClassDeclStmt *parent = resolve_parent_class(klass))
    {
        return class_derives_from(*parent, ancestor_name);
    }

    return false;
}

bool TreeWalker::class_implements_interface(const ClassDeclStmt &klass, const std::string &interface_name) const
{
    for (const Token &implemented : klass.interfaces)
    {
        if (implemented.lexeme == interface_name)
        {
            return true;
        }
    }

    if (ClassDeclStmt *parent = resolve_parent_class(klass))
    {
        return class_implements_interface(*parent, interface_name);
    }

    return false;
}

void TreeWalker::ensure_value_matches_annotation(const Value &value,
                                                 const Token &annotation,
                                                 const Token &site,
                                                 const std::string &context) const
{
    if (annotation.lexeme.empty())
    {
        return;
    }

    if (matches_type_name(value, annotation))
    {
        register_value_annotation(value, annotation);
        return;
    }

    throw_runtime_error(
        site,
        context + " attend une valeur de type " + annotation.lexeme +
            ", valeur reçue: " + value.type_name());
}

std::vector<Value> TreeWalker::enumerate_iterable(const Value &iterable, const Token &site) const
{
    if (!is_iterable_value(iterable))
    {
        throw_runtime_error(site, "la valeur n'est pas iterable");
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

    throw_runtime_error(site, "la valeur n'est pas iterable");
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
