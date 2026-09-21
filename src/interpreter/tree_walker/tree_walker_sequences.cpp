#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/runtime/members.hpp"

#include <cassert>

namespace lumiere
{

    Value TreeWalker::resolve_set_native_member(const Ref<EnsembleData> &set,
                                                const Token &member,
                                                Value receiver) const
    {
        if (set == nullptr)
        {
            return Value::rien();
        }

        const auto element_type = [set]() {
            return set->constraint ? set->constraint->element_type : std::string("Universel");
        };

        if (member.lexeme == "ajouter")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, set](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Ensemble.ajouter", call_site);
                walker.enforce_set_element_constraint(set, args[0].value, call_site, "Ensemble.ajouter");
                walker.require_dictionary_key(args[0].value, call_site);
                return Value::logique(set->insert(args[0].value)); });
        }
        if (member.lexeme == "retirer")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, set](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Ensemble.retirer", call_site);
                return Value::logique(set->erase(args[0].value)); });
        }
        if (member.lexeme == "en_liste")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, set, element_type](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "Ensemble.en_liste", call_site);
                auto list = make_ref<ListeData>();
                list->elements = set->items();
                Value result = Value::liste(std::move(list));
                walker.register_value_annotation(result, Token(TokenType::IDENT, "Liste[" + element_type() + "]", call_site.line, call_site.column));
                return result; });
        }
        if (member.lexeme == "union" || member.lexeme == "intersection" ||
            member.lexeme == "difference" || member.lexeme == "différence")
        {
            const std::string operation = member.lexeme;
            return make_tree_walker_native_method(std::move(receiver), [this, set, operation, element_type](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Ensemble." + operation, call_site);
                if (!args[0].value.is_ensemble())
                {
                    walker.throw_runtime_error(call_site, "Ensemble." + operation + " attend un Ensemble");
                }
                const auto other = args[0].value.as_ensemble();
                auto result = make_ref<EnsembleData>();
                result->constraint = set->constraint;
                if (operation == "union")
                {
                    for (const Value &element : set->items())
                        result->insert(element);
                    for (const Value &element : other->items())
                        result->insert(element);
                }
                else
                {
                    const bool keep_present = operation == "intersection";
                    for (const Value &element : set->items())
                    {
                        if (other->contains(element) == keep_present)
                            result->insert(element);
                    }
                }
                Value value = Value::ensemble(std::move(result));
                walker.register_value_annotation(value, Token(TokenType::IDENT, "Ensemble[" + element_type() + "]", call_site.line, call_site.column));
                return value; });
        }
        if (member.lexeme == "sous_ensemble_de")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, set](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Ensemble.sous_ensemble_de", call_site);
                if (!args[0].value.is_ensemble())
                {
                    walker.throw_runtime_error(call_site, "Ensemble.sous_ensemble_de attend un Ensemble");
                }
                const auto other = args[0].value.as_ensemble();
                for (const Value &element : set->items())
                {
                    if (!other->contains(element))
                    {
                        return Value::logique(false);
                    }
                }
                return Value::logique(true); });
        }

        return Value::rien();
    }

    Value TreeWalker::resolve_dict_native_member(const Ref<DictData> &dict,
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
                return Value::entier(static_cast<int64_t>(dict->size())); });
        }
        if (member.lexeme == "vide")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "Dictionnaire.vide", call_site);
                return Value::logique(dict->empty()); });
        }
        if (member.lexeme == "contient")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 1, 1, "Dictionnaire.contient", call_site);
                (void)walker;
                return Value::logique(dict->find(args[0].value) != nullptr); });
        }
        if (member.lexeme == "cles" || member.lexeme == "clés")
        {
            return make_tree_walker_native_method(std::move(receiver), [this, dict](TreeWalker &walker, const std::vector<RuntimeArgument> &args, const Token &call_site)
                                                  {
                require_positional_args(args, 0, 0, "Dictionnaire.clés", call_site);
                auto keys = make_ref<ListeData>();
                std::string key_type = "Universel";
                if (dict->constraint)
                {
                    key_type = dict->constraint->key_type;
                }
                for (const auto &entry : dict->items())
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
                auto values = make_ref<ListeData>();
                std::string value_type = "Universel";
                if (dict->constraint)
                {
                    value_type = dict->constraint->value_type;
                }
                for (const auto &entry : dict->items())
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
                auto pairs = make_ref<ListeData>();
                std::string key_type = "Universel";
                std::string value_type = "Universel";
                if (dict->constraint)
                {
                    key_type = dict->constraint->key_type;
                    value_type = dict->constraint->value_type;
                }
                const std::string pair_element_type = key_type == value_type ? key_type : "Universel";

                for (const auto &entry : dict->items())
                {
                    auto pair = make_ref<ListeFixeData>();
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
                Value removed;
                if (dict->erase(args[0].value, removed))
                {
                    return removed;
                }
                walker.throw_runtime_error(call_site, messages::cle_introuvable());
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

        if (const BuiltinMember *builtin = find_builtin_member(object, member.lexeme))
        {
            return Value::fonction(make_native_method(
                object,
                [builtin](IRuntime &runtime, const NativeArgs &native_args) -> Value {
                    // A bound method is only ever reached through the native
                    // call path, which fills both; nothing else constructs one.
                    assert(native_args.receiver != nullptr && native_args.arguments != nullptr);
                    return call_builtin_member(runtime,
                                               *builtin,
                                               *native_args.receiver,
                                               *native_args.arguments,
                                               native_args.site);
                }));
        }

        if (object.is_ensemble())
        {
            return resolve_set_native_member(object.as_ensemble(), member, object);
        }

        if (object.is_dictionnaire())
        {
            return resolve_dict_native_member(object.as_dictionnaire(), member, object);
        }

        return Value::rien();
    }

} // namespace lumiere
