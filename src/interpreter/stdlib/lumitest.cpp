#include "lumiere/interpreter/stdlib/modules.hpp"

#include <cmath>
#include <exception>
#include <sstream>

#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/tree_walker/runtime.hpp"

namespace lumiere
{

namespace
{

std::string lumitest_optional_message(IRuntime &runtime,
                                      const std::vector<RuntimeArgument> &args,
                                      std::size_t index,
                                      const std::string &signature,
                                      const RuntimeSite &site)
{
    if (args.size() <= index)
    {
        return {};
    }

    return stdlib_expect_text(runtime, args[index].value, signature, site);
}

std::string join_group_name(const std::vector<std::string> &groups)
{
    std::string full_name;
    for (const std::string &group : groups)
    {
        if (!full_name.empty())
        {
            full_name += " > ";
        }
        full_name += group;
    }
    return full_name;
}

std::string join_test_name(const LumiTestModuleState &state, const std::string &test_name)
{
    const std::string group_name = join_group_name(state.group_stack);
    if (group_name.empty())
    {
        return test_name;
    }

    return group_name + " > " + test_name;
}

bool contains_filter(const std::string &haystack, const std::string &needle)
{
    return needle.empty() || haystack.find(needle) != std::string::npos;
}

void run_hook(IRuntime &runtime,
              const Value &hook,
              const RuntimeSite &site,
              const std::string &signature)
{
    if (!hook.is_fonction())
    {
        runtime.raise_runtime_error(site, signature + " requiert une fonction");
    }

    const std::vector<RuntimeArgument> no_args;
    runtime.call(hook, NativeArgs{nullptr, &no_args, site});
}

Value invoke_callback(IRuntime &runtime,
                      const Value &callback,
                      const RuntimeSite &site,
                      const Value &context)
{
    if (!callback.is_fonction())
    {
        runtime.raise_runtime_error(site, "callback LumiTest invalide");
    }

    const auto function = callback.as_fonction();
    std::vector<RuntimeArgument> args;
    if (!context.is_rien() && function != nullptr && function->max_arity >= 1)
    {
        args.push_back(RuntimeArgument{"", context});
    }

    return runtime.call(callback, NativeArgs{nullptr, &args, site});
}

void run_before_all_hooks_if_needed(IRuntime &runtime,
                                    LumiTestModuleState &state,
                                    const RuntimeSite &site)
{
    for (auto &group : state.group_contexts)
    {
        if (group.before_all_ran)
        {
            continue;
        }

        group.before_all_ran = true;
        for (const Value &hook : group.before_all_hooks)
        {
            run_hook(runtime, hook, site, "LumiTest.avant_tout");
        }
    }
}

void run_after_each_hooks(IRuntime &runtime,
                          const LumiTestModuleState &state,
                          const RuntimeSite &site)
{
    for (auto group_it = state.group_contexts.rbegin(); group_it != state.group_contexts.rend(); ++group_it)
    {
        for (const Value &hook : group_it->after_each_hooks)
        {
            run_hook(runtime, hook, site, "LumiTest.après_chaque");
        }
    }
}

[[noreturn]] void fail_assertion(IRuntime &runtime,
                                 const RuntimeSite &site,
                                 const std::string &kind,
                                 const std::string &detail,
                                 const std::string &message)
{
    std::string full = kind;
    if (!detail.empty())
    {
        full += " échoué";
        full += ":\n";
        full += detail;
    }
    else
    {
        full += " échoué";
    }

    if (!message.empty())
    {
        full += "\nmessage: ";
        full += message;
    }

    runtime.raise_runtime_error(site, full);
    std::terminate();
}

bool value_contains(IRuntime &runtime,
                    const Value &container,
                    const Value &expected,
                    const RuntimeSite &site)
{
    if (container.is_texte())
    {
        const std::string haystack = container.as_texte();
        const std::string needle = stdlib_expect_text(runtime, expected, "LumiTest.vérifier_contient", site);
        return haystack.find(needle) != std::string::npos;
    }

    if (container.is_liste())
    {
        for (const Value &element : container.as_liste()->elements)
        {
            if (runtime.is_equal(element, expected))
            {
                return true;
            }
        }
        return false;
    }

    if (container.is_ensemble())
    {
        for (const Value &element : container.as_ensemble()->elements)
        {
            if (runtime.is_equal(element, expected))
            {
                return true;
            }
        }
        return false;
    }

    if (container.is_dictionnaire())
    {
        for (const auto &entry : container.as_dictionnaire()->entries)
        {
            if (runtime.is_equal(entry.first, expected))
            {
                return true;
            }
        }
        return false;
    }

    runtime.raise_runtime_error(site,
                                "LumiTest.vérifier_contient requiert une Liste, un Ensemble, un Dictionnaire ou un Texte");
    return false;
}

double as_numeric(IRuntime &runtime,
                  const Value &value,
                  const std::string &signature,
                  const RuntimeSite &site)
{
    if (value.is_decimal())
    {
        return value.as_decimal();
    }

    if (value.is_entier())
    {
        return static_cast<double>(value.as_entier());
    }

    runtime.raise_runtime_error(site, signature + " requiert une valeur numérique");
    return 0.0;
}

void bind_context_methods(const std::shared_ptr<LumiereObject> &context,
                          const std::shared_ptr<LumiereObject> &root)
{
    static const std::vector<std::string> method_names = {
        "test",
        "groupe",
        "avant_tout",
        "avant_chaque",
        "après_chaque",
        "après_tout",
        "vérifier",
        "vérifier_égal",
        "vérifier_différent",
        "vérifier_lance",
        "vérifier_contient",
        "vérifier_approx",
    };

    for (const std::string &name : method_names)
    {
        context->fields[name] = root->fields[name];
    }
}

} // namespace

void register_lumitest_module(Module &module,
                              std::shared_ptr<LumiTestModuleState> state)
{
    const auto &make_native_function = native_function_factory();
    module.state = state;
    auto root = std::make_shared<LumiereObject>();
    auto context_object = std::make_shared<LumiereObject>();
    const Value context_value = Value::objet(context_object);

    root->fields["test"] = Value::fonction(make_native_function(
        [state, context_value](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            if (state->abort_requested)
            {
                return Value::rien();
            }

            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 2, 2, "LumiTest.test", native_args.site);

            const std::string test_name = stdlib_expect_text(runtime, args[0].value, "LumiTest.test", native_args.site);
            if (!args[1].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.test requiert une fonction comme second argument");
            }

            const std::string full_name = join_test_name(*state, test_name);
            if (!contains_filter(full_name, state->options.filter) && !contains_filter(test_name, state->options.filter))
            {
                return Value::rien();
            }

            LumiTestCaseResult result;
            result.name = full_name;
            result.source_path = native_args.site.source_path;

            if (state->options.verbose)
            {
                result.line = native_args.site.line;
                result.column = native_args.site.column;
            }

            for (auto &group : state->group_contexts)
            {
                group.has_executed_test = true;
            }

            try
            {
                run_before_all_hooks_if_needed(runtime, *state, native_args.site);

                for (const auto &group : state->group_contexts)
                {
                    for (const Value &hook : group.before_each_hooks)
                    {
                        run_hook(runtime, hook, native_args.site, "LumiTest.avant_chaque");
                    }
                }

                invoke_callback(runtime, args[1].value, native_args.site, context_value);
                result.passed = true;
            }
            catch (const RuntimeError &error)
            {
                result.passed = false;
                result.failure_message = error.raw_message();
                result.source_path = error.source_path.empty() ? native_args.site.source_path : error.source_path;
                result.line = static_cast<int>(error.line);
                result.column = static_cast<int>(error.column);
            }
            catch (const std::exception &error)
            {
                result.passed = false;
                result.failure_message = error.what();
                result.line = native_args.site.line;
                result.column = native_args.site.column;
            }

            try
            {
                run_after_each_hooks(runtime, *state, native_args.site);
            }
            catch (const RuntimeError &error)
            {
                if (result.passed)
                {
                    result.passed = false;
                    result.failure_message = error.raw_message();
                    result.source_path = error.source_path.empty() ? native_args.site.source_path : error.source_path;
                    result.line = static_cast<int>(error.line);
                    result.column = static_cast<int>(error.column);
                }
            }
            catch (const std::exception &error)
            {
                if (result.passed)
                {
                    result.passed = false;
                    result.failure_message = error.what();
                    result.line = native_args.site.line;
                    result.column = native_args.site.column;
                }
            }

            state->summary.executed += 1;
            if (!result.passed)
            {
                state->summary.failed += 1;
                if (state->options.stop_on_failure)
                {
                    state->abort_requested = true;
                }
            }
            state->summary.results.push_back(std::move(result));
            return Value::rien();
        }));

