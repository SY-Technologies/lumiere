#include "lumiere/interpreter/runtime/members.hpp"

#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/runtime/nominal_type.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

namespace lumiere
{

/**
 * @brief One call of one builtin member: receiver, arguments, and where from.
 *
 * The signature (`Liste.ajouter`) is built on demand rather than stored. It is
 * only ever needed to word a diagnostic, and a member call that succeeds should
 * not pay to describe itself.
 */
struct MemberCall
{
    IRuntime &runtime;
    const Value &receiver;
    std::string_view member;
    const std::vector<RuntimeArgument> &args;
    const RuntimeSite &site;

    [[noreturn]] void fail(const std::string &message) const
    {
        runtime.raise_runtime_error(site, message);
        // The interface says that call must not return, and every member below
        // is written on that promise: one that returned would walk straight
        // into the operation the diagnostic was raised to prevent. A virtual
        // call carries no [[noreturn]] of its own, so this states it.
        std::abort();
    }

    [[noreturn]] void fail_argument(const std::size_t index, const std::string &message) const
    {
        const RuntimeSite &argument_site = args[index].site.line == 0 ? site : args[index].site;
        runtime.raise_runtime_error(argument_site, message);
        std::abort();
    }

    [[nodiscard]] std::string signature() const
    {
        return receiver.type_name() + "." + std::string(member);
    }

    /** @brief The rule every builtin applies: a fixed count, no names. */
    void expect(const std::size_t arity) const
    {
        if (args.size() != arity)
        {
            fail(messages::arite_exacte(signature(), arity));
        }
        for (std::size_t index = 0; index < args.size(); ++index)
        {
            if (!args[index].name.empty())
            {
                fail_argument(index, messages::arguments_nommes_refuses(signature()));
            }
        }
    }

    [[nodiscard]] const Value &argument(const std::size_t index) const { return args[index].value; }

    [[nodiscard]] std::int64_t integer(const std::size_t index) const
    {
        if (!argument(index).is_entier())
        {
            fail_argument(index, messages::valeur_attendue(signature(), "Entier"));
        }
        return argument(index).as_entier();
    }

    [[nodiscard]] const std::string &text(const std::size_t index) const
    {
        if (!argument(index).is_texte())
        {
            fail_argument(index, messages::valeur_attendue(signature(), "Texte"));
        }
        return argument(index).as_texte();
    }

    /** @brief Rejects a value that cannot stay equal to itself while stored. */
    void require_key(const Value &key) const
    {
        if (const auto rejection = dictionary_key_rejection(key))
        {
            fail(*rejection);
        }
    }

    void require_argument_key(const std::size_t index) const
    {
        if (const auto rejection = dictionary_key_rejection(argument(index)))
        {
            fail_argument(index, *rejection);
        }
    }

    /**
     * @brief Holds a value entering a collection to that collection's contract.
     *
     * A value that does not satisfy it is refused; one that does carries the
     * type from here on, so the inner lists of a `Liste[Liste[Entier]]` enforce
     * `Liste[Entier]` on what is later added to them.
     */
    void enforce(const std::size_t index, const std::string &type_name) const
    {
        const Value &value = argument(index);
        if (!runtime.matches_declared_type(value, type_name))
        {
            fail_argument(index,
                          messages::type_attendu(signature(),
                                                 display_runtime_type(type_name),
                                                 value.type_name()));
        }
        runtime.annotate_value(value, type_name, args[index].site.line == 0 ? site : args[index].site);
    }

