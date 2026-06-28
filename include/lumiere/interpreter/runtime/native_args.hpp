#pragma once

#include <cstddef>
#include <vector>

#include "lumiere/interpreter/runtime/runtime_site.hpp"

namespace lumiere
{

struct Value;
struct RuntimeArgument;

struct NativeArgs
{
    const Value *receiver = nullptr;
    const std::vector<RuntimeArgument> *arguments = nullptr;
    RuntimeSite site;
};

} // namespace lumiere
