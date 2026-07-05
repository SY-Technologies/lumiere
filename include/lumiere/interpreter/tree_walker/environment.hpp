#pragma once

#include "lumiere/interpreter/tree_walker/runtime.hpp"
#include "lumiere/interpreter/runtime/value.hpp"
#include <memory>
#include <string>
#include <unordered_map>

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

private:
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
