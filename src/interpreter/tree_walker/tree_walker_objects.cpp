#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

namespace lumiere
{

    ClassDeclStmt *TreeWalker::resolve_parent_class(const ClassDeclStmt &klass) const
    {
        if (klass.parent.empty() || m_env == nullptr || !m_env->contains(klass.parent.to_string()))
        {
            return nullptr;
        }

        const Value parent = m_env->get(klass.parent.to_string());
        if (!parent.is_classe())
        {
            return nullptr;
        }

        return class_decl(parent.as_classe());
    }

    bool TreeWalker::method_signatures_match(const FunctionDeclStmt &expected,
                                             const FunctionDeclStmt &actual) const
    {
        if (expected.params.size() != actual.params.size())
        {
            return false;
        }

        if (expected.return_type.to_string() != actual.return_type.to_string())
        {
            return false;
        }

        for (std::size_t i = 0; i < expected.params.size(); ++i)
        {
            if (expected.params[i].name != actual.params[i].name)
            {
                return false;
            }
            if (expected.params[i].type.to_string() != actual.params[i].type.to_string())
            {
                return false;
            }
            if (static_cast<bool>(expected.params[i].default_value) != static_cast<bool>(actual.params[i].default_value))
            {
                return false;
            }
        }

        return true;
    }

    VarDeclStmt *TreeWalker::find_field_decl(const std::shared_ptr<LumiereClass> &klass, const std::string &name) const
    {
        ClassDeclStmt *klass_decl = class_decl(klass);
        if (klass_decl == nullptr)
        {
            return nullptr;
        }

        for (auto &member : klass_decl->members)
        {
            if (auto *field = dynamic_cast<VarDeclStmt *>(member.get()))
            {
                if (field->name.lexeme == name)
                {
                    return field;
                }
            }
        }

        if (std::shared_ptr<LumiereClass> parent = parent_class(klass))
        {
            return find_field_decl(parent, name);
        }

        return nullptr;
    }

    FunctionDeclStmt *TreeWalker::find_method_decl(const std::shared_ptr<LumiereClass> &klass, const std::string &name) const
    {
        ClassDeclStmt *klass_decl = class_decl(klass);
        if (klass_decl == nullptr)
        {
            return nullptr;
        }

        for (auto &member : klass_decl->members)
        {
            if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
            {
                if (method->name.lexeme == name)
                {
                    return method;
                }
            }
        }

        if (std::shared_ptr<LumiereClass> parent = parent_class(klass))
        {
            return find_method_decl(parent, name);
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

    void TreeWalker::validate_class_interfaces(ClassDeclStmt &klass,
                                               const std::shared_ptr<LumiereClass> &class_value) const
    {
        if (m_env == nullptr)
        {
            throw_runtime_error(klass.name, "environnement d'execution absent");
        }

        for (const TypeExpr &interface_name : klass.interfaces)
        {
            std::string name = interface_name.to_string();
            std::unordered_set<std::string> visited;
            while (visited.insert(name).second)
            {
                const auto alias =
                    m_type_aliases.find(name);
                if (alias == m_type_aliases.end() ||
                    alias->second.kind !=
                        TypeExprKind::NAMED)
                {
                    break;
                }
                name = alias->second.name;
            }
            if (!m_env->contains(name))
            {
                throw_runtime_error(interface_name.source, "interface introuvable: " + name);
            }

            const Value interface_value = m_env->get(name);
            if (!interface_value.is_interface())
            {
                throw_runtime_error(interface_name.source, "le symbole n'est pas une interface: " + name);
            }

            InterfaceDeclStmt *iface_decl = interface_decl(interface_value.as_interface());
            if (name == "Erreur")
            {
                continue;
            }
            if (iface_decl == nullptr)
            {
                throw_runtime_error(interface_name.source, "interface non compatible avec ce backend: " + name);
            }

            for (auto &member : iface_decl->methods)
            {
                auto *required_method = dynamic_cast<FunctionDeclStmt *>(member.get());
                if (required_method == nullptr)
                {
                    continue;
                }

                if (find_method_decl(class_value, required_method->name.lexeme) == nullptr)
                {
                    throw_runtime_error(
                        klass.name,
                        "la classe " + klass.name.lexeme + " ne realise pas la methode requise " +
                            name + "." + required_method->name.lexeme);
                }

                FunctionDeclStmt *implemented_method = find_method_decl(class_value, required_method->name.lexeme);
                if (implemented_method != nullptr && !method_signatures_match(*required_method, *implemented_method))
                {
                    throw_runtime_error(
                        implemented_method->name,
                        "la methode " + klass.name.lexeme + "." + implemented_method->name.lexeme +
                            " ne respecte pas la signature requise par l'interface " + name);
                }

                if (implemented_method != nullptr && implemented_method->is_prive)
                {
                    throw_runtime_error(
                        implemented_method->name,
                        "la methode " + klass.name.lexeme + "." + implemented_method->name.lexeme +
                            " ne peut pas etre privee car elle realise l'interface " + name);
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