    /** @brief Records @p type_name on a collection built by this member. */
    void annotate(const Value &value, const std::string &type_name) const
    {
        runtime.annotate_value(value, type_name, site);
    }
};

struct BuiltinMember
{
    std::string_view name;
    std::size_t arity;
    Value (*body)(const MemberCall &);
};

namespace
{

// ---------------------------------------------------------------------------
// The members every sequence has
// ---------------------------------------------------------------------------

/** @brief The elements of a Liste, a ListeFixe or an Ensemble, in order. */
const std::vector<Value> &elements_of(const Value &receiver)
{
    if (receiver.is_liste())
    {
        return receiver.as_liste()->elements;
    }
    if (receiver.is_liste_fixe())
    {
        return receiver.as_liste_fixe()->elements;
    }
    // These members are reachable only from the three tables that list them,
    // so a receiver of any other family means a table wired to the wrong one.
    assert(receiver.is_ensemble());
    return receiver.as_ensemble()->items();
}

Value sequence_taille(const MemberCall &call)
{
    return Value::entier(static_cast<std::int64_t>(elements_of(call.receiver).size()));
}

Value sequence_vide(const MemberCall &call)
{
    return Value::logique(elements_of(call.receiver).empty());
}

Value sequence_contient(const MemberCall &call)
{
    for (const Value &element : elements_of(call.receiver))
    {
        if (call.runtime.is_equal(element, call.argument(0)))
        {
            return Value::logique(true);
        }
    }
    return Value::logique(false);
}

Value sequence_joindre(const MemberCall &call)
{
    const std::string &separator = call.text(0);
    const std::vector<Value> &elements = elements_of(call.receiver);
    std::string joined;
    for (std::size_t index = 0; index < elements.size(); ++index)
    {
        if (index > 0)
        {
            joined += separator;
        }
        joined += call.runtime.to_text(elements[index]);
    }
    return Value::texte(std::move(joined));
}

constexpr BuiltinMember sequence_members[] = {
    {"taille", 0, sequence_taille},
    {"vide", 0, sequence_vide},
    {"contient", 1, sequence_contient},
    {"joindre", 1, sequence_joindre},
};

// ---------------------------------------------------------------------------
// Liste
// ---------------------------------------------------------------------------

Value liste_ajouter(const MemberCall &call)
{
    const auto list = call.receiver.as_liste();
    if (list->constraint)
    {
        call.enforce(0, list->constraint->element_type);
    }
    list->elements.push_back(call.argument(0));
    return Value::entier(static_cast<std::int64_t>(list->elements.size()));
}

Value liste_inserer(const MemberCall &call)
{
    const auto list = call.receiver.as_liste();
    const std::int64_t position = call.integer(0);
    if (position < 0 || static_cast<std::size_t>(position) > list->elements.size())
    {
        call.fail_argument(0, "indice d'insertion hors limites");
    }
    if (list->constraint)
    {
        call.enforce(1, list->constraint->element_type);
    }
    list->elements.insert(list->elements.begin() + position, call.argument(1));
    return Value::entier(position);
}

Value liste_retirer_a(const MemberCall &call)
{
    const auto list = call.receiver.as_liste();
    const std::int64_t position = call.integer(0);
    if (position < 0 || static_cast<std::size_t>(position) >= list->elements.size())
    {
        call.fail_argument(0, messages::indice_hors_limites(position, list->elements.size(), "Liste"));
    }
    Value removed = list->elements[static_cast<std::size_t>(position)];
    list->elements.erase(list->elements.begin() + position);
    return removed;
}

Value liste_en_ensemble(const MemberCall &call)
{
    const auto list = call.receiver.as_liste();
    auto set = make_ref<EnsembleData>();
    set->reserve(list->elements.size());
    for (const Value &element : list->elements)
    {
        call.require_key(element);
        set->insert(element);
    }

    Value result = Value::ensemble(std::move(set));
    if (list->constraint)
    {
        call.annotate(result, "Ensemble[" + list->constraint->element_type + "]");
    }
    return result;
}

Value liste_en_liste_fixe(const MemberCall &call)
{
    const auto list = call.receiver.as_liste();
    const std::int64_t length = call.integer(0);
    if (length < 0)
    {
        call.fail_argument(0, "la taille d'une ListeFixe ne peut pas être négative");
    }
    if (static_cast<std::size_t>(length) != list->elements.size())
    {
        call.fail_argument(0,
                           call.signature() + " requiert une liste de taille exacte " +
                               std::to_string(length));
    }

    auto fixed = make_ref<ListeFixeData>();
    fixed->elements = list->elements;
    Value result = Value::liste_fixe(std::move(fixed));
    if (list->constraint)
    {
        call.annotate(result,
                      "ListeFixe[" + list->constraint->element_type + ", " + std::to_string(length) + "]");
    }
    return result;
}

constexpr BuiltinMember liste_members[] = {
    {"ajouter", 1, liste_ajouter},
    {"inserer", 2, liste_inserer},
    {"retirer_a", 1, liste_retirer_a},
    {"en_ensemble", 0, liste_en_ensemble},
    {"en_liste_fixe", 1, liste_en_liste_fixe},
};

// ---------------------------------------------------------------------------
// ListeFixe
// ---------------------------------------------------------------------------

Value liste_fixe_en_liste(const MemberCall &call)
{
    const auto fixed = call.receiver.as_liste_fixe();
    auto dynamic = make_ref<ListeData>();
    dynamic->elements = fixed->elements;

    Value result = Value::liste(std::move(dynamic));
    if (fixed->constraint)
    {
        call.annotate(result, "Liste[" + fixed->constraint->element_type + "]");
    }
    return result;
}

constexpr BuiltinMember liste_fixe_members[] = {
    {"en_liste", 0, liste_fixe_en_liste},
};

// ---------------------------------------------------------------------------
// Ensemble
// ---------------------------------------------------------------------------

/** @brief The Ensemble a set operation is applied to. */
Ref<EnsembleData> other_set(const MemberCall &call)
{
    if (!call.argument(0).is_ensemble())
    {
        call.fail_argument(0, messages::valeur_attendue(call.signature(), "Ensemble"));
    }
    return call.argument(0).as_ensemble();
}

/** @brief A set derived from this receiver, carrying its contract if it has one. */
Value derived_set(const MemberCall &call, Ref<EnsembleData> elements)
{
    const auto set = call.receiver.as_ensemble();
    Value result = Value::ensemble(std::move(elements));
    if (set->constraint)
    {
        call.annotate(result, "Ensemble[" + set->constraint->element_type + "]");
    }
    return result;
}

Value ensemble_contient(const MemberCall &call)
{
    // Indexed, unlike the shared sequence member, which scans.
    return Value::logique(call.receiver.as_ensemble()->contains(call.argument(0)));
}

Value ensemble_ajouter(const MemberCall &call)
{
    const auto set = call.receiver.as_ensemble();
    if (set->constraint)
    {
        call.enforce(0, set->constraint->element_type);
    }
    call.require_argument_key(0);
    return Value::logique(set->insert(call.argument(0)));
}

Value ensemble_retirer(const MemberCall &call)
{
    return Value::logique(call.receiver.as_ensemble()->erase(call.argument(0)));
}

Value ensemble_en_liste(const MemberCall &call)
{
    const auto set = call.receiver.as_ensemble();
    auto list = make_ref<ListeData>();
    list->elements = set->items();

    Value result = Value::liste(std::move(list));
    if (set->constraint)
    {
        call.annotate(result, "Liste[" + set->constraint->element_type + "]");
    }
    return result;
}

Value ensemble_union(const MemberCall &call)
{
    const auto other = other_set(call);
    auto result = make_ref<EnsembleData>();
    for (const Value &element : call.receiver.as_ensemble()->items())
    {
        result->insert(element);
    }
    for (const Value &element : other->items())
    {
        result->insert(element);
    }
    return derived_set(call, std::move(result));
}

// Intersection and difference differ only in which answer to the membership
// test they keep, so they walk the receiver once, the same way.
Value ensemble_retenir(const MemberCall &call, const bool keep_present)
{
    const auto other = other_set(call);
    auto result = make_ref<EnsembleData>();
    for (const Value &element : call.receiver.as_ensemble()->items())
    {
        if (other->contains(element) == keep_present)
        {
            result->insert(element);
        }
    }
    return derived_set(call, std::move(result));
}

Value ensemble_intersection(const MemberCall &call) { return ensemble_retenir(call, true); }

Value ensemble_difference(const MemberCall &call) { return ensemble_retenir(call, false); }

Value ensemble_sous_ensemble_de(const MemberCall &call)
{
    const auto other = other_set(call);
    for (const Value &element : call.receiver.as_ensemble()->items())
    {
        if (!other->contains(element))
        {
            return Value::logique(false);
        }
    }
    return Value::logique(true);
}

constexpr BuiltinMember ensemble_members[] = {
    {"contient", 1, ensemble_contient},
    {"ajouter", 1, ensemble_ajouter},
    {"retirer", 1, ensemble_retirer},
    {"en_liste", 0, ensemble_en_liste},
    {"union", 1, ensemble_union},
    {"intersection", 1, ensemble_intersection},
    {"difference", 1, ensemble_difference},
    {"différence", 1, ensemble_difference},
    {"sous_ensemble_de", 1, ensemble_sous_ensemble_de},
};

// ---------------------------------------------------------------------------
// Dictionnaire
// ---------------------------------------------------------------------------

Value dictionnaire_taille(const MemberCall &call)
{
    return Value::entier(static_cast<std::int64_t>(call.receiver.as_dictionnaire()->size()));
}

Value dictionnaire_vide(const MemberCall &call)
{
    return Value::logique(call.receiver.as_dictionnaire()->empty());
}

Value dictionnaire_contient(const MemberCall &call)
{
    return Value::logique(call.receiver.as_dictionnaire()->find(call.argument(0)) != nullptr);
}

// The keys and the values are the same walk over the same entries, differing
// only in which half of each one is taken.
Value dictionnaire_moitie(const MemberCall &call, const bool keys)
{
    const auto dictionary = call.receiver.as_dictionnaire();
    auto half = make_ref<ListeData>();
    for (const DictEntry &entry : dictionary->items())
    {
        half->elements.push_back(keys ? entry.first : entry.second);
    }

    Value result = Value::liste(std::move(half));
    if (dictionary->constraint)
    {
        const DictConstraint &constraint = *dictionary->constraint;
        call.annotate(result, "Liste[" + (keys ? constraint.key_type : constraint.value_type) + "]");
    }
    return result;
}

Value dictionnaire_cles(const MemberCall &call) { return dictionnaire_moitie(call, true); }

Value dictionnaire_valeurs(const MemberCall &call) { return dictionnaire_moitie(call, false); }

Value dictionnaire_paires(const MemberCall &call)
{
    const auto dictionary = call.receiver.as_dictionnaire();
    // A pair holds a key beside a value, so it has one element type only when
    // the two are the same type. Its length is 2 either way.
    std::string pair_type;
    if (dictionary->constraint)
    {
        const DictConstraint &constraint = *dictionary->constraint;
        const std::string element_type =
            constraint.key_type == constraint.value_type ? constraint.key_type : "Universel";
        pair_type = "ListeFixe[" + element_type + ", 2]";
    }

    auto pairs = make_ref<ListeData>();
    for (const DictEntry &entry : dictionary->items())
    {
        auto pair = make_ref<ListeFixeData>();
        pair->elements = {entry.first, entry.second};
        Value value = Value::liste_fixe(std::move(pair));
        if (!pair_type.empty())
        {
            call.annotate(value, pair_type);
        }
        pairs->elements.push_back(std::move(value));
    }

    Value result = Value::liste(std::move(pairs));
    if (!pair_type.empty())
    {
        call.annotate(result, "Liste[" + pair_type + "]");
    }
    return result;
}

Value dictionnaire_retirer(const MemberCall &call)
{
    Value removed;
    if (!call.receiver.as_dictionnaire()->erase(call.argument(0), removed))
    {
        call.fail_argument(0, messages::cle_introuvable());
    }
    return removed;
}

constexpr BuiltinMember dictionnaire_members[] = {
    {"taille", 0, dictionnaire_taille},
    {"vide", 0, dictionnaire_vide},
    {"contient", 1, dictionnaire_contient},
    {"cles", 0, dictionnaire_cles},
    {"clés", 0, dictionnaire_cles},
    {"valeurs", 0, dictionnaire_valeurs},
    {"paires", 0, dictionnaire_paires},
    {"retirer", 1, dictionnaire_retirer},
};

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

const BuiltinMember *find_in(const std::span<const BuiltinMember> table, const std::string_view name)
{
    for (const BuiltinMember &member : table)
    {
        if (member.name == name)
        {
            return &member;
        }
    }
    return nullptr;
}

} // namespace

const BuiltinMember *find_builtin_member(const Value &receiver, const std::string_view member)
{
    // A family's own table is searched first, so a family that has a better
    // answer than the shared one -- Ensemble.contient is indexed -- gives it.
    if (receiver.is_liste())
    {
        const BuiltinMember *found = find_in(liste_members, member);
        return found != nullptr ? found : find_in(sequence_members, member);
    }
    if (receiver.is_liste_fixe())
    {
        const BuiltinMember *found = find_in(liste_fixe_members, member);
        return found != nullptr ? found : find_in(sequence_members, member);
    }
    if (receiver.is_ensemble())
    {
        const BuiltinMember *found = find_in(ensemble_members, member);
        return found != nullptr ? found : find_in(sequence_members, member);
    }
    // A Dictionnaire is not a sequence: it answers only its own members.
    if (receiver.is_dictionnaire())
    {
        return find_in(dictionnaire_members, member);
    }
    return nullptr;
}

Value call_builtin_member(IRuntime &runtime,
                          const BuiltinMember &member,
                          const Value &receiver,
                          const std::vector<RuntimeArgument> &args,
                          const RuntimeSite &site)
{
    // The member and the receiver must be the pair the lookup made. The tree
    // walker binds one to a receiver and calls it later, so a bound method that
    // outlived its receiver's type would otherwise read another family's data.
    assert(find_builtin_member(receiver, member.name) == &member);

    const MemberCall call {runtime, receiver, member.name, args, site};
    call.expect(member.arity);
    return member.body(call);
}

std::optional<Value> call_builtin_member(IRuntime &runtime,
                                         const Value &receiver,
                                         const std::string_view member,
                                         const std::vector<RuntimeArgument> &args,
                                         const RuntimeSite &site)
{
    const BuiltinMember *found = find_builtin_member(receiver, member);
    if (found == nullptr)
    {
        return std::nullopt;
    }
    return call_builtin_member(runtime, *found, receiver, args, site);
}

} // namespace lumiere
