#pragma once

#include <string_view>

#include "lumiere/interpreter/runtime/iruntime.hpp"
#include "lumiere/interpreter/runtime/value.hpp"

namespace lumiere
{

/**
 * @brief Whether `valeur en <type>` names a conversion the language has.
 *
 * `type_name` is fully resolved: an alias is its expansion. The analyzer
 * refuses every other target, because no value could ever be converted to it.
 */
bool is_conversion_target(std::string_view type_name);

/**
 * @brief The value of `operand en target`.
 *
 * Raises at @p site when this operand has no conversion to @p target. Both
 * engines call this; they used to carry one each, and they had drifted on
 * every operand either of them refused.
 */
Value convert(IRuntime &runtime, const Value &operand, std::string_view target, const RuntimeSite &site);

} // namespace lumiere
