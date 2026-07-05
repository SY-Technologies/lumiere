#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

namespace lumiere
{

    ClassDeclStmt *TreeWalker::resolve_parent_class(const ClassDeclStmt &klass) const
    {
        if (klass.parent.lexeme.empty() || m_env == nullptr || !m_env->contains(klass.parent.lexeme))
        {
            return nullptr;
        }

        const Value parent = m_env->get(klass.parent.lexeme);
        if (!parent.is_classe())
        {
            return nullptr;
        }

        return class_decl(parent.as_classe());
    }

    VarDeclStmt *TreeWalker::find_field_decl(ClassDeclStmt &klass, const std::string &name) const
    {
        for (auto &member : klass.members)
        {
            if (auto *field = dynamic_cast<VarDeclStmt *>(member.get()))
            {
                if (field->name.lexeme == name)
                {
                    return field;
                }
            }
        }

        if (ClassDeclStmt *parent = resolve_parent_class(klass))
        {
            return find_field_decl(*parent, name);
        }

        return nullptr;
    }

    FunctionDeclStmt *TreeWalker::find_method_decl(ClassDeclStmt &klass, const std::string &name) const
    {
        for (auto &member : klass.members)
        {
            if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
            {
                if (method->name.lexeme == name)
                {
                    return method;
                }
            }
        }

        if (ClassDeclStmt *parent = resolve_parent_class(klass))
        {
            return find_method_decl(*parent, name);
        }

        return nullptr;
    }

    FunctionDeclStmt *TreeWalker::find_interface_method_decl(InterfaceDeclStmt &iface, const std::string &name) const
    {
        for (auto &member : iface.methods)
        {
            if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
            {
                if (method->name.lexeme == name)
                {
                    return method;
                }
            }
        }

        return nullptr;
    }

    void TreeWalker::validate_class_interfaces(ClassDeclStmt &klass) const
    {
        if (m_env == nullptr)
        {
            throw_runtime_error(klass.name, "environnement d'execution absent");
        }

        for (const Token &interface_name : klass.interfaces)
        {
            if (!m_env->contains(interface_name.lexeme))
            {
                throw_runtime_error(interface_name, "interface introuvable: " + interface_name.lexeme);
            }

            const Value interface_value = m_env->get(interface_name.lexeme);
            if (!interface_value.is_interface())
            {
                throw_runtime_error(interface_name, "le symbole n'est pas une interface: " + interface_name.lexeme);
            }

            InterfaceDeclStmt *iface_decl = interface_decl(interface_value.as_interface());
            if (iface_decl == nullptr)
            {
                throw_runtime_error(interface_name, "interface non compatible avec ce backend: " + interface_name.lexeme);
            }

            for (auto &member : iface_decl->methods)
            {
                auto *required_method = dynamic_cast<FunctionDeclStmt *>(member.get());
                if (required_method == nullptr)
                {
                    continue;
                }

                if (find_method_decl(klass, required_method->name.lexeme) == nullptr)
                {
                    throw_runtime_error(
                        klass.name,
                        "la classe " + klass.name.lexeme + " ne realise pas la methode requise " +
                            interface_name.lexeme + "." + required_method->name.lexeme);
                }
            }
        }
    }

    bool TreeWalker::access_uses_ici(const Expr &expr) const
    {
        auto *identifier = dynamic_cast<const IdentifierExpr *>(&expr);
        return identifier != nullptr &&
               (identifier->name.type == TokenType::ICI || identifier->name.type == TokenType::PARENT);
    }

} // namespace lumiere
