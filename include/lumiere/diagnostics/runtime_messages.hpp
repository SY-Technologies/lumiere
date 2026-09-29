#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lumiere::messages
{

/**
 * @brief The runtime diagnostics both engines can reach.
 *
 * A message a program can trigger is worded once, here. The tree walker and the
 * bytecode VM detect the same conditions in different code, and when each kept
 * its own wording they drifted: the same division reported "division par zero"
 * from one and "division par zéro interdite" from the other, and an unknown
 * member on a list named the member in one and blamed the receiver in the other.
 */

inline std::string memoire_insuffisante()
{
    return "mémoire insuffisante pour terminer l'opération";
}

inline std::string taille_hors_limites()
{
    return "la taille demandée dépasse ce que cette opération peut représenter";
}

inline std::string division_par_zero()
{
    return "division par zéro";
}

inline std::string modulo_par_zero()
{
    return "modulo par zéro";
}

inline std::string depassement_entier(const std::string_view operation)
{
    return std::string("dépassement de capacité d'un Entier lors de ") + std::string(operation);
}

inline std::string indice_hors_limites(const std::int64_t index,
                                       const std::size_t length,
                                       const std::string_view family)
{
    return "indice hors limites : " + std::to_string(index) + " pour " + std::string(family) +
           " de taille " + std::to_string(length);
}

inline std::string indice_non_entier(const std::string_view family)
{
    return "l'indice d'" + std::string(family) + " doit être un Entier";
}

inline std::string membre_introuvable(const std::string_view member, const std::string_view type_name)
{
    return "membre introuvable '" + std::string(member) + "' pour une valeur de type " + std::string(type_name);
}

inline std::string valeur_non_appelable(const std::string_view type_name)
{
    return "la valeur appelée n'est pas une fonction : elle est de type " + std::string(type_name);
}

inline std::string acces_indice_impossible(const std::string_view type_name)
{
    return "accès par indice impossible pour une valeur de type " + std::string(type_name);
}

inline std::string affectation_indice_impossible(const std::string_view type_name)
{
    return "affectation par indice impossible pour une valeur de type " + std::string(type_name);
}

inline std::string conversion_impossible(const std::string_view target, const std::string_view source)
{
    return "conversion vers " + std::string(target) + " impossible pour une valeur de type " + std::string(source);
}

inline std::string valeur_non_iterable(const std::string_view type_name)
{
    return "cette valeur n'est pas itérable : elle est de type " + std::string(type_name);
}

/**
 * @brief A binary operator applied to operands it does not accept.
 *
 * Naming both operand types is the difference between a message that says what
 * is wrong and one that only says what was wanted. The two engines detect this
 * in unrelated code -- one on an AST node, one on a stack -- so the wording
 * lives here rather than in either of them.
 */
inline std::string operandes_attendues(const std::string_view operation,
                                       const std::string_view expected,
                                       const std::string_view left_type,
                                       const std::string_view right_type)
{
    return std::string(operation) + " attend " + std::string(expected) + "; types reçus : " +
           std::string(left_type) + " et " + std::string(right_type);
}

/** @brief A unary operator applied to an operand it does not accept. */
inline std::string operande_attendue(const std::string_view operation,
                                     const std::string_view expected,
                                     const std::string_view received_type)
{
    return std::string(operation) + " attend " + std::string(expected) + "; type reçu : " +
           std::string(received_type);
}

/**
 * @brief A builtin called with the wrong number of arguments.
 *
 * Every builtin takes a fixed number of positional arguments, so the count and
 * the refusal of names below are the same two sentences everywhere: in the
 * stdlib modules, and in the members a collection carries.
 */
inline std::string arite_exacte(const std::string_view signature, const std::size_t expected)
{
    return std::string(signature) + " attend exactement " + std::to_string(expected) + " argument(s)";
}

/** @brief A builtin given an argument by name. */
inline std::string arguments_nommes_refuses(const std::string_view signature)
{
    return std::string(signature) + " n'accepte pas d'arguments nommés";
}

/** @brief A builtin argument of a type it does not take. */
inline std::string valeur_attendue(const std::string_view signature, const std::string_view type)
{
    return std::string(signature) + " attend une valeur de type " + std::string(type);
}

/** @brief A declared element, key or value type was not respected. */
inline std::string type_attendu(const std::string_view context,
                                const std::string_view expected,
                                const std::string_view received)
{
    return std::string(context) + " attend une valeur de type " + std::string(expected) +
           "; type reçu : " + std::string(received);
}

/** @brief A name that resolves to nothing, whether it is called or merely read. */
inline std::string symbole_introuvable(const std::string_view name)
{
    return "le symbole '" + std::string(name) + "' est introuvable dans la portée courante";
}

inline std::string cle_introuvable()
{
    return "clé introuvable dans le Dictionnaire";
}

inline std::string liste_fixe_immuable()
{
    return "une ListeFixe est immuable : ses éléments ne peuvent pas être remplacés";
}

inline std::string cle_non_nombre()
{
    return "une valeur non-nombre ne peut pas servir de clé : elle n'est égale à aucune valeur, "
           "pas même à elle-même";
}

} // namespace lumiere::messages
