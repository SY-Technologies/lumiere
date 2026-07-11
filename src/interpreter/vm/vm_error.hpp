#pragma once

#include <stdexcept>
#include <string>

namespace lumiere
{

class VmRuntimeError : public std::runtime_error
{
public:
    explicit VmRuntimeError(const std::string &message)
        : std::runtime_error(message) {}
};

} // namespace lumiere
