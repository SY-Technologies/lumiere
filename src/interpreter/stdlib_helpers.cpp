#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/runtime/nominal_type.hpp"

namespace lumiere
{

// These helpers intentionally keep stdlib entrypoints strict and uniform.
// Most stdlib files delegate argument-count and basic type checks here so
// maintainers do not have to hand-roll slightly different error behavior in
// every module.
void stdlib_expect_positional(IRuntime &runtime,
                              const std::vector<RuntimeArgument> &args,
                              std::size_t expected,
                              const std::string &signature,
                              const RuntimeSite &call_site)
{
    if (args.size() != expected)
    {
        runtime.raise_runtime_error(call_site, messages::arite_exacte(signature, expected));
    }
    for (const auto &arg : args)
    {
        if (!arg.name.empty())
        {
            runtime.raise_runtime_error(call_site, messages::arguments_nommes_refuses(signature));
        }
    }
}

void stdlib_expect_positional_range(IRuntime &runtime,
                                    const std::vector<RuntimeArgument> &args,
                                    std::size_t min_count,
                                    std::size_t max_count,
                                    const std::string &signature,
                                    const RuntimeSite &call_site)
{
    if (args.size() < min_count || args.size() > max_count)
    {
        if (min_count == max_count)
        {
            runtime.raise_runtime_error(call_site, messages::arite_exacte(signature, min_count));
        }
        runtime.raise_runtime_error(call_site,
                                    signature + " attend entre " + std::to_string(min_count) +
                                        " et " + std::to_string(max_count) + " arguments");
    }
    for (const auto &arg : args)
    {
        if (!arg.name.empty())
        {
            runtime.raise_runtime_error(call_site, messages::arguments_nommes_refuses(signature));
        }
    }
}

std::string stdlib_expect_text(IRuntime &runtime,
                               const Value &value,
                               const std::string &context,
                               const RuntimeSite &call_site)
{
    if (!value.is_texte())
    {
        runtime.raise_runtime_error(call_site, messages::valeur_attendue(context, "Texte"));
    }
    return value.as_texte();
}

int64_t stdlib_expect_integer(IRuntime &runtime,
                              const Value &value,
                              const std::string &context,
                              const RuntimeSite &call_site)
{
    if (!value.is_entier())
    {
        runtime.raise_runtime_error(call_site, messages::valeur_attendue(context, "Entier"));
    }
    return value.as_entier();
}

double stdlib_expect_decimal(IRuntime &runtime,
                             const Value &value,
                             const std::string &context,
                             const RuntimeSite &call_site)
{
    (void)context;
    if (value.is_entier())
    {
        return static_cast<double>(value.as_entier());
    }
    if (!value.is_decimal())
    {
        runtime.raise_runtime_error(call_site, context + " attend une valeur numérique");
    }
    return value.as_decimal();
}

std::filesystem::path stdlib_expect_path_arg(IRuntime &runtime,
                                             const std::vector<RuntimeArgument> &args,
                                             const std::string &signature,
                                             const RuntimeSite &call_site)
{
    if (args.size() != 1 || !args[0].name.empty())
    {
        runtime.raise_runtime_error(call_site, signature + " attend exactement un argument positionnel");
    }
    if (!args[0].value.is_texte())
    {
        runtime.raise_runtime_error(call_site, signature + " attend un chemin de type Texte");
    }
    return std::filesystem::path(args[0].value.as_texte());
}

std::pair<std::string, std::string> stdlib_expect_two_text_args(IRuntime &runtime,
                                                                const std::vector<RuntimeArgument> &args,
                                                                const std::string &signature,
                                                                const std::string &first_label,
                                                                const std::string &second_label,
                                                                const RuntimeSite &call_site)
{
    if (args.size() != 2 || !args[0].name.empty() || !args[1].name.empty())
    {
        runtime.raise_runtime_error(call_site, signature + " attend exactement deux arguments positionnels");
    }
    if (!args[0].value.is_texte())
    {
        runtime.raise_runtime_error(call_site, signature + " attend un " + first_label + " de type Texte");
    }
    if (!args[1].value.is_texte())
    {
        runtime.raise_runtime_error(call_site, signature + " attend un " + second_label + " de type Texte");
    }
    return {args[0].value.as_texte(), args[1].value.as_texte()};
}

Value stdlib_error_value(
    std::string type_name,
    std::string operation,
    std::string cause,
    std::string path)
{
    auto klass = make_ref<LumiereClass>();
    klass->name = type_name;
    klass->type_identity = native_nominal_type_identity(type_name);
    auto error_interface =
        make_ref<LumiereInterface>();
    error_interface->name = "Erreur";
    error_interface->type_identity = "Erreur";
    klass->interfaces.emplace(
        "Erreur",
        std::move(error_interface));

    auto object = make_ref<LumiereObject>();
    object->klass = std::move(klass);
    object->fields.emplace(
        "opération",
        Value::texte(std::move(operation)));
    object->fields.emplace(
        "cause",
        Value::texte(std::move(cause)));
    if (!path.empty())
    {
        object->fields.emplace(
            "chemin",
            Value::texte(std::move(path)));
    }
    return Value::objet(std::move(object));
}

Value stdlib_success(Value payload)
{
    return Value::resultat(true, std::move(payload));
}

Value stdlib_failure(Value error, const RuntimeSite &origin)
{
    return Value::resultat(
        false,
        std::move(error),
        origin);
}

void stdlib_bind_public_type(Module &module, const std::string &name)
{
    Token identity(TokenType::IDENT, native_nominal_type_identity(module.name, name), 0, 0);
    module.type_aliases.insert_or_assign(name, TypeExpr::named(std::move(identity)));
    module.public_type_aliases.insert(name);
}

void stdlib_bind_public_value(Module &module, const std::string &name, const Value &value)
{
    module.members[name] = value;
    module.public_members.insert(name);
    if (value.is_classe() || value.is_interface())
    {
        const std::string identity = native_nominal_type_identity(module.name, name);
        if (value.is_classe())
        {
            if (value.as_classe()->type_identity.empty())
                value.as_classe()->type_identity = identity;
        }
        else if (value.as_interface()->type_identity.empty())
            value.as_interface()->type_identity = identity;
        stdlib_bind_public_type(module, name);
        module.public_type_values.insert_or_assign(name, value);
    }
}

void stdlib_bind_public_function(Module &module,
                                 const NativeFunctionFactory &make_native_function,
                                 const std::string &name,
                                 LumiereFunction::NativeHandler handler)
{
    module.members[name] = Value::fonction(make_native_function(std::move(handler)));
    module.public_members.insert(name);
}

bool register_builtin_module(Module &module,
                             Ref<LumiTestModuleState> lumitest_state)
{
    if (module.name == "Chemin")
    {
        register_chemin_module(module);
    }
    else if (module.name == "Fichier")
    {
        register_fichier_module(module);
    }
    else if (module.name == "Texte")
    {
        register_texte_module(module);
    }
    else if (module.name == "Maths")
    {
        register_maths_module(module);
    }
    else if (module.name == "Temps")
    {
        register_temps_module(module);
    }
    else if (module.name == "Aléatoire" || module.name == "Aleatoire")
    {
        register_aleatoire_module(module);
    }
    else if (module.name == "LumiNet")
    {
        register_luminet_module(module);
    }
    else if (module.name == "Collections")
    {
        register_collections_module(module);
    }
    else if (module.name == "JSON")
    {
        register_json_module(module);
    }
    else if (module.name == "Regex")
    {
        register_regex_module(module);
    }
    else if (module.name == "LumiTest")
        register_lumitest_module(module,
                                 lumitest_state != nullptr
                                     ? std::move(lumitest_state)
                                     : make_ref<LumiTestModuleState>());
    else
    {
        return false;
    }

    return true;
}

const NativeFunctionFactory &native_function_factory()
{
    static const NativeFunctionFactory factory = [](LumiereFunction::NativeHandler handler) {
        auto function = make_ref<LumiereFunction>();
        function->name = "<native>";
        function->native_handler = std::move(handler);
        return function;
    };
    return factory;
}

} // namespace lumiere
