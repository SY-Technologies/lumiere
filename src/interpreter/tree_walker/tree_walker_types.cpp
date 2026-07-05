#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

namespace lumiere
{

    bool TreeWalker::matches_type_name(const Value &value, const Token &type_token) const
    {
        const std::string &full_type_name = type_token.lexeme;
        const std::string::size_type generic_start = full_type_name.find('[');
        const std::string type_name = generic_start == std::string::npos
                                          ? full_type_name
                                          : full_type_name.substr(0, generic_start);
        const std::string generic_spec = generic_start == std::string::npos
                                             ? std::string{}
                                             : full_type_name.substr(generic_start + 1, full_type_name.size() - generic_start - 2);

        if (type_name == "Entier")
        {
            return value.is_entier();
        }
        if (type_name == "Décimal" || type_name == "Decimal")
        {
            return value.is_decimal() || value.is_entier();
        }
        if (type_name == "Logique")
        {
            return value.is_logique();
        }
        if (type_name == "Symbole")
        {
            return value.is_symbole();
        }
        if (type_name == "Texte")
        {
            return value.is_texte();
        }
        if (type_name == "Rien")
        {
            return value.is_rien();
        }
        if (type_name == "Universel")
        {
            return true;
        }
        if (type_name == "Liste")
        {
            if (!value.is_liste())
            {
                return false;
            }
            if (generic_spec.empty())
            {
                return true;
            }

            const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);
            if (generic_args.size() != 1)
            {
                return false;
            }

            for (const Value &element : value.as_liste()->elements)
            {
                if (!matches_type_name(element, Token(TokenType::IDENT, generic_args[0], type_token.line, type_token.column)))
                {
                    return false;
                }
            }
            return true;
        }
        if (type_name == "ListeFixe")
        {
            if (!value.is_liste_fixe())
            {
                return false;
            }
            if (generic_spec.empty())
            {
                return true;
            }

            const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);
            if (generic_args.size() != 2)
            {
                return false;
            }

            std::size_t expected_length = 0;
            try
            {
                expected_length = static_cast<std::size_t>(std::stoll(generic_args[1]));
            }
            catch (...)
            {
                return false;
            }

            const auto list = value.as_liste_fixe();
            if (list->elements.size() != expected_length)
            {
                return false;
            }

