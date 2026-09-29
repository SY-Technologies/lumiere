#pragma once

#include "lumiere/interpreter/runtime/collection_constraints.hpp"
#include "lumiere/interpreter/runtime/value.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lumiere
{

inline std::string_view trim_type_name(std::string_view name)
{
    const std::size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
}

inline std::vector<std::string_view> split_generic_arguments(const std::string_view specification)
{
    std::vector<std::string_view> arguments;
    std::size_t start = 0;
    std::size_t depth = 0;
    for (std::size_t i = 0; i < specification.size(); ++i)
    {
        if (specification[i] == '[')
        {
            ++depth;
        }
        else if (specification[i] == ']')
        {
            --depth;
        }
        else if (specification[i] == ',' && depth == 0)
        {
            arguments.push_back(specification.substr(start, i - start));
            start = i + 1;
        }
    }
    arguments.push_back(specification.substr(start));
    return arguments;
}

// A collection carries its contract, and every element it holds was checked
// against that contract on the way in -- `enforce_declared_type` on insertion,
// `annotate` when the contract is first set, after a check. A collection
// whose contract is exactly the type asked about therefore matches it without
// its elements being looked at. Scanning them instead made passing a 300-entry
// Dictionnaire[Texte, Produit] as an argument cost 600 checks per call.
inline bool same_contract(const std::string &contract, const std::string_view requested)
{
    return trim_type_name(contract) == trim_type_name(requested);
}

/** @brief The builtin scalars, which a value satisfies by its tag alone. */
enum class TypeShape : std::uint8_t
{
    Entier,
    Decimal,
    Logique,
    Symbole,
    Texte,
    Rien,
    Universel,
};

inline bool matches_shape(const Value &value, const TypeShape shape)
{
    switch (shape)
    {
    case TypeShape::Entier:
        return value.is_entier();
    // An Entier is accepted where a Décimal is asked for, as it always has been.
    case TypeShape::Decimal:
        return value.is_decimal() || value.is_entier();
    case TypeShape::Logique:
        return value.is_logique();
    case TypeShape::Symbole:
        return value.is_symbole();
    case TypeShape::Texte:
        return value.is_texte();
    case TypeShape::Rien:
        return value.is_rien();
    case TypeShape::Universel:
        return true;
    }
    return false;
}

/**
 * @brief A type as the runtime checks it: its text, read once.
 *
 * Types travel into the bytecode -- and, for the tree walker, into a
 * collection's contract or a `Token`'s lexeme -- as text, and every check
 * used to re-read that text: trim it, scan it for a union bar, find the
 * bracket, split the arguments into a fresh vector, compare the head against
 * a dozen names. For a class the head is the class's identity, which embeds
 * the source path, so the cost of a check grew with the length of the path
 * the program was run from. A RuntimeType is that reading done once; matching
 * one walks the structure and compares a class identity only when there is an
 * object to compare it with.
 *
 * `parse_runtime_type` reads text exactly as each engine's own string matcher
 * used to, including what it did with text the compiler never produces, so
 * replacing one with the other changes no answer. Both engines reach it
 * through a `RuntimeTypeCache`, so a distinct piece of text is parsed once
 * for the run, however many times it is checked.
 */
struct RuntimeType
{
    enum class Kind : std::uint8_t
    {
        Scalar,
        Union,
        Resultat,
        Classe,
        Interface,
        Liste,
        ListeFixe,
        Dictionnaire,
        Ensemble,
        Nominal,
    };

    Kind kind = Kind::Nominal;
    TypeShape shape = TypeShape::Universel; // for a Scalar
    // Exactly the text this type was read from, surrounding spaces included:
    // it is what a collection's contract records, and what a contract is
    // compared with.
    std::string text;
    // The head, before any '[': for a class or an interface, its identity.
    std::string name;
    // Something was written between the brackets: without it, `Liste[]` or
    // `Liste`, any list matches.
    bool generic = false;
    // Written `Tête[...]` ending on its bracket, which is what an annotation
    // requires before it records anything.
    bool closed = false;
    // A ListeFixe's second argument, when it reads as a number.
    std::optional<std::size_t> length;
    // The bracketed arguments, or a union's two sides.
    std::vector<RuntimeType> arguments;
};

inline RuntimeType parse_runtime_type(const std::string_view text)
{
    using Kind = RuntimeType::Kind;
    RuntimeType type;
    // Trimmed once, here, rather than on every later comparison: a generic
    // argument split off after a comma (`Dictionnaire[Texte, Entier]`) carries
    // its leading space otherwise, and that space then shows up wherever this
    // text is displayed or stored as a contract.
    const std::string_view full = trim_type_name(text);
    type.text = full;
    if (const auto bar = find_collection_type_union(full); bar != std::string_view::npos)
    {
        type.kind = Kind::Union;
        type.arguments.push_back(parse_runtime_type(full.substr(0, bar)));
        type.arguments.push_back(parse_runtime_type(full.substr(bar + 1)));
        return type;
    }

    const std::size_t open = full.find('[');
    type.name = full.substr(0, open);
    if (open != std::string_view::npos)
    {
        const std::string_view specification = full.substr(open + 1, full.size() - open - 2);
        type.generic = !specification.empty();
        type.closed = full.back() == ']';
        for (const std::string_view argument : split_generic_arguments(specification))
        {
            type.arguments.push_back(parse_runtime_type(argument));
        }
    }

    static const std::pair<std::string_view, TypeShape> scalars[] = {
        {"Entier", TypeShape::Entier},       {"Décimal", TypeShape::Decimal},
        {"Decimal", TypeShape::Decimal},     {"Logique", TypeShape::Logique},
        {"Symbole", TypeShape::Symbole},     {"Texte", TypeShape::Texte},
        {"Rien", TypeShape::Rien},           {"Universel", TypeShape::Universel},
    };
    static const std::pair<std::string_view, Kind> kinds[] = {
        {"Résultat", Kind::Resultat},     {"Classe", Kind::Classe},
        {"Interface", Kind::Interface},   {"Liste", Kind::Liste},
        {"ListeFixe", Kind::ListeFixe},   {"Dictionnaire", Kind::Dictionnaire},
        {"Ensemble", Kind::Ensemble},
    };
    for (const auto &[name, shape] : scalars)
    {
        if (type.name == name)
        {
            type.kind = Kind::Scalar;
            type.shape = shape;
            return type;
        }
    }
    for (const auto &[name, kind] : kinds)
    {
        if (type.name == name)
        {
            type.kind = kind;
        }
    }
    if (type.kind == Kind::ListeFixe && type.arguments.size() == 2)
    {
        try
        {
            type.length = static_cast<std::size_t>(std::stoll(type.arguments[1].text));
        }
        catch (...)
        {
        }
    }
    return type;
}

// Whether a class, or one of its ancestors, is @p name or declares the
// interface @p name.
inline bool class_satisfies(const LumiereClass *klass, const std::string &name)
{
    for (; klass != nullptr; klass = klass->parent.get())
    {
        if ((klass->type_identity.empty() ? klass->name : klass->type_identity) == name ||
            klass->interfaces.contains(name))
        {
            return true;
        }
    }
    return false;
}

template <typename Elements>
bool all_match(const Elements &elements, const RuntimeType &type);

inline bool matches(const Value &value, const RuntimeType &type)
{
    using Kind = RuntimeType::Kind;
    switch (type.kind)
    {
    case Kind::Scalar:
        return matches_shape(value, type.shape);
    case Kind::Union:
        return matches(value, type.arguments[0]) || matches(value, type.arguments[1]);
    case Kind::Resultat:
    {
        if (!value.is_resultat())
            return false;
        if (!type.generic)
            return true;
        if (type.arguments.size() != 2)
            return false;
        const auto result = value.as_resultat();
        return matches(result->payload, type.arguments[result->success ? 0 : 1]);
    }
    case Kind::Classe:
        return value.is_classe();
    case Kind::Interface:
        return value.is_interface();
    default:
        break;
    }

    // Any other name may be a class or an interface the program declared.
    if (value.is_objet() && class_satisfies(value.as_objet_ptr()->klass.get(), type.name))
    {
        return true;
    }

    switch (type.kind)
    {
    case Kind::Liste:
    {
        if (!value.is_liste())
            return false;
        if (!type.generic)
            return true;
        if (type.arguments.size() != 1)
            return false;
        const auto list = value.as_liste();
        if (list->constraint && same_contract(list->constraint->element_type, type.arguments[0].text))
        {
            assert(all_match(list->elements, type.arguments[0]));
            return true;
        }
        return all_match(list->elements, type.arguments[0]);
    }
    case Kind::ListeFixe:
    {
        if (!value.is_liste_fixe())
            return false;
        if (!type.generic)
            return true;
        if (type.arguments.size() != 2 || !type.length)
            return false;
        const auto fixed = value.as_liste_fixe();
        if (fixed->elements.size() != *type.length)
            return false;
        if (fixed->constraint && fixed->constraint->length == *type.length &&
            same_contract(fixed->constraint->element_type, type.arguments[0].text))
        {
            assert(all_match(fixed->elements, type.arguments[0]));
            return true;
        }
        return all_match(fixed->elements, type.arguments[0]);
    }
    case Kind::Dictionnaire:
    {
        if (!value.is_dictionnaire())
            return false;
        if (!type.generic)
            return true;
        if (type.arguments.size() != 2)
            return false;
        const auto dictionary = value.as_dictionnaire();
        const auto every_entry_matches = [&] {
            return std::all_of(dictionary->items().begin(), dictionary->items().end(),
                               [&](const DictEntry &entry) {
                                   return matches(entry.first, type.arguments[0]) &&
                                          matches(entry.second, type.arguments[1]);
                               });
        };
        if (dictionary->constraint &&
            same_contract(dictionary->constraint->key_type, type.arguments[0].text) &&
            same_contract(dictionary->constraint->value_type, type.arguments[1].text))
        {
            assert(every_entry_matches());
            return true;
        }
        return every_entry_matches();
    }
    case Kind::Ensemble:
    {
        if (!value.is_ensemble())
            return false;
        if (!type.generic)
            return true;
        if (type.arguments.size() != 1)
            return false;
        const auto set = value.as_ensemble();
        if (set->constraint && same_contract(set->constraint->element_type, type.arguments[0].text))
        {
            assert(all_match(set->items(), type.arguments[0]));
            return true;
        }
        return all_match(set->items(), type.arguments[0]);
    }
    default:
        return false;
    }
}

template <typename Elements>
bool all_match(const Elements &elements, const RuntimeType &type)
{
    return std::all_of(elements.begin(), elements.end(),
                       [&](const Value &element) { return matches(element, type); });
}

/**
 * @brief Records @p type on a collection, or on a Résultat's payload, that
 * already satisfies it.
 *
 * The contract kept is the argument's text as written, as it always was.
 * Returns false when the collection already carries a different, concrete
 * contract -- the merge the caller must refuse and report; every other case,
 * including "nothing to annotate", succeeds.
 */
inline bool annotate(const Value &value, const RuntimeType &type)
{
    using Kind = RuntimeType::Kind;
    if (!value.is_liste() && !value.is_liste_fixe() && !value.is_dictionnaire() && !value.is_ensemble() && !value.is_resultat())
        return true;
    if (type.kind == Kind::Union)
    {
        return annotate(value, matches(value, type.arguments[0]) ? type.arguments[0] : type.arguments[1]);
    }
    if (!type.closed)
        return true;
    const auto &arguments = type.arguments;
    if (type.kind == Kind::Resultat && value.is_resultat() && arguments.size() == 2)
    {
        const auto result = value.as_resultat();
        return annotate(result->payload, arguments[result->success ? 0 : 1]);
    }
    // A collection already bound to exactly this contract has nothing to learn
    // from it: its elements were annotated when the contract was set, and every
    // one added since was annotated on the way in. Walking them again made a
    // typed argument cost as much as the collection it named.
    if (type.kind == Kind::Liste && value.is_liste() && arguments.size() == 1)
    {
        const auto list = value.as_liste();
        if (list->constraint && same_contract(list->constraint->element_type, arguments[0].text))
            return true;
        if (!merge_collection_constraint(list->constraint, ListConstraint{arguments[0].text}))
            return false;
        for (const auto &element : list->elements)
            if (!annotate(element, arguments[0]))
                return false;
        return true;
    }
    if (type.kind == Kind::ListeFixe && value.is_liste_fixe() && arguments.size() == 2)
    {
        const auto fixed = value.as_liste_fixe();
        if (fixed->constraint && fixed->constraint->length == fixed->elements.size() &&
            same_contract(fixed->constraint->element_type, arguments[0].text))
            return true;
        if (!merge_collection_constraint(fixed->constraint, FixedListConstraint{
                arguments[0].text, fixed->elements.size()}))
            return false;
        for (const auto &element : fixed->elements)
            if (!annotate(element, arguments[0]))
                return false;
        return true;
    }
    if (type.kind == Kind::Dictionnaire && value.is_dictionnaire() && arguments.size() == 2)
    {
        const auto dictionary = value.as_dictionnaire();
        if (dictionary->constraint && same_contract(dictionary->constraint->key_type, arguments[0].text) &&
            same_contract(dictionary->constraint->value_type, arguments[1].text))
            return true;
        if (!merge_collection_constraint(dictionary->constraint, DictConstraint{arguments[0].text, arguments[1].text}))
            return false;
        for (const auto &[key, element] : dictionary->items())
        {
            if (!annotate(key, arguments[0]) || !annotate(element, arguments[1]))
                return false;
        }
        return true;
    }
    if (type.kind == Kind::Ensemble && value.is_ensemble() && arguments.size() == 1)
    {
        const auto set = value.as_ensemble();
        if (set->constraint && same_contract(set->constraint->element_type, arguments[0].text))
            return true;
        if (!merge_collection_constraint(set->constraint, SetConstraint{arguments[0].text}))
            return false;
        for (const auto &element : set->items())
            if (!annotate(element, arguments[0]))
                return false;
        return true;
    }
    return true;
}

/**
 * @brief Every type text a run has met, each read once.
 *
 * Keyed by the text a type was written or carried as -- a module's own type,
 * a collection's contract, a builtin member's parameter type, text a `Token`
 * or a `TypeExpr` resolves to. An entry's address never changes, so a caller
 * may keep a pointer to it for the rest of the run.
 */
class RuntimeTypeCache
{
public:
    const RuntimeType &operator[](const std::string_view text) const
    {
        auto found = m_types.find(text);
        if (found == m_types.end())
        {
            found = m_types.emplace(std::string(text), parse_runtime_type(text)).first;
        }
        return found->second;
    }

private:
    struct TextHash
    {
        using is_transparent = void;
        std::size_t operator()(const std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };
    mutable std::unordered_map<std::string, RuntimeType, TextHash, std::equal_to<>> m_types;
};

} // namespace lumiere
