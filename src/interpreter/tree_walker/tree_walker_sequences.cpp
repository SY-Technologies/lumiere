#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

namespace lumiere
{

    Value TreeWalker::resolve_sequence_common_native_member(const std::vector<Value> &elements,
                                                            const std::string &family_name,
                                                            const Token &member,
                                                            Value receiver) const
    {
        if (member.lexeme == "taille")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, &elements, family_name](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, family_name + ".taille", call_site);
                return Value::entier(static_cast<int64_t>(elements.size())); });
        }
        if (member.lexeme == "vide")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, &elements, family_name](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, family_name + ".vide", call_site);
                return Value::logique(elements.empty()); });
        }
        if (member.lexeme == "contient")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, &elements, family_name](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, family_name + ".contient", call_site);
                for (const auto &element : elements)
                {
                    if (walker.is_equal(element, args[0].value))
                    {
                        return Value::logique(true);
                    }
                }
                return Value::logique(false); });
        }
        if (member.lexeme == "joindre")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, &elements, family_name](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, family_name + ".joindre", call_site);
                const std::string separator = walker.assert_texte(args[0].value, call_site);
                std::string result;
                for (std::size_t i = 0; i < elements.size(); ++i)
                {
                    if (i > 0)
                    {
                        result += separator;
                    }
                    result += walker.to_texte(elements[i]);
                }
                return Value::texte(std::move(result)); });
        }

        return Value::rien();
    }

    Value TreeWalker::resolve_list_native_member(const std::shared_ptr<ListeData> &list,
                                                 const Token &member,
                                                 Value receiver) const
    {
        if (list == nullptr)
        {
            return Value::rien();
        }

        if (Value common = resolve_sequence_common_native_member(list->elements, "Liste", member, receiver); !common.is_rien())
        {
            return common;
        }

        if (member.lexeme == "ajouter")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, list](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Liste.ajouter", call_site);
                walker.enforce_list_element_constraint(list, args[0].value, call_site, "Liste.ajouter");
                list->elements.push_back(args[0].value);
                return Value::entier(static_cast<int64_t>(list->elements.size())); });
        }
        if (member.lexeme == "inserer")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, list](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 2, 2, "Liste.inserer", call_site);
                const int64_t position = walker.assert_entier(args[0].value, call_site);
                if (position < 0 || static_cast<std::size_t>(position) > list->elements.size())
                {
                    walker.throw_runtime_error(call_site, "indice d'insertion hors limites");
                }
                walker.enforce_list_element_constraint(list, args[1].value, call_site, "Liste.inserer");
                list->elements.insert(list->elements.begin() + position, args[1].value);
                return Value::entier(position); });
        }
        if (member.lexeme == "retirer_a")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, list](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Liste.retirer_a", call_site);
                const int64_t position = walker.assert_entier(args[0].value, call_site);
                if (position < 0 || static_cast<std::size_t>(position) >= list->elements.size())
                {
                    walker.throw_runtime_error(call_site, "indice hors limites");
                }
                Value removed = list->elements[static_cast<std::size_t>(position)];
                list->elements.erase(list->elements.begin() + position);
                return removed; });
        }
        if (member.lexeme == "en_liste_fixe")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, list](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Liste.en_liste_fixe", call_site);
                const int64_t length = walker.assert_entier(args[0].value, call_site);
                if (length < 0)
                {
                    walker.throw_runtime_error(call_site, "la taille d'une ListeFixe ne peut pas etre negative");
                }
                if (static_cast<std::size_t>(length) != list->elements.size())
                {
                    walker.throw_runtime_error(call_site, "Liste.en_liste_fixe requiert une liste de taille exacte " + std::to_string(length));
                }

                auto fixed = std::make_shared<ListeFixeData>();
                fixed->elements = list->elements;
                Value result = Value::liste_fixe(std::move(fixed));

                std::string element_type = "Universel";
                if (const auto it = walker.m_list_constraints.find(list.get()); it != walker.m_list_constraints.end())
                {
                    element_type = it->second.element_type;
                }

                walker.register_value_annotation(
                    result,
                    Token(TokenType::IDENT,
                          "ListeFixe[" + element_type + ", " + std::to_string(length) + "]",
                          call_site.line,
                          call_site.column));
                return result; });
        }

        return Value::rien();
    }

    Value TreeWalker::resolve_fixed_list_native_member(const std::shared_ptr<ListeFixeData> &list,
                                                       const Token &member,
                                                       Value receiver) const
    {
        if (list == nullptr)
        {
            return Value::rien();
        }

        if (Value common = resolve_sequence_common_native_member(list->elements, "ListeFixe", member, receiver); !common.is_rien())
        {
            return common;
        }

        if (member.lexeme == "en_liste")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, list](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "ListeFixe.en_liste", call_site);
                auto dynamic = std::make_shared<ListeData>();
                dynamic->elements = list->elements;
                Value result = Value::liste(std::move(dynamic));
                if (const auto it = walker.m_fixed_list_constraints.find(list.get()); it != walker.m_fixed_list_constraints.end())
                {
                    walker.register_value_annotation(
                        result,
                        Token(TokenType::IDENT, "Liste[" + it->second.element_type + "]", call_site.line, call_site.column));
                }
                return result; });
        }

        return Value::rien();
    }

    Value TreeWalker::resolve_dict_native_member(const std::shared_ptr<DictData> &dict,
                                                 const Token &member,
                                                 Value receiver) const
    {
        if (dict == nullptr)
        {
            return Value::rien();
        }

        if (member.lexeme == "taille")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "Dictionnaire.taille", call_site);
                return Value::entier(static_cast<int64_t>(dict->entries.size())); });
        }
        if (member.lexeme == "vide")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "Dictionnaire.vide", call_site);
                return Value::logique(dict->entries.empty()); });
        }
        if (member.lexeme == "contient")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Dictionnaire.contient", call_site);
                for (const auto &entry : dict->entries)
                {
                    if (walker.is_equal(entry.first, args[0].value))
                    {
                        return Value::logique(true);
                    }
                }
                return Value::logique(false); });
        }
        if (member.lexeme == "cles")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "Dictionnaire.cles", call_site);
                auto keys = std::make_shared<ListeData>();
                std::string key_type = "Universel";
                if (const auto it = walker.m_dict_constraints.find(dict.get()); it != walker.m_dict_constraints.end())
                {
                    key_type = it->second.key_type;
                }
                for (const auto &entry : dict->entries)
                {
                    keys->elements.push_back(entry.first);
                }
                Value result = Value::liste(std::move(keys));
                walker.register_value_annotation(result, Token(TokenType::IDENT, "Liste[" + key_type + "]", call_site.line, call_site.column));
                return result; });
        }
        if (member.lexeme == "valeurs")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "Dictionnaire.valeurs", call_site);
                auto values = std::make_shared<ListeData>();
                std::string value_type = "Universel";
                if (const auto it = walker.m_dict_constraints.find(dict.get()); it != walker.m_dict_constraints.end())
                {
                    value_type = it->second.value_type;
                }
                for (const auto &entry : dict->entries)
                {
                    values->elements.push_back(entry.second);
                }
                Value result = Value::liste(std::move(values));
                walker.register_value_annotation(result, Token(TokenType::IDENT, "Liste[" + value_type + "]", call_site.line, call_site.column));
                return result; });
        }
        if (member.lexeme == "paires")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "Dictionnaire.paires", call_site);
                auto pairs = std::make_shared<ListeData>();
                std::string key_type = "Universel";
                std::string value_type = "Universel";
                if (const auto it = walker.m_dict_constraints.find(dict.get()); it != walker.m_dict_constraints.end())
                {
                    key_type = it->second.key_type;
                    value_type = it->second.value_type;
                }
                const std::string pair_element_type = key_type == value_type ? key_type : "Universel";

                for (const auto &entry : dict->entries)
                {
                    auto pair = std::make_shared<ListeFixeData>();
                    pair->elements.push_back(entry.first);
                    pair->elements.push_back(entry.second);
                    Value pair_value = Value::liste_fixe(std::move(pair));
                    walker.register_value_annotation(
                        pair_value,
                        Token(TokenType::IDENT,
                              "ListeFixe[" + pair_element_type + ", 2]",
                              call_site.line,
                              call_site.column));
                    pairs->elements.push_back(pair_value);
                }

                Value result = Value::liste(std::move(pairs));
                walker.register_value_annotation(
                    result,
                    Token(TokenType::IDENT,
                          "Liste[ListeFixe[" + pair_element_type + ", 2]]",
                          call_site.line,
                          call_site.column));
                return result; });
        }
        if (member.lexeme == "retirer")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Dictionnaire.retirer", call_site);
                for (auto it = dict->entries.begin(); it != dict->entries.end(); ++it)
                {
                    if (walker.is_equal(it->first, args[0].value))
                    {
                        Value removed = it->second;
                        dict->entries.erase(it);
                        return removed;
                    }
                }
                walker.throw_runtime_error(call_site, "cle introuvable dans le dictionnaire");
                return Value::rien(); });
        }

        return Value::rien();
    }

    Value TreeWalker::resolve_native_member(const Value &object, const Token &member) const
    {
        Value texte_member = Value::rien();
        if (try_resolve_texte_native_member(
                object,
                member.lexeme,
                [this](Value receiver, LumiereFunction::NativeHandler handler)
                {
                    return make_native_method(std::move(receiver), std::move(handler));
                },
                texte_member))
        {
            return texte_member;
        }

        if (object.is_liste())
        {
            return resolve_list_native_member(object.as_liste(), member, object);
        }

        if (object.is_liste_fixe())
        {
            return resolve_fixed_list_native_member(object.as_liste_fixe(), member, object);
        }

        if (object.is_dictionnaire())
        {
            return resolve_dict_native_member(object.as_dictionnaire(), member, object);
        }

        return Value::rien();
    }

} // namespace lumiere
