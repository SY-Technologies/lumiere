#include "lumiere/interpreter/stdlib/helpers.hpp"

namespace lumiere
{

void stdlib_expect_positional(IRuntime &runtime,
                              const std::vector<RuntimeArgument> &args,
                              std::size_t expected,
                              const std::string &signature,
                              const RuntimeSite &call_site)
{
    if (args.size() != expected)
    {
        runtime.raise_runtime_error(call_site, signature + " attend exactement " + std::to_string(expected) + " argument(s)");
    }
    for (const auto &arg : args)
    {
        if (!arg.name.empty())
        {
            runtime.raise_runtime_error(call_site, signature + " n'accepte pas d'arguments nommes");
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
            runtime.raise_runtime_error(call_site, signature + " attend exactement " + std::to_string(min_count) + " argument(s)");
        }
        runtime.raise_runtime_error(call_site,
                                    signature + " attend entre " + std::to_string(min_count) +
                                        " et " + std::to_string(max_count) + " arguments");
    }
    for (const auto &arg : args)
    {
        if (!arg.name.empty())
        {
            runtime.raise_runtime_error(call_site, signature + " n'accepte pas d'arguments nommes");
        }
    }
}

std::string stdlib_expect_text(IRuntime &runtime,
                               const Value &value,
                               const std::string &context,
                               const RuntimeSite &call_site)
{
    (void)context;
    if (!value.is_texte())
    {
        runtime.raise_runtime_error(call_site, context + " attend une valeur de type Texte");
    }
    return value.as_texte();
}

int64_t stdlib_expect_integer(IRuntime &runtime,
                              const Value &value,
                              const std::string &context,
                              const RuntimeSite &call_site)
{
    (void)context;
    if (!value.is_entier())
    {
        runtime.raise_runtime_error(call_site, context + " attend une valeur de type Entier");
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
        runtime.raise_runtime_error(call_site, context + " attend une valeur numerique");
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

void stdlib_throw_filesystem_failure(IRuntime &runtime,
                                     const RuntimeSite &call_site,
                                     const std::string &signature,
                                     const std::string &message)
{
    runtime.raise_runtime_error(call_site, signature + " a echoue: " + message);
}

void stdlib_bind_public_value(Module &module, const std::string &name, const Value &value)
{
    module.members[name] = value;
    module.public_members.insert(name);
}

void stdlib_bind_public_function(Module &module,
                                 const NativeFunctionFactory &make_native_function,
                                 const std::string &name,
                                 LumiereFunction::NativeHandler handler)
{
    module.members[name] = Value::fonction(make_native_function(std::move(handler)));
    module.public_members.insert(name);
}

} // namespace lumiere
