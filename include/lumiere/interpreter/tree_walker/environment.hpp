#pragma once

#include "lumiere/interpreter/tree_walker/runtime.hpp"
#include "lumiere/interpreter/runtime/value.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace lumiere
{

//  A single scope frame in the scope chain.
//  Each block, function call, and catch clause
//  gets its own Environment pushed on top of
//  its parent.
class Environment
{
public:
    struct Binding
    {
        Value value = Value::rien();
        bool is_fixe = false;
        std::string declared_type;
    };

    explicit Environment(std::shared_ptr<Environment> parent = nullptr)
        : m_parent_owner(std::move(parent)),
          m_parent(m_parent_owner.get()) {}

    // Creates a new binding in THIS scope only.
    // Throws if the name is already defined here
    void define(const std::string &name, Value val, std::string declared_type = {})
    {
        if (m_values.count(name))
        {
            throw RuntimeError(
                "le symbole '" + name + "' est deja declare dans cette portee"
            );
        }
        m_values[name] = Binding{std::move(val), false, std::move(declared_type)};
    }

    // Walks up the scope chain until it finds
    // the name or runs out of scopes.
    Value get(const std::string &name) const
    {
        auto it = m_values.find(name);
        if (it != m_values.end())
        {
            return it->second.value;
        }
        if (m_parent)
        {
            return m_parent->get(name);
        }
        throw RuntimeError(
            "le symbole '" + name + "' est introuvable dans la portee courante"
        );
    }

    bool contains(const std::string &name) const
    {
        if (m_values.count(name))
        {
            return true;
        }

        return m_parent != nullptr && m_parent->contains(name);
    }

    // Walks up the scope chain and updates the
    // first scope where the name exists.
    // Throws if the name is not defined anywhere
    // cannot assign to an undeclared var.
    // Also enforces fixe immutability.
    void assign(const std::string &name, Value val)
    {
        auto it = m_values.find(name);
        if (it != m_values.end())
        {
            if (it->second.is_fixe)
            {
                throw RuntimeError(
                    "le symbole '" + name + "' est fixe et ne peut pas etre modifie"
                );
            }
            it->second.value = std::move(val);
            return;
        }
        if (m_parent)
        {
            m_parent->assign(name, std::move(val));
            return;
        }
        throw RuntimeError(
            "le symbole '" + name + "' est introuvable dans la portee courante"
        );
    }

  
    // Same as define but marks the binding as
    // immutable. Used for `soit fixe`.
    void define_fixe(const std::string &name, Value val, std::string declared_type = {})
    {
        define(name, std::move(val), std::move(declared_type));
        m_values.at(name).is_fixe = true;
    }

    std::string declared_type_of(const std::string &name) const
    {
        auto it = m_values.find(name);
        if (it != m_values.end())
        {
            return it->second.declared_type;
        }
        if (m_parent)
        {
            return m_parent->declared_type_of(name);
        }
        throw RuntimeError(
            "le symbole '" + name + "' est introuvable dans la portee courante"
        );
    }

    Environment *parent() const { return m_parent; }

    void set_source_path(std::string path)
    {
        m_source_path = std::make_unique<std::string>(std::move(path));
    }

    const std::string &source_path() const
    {
        if (m_source_path)
            return *m_source_path;
        if (m_parent)
            return m_parent->source_path();
        static const std::string empty;
        return empty;
    }

    void set_source_identity(std::string identity)
    {
        m_source_identity = std::make_unique<std::string>(std::move(identity));
    }

    const std::string &source_identity() const
    {
        if (m_source_identity)
            return *m_source_identity;
        if (m_parent)
            return m_parent->source_identity();
        return source_path();
    }

    void define_type_alias(const std::string &name, TypeExpr target)
    {
        if (!m_type_aliases)
            m_type_aliases = std::make_unique<AliasTable>();
        m_type_aliases->insert_or_assign(name, std::move(target));
    }

    const TypeExpr *find_type_alias(const std::string &name) const
    {
        if (m_type_aliases)
        {
            const auto alias = m_type_aliases->find(name);
            if (alias != m_type_aliases->end())
                return &alias->second;
        }
        return m_parent ? m_parent->find_type_alias(name) : nullptr;
    }

    Value find_nominal_value(const std::string &identity) const
    {
        for (const Environment *scope = this; scope; scope = scope->m_parent)
        {
            for (const auto &[name, binding] : scope->m_values)
            {
                static_cast<void>(name);
                const Value &value = binding.value;
                if (value.is_classe() && value.as_classe()->type_identity == identity)
                    return value;
                if (value.is_interface() && value.as_interface()->type_identity == identity)
                    return value;
                if (!value.is_objet() || value.as_objet()->klass != nullptr)
                    continue;
                for (const auto &[member_name, member] : value.as_objet()->fields)
                {
                    static_cast<void>(member_name);
                    if (member.is_classe() && member.as_classe()->type_identity == identity)
                        return member;
                    if (member.is_interface() && member.as_interface()->type_identity == identity)
                        return member;
                }
            }
        }
        return Value::rien();
    }

    TypeExpr resolve_type_aliases(const TypeExpr &type,
                                 std::unordered_set<const TypeExpr *> resolving = {},
                                 bool resolve_nominal = true) const
    {
        if (type.kind == TypeExprKind::NAMED)
        {
            for (const Environment *scope = this; scope; scope = scope->m_parent)
            {
                if (const auto value = scope->m_values.find(type.name);
                    resolve_nominal && value != scope->m_values.end() &&
                    (value->second.value.is_classe() || value->second.value.is_interface()))
                {
                    const auto &type_identity = value->second.value.is_classe()
                        ? value->second.value.as_classe()->type_identity
                        : value->second.value.as_interface()->type_identity;
                    if (!type_identity.empty())
                    {
                        Token identity = type.source;
                        identity.lexeme = type_identity;
                        return TypeExpr::named(identity);
                    }
                }
                if (!scope->m_type_aliases)
                    continue;
                const auto alias = scope->m_type_aliases->find(type.name);
                if (alias == scope->m_type_aliases->end())
                    continue;
                if (!resolve_nominal && alias->second.kind == TypeExprKind::NAMED &&
                    alias->second.name.find('@') != std::string::npos)
                    return type;
                if (!resolving.insert(&alias->second).second)
                    throw std::invalid_argument("cycle d'alias de type impliquant '" + type.name + "'");
                // Resolve dependencies where the alias was defined, not where
                // it was used: an inner scope may shadow one of those names.
                return scope->resolve_type_aliases(alias->second, std::move(resolving), resolve_nominal);
            }
            return type;
        }
        TypeExpr resolved = type;
        for (TypeExpr &child : resolved.children)
            child = resolve_type_aliases(child, resolving, resolve_nominal);
        return resolved;
    }

private:
    using AliasTable = std::unordered_map<std::string, TypeExpr>;
    std::unique_ptr<AliasTable> m_type_aliases;
    std::unique_ptr<std::string> m_source_path;
    std::unique_ptr<std::string> m_source_identity;
    std::shared_ptr<Environment> m_parent_owner;
    Environment                          *m_parent = nullptr;
    std::unordered_map<std::string, Binding> m_values;
};


//  RAII wrapper that pushes a new Environment
//  on construction and restores the previous
//  one on destruction — even if an exception
//  (including ReturnSignal) unwinds the stack.
class ScopeGuard
{
public:
    // Reference to the caller's current environment pointer, so reassigning it
    // here updates the original variable rather than a local copy.
    ScopeGuard(Environment *&current, std::shared_ptr<Environment> &current_owner)
        : m_current(current),
          m_current_owner(current_owner),
          m_previous(current),
          m_previous_owner(current_owner)
    {
        m_current_owner = std::make_shared<Environment>(m_previous_owner);
        m_current = m_current_owner.get();
    }

    ~ScopeGuard()
    {
        m_current_owner = m_previous_owner;
        m_current = m_previous;
    }

    // non-copyable, non-movable
    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator=(const ScopeGuard &) = delete;

private:
    // Reference to the caller's current Environment pointer, so the guard can
    // switch the active scope and later restore it.
    Environment *&m_current;

    // Reference to the caller's shared owner of the current Environment.
    // This keeps the active scope alive and lets the guard restore it.
    // without the owner, reassigning the pointer would leave the object out of reach and dangling
    std::shared_ptr<Environment> &m_current_owner;

    // Saved raw pointer to the previously active Environment before entering
    // the new scope.
    Environment *m_previous;

    // Saved shared owner of the previously active Environment, used to keep
    // the old scope alive and restore ownership when the guard is destroyed.
    std::shared_ptr<Environment> m_previous_owner;
};

} // namespace lumiere
