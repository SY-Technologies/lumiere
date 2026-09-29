#pragma once

#include <string>
#include <utility>

#include "lumiere/interpreter/runtime/runtime_site.hpp"
#include "lumiere/interpreter/runtime/value.hpp"

namespace lumiere
{

struct RuntimeArgument
{
    RuntimeArgument(std::string argument_name,
                    Value argument_value,
                    RuntimeSite argument_site = {})
        : name(std::move(argument_name)),
          value(std::move(argument_value)),
          site(std::move(argument_site)) {}

    std::string name;
    Value value = Value::rien();
    RuntimeSite site;
};

} // namespace lumiere
