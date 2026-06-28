#pragma once

#include <string>

#include "lumiere/interpreter/runtime/value.hpp"

namespace lumiere
{

struct RuntimeArgument
{
    std::string name;
    Value value = Value::rien();
};

} // namespace lumiere
