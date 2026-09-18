#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/runtime/collection_constraints.hpp"
#include "lumiere/interpreter/runtime/type_aliases.hpp"
#include "lumiere/interpreter/runtime/nominal_type.hpp"

namespace lumiere
{

    bool TreeWalker::matches_type_name(const Value &value, const TypeExpr &type) const
    {
        if (type.empty())
        {
            return true;
        }
        if (type.kind == TypeExprKind::UNION)
        {
            for (const TypeExpr &alternative : type.children)
            {
                if (matches_type_name(value, alternative))
                {
                    return true;
                }
            }
            return false;
        }
        if (type.kind == TypeExprKind::NAMED)
        {
            if (m_env && m_env->find_type_alias(type.name))
            {
                Token resolved = type.as_token();
                resolved.lexeme = resolved_annotation_name(type);
                return matches_type_name(value, resolved);
            }
            return matches_type_name(value, type.as_token());
        }
        if (type.kind != TypeExprKind::GENERIC)
        {
            return false;
        }

        if (type.name == "Liste" && type.children.size() == 1 && value.is_liste())
        {
            for (const Value &element : value.as_liste()->elements)
            {
                if (!matches_type_name(element, type.children[0]))
                {
                    return false;
                }
            }
            return true;
        }
        if (type.name == "ListeFixe" && type.children.size() == 2 &&
            type.children[1].kind == TypeExprKind::INTEGER_ARGUMENT && value.is_liste_fixe())
        {
            const auto list = value.as_liste_fixe();
            if (list->elements.size() != type.children[1].integer)
            {
                return false;
            }
            for (const Value &element : list->elements)
            {
                if (!matches_type_name(element, type.children[0]))
                {
                    return false;
                }
            }
            return true;
        }
        if (type.name == "Dictionnaire" && type.children.size() == 2 && value.is_dictionnaire())
        {
            for (const auto &[key, entry_value] : value.as_dictionnaire()->items())
            {
                if (!matches_type_name(key, type.children[0]) ||
                    !matches_type_name(entry_value, type.children[1]))
                {
                    return false;
                }
            }
            return true;
        }
        if (type.name == "Ensemble" && type.children.size() == 1 && value.is_ensemble())
        {
            for (const Value &element : value.as_ensemble()->items())
            {
                if (!matches_type_name(element, type.children[0]))
                {
                    return false;
                }
            }
            return true;
        }
        if (type.name == "Résultat" && type.children.size() == 2 &&
            value.is_resultat())
        {
            const auto result = value.as_resultat();
            return matches_type_name(
                result->payload,
                type.children[result->success ? 0 : 1]);
        }
        return false;
    }