    root->fields["groupe"] = Value::fonction(make_native_function(
        [state, context_value](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            if (state->abort_requested)
            {
                return Value::rien();
            }

            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 2, 2, "LumiTest.groupe", native_args.site);

            const std::string group_name = stdlib_expect_text(runtime, args[0].value, "LumiTest.groupe", native_args.site);
            if (!args[1].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.groupe requiert une fonction comme second argument");
            }

            state->group_stack.push_back(group_name);
            state->group_contexts.push_back(
                LumiTestModuleState::GroupContext{group_name, join_group_name(state->group_stack), {}, {}, {}, {}, false, false});

            try
            {
                invoke_callback(runtime, args[1].value, native_args.site, context_value);

                if (state->group_contexts.back().before_all_ran)
                {
                    for (const Value &hook : state->group_contexts.back().after_all_hooks)
                    {
                        run_hook(runtime, hook, native_args.site, "LumiTest.après_tout");
                    }
                }
            }
            catch (...)
            {
                state->group_contexts.pop_back();
                state->group_stack.pop_back();
                throw;
            }

            state->group_contexts.pop_back();
            state->group_stack.pop_back();
            return Value::rien();
        }));

    root->fields["avant_tout"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 1, 1, "LumiTest.avant_tout", native_args.site);

            if (state->group_contexts.empty())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.avant_tout doit être appelé dans un groupe");
            }

            if (!args[0].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.avant_tout requiert une fonction");
            }

            state->group_contexts.back().before_all_hooks.push_back(args[0].value);
            return Value::rien();
        }));

    root->fields["avant_chaque"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 1, 1, "LumiTest.avant_chaque", native_args.site);

            if (state->group_contexts.empty())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.avant_chaque doit être appelé dans un groupe");
            }

            if (!args[0].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.avant_chaque requiert une fonction");
            }

            state->group_contexts.back().before_each_hooks.push_back(args[0].value);
            return Value::rien();
        }));

    root->fields["après_chaque"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 1, 1, "LumiTest.après_chaque", native_args.site);

            if (state->group_contexts.empty())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.après_chaque doit être appelé dans un groupe");
            }

            if (!args[0].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.après_chaque requiert une fonction");
            }

            state->group_contexts.back().after_each_hooks.push_back(args[0].value);
            return Value::rien();
        }));

    root->fields["après_tout"] = Value::fonction(make_native_function(
        [state](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 1, 1, "LumiTest.après_tout", native_args.site);

            if (state->group_contexts.empty())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.après_tout doit être appelé dans un groupe");
            }

            if (!args[0].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.après_tout requiert une fonction");
            }

            state->group_contexts.back().after_all_hooks.push_back(args[0].value);
            return Value::rien();
        }));

    root->fields["vérifier"] = Value::fonction(make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 1, 2, "LumiTest.vérifier", native_args.site);

            const std::string message = lumitest_optional_message(runtime, args, 1, "LumiTest.vérifier", native_args.site);
            const bool passed = !args[0].value.is_rien() && (!args[0].value.is_logique() || args[0].value.as_logique());
            if (!passed)
            {
                fail_assertion(runtime, native_args.site, "vérifier", "condition: faux", message);
            }
            return Value::rien();
        }));

    root->fields["vérifier_égal"] = Value::fonction(make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 2, 3, "LumiTest.vérifier_égal", native_args.site);

            const std::string message = lumitest_optional_message(runtime, args, 2, "LumiTest.vérifier_égal", native_args.site);
            if (!runtime.is_equal(args[0].value, args[1].value))
            {
                std::ostringstream detail;
                detail << "attendu: " << runtime.to_text(args[0].value) << "\n";
                detail << "reçu: " << runtime.to_text(args[1].value);
                fail_assertion(runtime, native_args.site, "vérifier_égal", detail.str(), message);
            }
            return Value::rien();
        }));

    root->fields["vérifier_différent"] = Value::fonction(make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 2, 3, "LumiTest.vérifier_différent", native_args.site);

            const std::string message = lumitest_optional_message(runtime, args, 2, "LumiTest.vérifier_différent", native_args.site);
            if (runtime.is_equal(args[0].value, args[1].value))
            {
                std::ostringstream detail;
                detail << "les deux valeurs sont: " << runtime.to_text(args[0].value);
                fail_assertion(runtime, native_args.site, "vérifier_différent", detail.str(), message);
            }
            return Value::rien();
        }));

    root->fields["vérifier_lance"] = Value::fonction(make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 1, 2, "LumiTest.vérifier_lance", native_args.site);

            if (!args[0].value.is_fonction())
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.vérifier_lance requiert une fonction");
            }

            const std::string message = lumitest_optional_message(runtime, args, 1, "LumiTest.vérifier_lance", native_args.site);
            try
            {
                const std::vector<RuntimeArgument> no_args;
                runtime.call(args[0].value, NativeArgs{nullptr, &no_args, native_args.site});
            }
            catch (const std::exception &)
            {
                return Value::rien();
            }

            fail_assertion(runtime, native_args.site, "vérifier_lance", "aucune erreur lancée", message);
        }));

    root->fields["vérifier_contient"] = Value::fonction(make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 2, 3, "LumiTest.vérifier_contient", native_args.site);

            const std::string message = lumitest_optional_message(runtime, args, 2, "LumiTest.vérifier_contient", native_args.site);
            if (!value_contains(runtime, args[0].value, args[1].value, native_args.site))
            {
                std::ostringstream detail;
                detail << "conteneur: " << runtime.to_text(args[0].value) << "\n";
                detail << "élément: " << runtime.to_text(args[1].value);
                fail_assertion(runtime, native_args.site, "vérifier_contient", detail.str(), message);
            }
            return Value::rien();
        }));

    root->fields["vérifier_approx"] = Value::fonction(make_native_function(
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional_range(runtime, args, 3, 4, "LumiTest.vérifier_approx", native_args.site);

            const double expected = as_numeric(runtime, args[0].value, "LumiTest.vérifier_approx", native_args.site);
            const double received = as_numeric(runtime, args[1].value, "LumiTest.vérifier_approx", native_args.site);
            const double tolerance = as_numeric(runtime, args[2].value, "LumiTest.vérifier_approx", native_args.site);
            const std::string message = lumitest_optional_message(runtime, args, 3, "LumiTest.vérifier_approx", native_args.site);

            if (tolerance < 0.0)
            {
                runtime.raise_runtime_error(native_args.site, "LumiTest.vérifier_approx requiert une tolérance positive");
            }

            if (std::fabs(expected - received) > tolerance)
            {
                std::ostringstream detail;
                detail << "attendu: " << expected << "\n";
                detail << "reçu: " << received << "\n";
                detail << "tolérance: " << tolerance;
                fail_assertion(runtime, native_args.site, "vérifier_approx", detail.str(), message);
            }
            return Value::rien();
        }));

    bind_context_methods(context_object, root);

    stdlib_bind_public_value(module, "test", root->fields["test"]);
    stdlib_bind_public_value(module, "groupe", root->fields["groupe"]);
    stdlib_bind_public_value(module, "avant_tout", root->fields["avant_tout"]);
    stdlib_bind_public_value(module, "avant_chaque", root->fields["avant_chaque"]);
    stdlib_bind_public_value(module, "après_chaque", root->fields["après_chaque"]);
    stdlib_bind_public_value(module, "après_tout", root->fields["après_tout"]);
    stdlib_bind_public_value(module, "vérifier", root->fields["vérifier"]);
    stdlib_bind_public_value(module, "vérifier_égal", root->fields["vérifier_égal"]);
    stdlib_bind_public_value(module, "vérifier_différent", root->fields["vérifier_différent"]);
    stdlib_bind_public_value(module, "vérifier_lance", root->fields["vérifier_lance"]);
    stdlib_bind_public_value(module, "vérifier_contient", root->fields["vérifier_contient"]);
    stdlib_bind_public_value(module, "vérifier_approx", root->fields["vérifier_approx"]);
}

} // namespace lumiere
