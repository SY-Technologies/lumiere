#pragma once

#include "lumiere/interpreter/runtime/value.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace lumiere
{

using NativeFunction = Value (*)(const std::vector<Value> &args);

std::unordered_map<std::string, NativeFunction> native_globals();

} // namespace lumiere
