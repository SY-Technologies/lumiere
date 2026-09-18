#pragma once

#include "lumiere/interpreter/runtime/value.hpp"

namespace lumiere
{

inline std::size_t find_collection_type_union(std::string_view type)
{
    int depth = 0;
    for (std::size_t i = 0; i < type.size(); ++i)
    {
        if (type[i] == '[') ++depth;
        else if (type[i] == ']') --depth;
        else if (type[i] == '|' && depth == 0) return i;
    }
    return std::string_view::npos;
}

// Re-annotation may refine Universel, but must never erase a concrete contract.
// Distinct concrete types are conservatively incompatible, even if current
// elements happen to satisfy both (in particular, empty collections).
inline bool merge_collection_type(std::string &requested, const std::string &existing)
{
    if (requested == existing || existing == "Universel")
        return true;
    if (requested != "Universel")
        return false;
    requested = existing;
    return true;
}

template <typename Constraint>
bool merge_collection_constraint(std::optional<Constraint> &existing, Constraint requested)
{
    if (existing)
    {
        if constexpr (requires { requested.key_type; })
        {
            if (!merge_collection_type(requested.key_type, existing->key_type) ||
                !merge_collection_type(requested.value_type, existing->value_type))
                return false;
        }
        else
        {
            if (!merge_collection_type(requested.element_type, existing->element_type))
                return false;
            if constexpr (requires { requested.length; })
            {
                if (requested.length != existing->length)
                    return false;
            }
        }
    }
    // Commit only after every field is compatible.
    existing = std::move(requested);
    return true;
}

} // namespace lumiere
