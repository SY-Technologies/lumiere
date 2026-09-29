#include "lumiere/interpreter/runtime/numeric.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/parser/utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace lumiere
{

namespace
{

// Turns any of the five iterable value kinds into a flat, ordered sequence,
// matching `pour chaque`'s own contract: a Dictionnaire yields its keys (the
// value is reached with d[cle]), and Texte yields one Symbole per Unicode
// scalar. This mirrors TreeWalker::enumerate_iterable, which native code
// cannot call directly -- stdlib modules only see IRuntime, and every other
// native module in this codebase (Chemin, Maths, ...) is likewise a
// self-contained C++ file with no dependency on either engine's internals.
std::vector<Value> enumerate(IRuntime &runtime,
                             const Value &value,
                             const std::string &context,
                             const RuntimeSite &site)
{
    if (value.is_liste())
    {
        return value.as_liste()->elements;
    }
    if (value.is_liste_fixe())
    {
        return value.as_liste_fixe()->elements;
    }
    if (value.is_ensemble())
    {
        const auto &items = value.as_ensemble()->items();
        return std::vector<Value>(items.begin(), items.end());
    }
    if (value.is_dictionnaire())
    {
        std::vector<Value> keys;
        keys.reserve(value.as_dictionnaire()->size());
        for (const auto &entry : value.as_dictionnaire()->items())
        {
            keys.push_back(entry.first);
        }
        return keys;
    }
    if (value.is_texte())
    {
        std::vector<Value> items;
        const std::string &text = value.as_texte();
        std::size_t offset = 0;
        while (offset < text.size())
        {
            char32_t character = 0;
            const std::optional<std::size_t> next = utf8::decode_one(text, offset, character);
            if (!next.has_value())
            {
                runtime.raise_runtime_error(site, context + " : texte UTF-8 invalide");
            }
            items.push_back(Value::symbole(character));
            offset = *next;
        }
        return items;
    }
    runtime.raise_runtime_error(
        site,
        context + " attend une valeur itérable (Liste, ListeFixe, Ensemble, Dictionnaire ou Texte); "
                  "type reçu: " +
            value.type_name());
    return {}; // unreachable: raise_runtime_error never returns, but that is
               // not visible to the compiler through a virtual IRuntime call.
}

Value make_list(std::vector<Value> elements,
                IRuntime &runtime,
                const RuntimeSite &site,
                std::string_view element_type = "Universel")
{
    auto data = make_ref<ListeData>();
    data->elements = std::move(elements);
    Value result = Value::liste(std::move(data));
    // Every caller except étendue passes the default "Universel" -- element
    // type is not knowable statically for the callback-based operations, see
    // the "Type-safety compromise" note for Collections in
    // docs/stdlib-foundations.md. étendue always produces Entier and passes
    // that explicitly, so its result carries a real element constraint
    // instead of a looser one than its own declared native signature.
    runtime.annotate_value(result, "Liste[" + std::string(element_type) + "]", site);
    return result;
}

void require_function(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    if (!value.is_fonction())
    {
        runtime.raise_runtime_error(site, context + " attend une fonction");
    }
}

Value invoke(IRuntime &runtime,
             const Value &callback,
             std::vector<RuntimeArgument> args,
             const RuntimeSite &site)
{
    const NativeArgs native_args{nullptr, &args, site};
    return runtime.call(callback, native_args);
}

bool call_predicate(IRuntime &runtime,
                    const Value &predicate,
                    const Value &item,
                    const std::string &context,
                    const RuntimeSite &site)
{
    const Value result = invoke(runtime, predicate, {RuntimeArgument{"", item}}, site);
    if (!result.is_logique())
    {
        runtime.raise_runtime_error(site, context + " : le prédicat doit retourner Logique");
    }
    return result.as_logique();
}

// The three value kinds Collections.trier/trier_par can order, matching
// docs/stdlib-foundations.md's contract exactly: mixed Entier/Décimal compare
// numerically, Texte and Symbole compare by Unicode scalar value, and any
// other mixture is a contract error.
enum class SortCategory
{
    Numeric,
    Texte,
    Symbole,
};

SortCategory sort_category(IRuntime &runtime, const Value &value, const std::string &context, const RuntimeSite &site)
{
    if (value.is_decimal() && std::isnan(value.as_decimal()))
    {
        // non_nombre has no order relative to anything, including itself.
        // Letting it into std::stable_sort's comparator would violate strict
        // weak ordering, which is undefined behavior, not just a surprising
        // result -- so this is a contract error like any other, not a
        // silent "sorts wherever" outcome.
        runtime.raise_runtime_error(site, context + " ne peut pas trier non_nombre : il n'a pas d'ordre défini");
    }
    if (value.is_entier() || value.is_decimal())
    {
        return SortCategory::Numeric;
    }
    if (value.is_texte())
    {
        return SortCategory::Texte;
    }
    if (value.is_symbole())
    {
        return SortCategory::Symbole;
    }
    runtime.raise_runtime_error(
        site,
        context + " ne peut trier que des valeurs Entier/Décimal, Texte ou Symbole homogènes; "
                  "type reçu: " +
            value.type_name());
    return SortCategory::Numeric; // unreachable, see the note in enumerate() above.
}

// Validates every value shares one sortable category (established by the
// first element) and returns it. An empty input has no category to check.
std::optional<SortCategory> require_homogeneous_sort_category(
    IRuntime &runtime,
    const std::vector<Value> &values,
    const std::string &context,
    const RuntimeSite &site)
{
    if (values.empty())
    {
        return std::nullopt;
    }
    const SortCategory category = sort_category(runtime, values.front(), context, site);
    for (std::size_t index = 1; index < values.size(); ++index)
    {
        if (sort_category(runtime, values[index], context, site) != category)
        {
            runtime.raise_runtime_error(
                site, context + " ne peut pas trier des valeurs de types différents ensemble");
        }
    }
    return category;
}

// Matches the language's own '<' operator: two Entier compare exactly, any
// other numeric mix promotes to Décimal. Introducing a stricter (e.g. exact
// int64-vs-double) comparison here would make Collections.trier disagree
// with what `a < b` already means for the same two values in Lumiere source.
bool sort_less(const Value &left, const Value &right, SortCategory category)
{
    switch (category)
    {
    case SortCategory::Numeric:
        if (left.is_entier() && right.is_entier())
        {
            return left.as_entier() < right.as_entier();
        }
        {
            const double left_value = left.is_entier() ? static_cast<double>(left.as_entier()) : left.as_decimal();
            const double right_value = right.is_entier() ? static_cast<double>(right.as_entier()) : right.as_decimal();
            return left_value < right_value;
        }
    case SortCategory::Texte:
        // Byte-wise comparison of well-formed UTF-8 already orders text by
        // Unicode scalar value; UTF-8 was designed to preserve that order.
        return left.as_texte() < right.as_texte();
    case SortCategory::Symbole:
        return left.as_symbole() < right.as_symbole();
    }
    return false;
}

}

void register_collections_module(Module &module)
{
    const auto &make_native_function = native_function_factory();

    stdlib_bind_public_function(
        module,
        make_native_function,
        "étendue",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 3, "Collections.étendue", call_site);
            const int64_t start = stdlib_expect_integer(runtime, args[0].value, "Collections.étendue", call_site);
            const int64_t stop = stdlib_expect_integer(runtime, args[1].value, "Collections.étendue", call_site);
            const int64_t step = stdlib_expect_integer(runtime, args[2].value, "Collections.étendue", call_site);
            if (step == 0)
            {
                runtime.raise_runtime_error(call_site, "Collections.étendue: pas ne peut pas être zéro");
            }
            if ((step > 0 && start >= stop) || (step < 0 && start <= stop))
            {
                return make_list({}, runtime, call_site, "Entier");
            }

            std::vector<Value> elements;
            for (int64_t current = start; (step > 0) ? (current < stop) : (current > stop);)
            {
                elements.push_back(Value::entier(current));
                const auto next = numeric::add(current, step);
                if (!next)
                {
                    runtime.raise_runtime_error(call_site, "Collections.étendue: dépassement de capacité");
                }
                current = *next;
            }
            return make_list(std::move(elements), runtime, call_site, "Entier");
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "transformer",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Collections.transformer", call_site);
            require_function(runtime, args[1].value, "Collections.transformer", call_site);

            const std::vector<Value> items = enumerate(runtime, args[0].value, "Collections.transformer", call_site);
            std::vector<Value> results;
            results.reserve(items.size());
            for (const Value &item : items)
            {
                results.push_back(invoke(runtime, args[1].value, {RuntimeArgument{"", item}}, call_site));
            }
            return make_list(std::move(results), runtime, call_site);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "filtrer",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Collections.filtrer", call_site);
            require_function(runtime, args[1].value, "Collections.filtrer", call_site);

            std::vector<Value> results;
            for (const Value &item : enumerate(runtime, args[0].value, "Collections.filtrer", call_site))
            {
                if (call_predicate(runtime, args[1].value, item, "Collections.filtrer", call_site))
                {
                    results.push_back(item);
                }
            }
            return make_list(std::move(results), runtime, call_site);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "réduire",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 3, "Collections.réduire", call_site);
            require_function(runtime, args[2].value, "Collections.réduire", call_site);

            Value accumulator = args[1].value;
            for (const Value &item : enumerate(runtime, args[0].value, "Collections.réduire", call_site))
            {
                accumulator = invoke(
                    runtime, args[2].value, {RuntimeArgument{"", accumulator}, RuntimeArgument{"", item}}, call_site);
            }
            return accumulator;
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "trouver",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Collections.trouver", call_site);
            require_function(runtime, args[1].value, "Collections.trouver", call_site);

            for (const Value &item : enumerate(runtime, args[0].value, "Collections.trouver", call_site))
            {
                if (call_predicate(runtime, args[1].value, item, "Collections.trouver", call_site))
                {
                    return item;
                }
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "position",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Collections.position", call_site);
            require_function(runtime, args[1].value, "Collections.position", call_site);

            int64_t index = 0;
            for (const Value &item : enumerate(runtime, args[0].value, "Collections.position", call_site))
            {
                if (call_predicate(runtime, args[1].value, item, "Collections.position", call_site))
                {
                    return Value::entier(index);
                }
                ++index;
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "tout",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Collections.tout", call_site);
            require_function(runtime, args[1].value, "Collections.tout", call_site);

            for (const Value &item : enumerate(runtime, args[0].value, "Collections.tout", call_site))
            {
                if (!call_predicate(runtime, args[1].value, item, "Collections.tout", call_site))
                {
                    return Value::logique(false);
                }
            }
            return Value::logique(true);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "au_moins_un",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Collections.au_moins_un", call_site);
            require_function(runtime, args[1].value, "Collections.au_moins_un", call_site);

            for (const Value &item : enumerate(runtime, args[0].value, "Collections.au_moins_un", call_site))
            {
                if (call_predicate(runtime, args[1].value, item, "Collections.au_moins_un", call_site))
                {
                    return Value::logique(true);
                }
            }
            return Value::logique(false);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "trier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Collections.trier", call_site);

            std::vector<Value> values = enumerate(runtime, args[0].value, "Collections.trier", call_site);
            const auto category = require_homogeneous_sort_category(runtime, values, "Collections.trier", call_site);
            if (category)
            {
                std::stable_sort(values.begin(), values.end(), [&](const Value &a, const Value &b) {
                    return sort_less(a, b, *category);
                });
            }
            return make_list(std::move(values), runtime, call_site);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "trier_par",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "Collections.trier_par", call_site);
            require_function(runtime, args[1].value, "Collections.trier_par", call_site);

            const std::vector<Value> values = enumerate(runtime, args[0].value, "Collections.trier_par", call_site);
            std::vector<Value> keys;
            keys.reserve(values.size());
            for (const Value &item : values)
            {
                keys.push_back(invoke(runtime, args[1].value, {RuntimeArgument{"", item}}, call_site));
            }
            const auto category =
                require_homogeneous_sort_category(runtime, keys, "Collections.trier_par", call_site);

            std::vector<std::pair<Value, Value>> keyed;
            keyed.reserve(values.size());
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                keyed.emplace_back(std::move(keys[index]), values[index]);
            }
            if (category)
            {
                std::stable_sort(keyed.begin(), keyed.end(), [&](const auto &a, const auto &b) {
                    return sort_less(a.first, b.first, *category);
                });
            }

            std::vector<Value> results;
            results.reserve(keyed.size());
            for (auto &entry : keyed)
            {
                results.push_back(std::move(entry.second));
            }
            return make_list(std::move(results), runtime, call_site);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "inverser",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "Collections.inverser", call_site);

            std::vector<Value> values = enumerate(runtime, args[0].value, "Collections.inverser", call_site);
            std::reverse(values.begin(), values.end());
            return make_list(std::move(values), runtime, call_site);
        });
}

}
