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

    // A generator holds no Lumière values.
    void trace_references(RefVisitor &) const override {}
    void clear_references() override {}
};

}

void register_aleatoire_module(Module &module)
{
    const auto &make_native_function = native_function_factory();
    auto state_ref = make_ref<AleatoireModuleState>();
    module.state = state_ref;
    // The handlers below reach the generator through a raw pointer. A Ref
    // captured inside a std::function is a reference nothing can enumerate, so
    // the owning one is declared on each finished function instead. This state
    // holds no Lumiere values today, so no cycle runs through it -- the rule is
    // uniform so that adding one later cannot quietly create a leak.
    auto *const state = state_ref.get();
    stdlib_bind_public_function(
        module,
        make_native_function,
        "graine",
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Aléatoire.graine", native_args.site);
            const int64_t seed_raw = stdlib_expect_integer(runtime, args[0].value, "Aléatoire.graine", native_args.site);
            if (seed_raw < 0)
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.graine attend une valeur non négative");
            }
            state->generator.seed(static_cast<uint64_t>(seed_raw));
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
            if (min_value > 0)
            {
                if (max_value - min_value > std::numeric_limits<int64_t>::max())
                {
                    runtime.raise_runtime_error(native_args.site, "Aléatoire.entier: l'intervalle est trop grand");
                }
            }
            else if (max_value > std::numeric_limits<int64_t>::max() + min_value)
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.entier: l'intervalle est trop grand");
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
            if (std::isnan(min_value) || std::isnan(max_value) || std::isinf(min_value) || std::isinf(max_value))
            {
                runtime.raise_runtime_error(native_args.site, "Aléatoire.décimal_entre attend des valeurs finies valides");
            }
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

            auto sample = make_ref<ListeData>();
            sample->elements.insert(sample->elements.end(), shuffled.begin(), shuffled.begin() + count);
            Value result = Value::liste(std::move(sample));
            runtime.annotate_value(result, "Liste[Universel]", native_args.site);
            return result;
        });

    for (auto &[name, member] : module.members)
    {
        if (member.is_fonction())
        {
            member.as_fonction()->native_captures.push_back(state_ref);
        }
    }
}

} // namespace lumiere
