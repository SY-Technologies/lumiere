#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"

#include <cmath>
#include <limits>
#include <sstream>

namespace lumiere
{

void register_maths_module(Module &module, const NativeFunctionFactory &make_native_function)
{
    const Value pi_value = Value::decimal(3.14159265358979323846);
    const Value e_value = Value::decimal(2.71828182845904523536);
    const Value infini_value = Value::decimal(std::numeric_limits<double>::infinity());
    const Value non_nombre_value = Value::decimal(std::numeric_limits<double>::quiet_NaN());

    stdlib_bind_public_value(module, "pi", pi_value);
    stdlib_bind_public_value(module, "e", e_value);
    stdlib_bind_public_value(module, "infini", infini_value);
    stdlib_bind_public_value(module, "non_nombre", non_nombre_value);

    const auto absolu_function = make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.absolu", call_site);
            if (args[0].value.is_entier())
            {
                return Value::entier(std::llabs(args[0].value.as_entier()));
            }
            return Value::decimal(std::fabs(stdlib_expect_decimal(runtime, args[0].value, "Maths.absolu", call_site)));
        });
    stdlib_bind_public_value(module, "absolu", Value::fonction(absolu_function));
    stdlib_bind_public_value(module, "abs", Value::fonction(absolu_function));

    stdlib_bind_public_function(
        module,
        make_native_function,
        "min",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Maths.min", call_site);
            if (args[0].value.is_entier() && args[1].value.is_entier())
            {
                return Value::entier(std::min(args[0].value.as_entier(), args[1].value.as_entier()));
            }
            return Value::decimal(std::min(stdlib_expect_decimal(runtime, args[0].value, "Maths.min", call_site),
                                           stdlib_expect_decimal(runtime, args[1].value, "Maths.min", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "max",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Maths.max", call_site);
            if (args[0].value.is_entier() && args[1].value.is_entier())
            {
                return Value::entier(std::max(args[0].value.as_entier(), args[1].value.as_entier()));
            }
            return Value::decimal(std::max(stdlib_expect_decimal(runtime, args[0].value, "Maths.max", call_site),
                                           stdlib_expect_decimal(runtime, args[1].value, "Maths.max", call_site)));
        });

    const auto arrondir_function = make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.arrondir", call_site);
            return Value::entier(static_cast<int64_t>(std::llround(stdlib_expect_decimal(runtime, args[0].value, "Maths.arrondir", call_site))));
        });
    stdlib_bind_public_value(module, "arrondir", Value::fonction(arrondir_function));
    stdlib_bind_public_value(module, "arrondi", Value::fonction(arrondir_function));

    stdlib_bind_public_function(
        module,
        make_native_function,
        "plancher",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.plancher", call_site);
            return Value::entier(static_cast<int64_t>(std::floor(stdlib_expect_decimal(runtime, args[0].value, "Maths.plancher", call_site))));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "plafond",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.plafond", call_site);
            return Value::entier(static_cast<int64_t>(std::ceil(stdlib_expect_decimal(runtime, args[0].value, "Maths.plafond", call_site))));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "tronquer",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.tronquer", call_site);
            return Value::entier(static_cast<int64_t>(std::trunc(stdlib_expect_decimal(runtime, args[0].value, "Maths.tronquer", call_site))));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "racine",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.racine", call_site);
            const double value = stdlib_expect_decimal(runtime, args[0].value, "Maths.racine", call_site);
            if (value < 0.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.racine attend une valeur non negative");
            }
            return Value::decimal(std::sqrt(value));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "racine_n",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Maths.racine_n", call_site);
            const double value = stdlib_expect_decimal(runtime, args[0].value, "Maths.racine_n", call_site);
            const double degree = stdlib_expect_decimal(runtime, args[1].value, "Maths.racine_n", call_site);
            if (degree == 0.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.racine_n attend un degre non nul");
            }
            if (value < 0.0 && std::fmod(std::fabs(degree), 2.0) == 0.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.racine_n ne peut pas calculer une racine paire d'une valeur negative");
            }
            return Value::decimal(std::pow(value, 1.0 / degree));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "puissance",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Maths.puissance", call_site);
            const double base = stdlib_expect_decimal(runtime, args[0].value, "Maths.puissance", call_site);
            const double exponent = stdlib_expect_decimal(runtime, args[1].value, "Maths.puissance", call_site);
            return Value::decimal(std::pow(base, exponent));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "log",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.log", call_site);
            return Value::decimal(std::log(stdlib_expect_decimal(runtime, args[0].value, "Maths.log", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "log10",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.log10", call_site);
            return Value::decimal(std::log10(stdlib_expect_decimal(runtime, args[0].value, "Maths.log10", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "log2",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.log2", call_site);
            return Value::decimal(std::log2(stdlib_expect_decimal(runtime, args[0].value, "Maths.log2", call_site)));
        });

    const auto sin_function = make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.sin", call_site);
            return Value::decimal(std::sin(stdlib_expect_decimal(runtime, args[0].value, "Maths.sin", call_site)));
        });
    stdlib_bind_public_value(module, "sin", Value::fonction(sin_function));
    stdlib_bind_public_value(module, "sinus", Value::fonction(sin_function));

    const auto cos_function = make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.cos", call_site);
            return Value::decimal(std::cos(stdlib_expect_decimal(runtime, args[0].value, "Maths.cos", call_site)));
        });
    stdlib_bind_public_value(module, "cos", Value::fonction(cos_function));
    stdlib_bind_public_value(module, "cosinus", Value::fonction(cos_function));

    const auto tan_function = make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.tan", call_site);
            return Value::decimal(std::tan(stdlib_expect_decimal(runtime, args[0].value, "Maths.tan", call_site)));
        });
    stdlib_bind_public_value(module, "tan", Value::fonction(tan_function));
    stdlib_bind_public_value(module, "tangente", Value::fonction(tan_function));

    stdlib_bind_public_function(
        module,
        make_native_function,
        "asin",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.asin", call_site);
            return Value::decimal(std::asin(stdlib_expect_decimal(runtime, args[0].value, "Maths.asin", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "acos",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.acos", call_site);
            return Value::decimal(std::acos(stdlib_expect_decimal(runtime, args[0].value, "Maths.acos", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "atan",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.atan", call_site);
            return Value::decimal(std::atan(stdlib_expect_decimal(runtime, args[0].value, "Maths.atan", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "atan2",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Maths.atan2", call_site);
            return Value::decimal(std::atan2(stdlib_expect_decimal(runtime, args[0].value, "Maths.atan2", call_site),
                                             stdlib_expect_decimal(runtime, args[1].value, "Maths.atan2", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "degres_vers_radians",
        [pi_value](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.degres_vers_radians", call_site);
            return Value::decimal(stdlib_expect_decimal(runtime, args[0].value, "Maths.degres_vers_radians", call_site) * pi_value.as_decimal() / 180.0);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "radians_vers_degres",
        [pi_value](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.radians_vers_degres", call_site);
            return Value::decimal(stdlib_expect_decimal(runtime, args[0].value, "Maths.radians_vers_degres", call_site) * 180.0 / pi_value.as_decimal());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_non_nombre",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.est_non_nombre", call_site);
            return Value::logique(std::isnan(stdlib_expect_decimal(runtime, args[0].value, "Maths.est_non_nombre", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_infini",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.est_infini", call_site);
            return Value::logique(std::isinf(stdlib_expect_decimal(runtime, args[0].value, "Maths.est_infini", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_pair",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.est_pair", call_site);
            return Value::logique(stdlib_expect_integer(runtime, args[0].value, "Maths.est_pair", call_site) % 2 == 0);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_impair",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.est_impair", call_site);
            return Value::logique(stdlib_expect_integer(runtime, args[0].value, "Maths.est_impair", call_site) % 2 != 0);
        });
}

} // namespace lumiere
