#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lumiere/interpreter/runtime/value.hpp"

namespace lumiere
{

using NativeFunctionFactory = std::function<std::shared_ptr<LumiereFunction>(LumiereFunction::NativeHandler)>;
using NativeMethodFactory = std::function<std::shared_ptr<LumiereFunction>(Value, LumiereFunction::NativeHandler)>;

void register_chemin_module(Module &module, const NativeFunctionFactory &make_native_function);
void register_fichier_module(Module &module, const NativeFunctionFactory &make_native_function);
void register_texte_module(Module &module, const NativeFunctionFactory &make_native_function);
void register_maths_module(Module &module, const NativeFunctionFactory &make_native_function);
void register_temps_module(Module &module, const NativeFunctionFactory &make_native_function);
void register_aleatoire_module(Module &module, const NativeFunctionFactory &make_native_function);
void register_luminet_module(Module &module, const NativeFunctionFactory &make_native_function);
struct LumiTestRuntimeOptions
{
    bool verbose = false;
    bool stop_on_failure = false;
    std::string filter;
};

struct LumiTestCaseResult
{
    std::string name;
    std::string source_path;
    int line = 0;
    int column = 0;
    bool passed = true;
    std::string failure_message;
};

struct LumiTestRunSummary
{
    std::vector<LumiTestCaseResult> results;
    int executed = 0;
    int failed = 0;
};

struct LumiTestModuleState : RuntimeModuleState
{
    struct GroupContext
    {
        std::string name;
        std::string full_name;
        std::vector<Value> before_all_hooks;
        std::vector<Value> before_each_hooks;
        std::vector<Value> after_each_hooks;
        std::vector<Value> after_all_hooks;
        bool before_all_ran = false;
        bool has_executed_test = false;
    };

    LumiTestRuntimeOptions options;
    LumiTestRunSummary summary;
    std::vector<std::string> group_stack;
    std::vector<GroupContext> group_contexts;
    bool abort_requested = false;
};

void register_lumitest_module(Module &module,
                              const NativeFunctionFactory &make_native_function,
                              std::shared_ptr<LumiTestModuleState> state);
bool try_resolve_texte_native_member(const Value &object,
                                     std::string_view member_name,
                                     const NativeMethodFactory &make_native_method,
                                     Value &result);

} // namespace lumiere
