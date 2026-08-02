#pragma once

#include "lumiere/analysis/semantic_analysis.hpp"
#include "lumiere/parser/type_expr.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumiere
{

struct NativeParameterSpec
{
    std::string name;
    TypeExpr type;
    bool optional = false;
};

struct NativeCallableSpec
{
    std::string name;
    std::vector<NativeParameterSpec> parameters;
    TypeExpr return_type;
    bool variadic = false;
    TypeExpr variadic_type;
};

/**
 * Returns the backend-independent signatures of Lumiere's core native
 * functions. Module-native APIs are migrated through the same descriptor
 * shape during N0.
 */
[[nodiscard]] const std::vector<NativeCallableSpec> &core_native_signatures();

/**
 * Returns the exact public semantic surface of a built-in module.
 */
[[nodiscard]] std::optional<SemanticModuleExports>
native_module_exports(std::string_view module_name);

} // namespace lumiere