            for (const Value &element : list->elements)
            {
                if (!matches_type_name(element, Token(TokenType::IDENT, generic_args[0], type_token.line, type_token.column)))
                {
                    return false;
                }
            }
            return true;
        }
        if (type_name == "Dictionnaire")
        {
            if (!value.is_dictionnaire())
            {
                return false;
            }
            if (generic_spec.empty())
            {
                return true;
            }

            const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);
            if (generic_args.size() != 2)
            {
                return false;
            }

            for (const auto &[key, entry_value] : value.as_dictionnaire()->entries)
            {
                if (!matches_type_name(key, Token(TokenType::IDENT, generic_args[0], type_token.line, type_token.column)) ||
                    !matches_type_name(entry_value, Token(TokenType::IDENT, generic_args[1], type_token.line, type_token.column)))
                {
                    return false;
                }
            }
            return true;
        }
        if (type_name == "Ensemble")
        {
            if (!value.is_ensemble())
            {
                return false;
            }
            if (generic_spec.empty())
            {
                return true;
            }

            const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);
            if (generic_args.size() != 1)
            {
                return false;
            }

            for (const Value &element : value.as_ensemble()->elements)
            {
                if (!matches_type_name(element, Token(TokenType::IDENT, generic_args[0], type_token.line, type_token.column)))
                {
                    return false;
                }
            }
            return true;
        }
        if (type_name == "Classe")
        {
            return value.is_classe();
        }
        if (type_name == "Interface")
        {
            return value.is_interface();
        }

        if (value.is_objet())
        {
            auto object = value.as_objet();
            return object != nullptr &&
                   object->klass != nullptr &&
                   ([&]()
                    {
                        return class_derives_from(object->klass, type_name) ||
                               class_implements_interface(object->klass, type_name);
                    })();
        }

        return false;
    }

    std::vector<std::string> TreeWalker::split_generic_arguments(const std::string &generic_spec) const
    {
        std::vector<std::string> result;
        std::string current;
        int depth = 0;

        for (char ch : generic_spec)
        {
            if (ch == '[')
            {
                ++depth;
                current += ch;
                continue;
            }
            if (ch == ']')
            {
                --depth;
                current += ch;
                continue;
            }
            if (ch == ',' && depth == 0)
            {
                if (!current.empty())
                {
                    std::string trimmed = current;
                    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
                    result.push_back(trimmed);
                }
                current.clear();
                continue;
            }

            current += ch;
        }

        if (!current.empty())
        {
            std::string trimmed = current;
            trimmed.erase(0, trimmed.find_first_not_of(" \t"));
            trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
            result.push_back(trimmed);
        }

        return result;
    }

    void TreeWalker::register_value_annotation(const Value &value, const Token &annotation) const
    {
        if (annotation.lexeme.empty())
        {
            return;
        }

        const std::string &full_type_name = annotation.lexeme;
        const std::string::size_type generic_start = full_type_name.find('[');
        if (generic_start == std::string::npos)
        {
            return;
        }

        const std::string type_name = full_type_name.substr(0, generic_start);
        const std::string generic_spec = full_type_name.substr(generic_start + 1, full_type_name.size() - generic_start - 2);
        const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);

        if (type_name == "Liste" && value.is_liste() && generic_args.size() == 1)
        {
            m_list_constraints[value.as_liste().get()] = ListConstraint{generic_args[0]};
            for (const Value &element : value.as_liste()->elements)
            {
                register_value_annotation(element, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
            }
            return;
        }

        if (type_name == "ListeFixe" && value.is_liste_fixe() && generic_args.size() == 2)
        {
            std::size_t expected_length = 0;
            try
            {
                expected_length = static_cast<std::size_t>(std::stoll(generic_args[1]));
            }
            catch (...)
            {
                return;
            }

            m_fixed_list_constraints[value.as_liste_fixe().get()] = FixedListConstraint{generic_args[0], expected_length};
            for (const Value &element : value.as_liste_fixe()->elements)
            {
                register_value_annotation(element, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
            }
            return;
        }

        if (type_name == "Dictionnaire" && value.is_dictionnaire() && generic_args.size() == 2)
        {
            m_dict_constraints[value.as_dictionnaire().get()] = DictConstraint{generic_args[0], generic_args[1]};
            for (const auto &[key, entry_value] : value.as_dictionnaire()->entries)
            {
                register_value_annotation(key, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
                register_value_annotation(entry_value, Token(TokenType::IDENT, generic_args[1], annotation.line, annotation.column));
            }
            return;
        }

        if (type_name == "Ensemble" && value.is_ensemble() && generic_args.size() == 1)
        {
            m_set_constraints[value.as_ensemble().get()] = SetConstraint{generic_args[0]};
            for (const Value &element : value.as_ensemble()->elements)
            {
                register_value_annotation(element, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
            }
        }
    }

    void TreeWalker::enforce_list_element_constraint(const std::shared_ptr<ListeData> &list,
                                                     const Value &element,
                                                     const Token &site,
                                                     const std::string &context) const
    {
        if (list == nullptr)
        {
            return;
        }

        const auto it = m_list_constraints.find(list.get());
        if (it == m_list_constraints.end())
        {
            return;
        }

        const Token annotation(TokenType::IDENT, it->second.element_type, site.line, site.column);
        ensure_value_matches_annotation(element, annotation, site, context);
    }

    void TreeWalker::enforce_fixed_list_element_constraint(const std::shared_ptr<ListeFixeData> &list,
                                                           const Value &element,
                                                           const Token &site,
                                                           const std::string &context) const
    {
        if (list == nullptr)
        {
            return;
        }

        const auto it = m_fixed_list_constraints.find(list.get());
        if (it == m_fixed_list_constraints.end())
        {
            return;
        }

        const Token annotation(TokenType::IDENT, it->second.element_type, site.line, site.column);
        ensure_value_matches_annotation(element, annotation, site, context);
    }

    void TreeWalker::enforce_dict_entry_constraint(const std::shared_ptr<DictData> &dict,
                                                   const Value &key,
                                                   const Value &entry_value,
                                                   const Token &site,
                                                   const std::string &context) const
    {
        if (dict == nullptr)
        {
            return;
        }

        const auto it = m_dict_constraints.find(dict.get());
        if (it == m_dict_constraints.end())
        {
            return;
        }

        const Token key_annotation(TokenType::IDENT, it->second.key_type, site.line, site.column);
        const Token value_annotation(TokenType::IDENT, it->second.value_type, site.line, site.column);
        ensure_value_matches_annotation(key, key_annotation, site, context + " (cle)");
        ensure_value_matches_annotation(entry_value, value_annotation, site, context + " (valeur)");
    }

    bool TreeWalker::class_derives_from(const std::shared_ptr<LumiereClass> &klass,
                                        const std::string &ancestor_name) const
    {
        for (std::shared_ptr<LumiereClass> current = klass; current != nullptr; current = parent_class(current))
        {
            if (current->name == ancestor_name)
            {
                return true;
            }
        }

        return false;
    }

    bool TreeWalker::class_implements_interface(const std::shared_ptr<LumiereClass> &klass,
                                                const std::string &interface_name) const
    {
        for (std::shared_ptr<LumiereClass> current = klass; current != nullptr; current = parent_class(current))
        {
            if (current->interfaces.count(interface_name) != 0)
            {
                return true;
            }
        }

        return false;
    }

    void TreeWalker::ensure_value_matches_annotation(const Value &value,
                                                     const Token &annotation,
                                                     const Token &site,
                                                     const std::string &context) const
    {
        if (annotation.lexeme.empty())
        {
            return;
        }

        if (matches_type_name(value, annotation))
        {
            register_value_annotation(value, annotation);
            return;
        }

        throw_runtime_error(
            site,
            context + " attend une valeur de type " + annotation.lexeme +
                "; type recu: " + value.type_name());
    }

} // namespace lumiere
