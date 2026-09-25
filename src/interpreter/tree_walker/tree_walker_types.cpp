#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/runtime/collection_constraints.hpp"
#include "lumiere/interpreter/runtime/runtime_type.hpp"
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
        // `resolved_annotation_name` is what `register_value_annotation` below
        // already resolved this same annotation through, every call, before
        // this task: a return type in particular is not always the same
        // declaration's text twice in a row (a closure's own alias scope can
        // resolve it differently), so the type this text reads to is cached
        // by the text itself, not by this TypeExpr's address -- an address a
        // caller may hold in a local copy, and reuse for an unrelated
        // annotation on its next call.
        return lumiere::matches(value, m_type_cache[resolved_annotation_name(type)]);
    }

    bool TreeWalker::matches_type_name(const Value &value, const Token &type_token) const
    {
        return lumiere::matches(value, m_type_cache[type_token.lexeme]);
    }

    void TreeWalker::register_value_annotation(const Value &value, const Token &annotation) const
    {
        if (!value.is_liste() && !value.is_liste_fixe() && !value.is_dictionnaire() && !value.is_ensemble() && !value.is_resultat())
            return;
        if (annotation.lexeme.empty())
        {
            return;
        }
        if (!lumiere::annotate(value, m_type_cache[annotation.lexeme]))
        {
            throw_runtime_error(annotation, "annotation de collection incompatible avec le contrat existant");
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
        if (annotation.empty())
        {
            return;
        }
        if (!value.is_liste() && !value.is_liste_fixe() && !value.is_dictionnaire() && !value.is_ensemble() && !value.is_resultat())
            return;
        if (!lumiere::annotate(value, m_type_cache[resolved_annotation_name(annotation)]))
        {
            throw_runtime_error(annotation.source, "annotation de collection incompatible avec le contrat existant");
        }
    }

    void TreeWalker::enforce_list_element_constraint(const Ref<ListeData> &list,
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

    void TreeWalker::enforce_dict_entry_constraint(const Ref<DictData> &dict,
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
            messages::type_attendu(context,
                                   display_runtime_type(annotation.to_string()),
                                   value.type_name()));
    }

} // namespace lumiere
