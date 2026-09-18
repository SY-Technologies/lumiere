#pragma once

#include "lumiere/parser/type_expr.hpp"
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace lumiere
{

using TypeAliasTable = std::unordered_map<std::string, TypeExpr>;

inline TypeExpr resolve_type_aliases(const TypeExpr &type, const TypeAliasTable &aliases,
                                    std::unordered_set<std::string> resolving = {},
                                    bool resolve_nominal = true)
{
    if (type.kind == TypeExprKind::NAMED)
    {
        const auto alias = aliases.find(type.name);
        if (alias == aliases.end())
            return type;
        // Value lookup needs the binding (e.g. an interface in a réalise clause),
        // while annotations need the declaration identity stored in that binding.
        if (!resolve_nominal && alias->second.kind == TypeExprKind::NAMED &&
            alias->second.name.find('@') != std::string::npos)
            return type;
        if (!resolving.insert(type.name).second)
            throw std::invalid_argument("cycle d'alias de type impliquant '" + type.name + "'");
        return resolve_type_aliases(alias->second, aliases, std::move(resolving), resolve_nominal);
    }
    TypeExpr resolved = type;
    for (TypeExpr &child : resolved.children)
        child = resolve_type_aliases(child, aliases, resolving, resolve_nominal);
    return resolved;
}

} // namespace lumiere
