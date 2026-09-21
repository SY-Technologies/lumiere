#include "lumiere/interpreter/runtime/members.hpp"

#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/runtime/nominal_type.hpp"

#include <cstdint>
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
        for (const RuntimeArgument &argument : args)
        {
            if (!argument.name.empty())
            {
                fail(messages::arguments_nommes_refuses(signature()));
            }
        }
    }

    [[nodiscard]] const Value &argument(const std::size_t index) const { return args[index].value; }

    [[nodiscard]] std::int64_t integer(const std::size_t index) const
    {
        if (!argument(index).is_entier())
        {
            fail(messages::valeur_attendue(signature(), "Entier"));
        }
        return argument(index).as_entier();
    }

    [[nodiscard]] const std::string &text(const std::size_t index) const
    {
        if (!argument(index).is_texte())
        {
            fail(messages::valeur_attendue(signature(), "Texte"));
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

    /**
     * @brief Holds a value entering a collection to that collection's contract.
     *
     * A value that does not satisfy it is refused; one that does carries the
     * type from here on, so the inner lists of a `Liste[Liste[Entier]]` enforce
     * `Liste[Entier]` on what is later added to them.
     */
    void enforce(const Value &value, const std::string &type_name) const
    {
        if (!runtime.matches_declared_type(value, type_name))
        {
            fail(messages::type_attendu(signature(), display_runtime_type(type_name), value.type_name()));
        }
        runtime.annotate_value(value, type_name, site);
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
        call.enforce(call.argument(0), list->constraint->element_type);
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
        call.fail("indice d'insertion hors limites");
    }
    if (list->constraint)
    {
        call.enforce(call.argument(1), list->constraint->element_type);
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
        call.fail(messages::indice_hors_limites(position, list->elements.size(), "Liste"));
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
        call.fail("la taille d'une ListeFixe ne peut pas être négative");
    }
    if (static_cast<std::size_t>(length) != list->elements.size())
    {
        call.fail(call.signature() + " requiert une liste de taille exacte " + std::to_string(length));
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
// Ensemble
// ---------------------------------------------------------------------------

Value ensemble_contient(const MemberCall &call)
{
    // Indexed, unlike the shared sequence member, which scans.
    return Value::logique(call.receiver.as_ensemble()->contains(call.argument(0)));
}

constexpr BuiltinMember ensemble_members[] = {
    {"contient", 1, ensemble_contient},
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
        return find_in(sequence_members, member);
    }
    if (receiver.is_ensemble())
    {
        const BuiltinMember *found = find_in(ensemble_members, member);
        return found != nullptr ? found : find_in(sequence_members, member);
    }
    return nullptr;
}

Value call_builtin_member(IRuntime &runtime,
                          const BuiltinMember &member,
                          const Value &receiver,
                          const std::vector<RuntimeArgument> &args,
                          const RuntimeSite &site)
{
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
