#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

namespace lumiere
{

namespace
{

std::string path_to_text(const std::filesystem::path &path)
{
    std::filesystem::path normalized = path;
    normalized.make_preferred();
    return normalized.string();
}

}

void register_chemin_module(Module &module, const NativeFunctionFactory &make_native_function)
{
    stdlib_bind_public_value(module, "separateur", Value::texte(std::string(1, std::filesystem::path::preferred_separator)));

    stdlib_bind_public_function(
        module,
        make_native_function,
        "dossier_courant",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 0, "Chemin.dossier_courant", native_args.site);
            return Value::texte(path_to_text(std::filesystem::current_path()));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "joindre",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            if (args.empty())
            {
                runtime.raise_runtime_error(call_site, "Chemin.joindre attend au moins un segment");
            }

            std::filesystem::path path;
            for (const auto &arg : args)
            {
                if (!arg.name.empty())
                {
                    runtime.raise_runtime_error(call_site, "Chemin.joindre n'accepte pas d'arguments nommes");
                }
                if (!arg.value.is_texte())
                {
                    runtime.raise_runtime_error(call_site, "Chemin.joindre attend des segments de type Texte");
                }
                path /= arg.value.as_texte();
            }

            return Value::texte(path_to_text(path.lexically_normal()));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "absolu",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.absolu", call_site);
            const std::filesystem::path absolute = path.is_absolute()
                ? path.lexically_normal()
                : (std::filesystem::current_path() / path).lexically_normal();
            return Value::texte(path_to_text(absolute));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "nom",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.nom", call_site);
            return Value::texte(path_to_text(path.filename()));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "nom_sans_extension",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.nom_sans_extension", call_site);
            return Value::texte(path_to_text(path.stem()));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "extension",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.extension", call_site);
            return Value::texte(path_to_text(path.extension()));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "dossier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.dossier", call_site);
            return Value::texte(path_to_text(path.parent_path()));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "parties",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.parties", call_site).lexically_normal();

            auto parts = std::make_shared<ListeData>();
            for (const auto &part : path)
            {
                const std::string part_text = path_to_text(part);
                if (!part_text.empty() &&
                    part_text != std::string(1, std::filesystem::path::preferred_separator))
                {
                    parts->elements.push_back(Value::texte(part_text));
                }
            }

            Value result = Value::liste(std::move(parts));
            runtime.annotate_value(result, "Liste[Texte]", call_site);
            return result;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_absolu",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.est_absolu", call_site);
            return Value::logique(path.is_absolute());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_relatif",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.est_relatif", call_site);
            return Value::logique(path.is_relative());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "normaliser",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Chemin.normaliser", call_site);
            return Value::texte(path_to_text(path.lexically_normal()));
        });
}

} // namespace lumiere
