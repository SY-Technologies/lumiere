#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"

#include <cmath>
#include <limits>
#include <sstream>

namespace lumiere
{

namespace
{
Value rounded_integer(IRuntime &runtime, const RuntimeSite &site, double value)
{
    const auto integer = numeric::to_integer(value);
    if (!integer)
        runtime.raise_runtime_error(site, "Maths: valeur hors limites pour Entier");
    return Value::entier(*integer);
}

// Exact int64 exponentiation by repeated squaring, or nullopt on overflow.
// puissance's general path converts both operands to double and calls
// std::pow, which loses precision for any Entier argument or result past
// 2^53 -- both in the Entier->double coercion of the inputs and, separately,
// in whatever rounding std::pow's own implementation does, which is not
// required to be correctly rounded. When both operands started out as
// Entier with a non-negative exponent, the true result is representable
// exactly in int64 arithmetic up to int64's own range, so compute it that
// way instead of going through double at all.
std::optional<std::int64_t> integer_power(std::int64_t base, std::int64_t exponent)
{
    std::int64_t result = 1;
    std::int64_t squared_base = base;
    for (std::int64_t remaining_exponent = exponent; remaining_exponent > 0; remaining_exponent >>= 1)
    {
        if (remaining_exponent & 1)
        {
            const auto next_result = numeric::multiply(result, squared_base);
            if (!next_result)
                return std::nullopt;
            result = *next_result;
        }
        if (remaining_exponent > 1)
        {
            const auto next_squared_base = numeric::multiply(squared_base, squared_base);
            if (!next_squared_base)
                return std::nullopt;
            squared_base = *next_squared_base;
        }
    }
    return result;
}
}

void register_maths_module(Module &module)
{
    const auto &make_native_function = native_function_factory();
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
                const int64_t val = args[0].value.as_entier();
                if (val == std::numeric_limits<int64_t>::min())
                {
                    runtime.raise_runtime_error(call_site, "Maths.absolu: la valeur absolue de -2^63 dépasse la limite d'un Entier");
                }
                return Value::entier(std::llabs(val));
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
            const double arrondir_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.arrondir", call_site);
            if (arrondir_val < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
                arrondir_val > static_cast<double>(std::numeric_limits<int64_t>::max()))
            {
                runtime.raise_runtime_error(call_site, "Maths.arrondir: le résultat dépasse la limite d'un Entier");
            }
            return rounded_integer(runtime, call_site, std::round(arrondir_val));
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
            const double plancher_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.plancher", call_site);
            if (plancher_val < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
                plancher_val > static_cast<double>(std::numeric_limits<int64_t>::max()))
            {
                runtime.raise_runtime_error(call_site, "Maths.plancher: le résultat dépasse la limite d'un Entier");
            }
            return rounded_integer(runtime, call_site, std::floor(plancher_val));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "plafond",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.plafond", call_site);
            const double plafond_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.plafond", call_site);
            if (plafond_val < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
                plafond_val > static_cast<double>(std::numeric_limits<int64_t>::max()))
            {
                runtime.raise_runtime_error(call_site, "Maths.plafond: le résultat dépasse la limite d'un Entier");
            }
            return rounded_integer(runtime, call_site, std::ceil(plafond_val));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "tronquer",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.tronquer", call_site);
            const double tronquer_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.tronquer", call_site);
            if (tronquer_val < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
                tronquer_val > static_cast<double>(std::numeric_limits<int64_t>::max()))
            {
                runtime.raise_runtime_error(call_site, "Maths.tronquer: le résultat dépasse la limite d'un Entier");
            }
            return rounded_integer(runtime, call_site, std::trunc(tronquer_val));
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
            if (std::isnan(value) || value < 0.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.racine attend une valeur non négative");
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
            if (std::isnan(degree) || std::isnan(value) || std::isinf(degree) || std::isinf(value))
            {
                runtime.raise_runtime_error(call_site, "Maths.racine_n attend des valeurs finies valides");
            }
            if (degree == 0.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.racine_n attend un degre non nul");
            }
            if (value < 0.0 && std::fmod(std::fabs(degree), 2.0) != 1.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.racine_n ne peut pas calculer une racine paire d'une valeur négative");
            }
            return Value::decimal(value < 0.0
                                      ? -std::pow(-value, 1.0 / degree)
                                      : std::pow(value, 1.0 / degree));
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
            // A negative base raised to a non-integer real exponent has no
            // real result (it is complex), unlike every other case here,
            // which either has one or names infini/non_nombre honestly the
            // way IEEE already does. Every domain-sensitive function in this
            // file (racine, racine_n, log*, asin, acos) raises rather than
            // hands back a silent non_nombre for the one input shape that
            // is genuinely undefined; puissance did not.
            if (base < 0.0 && std::isfinite(exponent) && std::trunc(exponent) != exponent)
            {
                runtime.raise_runtime_error(call_site,
                    "Maths.puissance ne peut pas élever une valeur négative à une puissance non entière");
            }
            if (args[0].value.is_entier() && args[1].value.is_entier() && args[1].value.as_entier() >= 0)
            {
                if (const auto exact = integer_power(args[0].value.as_entier(), args[1].value.as_entier()))
                {
                    return Value::decimal(static_cast<double>(*exact));
                }
                // int64 overflow: fall through to the general path below,
                // which reports it honestly as infini rather than erroring,
                // matching this function's IEEE-honesty policy above.
            }
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
            const double log_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.log", call_site);
            if (log_val <= 0.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.log attend une valeur positive");
            }
            return Value::decimal(std::log(log_val));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "log10",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.log10", call_site);
            const double log10_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.log10", call_site);
            if (log10_val <= 0.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.log10 attend une valeur positive");
            }
            return Value::decimal(std::log10(log10_val));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "log2",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.log2", call_site);
            const double log2_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.log2", call_site);
            if (log2_val <= 0.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.log2 attend une valeur positive");
            }
            return Value::decimal(std::log2(log2_val));
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
            const double asin_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.asin", call_site);
            if (asin_val < -1.0 || asin_val > 1.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.asin attend une valeur entre -1 et 1");
            }
            return Value::decimal(std::asin(asin_val));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "acos",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Maths.acos", call_site);
            const double acos_val = stdlib_expect_decimal(runtime, args[0].value, "Maths.acos", call_site);
            if (acos_val < -1.0 || acos_val > 1.0)
            {
                runtime.raise_runtime_error(call_site, "Maths.acos attend une valeur entre -1 et 1");
            }
            return Value::decimal(std::acos(acos_val));
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
