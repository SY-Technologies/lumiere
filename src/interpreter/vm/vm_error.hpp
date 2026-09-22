#pragma once

#include "lumiere/interpreter/runtime/runtime_site.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace lumiere
{

class VmRuntimeError : public std::runtime_error
{
public:
    explicit VmRuntimeError(const std::string &message)
        : std::runtime_error(message) {}

    VmRuntimeError(const std::string &message, RuntimeSite site)
        : std::runtime_error(message), m_site(std::move(site)) {}

    [[nodiscard]] const std::optional<RuntimeSite> &site() const noexcept { return m_site; }

private:
    std::optional<RuntimeSite> m_site;
};

} // namespace lumiere