    bool TreeWalker::matches_type_name(const Value &value, const Token &type_token) const
    {
        const std::string &full_type_name = type_token.lexeme;
        int union_depth = 0;
        for (std::size_t i = 0; i < full_type_name.size(); ++i)
        {
            if (full_type_name[i] == '[')
            {
                ++union_depth;
            }
            else if (full_type_name[i] == ']')
            {
                --union_depth;
            }
            else if (full_type_name[i] == '|' && union_depth == 0)
            {
                auto trim = [](std::string name)
                {
                    const std::size_t first = name.find_first_not_of(" \t\r\n");
                    if (first == std::string::npos)
                    {
                        return std::string{};
                    }
                    return name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
                };
                return matches_type_name(
                           value,
                           Token(TokenType::IDENT, trim(full_type_name.substr(0, i)), type_token.line, type_token.column)) ||
                       matches_type_name(
                           value,
                           Token(TokenType::IDENT, trim(full_type_name.substr(i + 1)), type_token.line, type_token.column));
            }
        }
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
        if (type_name == "Résultat")
        {
            if (!value.is_resultat())
            {
                return false;
            }
            if (generic_spec.empty())
            {
                return true;
            }
            const std::vector<std::string> generic_args =
                split_generic_arguments(generic_spec);
            if (generic_args.size() != 2)
            {
                return false;
            }
            const auto result = value.as_resultat();
            return matches_type_name(
                result->payload,
                Token(TokenType::IDENT,
                      generic_args[result->success ? 0 : 1],
                      type_token.line,
                      type_token.column));
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

            for (const auto &[key, entry_value] : value.as_dictionnaire()->items())
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

            for (const Value &element : value.as_ensemble()->items())
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
                        const std::string identity = m_env
                            ? m_env->resolve_type_aliases(TypeExpr::named(type_token)).to_string()
                            : type_name;
                        return class_derives_from(object->klass, identity) ||
                               class_implements_interface(object->klass, identity);
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
        if (!value.is_liste() && !value.is_liste_fixe() && !value.is_dictionnaire() && !value.is_ensemble() && !value.is_resultat())
            return;
        if (annotation.lexeme.empty())
        {
            return;
        }

        const std::string &full_type_name = annotation.lexeme;
        if (const auto separator = find_collection_type_union(full_type_name); separator != std::string::npos)
        {
            Token alternative = annotation;
            alternative.lexeme = full_type_name.substr(0, separator);
            const auto trim = [](std::string text) {
                const auto begin = text.find_first_not_of(" \t\r\n");
                return begin == std::string::npos ? std::string{} :
                    text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
            };
            alternative.lexeme = trim(alternative.lexeme);
            if (!matches_type_name(value, alternative))
                alternative.lexeme = trim(full_type_name.substr(separator + 1));
            register_value_annotation(value, alternative);
            return;
        }
        const std::string::size_type generic_start = full_type_name.find('[');
        if (generic_start == std::string::npos)
        {
            return;
        }

        const std::string type_name = full_type_name.substr(0, generic_start);
        const std::string generic_spec = full_type_name.substr(generic_start + 1, full_type_name.size() - generic_start - 2);
        const std::vector<std::string> generic_args = split_generic_arguments(generic_spec);

        if (type_name == "Résultat" && value.is_resultat() && generic_args.size() == 2)
        {
            const auto result = value.as_resultat();
            register_value_annotation(result->payload,
                Token(TokenType::IDENT, generic_args[result->success ? 0 : 1], annotation.line, annotation.column));
            return;
        }

        if (type_name == "Liste" && value.is_liste() && generic_args.size() == 1)
        {
            if (!merge_collection_constraint(value.as_liste()->constraint, ListConstraint{generic_args[0]}))
                throw_runtime_error(annotation, "annotation de collection incompatible avec le contrat existant");
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

            if (!merge_collection_constraint(value.as_liste_fixe()->constraint, FixedListConstraint{generic_args[0], expected_length}))
                throw_runtime_error(annotation, "annotation de collection incompatible avec le contrat existant");
            for (const Value &element : value.as_liste_fixe()->elements)
            {
                register_value_annotation(element, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
            }
            return;
        }

        if (type_name == "Dictionnaire" && value.is_dictionnaire() && generic_args.size() == 2)
        {
            if (!merge_collection_constraint(value.as_dictionnaire()->constraint, DictConstraint{generic_args[0], generic_args[1]}))
                throw_runtime_error(annotation, "annotation de collection incompatible avec le contrat existant");
            for (const auto &[key, entry_value] : value.as_dictionnaire()->items())
            {
                register_value_annotation(key, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
                register_value_annotation(entry_value, Token(TokenType::IDENT, generic_args[1], annotation.line, annotation.column));
            }
            return;
        }

        if (type_name == "Ensemble" && value.is_ensemble() && generic_args.size() == 1)
        {
            if (!merge_collection_constraint(value.as_ensemble()->constraint, SetConstraint{generic_args[0]}))
                throw_runtime_error(annotation, "annotation de collection incompatible avec le contrat existant");
            for (const Value &element : value.as_ensemble()->items())
            {
                register_value_annotation(element, Token(TokenType::IDENT, generic_args[0], annotation.line, annotation.column));
            }
        }
    }

    std::string TreeWalker::resolved_annotation_name(const TypeExpr &annotation) const
    {
        try
        {
            return m_env ? m_env->resolve_type_aliases(annotation).to_string() : annotation.to_string();
        }
        catch (const std::invalid_argument &error)
        {
            throw_runtime_error(annotation.source, error.what());
        }
    }

    void TreeWalker::register_value_annotation(const Value &value, const TypeExpr &annotation) const
    {
        if (!value.is_liste() && !value.is_liste_fixe() && !value.is_dictionnaire() && !value.is_ensemble() && !value.is_resultat())
            return;
        Token resolved = annotation.as_token();
        resolved.lexeme = resolved_annotation_name(annotation);
        register_value_annotation(value, resolved);
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

        if (!list->constraint)
        {
            return;
        }

        const Token annotation(TokenType::IDENT, list->constraint->element_type, site.line, site.column);
        ensure_value_matches_annotation(element, annotation, site, context);
    }

    void TreeWalker::enforce_set_element_constraint(const std::shared_ptr<EnsembleData> &set,
                                                    const Value &element,
                                                    const Token &site,
                                                    const std::string &context) const
    {
        if (set == nullptr || !set->constraint)
        {
            return;
        }

        const Token annotation(TokenType::IDENT, set->constraint->element_type, site.line, site.column);
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

        if (!dict->constraint)
        {
            return;
        }

        const Token key_annotation(TokenType::IDENT, dict->constraint->key_type, site.line, site.column);
        const Token value_annotation(TokenType::IDENT, dict->constraint->value_type, site.line, site.column);
        ensure_value_matches_annotation(key, key_annotation, site, context + " (clé)");
        ensure_value_matches_annotation(entry_value, value_annotation, site, context + " (valeur)");
    }

    bool TreeWalker::class_derives_from(const std::shared_ptr<LumiereClass> &klass,
                                        const std::string &ancestor_name) const
    {
        for (std::shared_ptr<LumiereClass> current = klass; current != nullptr; current = parent_class(current))
        {
            if ((current->type_identity.empty() ? current->name : current->type_identity) == ancestor_name)
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
            messages::type_attendu(context, display_runtime_type(annotation.lexeme), value.type_name()));
    }

    void TreeWalker::ensure_value_matches_annotation(const Value &value,
                                                     const TypeExpr &annotation,
                                                     const Token &site,
                                                     const std::string &context) const
    {
        if (annotation.empty())
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
            context + " attend une valeur de type " + display_runtime_type(annotation.to_string()) +
                "; type reçu: " + value.type_name());
    }

} // namespace lumiere
