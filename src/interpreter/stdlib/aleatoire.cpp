#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

#include <algorithm>
#include <random>

namespace lumiere
{

namespace
{

struct AleatoireModuleState : RuntimeModuleState
{
    // The generator lives in module state so repeated imports share the same
    // pseudorandom stream, and graine() can deterministically reset it.
    std::mt19937_64 generator{std::random_device{}()};
};

}

void register_aleatoire_module(Module &module)
{
    const auto &make_native_function = native_function_factory();
    auto state = std::make_shared<AleatoireModuleState>();
    module.state = state;
    stdlib_bind_public_function(
        module,
        make_native_function,
        "graine",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Aléatoire.graine", native_args.site);
            const uint64_t seed = static_cast<uint64_t>(stdlib_expect_integer(runtime, args[0].value, "Aléatoire.graine", native_args.site));
            state->generator.seed(seed);
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "entier",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Aléatoire.entier", native_args.site);
            const int64_t min_value = stdlib_expect_integer(runtime, args[0].value, "Aléatoire.entier", native_args.site);
            const int64_t max_value = stdlib_expect_integer(runtime, args[1].value, "Aléatoire.entier", native_args.site);
            if (min_value > max_value)
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.entier attend min <= max");
            }
            std::uniform_int_distribution<int64_t> distribution(min_value, max_value);
            return Value::entier(distribution(state->generator));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "décimal",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 0, "Aléatoire.décimal", native_args.site);
            std::uniform_real_distribution<double> distribution(0.0, 1.0);
            return Value::decimal(distribution(state->generator));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "décimal_entre",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Aléatoire.décimal_entre", native_args.site);
            const double min_value = stdlib_expect_decimal(runtime, args[0].value, "Aléatoire.décimal_entre", native_args.site);
            const double max_value = stdlib_expect_decimal(runtime, args[1].value, "Aléatoire.décimal_entre", native_args.site);
            if (min_value > max_value)
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.décimal_entre attend min <= max");
            }
            std::uniform_real_distribution<double> distribution(min_value, max_value);
            return Value::decimal(distribution(state->generator));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "choisir",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Aléatoire.choisir", native_args.site);
            if (!args[0].value.is_liste())
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.choisir attend une Liste");
            }

            const auto list = args[0].value.as_liste();
            if (list->elements.empty())
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.choisir ne peut pas choisir dans une liste vide");
            }

            std::uniform_int_distribution<std::size_t> distribution(0, list->elements.size() - 1);
            return list->elements[distribution(state->generator)];
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "mélanger",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Aléatoire.mélanger", native_args.site);
            if (!args[0].value.is_liste())
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.mélanger attend une Liste");
            }

            const auto list = args[0].value.as_liste();
            std::shuffle(list->elements.begin(), list->elements.end(), state->generator);
            return args[0].value;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "échantillon",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Aléatoire.échantillon", native_args.site);
            if (!args[0].value.is_liste())
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.échantillon attend une Liste");
            }

            const int64_t count = stdlib_expect_integer(runtime, args[1].value, "Aléatoire.échantillon", native_args.site);
            const auto list = args[0].value.as_liste();
            if (count < 0 || static_cast<std::size_t>(count) > list->elements.size())
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.échantillon attend 0 <= n <= taille");
            }

            std::vector<Value> shuffled = list->elements;
            std::shuffle(shuffled.begin(), shuffled.end(), state->generator);

            auto sample = std::make_shared<ListeData>();
            sample->elements.insert(sample->elements.end(), shuffled.begin(), shuffled.begin() + count);
            Value result = Value::liste(std::move(sample));
            runtime.annotate_value(result, "Liste[Universel]", native_args.site);
            return result;
        });
}

} // namespace lumiere
