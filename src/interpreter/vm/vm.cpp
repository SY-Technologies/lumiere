#include "lumiere/interpreter/vm/vm.hpp"
#include "lumiere/interpreter/runtime/members.hpp"

#include "lumiere/interpreter/vm/compiler.hpp"
#include "lumiere/interpreter/vm/verifier.hpp"
#include "lumiere/interpreter/runtime/cycles.hpp"
#include "native_globals.hpp"
#include "vm_error.hpp"
#include "lumiere/interpreter/runtime/iruntime.hpp"
#include "lumiere/interpreter/runtime/runtime_argument.hpp"
#include "lumiere/interpreter/tree_walker/runtime.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/parser/utf8.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"
#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/runtime/collection_constraints.hpp"
#include "lumiere/interpreter/runtime/nominal_type.hpp"

#include <cassert>
#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lumiere
{
namespace
{

Value checked_integer(std::optional<int64_t> value)
{
    if (!value)
        throw VmRuntimeError("VM: valeur hors limites pour Entier");
    return Value::entier(*value);
}

std::uint8_t read_byte(const Chunk &chunk, std::size_t &ip)
{
    // verify_module has already proved that every operand this reads is present,
    // so the bound is not re-tested here: it was two comparisons per instruction
    // on the hottest path in the runtime. The assertion keeps the guarantee under
    // test, where the Debug and sanitizer builds run the whole suite.
    assert(ip < chunk.code.size());
    return chunk.code[ip++];
}

Opcode read_opcode(const Chunk &chunk, std::size_t &ip)
{
    return static_cast<Opcode>(read_byte(chunk, ip));
}

std::size_t read_u24(const Chunk &chunk, std::size_t &ip)
{
    const std::size_t byte2 = read_byte(chunk, ip);
    const std::size_t byte1 = read_byte(chunk, ip);
    const std::size_t byte0 = read_byte(chunk, ip);
    return (byte2 << 16) | (byte1 << 8) | byte0;
}

/**
 * @brief The argument names of a call, read from the bytecode where they lie.
 *
 * A call used to copy them into a vector of std::string before touching the
 * stack -- one allocation and one string copy per call, on the path that was
 * measured at four allocations per call and 3.5x CPython. The operands are
 * contiguous and the verifier has already proved they are present, so the
 * instruction can simply remember where they start and read the one it needs.
 */
class ArgumentNames
{
public:
    // Consumes the operands: `ip` is left after the last one.
    ArgumentNames(const Chunk &chunk, std::size_t &ip, const std::size_t count)
        : m_chunk(chunk), m_offset(ip)
    {
        ip += count * 2;
    }

    [[nodiscard]] std::size_t index(const std::size_t argument) const
    {
        const std::size_t at = m_offset + argument * 2;
        return (static_cast<std::size_t>(m_chunk.code[at]) << 8) | m_chunk.code[at + 1];
    }

    [[nodiscard]] SourceLocation location(const std::size_t argument) const
    {
        return m_chunk.locations[m_offset + argument * 2];
    }

private:
    const Chunk &m_chunk;
    std::size_t m_offset;
};

std::size_t read_u16(const Chunk &chunk, std::size_t &ip)
{
    const std::size_t byte1 = read_byte(chunk, ip);
    const std::size_t byte0 = read_byte(chunk, ip);
    return (byte1 << 8) | byte0;
}

Value pop_value(std::vector<Value> &stack)
{
    if (stack.empty())
    {
        throw VmRuntimeError("VM: pile vide");
    }

    // Arithmetic-heavy loops need only the integer payload, not a variant move.
    Value value = stack.back().is_entier()
                      ? Value::entier(stack.back().as_entier())
                      : std::move(stack.back());
    stack.pop_back();
    return value;
}

double numeric_value(const Value &value)
{
    if (value.is_decimal())
    {
        return value.as_decimal();
    }
    if (value.is_entier())
    {
        return static_cast<double>(value.as_entier());
    }
    throw VmRuntimeError("VM: opération arithmétique attend des valeurs numériques");
}

bool is_truthy(const Value &value)
{
    if (value.is_rien())
    {
        return false;
    }
    if (value.is_logique())
    {
        return value.as_logique();
    }
    return true;
}

bool values_equal(const Value &left, const Value &right)
{
    return left == right;
}

void require_dictionary_key(const Value &key)
{
    if (const auto rejection = dictionary_key_rejection(key))
    {
        throw VmRuntimeError("VM: " + *rejection);
    }
}

class VmRuntimeServices final : public IRuntime
{
public:
    using CallbackExecutor = std::function<Value(Value, const NativeArgs &)>;

    Value call(Value callee, const NativeArgs &args) override;

    void set_callback_executor(CallbackExecutor executor)
    {
        m_callback_executor = std::move(executor);
    }

    [[noreturn]] void raise_runtime_error(const RuntimeSite &site, const std::string &message) const override
    {
        throw VmRuntimeError("VM: " + message, site);
    }

    bool is_equal(const Value &left, const Value &right) const override
    {
        return values_equal(left, right);
    }

    std::string to_text(const Value &value) const override
    {
        return value.to_string();
    }

    bool matches_declared_type(const Value &value, std::string_view type_name) const override;

    void annotate_value(const Value &value, std::string_view type_name, const RuntimeSite &) const override;

    void enforce_declared_type(const Value &value,
                               const std::string &type_name,
                               const std::string &context) const;
    void enforce_list_element(const Ref<ListeData> &list,
                              const Value &value,
                              const std::string &context) const;
    void enforce_dictionary_entry(const Ref<DictData> &dictionary,
                                  const Value &key,
                                  const Value &value,
                                  const std::string &context) const;


private:
    CallbackExecutor m_callback_executor;
};

std::vector<std::string_view> split_generic_arguments(const std::string_view specification)
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

std::string_view trim_type_name(std::string_view name)
{
    const std::size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
}

bool matches_type_name(const Value &value, std::string_view full_name)
{
    full_name = trim_type_name(full_name);
    std::size_t depth = 0;
    for (std::size_t i = 0; i < full_name.size(); ++i)
    {
        if (full_name[i] == '[')
        {
            ++depth;
        }
        else if (full_name[i] == ']')
        {
            --depth;
        }
        else if (full_name[i] == '|' && depth == 0)
        {
            return matches_type_name(value, full_name.substr(0, i)) ||
                   matches_type_name(value, full_name.substr(i + 1));
        }
    }

    const std::size_t generic_start = full_name.find('[');
    const std::string_view name = full_name.substr(0, generic_start);
    const std::string_view generic_spec = generic_start == std::string_view::npos
                                              ? std::string_view{}
                                              : full_name.substr(generic_start + 1, full_name.size() - generic_start - 2);

    if (name == "Entier")
    {
        return value.is_entier();
    }
    if (name == "Décimal" || name == "Decimal")
    {
        return value.is_decimal() || value.is_entier();
    }
    if (name == "Logique")
    {
        return value.is_logique();
    }
    if (name == "Symbole")
    {
        return value.is_symbole();
    }
    if (name == "Texte")
    {
        return value.is_texte();
    }
    if (name == "Rien")
    {
        return value.is_rien();
    }
    if (name == "Universel")
    {
        return true;
    }
    if (name == "Résultat")
    {
        if (!value.is_resultat())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }
        const auto arguments = split_generic_arguments(generic_spec);
        if (arguments.size() != 2)
        {
            return false;
        }
        const auto result = value.as_resultat();
        return matches_type_name(
            result->payload,
            arguments[result->success ? 0 : 1]);
    }
    if (name == "Classe")
    {
        return value.is_classe();
    }
    if (name == "Interface")
    {
        return value.is_interface();
    }

    if (value.is_objet())
    {
        Ref<LumiereClass> klass = value.as_objet()->klass;
        while (klass != nullptr)
        {
            if ((klass->type_identity.empty() ? klass->name : klass->type_identity) == name ||
                klass->interfaces.contains(std::string(name)))
            {
                return true;
            }
            klass = klass->parent;
        }
    }

    if (name == "Liste")
    {
        if (!value.is_liste())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }
        const auto arguments = split_generic_arguments(generic_spec);
        if (arguments.size() != 1)
        {
            return false;
        }
        for (const Value &element : value.as_liste()->elements)
        {
            if (!matches_type_name(element, arguments[0]))
            {
                return false;
            }
        }
        return true;
    }

    if (name == "ListeFixe")
    {
        if (!value.is_liste_fixe())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }
        const auto arguments = split_generic_arguments(generic_spec);
        if (arguments.size() != 2)
        {
            return false;
        }
        std::size_t expected_length = 0;
        try
        {
            expected_length = static_cast<std::size_t>(std::stoll(std::string(arguments[1])));
        }
        catch (...)
        {
            return false;
        }
        if (value.as_liste_fixe()->elements.size() != expected_length)
        {
            return false;
        }
        for (const Value &element : value.as_liste_fixe()->elements)
        {
            if (!matches_type_name(element, arguments[0]))
            {
                return false;
            }
        }
        return true;
    }

    if (name == "Dictionnaire")
    {
        if (!value.is_dictionnaire())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }
        const auto arguments = split_generic_arguments(generic_spec);
        if (arguments.size() != 2)
        {
            return false;
        }
        for (const auto &[key, entry_value] : value.as_dictionnaire()->items())
        {
            if (!matches_type_name(key, arguments[0]) || !matches_type_name(entry_value, arguments[1]))
            {
                return false;
            }
        }
        return true;
    }

    if (name == "Ensemble")
    {
        if (!value.is_ensemble())
        {
            return false;
        }
        if (generic_spec.empty())
        {
            return true;
        }
        const auto arguments = split_generic_arguments(generic_spec);
        if (arguments.size() != 1)
        {
            return false;
        }
        for (const Value &element : value.as_ensemble()->items())
        {
            if (!matches_type_name(element, arguments[0]))
            {
                return false;
            }
        }
        return true;
    }

    return false;
}

std::vector<std::string_view> annotation_arguments(const std::string_view annotation)
{
    const std::size_t begin = annotation.find('[');
    if (begin == std::string_view::npos || annotation.back() != ']')
    {
        return {};
    }
    return split_generic_arguments(annotation.substr(begin + 1, annotation.size() - begin - 2));
}

void VmRuntimeServices::annotate_value(const Value &value,
                                     std::string_view type_name,
                                     const RuntimeSite &site) const
{
    if (!value.is_liste() && !value.is_liste_fixe() && !value.is_dictionnaire() && !value.is_ensemble() && !value.is_resultat())
        return;
    // Re-annotation may update metadata that owns the caller's string view.
    const std::string annotation(trim_type_name(type_name));
    type_name = annotation;
    if (const auto separator = find_collection_type_union(type_name); separator != std::string_view::npos)
    {
        const auto left = type_name.substr(0, separator);
        annotate_value(value, matches_type_name(value, left) ? left : type_name.substr(separator + 1), site);
        return;
    }
    const auto arguments = annotation_arguments(type_name);
    if (type_name.starts_with("Résultat[") && value.is_resultat() && arguments.size() == 2)
    {
        const auto result = value.as_resultat();
        annotate_value(result->payload, arguments[result->success ? 0 : 1], site);
    }
    else if (type_name.starts_with("Liste[") && value.is_liste() && arguments.size() == 1)
    {
        if (!merge_collection_constraint(value.as_liste()->constraint, ListConstraint{std::string(arguments[0])}))
            throw VmRuntimeError("annotation de collection incompatible avec le contrat existant");
        for (const auto &element : value.as_liste()->elements)
            annotate_value(element, arguments[0], site);
    }
    else if (type_name.starts_with("ListeFixe[") && value.is_liste_fixe() && arguments.size() == 2)
    {
        if (!merge_collection_constraint(value.as_liste_fixe()->constraint, FixedListConstraint{
                std::string(arguments[0]), value.as_liste_fixe()->elements.size()}))
            throw VmRuntimeError("annotation de collection incompatible avec le contrat existant");
        for (const auto &element : value.as_liste_fixe()->elements)
            annotate_value(element, arguments[0], site);
    }
    else if (type_name.starts_with("Dictionnaire[") && value.is_dictionnaire() && arguments.size() == 2)
    {
        if (!merge_collection_constraint(value.as_dictionnaire()->constraint, DictConstraint{std::string(arguments[0]), std::string(arguments[1])}))
            throw VmRuntimeError("annotation de collection incompatible avec le contrat existant");
        for (const auto &[key, element] : value.as_dictionnaire()->items())
        {
            annotate_value(key, arguments[0], site);
            annotate_value(element, arguments[1], site);
        }
    }
    else if (type_name.starts_with("Ensemble[") && value.is_ensemble() && arguments.size() == 1)
    {
        if (!merge_collection_constraint(value.as_ensemble()->constraint, SetConstraint{std::string(arguments[0])}))
            throw VmRuntimeError("annotation de collection incompatible avec le contrat existant");
        for (const auto &element : value.as_ensemble()->items())
            annotate_value(element, arguments[0], site);
    }
}

bool VmRuntimeServices::matches_declared_type(const Value &value, const std::string_view type_name) const
{
    return matches_type_name(value, type_name);
}

// The two element checks say the same thing about different containers, so they
// say it once: refuse a value the contract does not admit, and record the
// contract on one it does.
void VmRuntimeServices::enforce_declared_type(const Value &value,
                                              const std::string &type_name,
                                              const std::string &context) const
{
    if (!matches_type_name(value, type_name))
    {
        throw VmRuntimeError("VM: " + messages::type_attendu(context, display_runtime_type(type_name), value.type_name()));
    }
    annotate_value(value, type_name, {});
}

void VmRuntimeServices::enforce_list_element(const Ref<ListeData> &list,
                                           const Value &value,
                                           const std::string &context) const
{
    if (list->constraint)
        enforce_declared_type(value, list->constraint->element_type, context);
}

void VmRuntimeServices::enforce_dictionary_entry(const Ref<DictData> &dictionary,
                                               const Value &key,
                                               const Value &value,
                                               const std::string &context) const
{
    const auto &constraint = dictionary->constraint;
    if (!constraint)
    {
        return;
    }
    if (!matches_type_name(key, constraint->key_type))
    {
        throw VmRuntimeError("VM: " + messages::type_attendu(context + " (clé)",
                                                             display_runtime_type(constraint->key_type),
                                                             key.type_name()));
    }
    if (!matches_type_name(value, constraint->value_type))
    {
        throw VmRuntimeError("VM: " + messages::type_attendu(context + " (valeur)",
                                                             display_runtime_type(constraint->value_type),
                                                             value.type_name()));
    }
    annotate_value(key, constraint->key_type, {});
    annotate_value(value, constraint->value_type, {});
}

void execute_cast(std::vector<Value> &stack, const std::string &target)
{
    const Value operand = pop_value(stack);

    if (target == "Entier")
    {
        if (operand.is_entier())
        {
            stack.push_back(operand);
        }
        else if (operand.is_decimal())
        {
            stack.push_back(checked_integer(numeric::to_integer(operand.as_decimal())));
        }
        else if (operand.is_symbole())
        {
            stack.push_back(Value::entier(static_cast<std::int64_t>(operand.as_symbole())));
        }
        else if (operand.is_texte())
        {
            try {
                std::size_t consumed = 0;
                const auto value = std::stoll(operand.as_texte(), &consumed);
                if (consumed != operand.as_texte().size())
                    throw std::invalid_argument("caractères restants");
                stack.push_back(Value::entier(value));
            }
            catch (...) { throw VmRuntimeError("VM: " + messages::conversion_impossible("Entier", "Texte")); }
        }
        else
        {
            throw VmRuntimeError("VM: conversion explicite non prise en charge vers Entier");
        }
        return;
    }

    if (target == "Décimal" || target == "Decimal")
    {
        if (operand.is_decimal())
        {
            stack.push_back(operand);
        }
        else if (operand.is_entier())
        {
            stack.push_back(Value::decimal(static_cast<double>(operand.as_entier())));
        }
        else if (operand.is_texte())
        {
            const auto value = numeric::parse_decimal(operand.as_texte());
            if (!value)
            {
                throw VmRuntimeError("VM: " + messages::conversion_impossible("Décimal", "Texte"));
            }
            stack.push_back(Value::decimal(*value));
        }
        else
        {
            throw VmRuntimeError("VM: conversion explicite non prise en charge vers Décimal");
        }
        return;
    }

    if (target == "Logique")
    {
        if (operand.is_logique())
        {
            stack.push_back(operand);
        }
        else if (operand.is_texte() && operand.as_texte() == "vrai")
        {
            stack.push_back(Value::logique(true));
        }
        else if (operand.is_texte() && operand.as_texte() == "faux")
        {
            stack.push_back(Value::logique(false));
        }
        else
        {
            throw VmRuntimeError("VM: conversion vers Logique impossible: le texte doit valoir 'vrai' ou 'faux'");
        }
        return;
    }

    if (target == "Symbole")
    {
        if (operand.is_symbole())
        {
            stack.push_back(operand);
        }
        else if (operand.is_entier())
        {
            const std::int64_t code_point = operand.as_entier();
            if (code_point < 0 || code_point > 0x10FFFF ||
                (code_point >= 0xD800 && code_point <= 0xDFFF))
            {
                throw VmRuntimeError("VM: conversion vers Symbole impossible: le point de code Unicode est invalide");
            }
            stack.push_back(Value::symbole(static_cast<char32_t>(code_point)));
        }
        else if (operand.is_texte())
        {
            const auto character = utf8::decode_single_character(operand.as_texte());
            if (!character.has_value())
            {
                throw VmRuntimeError("VM: conversion vers Symbole impossible: le texte doit contenir exactement un caractère");
            }
            stack.push_back(Value::symbole(*character));
        }
        else
        {
            throw VmRuntimeError("VM: conversion explicite non prise en charge vers Symbole");
        }
        return;
    }

    if (target == "Texte")
    {
        stack.push_back(Value::texte(operand.to_string()));
        return;
    }
    if (target == "Universel")
    {
        stack.push_back(operand);
        return;
    }

    throw VmRuntimeError("VM: conversion explicite non prise en charge vers le type '" + target + "'");
}

void execute_type_check(std::vector<Value> &stack, const std::string &type_name)
{
    stack.push_back(Value::logique(matches_type_name(pop_value(stack), type_name)));
}

/**
 * @brief What an annotation demands of a value, decided once per module.
 *
 * A type is written as text in the source and travels into the bytecode as
 * text, so every check used to re-read that text: scan for a union bar, find
 * the generic bracket, then compare the head against seven builtin names.
 * `valeur: Entier` paid that twice per call -- once for the parameter and once
 * for the return -- and it showed: string work around type assertions was
 * about a tenth of the time on a method-call loop.
 *
 * The answer for a builtin scalar is one tag comparison. Anything with a
 * bracket, a bar, or a name the runtime does not know is Composite and still
 * goes through matches_type_name, which is where the real rules live.
 */
enum class TypeShape : std::uint8_t
{
    Entier,
    Decimal,
    Logique,
    Symbole,
    Texte,
    Rien,
    Universel,
    Composite,
};

TypeShape classify_type_name(std::string_view name)
{
    name = trim_type_name(name);
    if (name == "Entier") return TypeShape::Entier;
    if (name == "Décimal" || name == "Decimal") return TypeShape::Decimal;
    if (name == "Logique") return TypeShape::Logique;
    if (name == "Symbole") return TypeShape::Symbole;
    if (name == "Texte") return TypeShape::Texte;
    if (name == "Rien") return TypeShape::Rien;
    if (name == "Universel") return TypeShape::Universel;
    return TypeShape::Composite;
}

std::vector<TypeShape> classify_types(const std::vector<std::string> &types)
{
    std::vector<TypeShape> shapes;
    shapes.reserve(types.size());
    for (const std::string &name : types)
    {
        shapes.push_back(classify_type_name(name));
    }
    return shapes;
}

/** @brief Whether @p value satisfies a shape. Composite is never asked. */
bool matches_shape(const Value &value, const TypeShape shape)
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
    case TypeShape::Composite:
        break;
    }
    return false;
}

bool matches_type(const Value &value, const TypeShape shape, const std::string &name)
{
    return shape == TypeShape::Composite ? matches_type_name(value, name)
                                         : matches_shape(value, shape);
}

void execute_type_assertion(std::vector<Value> &stack,
                            const std::string &type_name,
                            const TypeShape shape,
                            const std::string &context,
                            VmRuntimeServices &runtime)
{
    if (stack.empty())
    {
        throw VmRuntimeError("VM: pile vide pendant la verification de type");
    }
    if (!matches_type(stack.back(), shape, type_name))
    {
        throw VmRuntimeError("VM: " + messages::type_attendu(context,
                                                             display_runtime_type(type_name),
                                                             stack.back().type_name()));
    }
    // Only a collection or a Résultat carries an annotation, and no builtin
    // scalar shape is either of those.
    if (shape == TypeShape::Composite || shape == TypeShape::Universel)
    {
        runtime.annotate_value(stack.back(), type_name, {});
    }
}

void execute_add(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        stack.push_back(checked_integer(numeric::add(left.as_entier(), right.as_entier())));
        return;
    }

    if (left.is_numeric() && right.is_numeric())
    {
        stack.push_back(Value::decimal(numeric_value(left) + numeric_value(right)));
        return;
    }

    if (left.is_texte() || right.is_texte())
    {
        stack.push_back(Value::texte(left.to_string() + right.to_string()));
        return;
    }

    throw VmRuntimeError("VM: " + messages::operandes_attendues(
        "l'addition", "deux valeurs numériques ou au moins un Texte",
        left.type_name(), right.type_name()));
}

void execute_subtract(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        stack.push_back(checked_integer(numeric::subtract(left.as_entier(), right.as_entier())));
        return;
    }

    if (left.is_numeric() && right.is_numeric())
    {
        stack.push_back(Value::decimal(numeric_value(left) - numeric_value(right)));
        return;
    }

    throw VmRuntimeError("VM: " + messages::operandes_attendues(
        "la soustraction", "deux valeurs numériques", left.type_name(), right.type_name()));
}

void execute_multiply(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        stack.push_back(checked_integer(numeric::multiply(left.as_entier(), right.as_entier())));
        return;
    }

    if (left.is_numeric() && right.is_numeric())
    {
        stack.push_back(Value::decimal(numeric_value(left) * numeric_value(right)));
        return;
    }

    throw VmRuntimeError("VM: " + messages::operandes_attendues(
        "la multiplication", "deux valeurs numériques", left.type_name(), right.type_name()));
}

void execute_divide(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        if (right.as_entier() == 0)
        {
            throw VmRuntimeError("VM: " + messages::division_par_zero());
        }
        stack.push_back(checked_integer(numeric::divide(left.as_entier(), right.as_entier())));
        return;
    }

    if (left.is_numeric() && right.is_numeric())
    {
        const double divisor = numeric_value(right);
        if (divisor == 0.0)
        {
            throw VmRuntimeError("VM: " + messages::division_par_zero());
        }
        stack.push_back(Value::decimal(numeric_value(left) / divisor));
        return;
    }

    throw VmRuntimeError("VM: " + messages::operandes_attendues(
        "la division", "deux valeurs numériques", left.type_name(), right.type_name()));
}

void execute_modulo(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);
    if (!left.is_entier() || !right.is_entier())
    {
        throw VmRuntimeError("VM: " + messages::operandes_attendues(
            "le modulo", "deux valeurs de type Entier", left.type_name(), right.type_name()));
    }
    if (right.as_entier() == 0)
    {
        throw VmRuntimeError("VM: " + messages::modulo_par_zero());
    }
    stack.push_back(checked_integer(numeric::remainder(left.as_entier(), right.as_entier())));
}

void execute_negate(std::vector<Value> &stack)
{
    const Value operand = pop_value(stack);

    if (operand.is_entier())
    {
        stack.push_back(checked_integer(numeric::negate(operand.as_entier())));
        return;
    }

    if (operand.is_decimal())
    {
        stack.push_back(Value::decimal(-operand.as_decimal()));
        return;
    }

    throw VmRuntimeError("VM: négation attend une valeur numérique");
}

void execute_not(std::vector<Value> &stack)
{
    const Value operand = pop_value(stack);
    stack.push_back(Value::logique(!is_truthy(operand)));
}

void execute_equal(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);
    stack.push_back(Value::logique(values_equal(left, right)));
}

void execute_not_equal(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);
    stack.push_back(Value::logique(!values_equal(left, right)));
}

template <typename Predicate>
void execute_numeric_compare(std::vector<Value> &stack, Predicate predicate, const char *operation)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        stack.push_back(Value::logique(predicate(left.as_entier(), right.as_entier())));
        return;
    }

    if (left.is_numeric() && right.is_numeric())
    {
        stack.push_back(Value::logique(predicate(numeric_value(left), numeric_value(right))));
        return;
    }

    throw VmRuntimeError("VM: " + messages::operandes_attendues(operation, "deux valeurs numériques",
                                                                left.type_name(), right.type_name()));
}

void execute_list(std::vector<Value> &stack, const std::size_t length)
{
    if (stack.size() < length)
    {
        throw VmRuntimeError("VM: pile insuffisante pour construire une liste");
    }

    auto data = make_ref<ListeData>();
    data->elements.reserve(length);

    const std::size_t start = stack.size() - length;
    for (std::size_t i = start; i < stack.size(); ++i)
    {
        data->elements.push_back(std::move(stack[i]));
    }

    stack.resize(start);
    stack.push_back(Value::liste(std::move(data)));
}

void execute_dictionary(std::vector<Value> &stack, const std::size_t entry_count)
{
    const std::size_t value_count = entry_count * 2;
    if (stack.size() < value_count)
    {
        throw VmRuntimeError("VM: pile insuffisante pour construire un dictionnaire");
    }

    auto data = make_ref<DictData>();
    data->reserve(entry_count);

    const std::size_t start = stack.size() - value_count;
    for (std::size_t i = start; i < stack.size(); i += 2)
    {
        require_dictionary_key(stack[i]);
        data->set(std::move(stack[i]), std::move(stack[i + 1]));
    }

    stack.resize(start);
    stack.push_back(Value::dictionnaire(std::move(data)));
}

void execute_ensemble(std::vector<Value> &stack, const std::size_t element_count)
{
    if (stack.size() < element_count)
    {
        throw VmRuntimeError("VM: pile insuffisante pour construire un ensemble");
    }

    auto data = make_ref<EnsembleData>();
    data->reserve(element_count);

    const std::size_t start = stack.size() - element_count;
    for (std::size_t i = start; i < stack.size(); ++i)
    {
        require_dictionary_key(stack[i]);
        data->insert(std::move(stack[i]));
    }

    stack.resize(start);
    stack.push_back(Value::ensemble(std::move(data)));
}

void execute_iteration_snapshot(std::vector<Value> &stack)
{
    const Value iterable = pop_value(stack);
    auto snapshot = make_ref<ListeData>();
    // Match the tree walker: membership is fixed before the first iteration.
    if (iterable.is_liste())
        snapshot->elements = iterable.as_liste()->elements;
    else if (iterable.is_liste_fixe())
        snapshot->elements = iterable.as_liste_fixe()->elements;
    else if (iterable.is_ensemble())
        snapshot->elements = iterable.as_ensemble()->items();
    else if (iterable.is_dictionnaire())
    {
        // Walking a dictionary walks its keys, matching the tree walker.
        snapshot->elements.reserve(iterable.as_dictionnaire()->size());
        for (const auto &entry : iterable.as_dictionnaire()->items())
            snapshot->elements.push_back(entry.first);
    }
    else if (iterable.is_texte())
    {
        const auto &text = iterable.as_texte();
        std::size_t offset = 0;
        while (offset < text.size())
        {
            char32_t character = 0;
            const auto next = utf8::decode_one(text, offset, character);
            if (!next)
                throw VmRuntimeError("VM: texte UTF-8 invalide");
            snapshot->elements.push_back(Value::symbole(character));
            offset = *next;
        }
    }
    else
        throw VmRuntimeError("VM: " + messages::valeur_non_iterable(iterable.type_name()));
    stack.push_back(Value::liste(std::move(snapshot)));
}

void execute_sequence_length(std::vector<Value> &stack)
{
    const Value sequence = pop_value(stack);
    if (sequence.is_liste())
    {
        stack.push_back(Value::entier(static_cast<std::int64_t>(sequence.as_liste()->elements.size())));
        return;
    }
    if (sequence.is_liste_fixe())
    {
        stack.push_back(Value::entier(static_cast<std::int64_t>(sequence.as_liste_fixe()->elements.size())));
        return;
    }
    if (sequence.is_texte())
    {
        const auto count = utf8::character_count(sequence.as_texte());
        if (!count.has_value())
        {
            throw VmRuntimeError("VM: texte UTF-8 invalide");
        }
        stack.push_back(Value::entier(static_cast<std::int64_t>(*count)));
        return;
    }

    throw VmRuntimeError("VM: longueur demandee sur une valeur non itérable");
}

void execute_index_get(std::vector<Value> &stack)
{
    const Value index = pop_value(stack);
    const Value sequence = pop_value(stack);
    if (sequence.is_dictionnaire())
    {
        if (const DictEntry *entry = sequence.as_dictionnaire()->find(index))
        {
            stack.push_back(entry->second);
            return;
        }
        throw VmRuntimeError("VM: " + messages::cle_introuvable());
    }

    if (!index.is_entier())
    {
        throw VmRuntimeError("VM: " + messages::indice_non_entier("une séquence"));
    }

    const int64_t raw_index = index.as_entier();
    const std::size_t offset = raw_index < 0 ? 0 : static_cast<std::size_t>(raw_index);
    const auto out_of_range = [&](const std::size_t length) {
        return VmRuntimeError("VM: " + messages::indice_hors_limites(raw_index, length, sequence.type_name()));
    };

    if (sequence.is_liste())
    {
        if (raw_index < 0 || offset >= sequence.as_liste()->elements.size())
        {
            throw out_of_range(sequence.as_liste()->elements.size());
        }
        stack.push_back(sequence.as_liste()->elements[offset]);
        return;
    }
    if (sequence.is_liste_fixe())
    {
        if (raw_index < 0 || offset >= sequence.as_liste_fixe()->elements.size())
        {
            throw out_of_range(sequence.as_liste_fixe()->elements.size());
        }
        stack.push_back(sequence.as_liste_fixe()->elements[offset]);
        return;
    }
    if (sequence.is_texte())
    {
        const auto character = raw_index < 0 ? std::nullopt : utf8::character_at(sequence.as_texte(), offset);
        if (!character.has_value())
        {
            throw out_of_range(utf8::character_count(sequence.as_texte()).value_or(0));
        }
        stack.push_back(Value::symbole(*character));
        return;
    }

    throw VmRuntimeError("VM: " + messages::acces_indice_impossible(sequence.type_name()));
}

void execute_index_set(std::vector<Value> &stack, VmRuntimeServices &runtime)
{
    const Value value = pop_value(stack);
    const Value index = pop_value(stack);
    const Value object = pop_value(stack);

    if (object.is_liste())
    {
        if (!index.is_entier())
        {
            throw VmRuntimeError("VM: l'index de liste doit être un Entier");
        }
        const std::int64_t raw_index = index.as_entier();
        if (raw_index < 0 || static_cast<std::size_t>(raw_index) >= object.as_liste()->elements.size())
        {
            throw VmRuntimeError("VM: index de liste hors limites");
        }
        runtime.enforce_list_element(object.as_liste(), value, "Liste.ajouter");
        object.as_liste()->elements[static_cast<std::size_t>(raw_index)] = value;
        stack.push_back(value);
        return;
    }

    if (object.is_liste_fixe())
    {
        throw VmRuntimeError("VM: " + messages::liste_fixe_immuable());
    }

    if (object.is_dictionnaire())
    {
        auto dictionary = object.as_dictionnaire();
        runtime.enforce_dictionary_entry(dictionary, index, value, "l'entrée du dictionnaire");
        require_dictionary_key(index);
        dictionary->set(index, value);
        stack.push_back(value);
        return;
    }

    throw VmRuntimeError("VM: " + messages::affectation_indice_impossible(object.type_name()));
}

// These take a view rather than a string because every caller hands them a
// literal or a name the module already owns. Binding a literal to a
// `const std::string &` builds a string, and `valeurs.ajouter(index)` did that
// twice per call on a path whose whole job is to push one value.
Value execute_member_call(const Value &receiver,
                          const std::string &member,
                          const std::vector<RuntimeArgument> &runtime_args,
                          VmRuntimeServices &runtime,
                          const RuntimeSite &site)
{
    if (receiver.is_texte())
    {
        return execute_texte_member(runtime, receiver, member, runtime_args, site);
    }
    if (std::optional<Value> result = call_builtin_member(runtime, receiver, member, runtime_args, site))
    {
        return std::move(*result);
    }
    throw VmRuntimeError("VM: " + messages::membre_introuvable(member, receiver.type_name()));
}

Value VmRuntimeServices::call(Value callee, const NativeArgs &args)
{
    if (!callee.is_fonction())
    {
        throw VmRuntimeError("VM: " + messages::valeur_non_appelable(callee.type_name()));
    }
    if (callee.as_fonction_ptr()->is_native())
    {
        return callee.as_fonction_ptr()->native_handler(*this, args);
    }
    if (!m_callback_executor)
    {
        throw VmRuntimeError("VM: contexte d'appel bytecode indisponible");
    }
    return m_callback_executor(std::move(callee), args);
}

Value make_bound_member(Value receiver,
                        std::string member,
                        VmRuntimeServices &runtime)
{
    auto function = make_ref<LumiereFunction>();
    function->name = member;
    function->receiver = std::move(receiver);
    function->min_arity = 0;
    function->max_arity = std::numeric_limits<std::size_t>::max();
    function->native_handler = [member = std::move(member), &runtime](IRuntime &,
                                                                      const NativeArgs &native_args) -> Value {
        if (native_args.receiver == nullptr || native_args.arguments == nullptr)
        {
            runtime.raise_runtime_error(native_args.site, "contexte d'appel membre VM invalide");
        }
        return execute_member_call(*native_args.receiver,
                                   member,
                                   *native_args.arguments,
                                   runtime,
                                   native_args.site);
    };
    return Value::fonction(std::move(function));
}

struct LocalSlot
{
    Value value = Value::rien();
    CellRef cell;

    Value &get() { return cell ? cell->value : value; }

    CellRef capture()
    {
        // Only escaping locals need stable, shared storage. All readers and
        // writers use the same cell after its first capture.
        if (!cell)
            cell = make_ref<CellData>(std::move(value));
        return cell;
    }
};

struct CallFrame
{
    const FunctionBytecode *function = nullptr;
    std::size_t ip = 0;
    std::size_t stack_base = 0;
    std::vector<LocalSlot> locals;
    std::vector<CellRef> captures;
    SourceLocation call_site {};

    CellRef capture(bool from_capture, std::size_t index)
    {
        if (index >= (from_capture ? captures.size() : locals.size()))
            throw VmRuntimeError("VM: source de capture invalide");
        return from_capture ? captures[index] : locals[index].capture();
    }
};

/**
 * @brief Active call frames plus storage retained for the next call at a depth.
 *
 * A push may grow m_frames and invalidate every CallFrame reference. The
 * dispatch loop therefore pushes only as its final action before `break`; it
 * never reads its `frame` reference after a push.
 */
class FrameStack
{
public:
    CallFrame &push()
    {
        if (m_depth == m_frames.size())
        {
            m_frames.emplace_back();
        }
        ++m_depth;
        m_top = m_frames.data() + (m_depth - 1);
        return *m_top;
    }

    void pop()
    {
        assert(m_depth != 0);
        CallFrame &frame = m_frames[--m_depth];
        frame.locals.clear();
        frame.captures.clear();
        frame.function = nullptr;
        m_top = m_depth != 0 ? m_frames.data() + (m_depth - 1) : nullptr;
    }

    // The dispatch loop asks for this once per *instruction*, not once per
    // call, so it is kept as a pointer rather than computed from the depth: a
    // frame is 88 bytes, so indexing needs a multiply, and paying for one on
    // every instruction was worth 4.5% of the integer loop.
    CallFrame &back()
    {
        assert(m_top != nullptr);
        return *m_top;
    }

    const CallFrame &back() const
    {
        assert(m_top != nullptr);
        return *m_top;
    }

    [[nodiscard]] bool empty() const { return m_depth == 0; }
    [[nodiscard]] std::size_t size() const { return m_depth; }

    const CallFrame &operator[](const std::size_t index) const
    {
        assert(index < m_depth);
        return m_frames[index];
    }

private:
    std::vector<CallFrame> m_frames;
    std::size_t m_depth = 0;
    CallFrame *m_top = nullptr;
};

/**
 * @brief A field, with what its declared type demands of a value.
 *
 * The shape is settled when the field is first resolved, for the same reason
 * annotations carry one: assigning to `total: Entier` used to re-read the word
 * "Entier" every time.
 */
struct ResolvedField
{
    const VmFieldDescriptor *descriptor = nullptr;
    TypeShape shape = TypeShape::Composite;
};

/**
 * @brief Whether this frame is a method running on that very object.
 *
 * What "privé" means: the member is reachable from the object's own methods and
 * nowhere else. A frame qualifies when it is a method -- the compiler names a
 * method `Classe.methode`, which is the only name with a dot in it -- and its
 * receiver, which always occupies slot zero, is the same object.
 */
bool accesses_own_object(CallFrame &frame, const Value &receiver)
{
    return !frame.locals.empty() &&
           frame.function->name.find('.') != std::string::npos &&
           frame.locals[0].get().is_objet() &&
           frame.locals[0].get().as_objet_ptr() == receiver.as_objet_ptr();
}

struct VmClosureBody final : RuntimeFunctionBody
{
    std::size_t function_index = 0;
    std::vector<CellRef> captures;

    VmClosureBody() { origin = BodyOrigin::Vm; }

    void trace_references(RefVisitor &visitor) const override
    {
        for (const CellRef &cell : captures)
        {
            if (cell)
            {
                visitor.visit(cell.get());
            }
        }
    }

    void clear_references() override { captures.clear(); }
};

struct VmClassBody final : RuntimeClassBody
{
    std::size_t descriptor_index = 0;
    std::unordered_map<std::size_t, std::vector<CellRef>> method_captures;

    // What a member index resolves to in this class and its ancestors, worked
    // out the first time it is asked. Resolving walks the chain comparing
    // names, and that happened on every field access and every method call --
    // six million string walks for two million calls. The answer cannot change:
    // a class's parent is fixed when the class is made.
    mutable std::vector<std::optional<ResolvedField>> fields_by_member;
    mutable std::vector<std::optional<const VmMethodDescriptor *>> methods_by_member;

    VmClassBody() { origin = BodyOrigin::Vm; }

    void trace_references(RefVisitor &visitor) const override
    {
        for (const auto &[index, cells] : method_captures)
        {
            for (const CellRef &cell : cells)
            {
                if (cell)
                {
                    visitor.visit(cell.get());
                }
            }
        }
    }

    void clear_references() override { method_captures.clear(); }
};

struct VmInterfaceBody final : RuntimeInterfaceBody
{
    std::size_t descriptor_index = 0;

    VmInterfaceBody() { origin = BodyOrigin::Vm; }

    void trace_references(RefVisitor &) const override {}
    void clear_references() override {}
};

// A body the VM made, or nullptr. The tag says which engine made it, and each
// engine defines exactly one body of each kind, so the tag identifies the type.
// The assert is what keeps that claim honest: it runs in the Debug and
// sanitizer builds, where the whole suite runs.
const VmClassBody *vm_class_body(const RuntimeClassBody *body)
{
    if (body == nullptr || body->origin != BodyOrigin::Vm)
    {
        return nullptr;
    }
    assert(dynamic_cast<const VmClassBody *>(body) != nullptr);
    return static_cast<const VmClassBody *>(body);
}

const VmClosureBody *vm_closure_body(const RuntimeFunctionBody *body)
{
    if (body == nullptr || body->origin != BodyOrigin::Vm)
    {
        return nullptr;
    }
    assert(dynamic_cast<const VmClosureBody *>(body) != nullptr);
    return static_cast<const VmClosureBody *>(body);
}

// These walk a class and its ancestors to answer a question. They take a raw
// pointer and step with `klass->parent.get()`, because taking a Ref for each
// link would be an increment and a decrement per ancestor per lookup, and a
// lookup happens on every member access. The chain is owned by the value that
// asked, and cannot go away while the answer is being computed.
const VmClassDescriptor *class_descriptor(const ModuleBytecode &module,
                                          const LumiereClass *klass)
{
    if (klass == nullptr)
    {
        return nullptr;
    }
    const VmClassBody *body = vm_class_body(klass->body.get());
    if (body == nullptr || body->descriptor_index >= module.classes.size())
    {
        return nullptr;
    }
    return &module.classes[body->descriptor_index];
}

const VmMethodDescriptor *find_vm_method(const ModuleBytecode &module,
                                         const LumiereClass *klass,
                                         const std::string &name)
{
    while (klass != nullptr)
    {
        const VmClassDescriptor *descriptor = class_descriptor(module, klass);
        if (descriptor == nullptr)
        {
            return nullptr;
        }
        for (const VmMethodDescriptor &method : descriptor->methods)
        {
            if (method.name == name)
            {
                return &method;
            }
        }
        klass = klass->parent.get();
    }
    return nullptr;
}

const VmFieldDescriptor *find_vm_field(const ModuleBytecode &module,
                                       const LumiereClass *klass,
                                       const std::string &name)
{
    while (klass != nullptr)
    {
        const VmClassDescriptor *descriptor = class_descriptor(module, klass);
        if (descriptor == nullptr)
        {
            return nullptr;
        }
        for (const VmFieldDescriptor &field : descriptor->fields)
        {
            if (field.name == name)
            {
                return &field;
            }
        }
        klass = klass->parent.get();
    }
    return nullptr;
}

// Returns the captures in place rather than a copy: most methods capture
// nothing, and the caller copies only when it is building a frame.
const std::vector<CellRef> *find_vm_method_captures(const LumiereClass *klass,
                                                    const std::size_t function_index)
{
    while (klass != nullptr)
    {
        const VmClassBody *body = vm_class_body(klass->body.get());
        if (body != nullptr)
        {
            const auto captures = body->method_captures.find(function_index);
            if (captures != body->method_captures.end())
            {
                return &captures->second;
            }
        }
        klass = klass->parent.get();
    }
    return nullptr;
}

// The cached forms of the two walks above. A class with no VM body -- one the
// tree walker made, reached through a shared value -- falls back to the walk.
ResolvedField resolve_field(const ModuleBytecode &module,
                            const LumiereClass *klass,
                            const std::size_t member_index)
{
    const auto look_up = [](const ModuleBytecode &in_module,
                            const LumiereClass *in_klass,
                            const std::string &name) {
        const VmFieldDescriptor *descriptor = find_vm_field(in_module, in_klass, name);
        return ResolvedField{descriptor,
                             descriptor != nullptr ? classify_type_name(descriptor->type)
                                                   : TypeShape::Composite};
    };
    const VmClassBody *body = klass != nullptr ? vm_class_body(klass->body.get()) : nullptr;
    if (body == nullptr)
    {
        return look_up(module, klass, module.members[member_index]);
    }
    auto &cache = body->fields_by_member;
    if (cache.size() != module.members.size())
    {
        cache.assign(module.members.size(), std::nullopt);
    }
    std::optional<ResolvedField> &slot = cache[member_index];
    if (!slot.has_value())
    {
        slot = look_up(module, klass, module.members[member_index]);
    }
    return *slot;
}

const VmMethodDescriptor *resolve_method(const ModuleBytecode &module,
                                         const LumiereClass *klass,
                                         const std::size_t member_index)
{
    const VmClassBody *body = klass != nullptr ? vm_class_body(klass->body.get()) : nullptr;
    if (body == nullptr)
    {
        return find_vm_method(module, klass, module.members[member_index]);
    }
    auto &cache = body->methods_by_member;
    if (cache.size() != module.members.size())
    {
        cache.assign(module.members.size(), std::nullopt);
    }
    std::optional<const VmMethodDescriptor *> &slot = cache[member_index];
    if (!slot.has_value())
    {
        slot = find_vm_method(module, klass, module.members[member_index]);
    }
    return *slot;
}

std::vector<CellRef> vm_method_captures(const LumiereClass *klass,
                                        const std::size_t function_index)
{
    const std::vector<CellRef> *captures = find_vm_method_captures(klass, function_index);
    return captures != nullptr ? *captures : std::vector<CellRef>{};
}

void collect_vm_fields(const ModuleBytecode &module,
                       const Ref<LumiereClass> &klass,
                       std::vector<const VmFieldDescriptor *> &fields)
{
    if (klass == nullptr)
    {
        return;
    }
    collect_vm_fields(module, klass->parent, fields);
    const VmClassDescriptor *descriptor = class_descriptor(module, klass.get());
    if (descriptor == nullptr)
    {
        return;
    }
    for (const VmFieldDescriptor &field : descriptor->fields)
    {
        fields.push_back(&field);
    }
}

Value instantiate_vm_class(const ModuleBytecode &module,
                           const Ref<LumiereClass> &klass,
                           const std::vector<RuntimeArgument> &arguments)
{
    std::vector<const VmFieldDescriptor *> fields;
    collect_vm_fields(module, klass, fields);
    if (arguments.size() > fields.size())
    {
        throw VmRuntimeError("VM: trop d'arguments pour construire '" + klass->name + "'");
    }
    auto object = make_ref<LumiereObject>();
    object->klass = klass;
    std::unordered_map<std::string, Value> assigned;
    std::size_t positional = 0;
    for (const RuntimeArgument &argument : arguments)
    {
        std::string name = argument.name;
        if (name.empty())
        {
            while (positional < fields.size() && assigned.contains(fields[positional]->name))
            {
                ++positional;
            }
            if (positional >= fields.size())
            {
                throw VmRuntimeError("VM: trop d'arguments pour construire '" + klass->name + "'");
            }
            name = fields[positional++]->name;
        }
        if (assigned.contains(name))
        {
            throw VmRuntimeError("VM: champ fourni plusieurs fois: " + name);
        }
        const auto field = std::find_if(fields.begin(), fields.end(), [&](const VmFieldDescriptor *candidate) {
            return candidate->name == name;
        });
        if (field == fields.end())
        {
            throw VmRuntimeError("VM: champ constructeur inconnu: " + name);
        }
        assigned.emplace(name, argument.value);
    }
    for (const VmFieldDescriptor *field : fields)
    {
        const auto value_it = assigned.find(field->name);
        const Value value = value_it == assigned.end() ? Value::rien() : value_it->second;
        if (!field->type.empty() && !matches_type_name(value, field->type))
        {
            throw VmRuntimeError("VM: le champ '" + field->name + "' attend " + display_runtime_type(field->type));
        }
        object->fields[field->name] = value;
    }
    return Value::objet(std::move(object));
}

CallFrame &push_empty_call_frame(FrameStack &frames,
                                 const ModuleBytecode &module,
                                 const std::size_t function_index,
                                 const std::size_t stack_base,
                                 std::vector<CellRef> captures,
                                 const SourceLocation call_site)
{
    if (function_index >= module.functions.size())
    {
        throw VmRuntimeError("VM: index de fonction invalide");
    }

    const FunctionBytecode &function = module.functions[function_index];
    if (captures.size() != function.capture_count)
    {
        throw VmRuntimeError("VM: nombre de captures invalide pour '" + function.name + "'");
    }

    CallFrame &frame = frames.push();
    frame.function = &function;
    frame.ip = 0;
    frame.stack_base = stack_base;
    frame.locals.resize(function.local_slot_count);
    frame.captures.assign(std::make_move_iterator(captures.begin()),
                          std::make_move_iterator(captures.end()));
    frame.call_site = call_site;
    return frame;
}

void push_call_frame(FrameStack &frames,
                     const ModuleBytecode &module,
                     const std::size_t function_index,
                     std::vector<Value> args,
                     const std::size_t stack_base,
                     std::vector<CellRef> captures = {},
                     const SourceLocation call_site = {})
{
    if (function_index >= module.functions.size())
    {
        throw VmRuntimeError("VM: index de fonction invalide");
    }
    const FunctionBytecode &function = module.functions[function_index];
    if (args.size() != function.arity)
    {
        throw VmRuntimeError("VM: arite invalide pour '" + function.name + "'");
    }

    CallFrame &frame = push_empty_call_frame(frames,
                                             module,
                                             function_index,
                                             stack_base,
                                             std::move(captures),
                                             call_site);
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        frame.locals[i].get() = std::move(args[i]);
    }
}

void push_call_frame(FrameStack &frames,
                     const ModuleBytecode &module,
                     const std::size_t function_index,
                     std::vector<RuntimeArgument> &args,
                     const std::size_t stack_base,
                     std::vector<CellRef> captures = {},
                     const SourceLocation call_site = {})
{
    if (function_index >= module.functions.size())
    {
        throw VmRuntimeError("VM: index de fonction invalide");
    }
    const FunctionBytecode &function = module.functions[function_index];
    if (args.size() != function.arity)
    {
        throw VmRuntimeError("VM: arite invalide pour '" + function.name + "'");
    }

    CallFrame &frame = push_empty_call_frame(frames,
                                             module,
                                             function_index,
                                             stack_base,
                                             std::move(captures),
                                             call_site);
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        frame.locals[i].get() = std::move(args[i].value);
    }
}

// A parameter is named in diagnostics; bytecode that arrived without the name
// table still has to say something, so fall back to its position.
std::string parameter_label(const FunctionBytecode &function, const std::size_t index)
{
    return index < function.parameter_names.size() ? function.parameter_names[index]
                                                   : std::to_string(index + 1);
}

// No argument fills this parameter. Any value at or past the argument count
// says the same thing, which lets one bounds test serve both binding rules.
constexpr std::size_t no_argument = static_cast<std::size_t>(-1);

/**
 * @brief Resolves a call that names at least one of its arguments.
 *
 * Returns, for each parameter, the index of the argument that fills it, or
 * `no_argument`. The rule is the tree walker's: a named argument goes to the
 * parameter it names, a positional one to the next parameter still unbound.
 */
std::vector<std::size_t> bind_named_arguments(const FunctionBytecode &function,
                                              const std::vector<RuntimeArgument> &args)
{
    const std::size_t count = function.source_arity;
    std::vector<std::size_t> sources(count, no_argument);
    std::size_t next_positional = 0;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
        std::size_t target = count;
        if (!args[i].name.empty())
        {
            for (std::size_t parameter = 0; parameter < count; ++parameter)
            {
                if (parameter_label(function, parameter) == args[i].name)
                {
                    target = parameter;
                    break;
                }
            }
            if (target == count)
            {
                throw VmRuntimeError("aucun paramètre nommé '" + args[i].name + "'", args[i].site);
            }
        }
        else
        {
            while (next_positional < count && sources[next_positional] != no_argument)
            {
                ++next_positional;
            }
            if (next_positional == count)
            {
                throw VmRuntimeError("trop d'arguments fournis a l'appel de fonction", args[i].site);
            }
            target = next_positional++;
        }

        if (sources[target] != no_argument)
        {
            throw VmRuntimeError("le paramètre '" + parameter_label(function, target) +
                                     "' est fourni plusieurs fois",
                                 args[i].site);
        }
        sources[target] = i;
    }
    return sources;
}

/**
 * @brief Lays a call's arguments out the way the callee's frame expects them.
 *
 * The VM used to bind purely by position, so `f(b: 1, a: 10)` computed `1 - 10`
 * whenever the callee was not known at compile time -- a call through a
 * variable, or any method call. The names travel in the bytecode; nothing read
 * them. The analyzer accepts such a program, so the wrong answer was silent.
 *
 * Writes the layout the compiler's prologue expects directly into a new call
 * frame: an optional receiver, the `source_arity` values, then one Logique per
 * optional parameter saying whether the caller supplied it.
 */
void normalize_closure_arguments(FrameStack &frames,
                                 const ModuleBytecode &module,
                                 const std::size_t function_index,
                                 std::vector<RuntimeArgument> &args,
                                 const std::size_t stack_base,
                                 std::vector<CellRef> captures,
                                 const SourceLocation call_site,
                                 const Value *receiver = nullptr)
{
    if (function_index >= module.functions.size())
    {
        throw VmRuntimeError("VM: index de fonction invalide");
    }
    const FunctionBytecode &function = module.functions[function_index];
    const std::size_t count = function.source_arity;
    const bool named = std::any_of(args.begin(), args.end(),
                                   [](const RuntimeArgument &arg) { return !arg.name.empty(); });
    if (!named && args.size() > count)
    {
        throw VmRuntimeError("trop d'arguments fournis a l'appel de fonction", args[count].site);
    }

    // Positional binding is the identity -- argument i fills parameter i -- and
    // the compiler strips the names from every call whose callee it knows, so
    // the mapping is materialised only when a call really names an argument.
    // Method calls run through here on every iteration of a hot loop; an
    // allocation none of them needs is one they would all pay for.
    const std::vector<std::size_t> sources =
        named ? bind_named_arguments(function, args) : std::vector<std::size_t>{};
    const auto source_for = [&](const std::size_t parameter) {
        return named ? sources[parameter] : parameter;
    };

    for (std::size_t i = 0; i < count; ++i)
    {
        const bool supplied = source_for(i) < args.size();
        const bool optional = i < function.optional_params.size() && function.optional_params[i];
        if (!supplied && !optional)
        {
            throw VmRuntimeError("argument manquant pour le paramètre '" + parameter_label(function, i) + "'");
        }
    }

    const std::size_t receiver_slots = receiver == nullptr ? 0 : 1;
    if (receiver_slots + count +
            static_cast<std::size_t>(std::count(function.optional_params.begin(),
                                                function.optional_params.end(),
                                                true)) !=
        function.arity)
    {
        throw VmRuntimeError("VM: arite invalide pour '" + function.name + "'");
    }

    CallFrame &frame = push_empty_call_frame(frames,
                                             module,
                                             function_index,
                                             stack_base,
                                             std::move(captures),
                                             call_site);
    if (receiver != nullptr)
    {
        frame.locals[0].get() = *receiver;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t source = source_for(i);
        frame.locals[receiver_slots + i].get() =
            source < args.size() ? std::move(args[source].value) : Value::rien();
    }

    // The prologue evaluates a default only where the caller supplied nothing,
    // so the flag follows the binding rather than the argument count.
    std::size_t flag = receiver_slots + count;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (i < function.optional_params.size() && function.optional_params[i])
        {
            frame.locals[flag++].get() = Value::logique(source_for(i) < args.size());
        }
    }
}

struct VmExecutionState
{
    const ModuleBytecode &module;
    const std::unordered_map<std::string, NativeFunction> &natives;
    const std::unordered_map<std::string, std::size_t> &function_indices;
    std::vector<Value> &globals;
    std::vector<bool> &global_defined;
    std::vector<bool> &initialized_functions;
    VmRuntimeServices runtime_services;
    // One entry per module type, in the same order: what that type demands of
    // a value, worked out once instead of parsed at every assertion.
    std::vector<TypeShape> type_shapes;
};

Value execute_frames(VmExecutionState &execution, FrameStack frames)
{
    const ModuleBytecode &module = execution.module;
    const auto &natives = execution.natives;
    const auto &function_indices = execution.function_indices;
    auto &globals = execution.globals;
    auto &global_defined = execution.global_defined;
    auto &initialized_functions = execution.initialized_functions;
    std::vector<Value> stack;
    auto &runtime_services = execution.runtime_services;

    const auto build_runtime_error = [&frames](const VmRuntimeError &error,
                                               const std::size_t opcode_offset)
    {
        std::string message = error.what();
        if (message.starts_with("VM: "))
        {
            message.erase(0, 4);
        }
        const CallFrame &current = frames.back();
        SourceLocation location {};
        if (opcode_offset < current.function->chunk.locations.size())
        {
            location = current.function->chunk.locations[opcode_offset];
        }
        std::string source_path = current.function->source_path;
        if (error.site().has_value())
        {
            if (!error.site()->source_path.empty())
            {
                source_path = error.site()->source_path;
            }
            location.line = static_cast<std::size_t>(error.site()->line);
            location.column = static_cast<std::size_t>(error.site()->column);
        }
        std::vector<StackFrame> trace;
        trace.reserve(frames.size());
        for (std::size_t index = 0; index < frames.size(); ++index)
        {
            const CallFrame &frame = frames[index];
            // Top-level code is a real frame to the VM and no frame at all to
            // the tree walker. Showing it would make the same failure read
            // differently depending on which engine ran it.
            if (frame.function->is_module_initializer)
            {
                continue;
            }
            trace.push_back({frame.function->name,
                             frame.function->source_path,
                             static_cast<std::uint32_t>(frame.call_site.line),
                             static_cast<std::uint32_t>(frame.call_site.column)});
        }
        return RuntimeError(std::move(message),
                            std::move(source_path),
                            current.function->source_text,
                            static_cast<std::uint32_t>(location.line),
                            static_cast<std::uint32_t>(location.column),
                            std::move(trace));
    };

    while (!frames.empty())
    {
        std::size_t opcode_offset = 0;
        try
        {
        CallFrame &frame = frames.back();
        const Chunk &chunk = frame.function->chunk;
        std::size_t &ip = frame.ip;
        auto &locals = frame.locals;
        if (ip >= chunk.code.size())
        {
            throw VmRuntimeError("VM: fonction terminée sans RETURN");
        }

        opcode_offset = ip;
        const Opcode opcode = read_opcode(chunk, ip);
        // Only the call opcodes below need this, and looking it up for every
        // instruction cost a bounds check and a copy on the hottest path there is.
        const auto dispatch_location = [&chunk, &opcode_offset]() {
            return opcode_offset < chunk.locations.size() ? chunk.locations[opcode_offset]
                                                          : SourceLocation{};
        };
        switch (opcode)
        {
        case Opcode::CONSTANT:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= chunk.constants.size())
            {
                throw VmRuntimeError("VM: index de constante invalide");
            }
            stack.push_back(chunk.constants[index]);
            break;
        }
        case Opcode::CONSTANT_LONG:
        {
            const std::size_t index = read_u24(chunk, ip);
            if (index >= chunk.constants.size())
            {
                throw VmRuntimeError("VM: index de constante invalide");
            }
            stack.push_back(chunk.constants[index]);
            break;
        }
        case Opcode::NIL:
            stack.push_back(Value::rien());
            break;
        case Opcode::TRUE_VALUE:
            stack.push_back(Value::logique(true));
            break;
        case Opcode::FALSE_VALUE:
            stack.push_back(Value::logique(false));
            break;
        case Opcode::GET_GLOBAL:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= globals.size())
            {
                throw VmRuntimeError("VM: index global invalide");
            }
            if (!global_defined[index])
            {
                throw VmRuntimeError("VM: " + messages::symbole_introuvable(module.globals[index]));
            }
            stack.push_back(globals[index]);
            break;
        }
        case Opcode::GET_GLOBAL_LONG:
        {
            const std::size_t index = read_u24(chunk, ip);
            if (index >= globals.size())
            {
                throw VmRuntimeError("VM: index global invalide");
            }
            if (!global_defined[index])
            {
                throw VmRuntimeError("VM: " + messages::symbole_introuvable(module.globals[index]));
            }
            stack.push_back(globals[index]);
            break;
        }
        case Opcode::SET_GLOBAL:
        case Opcode::SET_GLOBAL_LONG:
        {
            const std::size_t index = opcode == Opcode::SET_GLOBAL_LONG
                                          ? read_u24(chunk, ip)
                                          : read_byte(chunk, ip);
            if (index >= globals.size())
            {
                throw VmRuntimeError("VM: index global invalide");
            }
            globals[index] = pop_value(stack);
            global_defined[index] = true;
            break;
        }
        case Opcode::INIT_GLOBAL:
        case Opcode::INIT_GLOBAL_LONG:
        {
            const std::size_t index = opcode == Opcode::INIT_GLOBAL_LONG
                                          ? read_u24(chunk, ip)
                                          : read_byte(chunk, ip);
            if (index >= globals.size() || !global_defined[index] || !globals[index].is_fonction())
            {
                throw VmRuntimeError("VM: initialiseur de module invalide");
            }
            const auto body = dynamic_ref_cast<VmClosureBody>(globals[index].as_fonction()->body);
            if (body == nullptr || body->function_index >= initialized_functions.size())
            {
                throw VmRuntimeError("VM: corps d'initialiseur de module invalide");
            }
            if (initialized_functions[body->function_index])
            {
                break;
            }
            initialized_functions[body->function_index] = true;
            push_call_frame(frames,
                            module,
                            body->function_index,
                            {},
                            stack.size(),
                            {},
                            dispatch_location());
            break;
        }
        case Opcode::GET_LOCAL:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= locals.size())
            {
                throw VmRuntimeError("VM: index local invalide");
            }
            stack.push_back(locals[index].get());
            break;
        }
        case Opcode::SET_LOCAL:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= locals.size())
            {
                throw VmRuntimeError("VM: index local invalide");
            }
            locals[index].get() = pop_value(stack);
            break;
        }
        case Opcode::CLEAR_LOCALS:
        {
            // A loop body's locals are new bindings on every iteration. Dropping
            // the slots here is what makes that true for a closure as well: the
            // cell a closure captured on the previous iteration stays with that
            // closure, and the next capture of this slot allocates a fresh one.
            const std::uint8_t first = read_byte(chunk, ip);
            const std::uint8_t count = read_byte(chunk, ip);
            const std::size_t end = std::min<std::size_t>(first + count, locals.size());
            for (std::size_t slot = first; slot < end; ++slot)
            {
                locals[slot] = LocalSlot{};
            }
            break;
        }
        case Opcode::GET_CAPTURE:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= frame.captures.size())
            {
                throw VmRuntimeError("VM: index de capture invalide");
            }
            stack.push_back(frame.captures[index]->value);
            break;
        }
        case Opcode::SET_CAPTURE:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= frame.captures.size())
            {
                throw VmRuntimeError("VM: index de capture invalide");
            }
            frame.captures[index]->value = pop_value(stack);
            break;
        }
        case Opcode::CLOSURE:
        {
            const std::size_t function_index = read_u16(chunk, ip);
            const std::uint8_t capture_count = read_byte(chunk, ip);
            if (function_index >= module.functions.size())
            {
                throw VmRuntimeError("VM: index de fermeture invalide");
            }
            auto body = make_ref<VmClosureBody>();
            body->function_index = function_index;
            body->captures.reserve(capture_count);
            for (std::size_t i = 0; i < capture_count; ++i)
            {
                const bool from_capture = read_byte(chunk, ip) != 0;
                const std::uint8_t source_index = read_byte(chunk, ip);
                body->captures.push_back(frame.capture(from_capture, source_index));
            }
            auto closure = make_ref<LumiereFunction>();
            closure->name = module.functions[function_index].name;
            closure->body = std::move(body);
            stack.push_back(Value::fonction(std::move(closure)));
            break;
        }
        case Opcode::CLASS:
        {
            const std::size_t descriptor_index = read_u16(chunk, ip);
            if (descriptor_index >= module.classes.size())
            {
                throw VmRuntimeError("VM: descripteur de classe invalide");
            }
            const VmClassDescriptor &descriptor = module.classes[descriptor_index];
            std::vector<Value> interface_values(descriptor.interfaces.size());
            for (std::size_t i = descriptor.interfaces.size(); i > 0; --i)
            {
                interface_values[i - 1] = pop_value(stack);
            }
            const Value parent_value = descriptor.parent.empty() ? Value::rien() : pop_value(stack);
            auto klass = make_ref<LumiereClass>();
            klass->name = descriptor.name;
            klass->type_identity = descriptor.type_identity;
            auto body = make_ref<VmClassBody>();
            body->descriptor_index = descriptor_index;
            for (const VmMethodDescriptor &method : descriptor.methods)
            {
                auto &captures = body->method_captures[method.function_index];
                for (const VmMethodDescriptor::CaptureSource &source : method.capture_sources)
                {
                    captures.push_back(frame.capture(source.from_capture, source.index));
                }
            }
            klass->body = std::move(body);
            if (!descriptor.parent.empty())
            {
                if (!parent_value.is_classe())
                {
                    throw VmRuntimeError("VM: la classe parente n'est pas une classe: " + descriptor.parent);
                }
                klass->parent = parent_value.as_classe();
            }
            for (std::size_t i = 0; i < descriptor.interfaces.size(); ++i)
            {
                const std::string &interface_name = descriptor.interfaces[i];
                if (!interface_values[i].is_interface())
                {
                    throw VmRuntimeError("VM: le symbole n'est pas une interface: " + interface_name);
                }
                const bool error_marker =
                    interface_name == "Erreur" ||
                    (interface_name.size() > 8 &&
                     interface_name.compare(
                         interface_name.size() - 8,
                         8,
                         "::Erreur") == 0);
                const auto interface = interface_values[i].as_interface();
                klass->interfaces[error_marker ? "Erreur" :
                    (interface->type_identity.empty() ? interface_name : interface->type_identity)] = interface;
            }
            for (const VmMethodDescriptor &method : descriptor.methods)
            {
                const VmMethodDescriptor *parent_method = find_vm_method(module, klass->parent.get(), method.name);
                if (method.is_override && parent_method == nullptr)
                {
                    throw VmRuntimeError("VM: remplace utilise sans méthode parente correspondante: " + method.name);
                }
                if (!method.is_override && parent_method != nullptr)
                {
                    throw VmRuntimeError("VM: méthode parente déjà définie; utilisez remplace: " + method.name);
                }
                if (parent_method != nullptr &&
                    (parent_method->parameter_types != method.parameter_types ||
                     parent_method->return_type != method.return_type))
                {
                    throw VmRuntimeError("VM: la méthode remplacee doit conserver la même signature: " + method.name);
                }
            }
            for (const auto &[interface_name, interface] : klass->interfaces)
            {
                if (interface_name == "Erreur")
                {
                    continue;
                }
                const auto interface_body = dynamic_ref_cast<VmInterfaceBody>(interface->body);
                if (interface_body == nullptr || interface_body->descriptor_index >= module.interfaces.size())
                {
                    throw VmRuntimeError("VM: interface non compatible avec le backend: " + interface->name);
                }
                for (const VmInterfaceMethodDescriptor &required :
                     module.interfaces[interface_body->descriptor_index].methods)
                {
                    const VmMethodDescriptor *implemented = find_vm_method(module, klass.get(), required.name);
                    if (implemented == nullptr)
                    {
                        throw VmRuntimeError("VM: la classe " + descriptor.name +
                                             " ne réalise pas la méthode requise " + interface->name + "." + required.name);
                    }
                    if (implemented->parameter_types != required.parameter_types ||
                        implemented->return_type != required.return_type)
                    {
                        throw VmRuntimeError("VM: la méthode " + descriptor.name + "." + required.name +
                                             " ne respecte pas la signature requise par l'interface " + interface->name);
                    }
                    if (implemented->is_private)
                    {
                        throw VmRuntimeError("VM: la méthode " + descriptor.name + "." + required.name +
                                             " ne peut pas être privée car elle réalise l'interface " + interface->name);
                    }
                }
            }
            stack.push_back(Value::classe(std::move(klass)));
            break;
        }
        case Opcode::INTERFACE:
        {
            const std::size_t descriptor_index = read_u16(chunk, ip);
            if (descriptor_index >= module.interfaces.size())
            {
                throw VmRuntimeError("VM: descripteur d'interface invalide");
            }
            auto interface = make_ref<LumiereInterface>();
            interface->name = module.interfaces[descriptor_index].name;
            interface->type_identity = module.interfaces[descriptor_index].type_identity;
            auto body = make_ref<VmInterfaceBody>();
            body->descriptor_index = descriptor_index;
            interface->body = std::move(body);
            stack.push_back(Value::interface(std::move(interface)));
            break;
        }
        case Opcode::NAMESPACE:
        {
            const std::size_t descriptor_index = read_u16(chunk, ip);
            if (descriptor_index >= module.namespaces.size())
            {
                throw VmRuntimeError("VM: descripteur d'espace de noms invalide");
            }
            auto name_space = make_ref<LumiereObject>();
            for (const VmNamespaceMember &member : module.namespaces[descriptor_index].members)
            {
                if (member.global_index >= globals.size() || !global_defined[member.global_index])
                {
                    throw VmRuntimeError("VM: export de module non initialise: " + member.name);
                }
                name_space->fields[member.name] = globals[member.global_index];
            }
            stack.push_back(Value::objet(std::move(name_space)));
            break;
        }
        case Opcode::JUMP:
        {
            const std::size_t target = read_u16(chunk, ip);
            if (target >= chunk.code.size())
            {
                throw VmRuntimeError("VM: cible de saut invalide");
            }
            if (target < ip)
            {
                // A loop's back edge: a safe point, and the only one a program
                // that never calls anything will reach.
                collect_cycles_if_due();
            }
            ip = target;
            break;
        }
        case Opcode::JUMP_IF_FALSE:
        {
            const std::size_t true_target = read_u16(chunk, ip);
            const std::size_t false_target = read_u16(chunk, ip);
            const Value condition = pop_value(stack);
            const std::size_t target = is_truthy(condition) ? true_target : false_target;
            if (target >= chunk.code.size())
            {
                throw VmRuntimeError("VM: cible de branche invalide");
            }
            ip = target;
            break;
        }
        case Opcode::NEGATE:
            execute_negate(stack);
            break;
        case Opcode::NOT:
            execute_not(stack);
            break;
        case Opcode::ADD:
            execute_add(stack);
            break;
        case Opcode::SUBTRACT:
            execute_subtract(stack);
            break;
        case Opcode::MULTIPLY:
            execute_multiply(stack);
            break;
        case Opcode::DIVIDE:
            execute_divide(stack);
            break;
        case Opcode::MODULO:
            execute_modulo(stack);
            break;
        case Opcode::EQUAL:
            execute_equal(stack);
            break;
        case Opcode::NOT_EQUAL:
            execute_not_equal(stack);
            break;
        case Opcode::LESS:
            execute_numeric_compare(stack, [](const auto left, const auto right) { return left < right; },
                                    "la comparaison '<'");
            break;
        case Opcode::LESS_EQUAL:
            execute_numeric_compare(stack, [](const auto left, const auto right) { return left <= right; },
                                    "la comparaison '<='");
            break;
        case Opcode::GREATER:
            execute_numeric_compare(stack, [](const auto left, const auto right) { return left > right; },
                                    "la comparaison '>'");
            break;
        case Opcode::GREATER_EQUAL:
            execute_numeric_compare(stack, [](const auto left, const auto right) { return left >= right; },
                                    "la comparaison '>='");
            break;
        case Opcode::CALL:
        {
            const std::uint8_t arity = read_byte(chunk, ip);
            const ArgumentNames argument_names(chunk, ip, arity);
            const auto argument_name = [&](const std::size_t i) -> const std::string & {
                const std::size_t index = argument_names.index(i);
                if (index >= module.argument_names.size())
                {
                    throw VmRuntimeError("VM: index de nom d'argument invalide");
                }
                return module.argument_names[index];
            };
            if (stack.size() < frame.stack_base + static_cast<std::size_t>(arity) + 1)
            {
                throw VmRuntimeError("VM: pile insuffisante pour l'appel");
            }

            const std::size_t callee_index = stack.size() - static_cast<std::size_t>(arity) - 1;
            const Value callee = std::move(stack[callee_index]);
            std::vector<RuntimeArgument> call_args;
            call_args.reserve(arity);
            for (std::size_t i = callee_index + 1; i < stack.size(); ++i)
            {
                const std::size_t argument = i - callee_index - 1;
                const SourceLocation location = argument_names.location(argument);
                call_args.push_back({argument_name(argument),
                                     std::move(stack[i]),
                                     RuntimeSite{frame.function->source_path,
                                                 static_cast<int>(location.line),
                                                 static_cast<int>(location.column)}});
            }

            stack.resize(callee_index);
            RuntimeSite site;
            if (opcode_offset < chunk.locations.size())
            {
                site.line = static_cast<int>(chunk.locations[opcode_offset].line);
                site.column = static_cast<int>(chunk.locations[opcode_offset].column);
            }
            if (callee.is_classe())
            {
                stack.push_back(instantiate_vm_class(module, callee.as_classe(), call_args));
                break;
            }
            const LumiereFunction *function = callee.is_fonction() ? callee.as_fonction_ptr() : nullptr;
            if (function != nullptr && !function->is_native())
            {
                const VmClosureBody *body = vm_closure_body(function->body.get());
                if (body == nullptr)
                {
                    throw VmRuntimeError("VM: corps de fonction non compatible avec le backend VM");
                }
                normalize_closure_arguments(frames,
                                            module,
                                            body->function_index,
                                            call_args,
                                            stack.size(),
                                            body->captures,
                                            dispatch_location(),
                                            function->is_method() ? &function->receiver : nullptr);
                break;
            }
            const Value *receiver = function != nullptr && function->is_method() ? &function->receiver : nullptr;
            stack.push_back(runtime_services.call(callee, NativeArgs{receiver, &call_args, site}));
            break;
        }
        case Opcode::CALL_GLOBAL:
        case Opcode::CALL_GLOBAL_LONG:
        {
            const std::size_t global_index = opcode == Opcode::CALL_GLOBAL_LONG
                                                 ? read_u24(chunk, ip)
                                                 : read_byte(chunk, ip);
            const std::uint8_t arity = read_byte(chunk, ip);
            const ArgumentNames argument_names(chunk, ip, arity);
            const auto argument_name = [&](const std::size_t i) -> const std::string & {
                const std::size_t index = argument_names.index(i);
                if (index >= module.argument_names.size())
                {
                    throw VmRuntimeError("VM: index de nom d'argument invalide");
                }
                return module.argument_names[index];
            };
            if (global_index >= module.globals.size())
            {
                throw VmRuntimeError("VM: index global invalide");
            }
            if (stack.size() < frame.stack_base + arity)
            {
                throw VmRuntimeError("VM: pile insuffisante pour l'appel global");
            }

            const std::size_t args_start = stack.size() - arity;
            std::vector<RuntimeArgument> call_args;
            call_args.reserve(arity);
            for (std::size_t i = 0; i < arity; ++i)
            {
                const SourceLocation location = argument_names.location(i);
                call_args.push_back({argument_name(i),
                                     std::move(stack[args_start + i]),
                                     RuntimeSite{frame.function->source_path,
                                                 static_cast<int>(location.line),
                                                 static_cast<int>(location.column)}});
            }
            stack.resize(args_start);

            const std::string &name = module.globals[global_index];
            if (global_defined[global_index] && globals[global_index].is_classe())
            {
                stack.push_back(instantiate_vm_class(module,
                                                     globals[global_index].as_classe(),
                                                     call_args));
                break;
            }
            if (const auto direct = function_indices.find(name); direct != function_indices.end())
            {
                push_call_frame(frames,
                                module,
                                direct->second,
                                call_args,
                                stack.size(),
                                {},
                                dispatch_location());
                break;
            }
            if (global_defined[global_index] && globals[global_index].is_fonction())
            {
                const LumiereFunction *function = globals[global_index].as_fonction_ptr();
                if (function->is_native())
                {
                    RuntimeSite site;
                    site.source_path = module.source_path;
                    if (opcode_offset < chunk.locations.size())
                    {
                        site.line = static_cast<int>(
                            chunk.locations[opcode_offset].line);
                        site.column = static_cast<int>(
                            chunk.locations[opcode_offset].column);
                    }
                    stack.push_back(runtime_services.call(
                        globals[global_index],
                        NativeArgs{nullptr, &call_args, std::move(site)}));
                    break;
                }
                const VmClosureBody *body = vm_closure_body(function->body.get());
                if (body == nullptr)
                {
                    throw VmRuntimeError("VM: corps de fonction globale incompatible");
                }
                normalize_closure_arguments(frames,
                                            module,
                                            body->function_index,
                                            call_args,
                                            stack.size(),
                                            body->captures,
                                            dispatch_location());
                break;
            }
            const auto native = natives.find(name);
            if (native != natives.end())
            {
                for (const RuntimeArgument &argument : call_args)
                {
                    if (!argument.name.empty())
                    {
                        throw VmRuntimeError("VM: " + name + " n'accepte pas d'arguments nommés");
                    }
                }
                std::vector<Value> values;
                values.reserve(call_args.size());
                for (const RuntimeArgument &argument : call_args)
                {
                    values.push_back(argument.value);
                }
                Value result = native->second(values);
                if (result.is_resultat() &&
                    !result.as_resultat()->success &&
                    !result.as_resultat()->origin.has_value())
                {
                    RuntimeSite origin;
                    origin.source_path = module.source_path;
                    if (opcode_offset < chunk.locations.size())
                    {
                        origin.line = static_cast<int>(
                            chunk.locations[opcode_offset].line);
                        origin.column = static_cast<int>(
                            chunk.locations[opcode_offset].column);
                    }
                    result = Value::resultat(
                        false,
                        result.as_resultat()->payload,
                        std::move(origin));
                }
                stack.push_back(std::move(result));
                break;
            }

            throw VmRuntimeError("VM: " + messages::symbole_introuvable(name));
        }
        case Opcode::CALL_MEMBER:
        case Opcode::CALL_MEMBER_LONG:
        case Opcode::CALL_PARENT:
        case Opcode::CALL_PARENT_LONG:
        {
            const bool long_operand = opcode == Opcode::CALL_MEMBER_LONG || opcode == Opcode::CALL_PARENT_LONG;
            const bool parent_dispatch = opcode == Opcode::CALL_PARENT || opcode == Opcode::CALL_PARENT_LONG;
            const std::size_t member_index = long_operand
                                                 ? read_u24(chunk, ip)
                                                 : read_byte(chunk, ip);
            const std::uint8_t arity = read_byte(chunk, ip);
            const ArgumentNames argument_names(chunk, ip, arity);
            const auto argument_name = [&](const std::size_t i) -> const std::string & {
                const std::size_t index = argument_names.index(i);
                if (index >= module.argument_names.size())
                {
                    throw VmRuntimeError("VM: index de nom d'argument invalide");
                }
                return module.argument_names[index];
            };
            if (member_index >= module.members.size())
            {
                throw VmRuntimeError("VM: index de membre invalide");
            }
            if (stack.size() < frame.stack_base + static_cast<std::size_t>(arity) + 1)
            {
                throw VmRuntimeError("VM: pile insuffisante pour l'appel membre");
            }

            const std::size_t receiver_index = stack.size() - arity - 1;
            const Value receiver = std::move(stack[receiver_index]);
            std::vector<RuntimeArgument> args;
            args.reserve(arity);
            for (std::size_t i = 0; i < arity; ++i)
            {
                const SourceLocation location = argument_names.location(i);
                args.push_back({argument_name(i),
                                std::move(stack[receiver_index + 1 + i]),
                                RuntimeSite{frame.function->source_path,
                                            static_cast<int>(location.line),
                                            static_cast<int>(location.column)}});
            }
            stack.resize(receiver_index);

            if (receiver.is_objet())
            {
                const auto field = receiver.as_objet_ptr()->fields.find(module.members[member_index]);
                if (field != receiver.as_objet_ptr()->fields.end())
                {
                    if (field->second.is_classe())
                    {
                        stack.push_back(instantiate_vm_class(module, field->second.as_classe(), args));
                        break;
                    }
                    if (!field->second.is_fonction())
                    {
                        throw VmRuntimeError("VM: le membre '" + module.members[member_index] + "' n'est pas appelable");
                    }
                    const LumiereFunction *function = field->second.as_fonction_ptr();
                    if (function->is_native())
                    {
                        RuntimeSite site;
                        stack.push_back(runtime_services.call(field->second,
                                                            NativeArgs{nullptr, &args, site}));
                        break;
                    }
                    const VmClosureBody *body = vm_closure_body(function->body.get());
                    if (body == nullptr)
                    {
                        throw VmRuntimeError("VM: corps de membre incompatible");
                    }
                    normalize_closure_arguments(frames,
                                                module,
                                                body->function_index,
                                                args,
                                                stack.size(),
                                                body->captures,
                                                dispatch_location());
                    break;
                }
                const LumiereObject *object = receiver.as_objet_ptr();
                const VmMethodDescriptor *method = resolve_method(module,
                                                                  parent_dispatch
                                                                      ? object->klass->parent.get()
                                                                      : object->klass.get(),
                                                                  member_index);
                if (method == nullptr)
                {
                    throw VmRuntimeError("VM: méthode introuvable '" + module.members[member_index] + "'");
                }
                if (method->is_private && !accesses_own_object(frame, receiver))
                {
                    throw VmRuntimeError("VM: accès interdit à la méthode privée '" + method->name + "'");
                }
                normalize_closure_arguments(frames,
                                            module,
                                            method->function_index,
                                            args,
                                            stack.size(),
                                            vm_method_captures(receiver.as_objet_ptr()->klass.get(),
                                                               method->function_index),
                                            dispatch_location(),
                                            &receiver);
                break;
            }

            RuntimeSite site;
            if (opcode_offset < chunk.locations.size())
            {
                site.line = static_cast<int>(chunk.locations[opcode_offset].line);
                site.column = static_cast<int>(chunk.locations[opcode_offset].column);
            }
            stack.push_back(execute_member_call(receiver,
                                                module.members[member_index],
                                                args,
                                                runtime_services,
                                                site));
            break;
        }
        case Opcode::GET_MEMBER:
        case Opcode::GET_MEMBER_LONG:
        case Opcode::GET_PARENT:
        case Opcode::GET_PARENT_LONG:
        {
            const bool long_operand = opcode == Opcode::GET_MEMBER_LONG || opcode == Opcode::GET_PARENT_LONG;
            const bool parent_dispatch = opcode == Opcode::GET_PARENT || opcode == Opcode::GET_PARENT_LONG;
            const std::size_t member_index = long_operand
                                                 ? read_u24(chunk, ip)
                                                 : read_byte(chunk, ip);
            if (member_index >= module.members.size())
            {
                throw VmRuntimeError("VM: index de membre invalide");
            }
            if (stack.size() <= frame.stack_base)
            {
                throw VmRuntimeError("VM: pile insuffisante pour l'accès membre");
            }
            const Value receiver = pop_value(stack);
            if (receiver.is_objet())
            {
                const auto field = receiver.as_objet_ptr()->fields.find(module.members[member_index]);
                if (field != receiver.as_objet_ptr()->fields.end())
                {
                    const VmFieldDescriptor *descriptor = resolve_field(module,
                                                                        receiver.as_objet_ptr()->klass.get(),
                                                                        member_index).descriptor;
                    // Asking whether this frame is the object's own method
                    // searches the frame's name, so it is only asked about a
                    // member that is actually private.
                    if (descriptor != nullptr && descriptor->is_private &&
                        !accesses_own_object(frame, receiver))
                    {
                        throw VmRuntimeError("VM: accès interdit au champ privé '" + descriptor->name + "'");
                    }
                    stack.push_back(field->second);
                    break;
                }
                const LumiereObject *object = receiver.as_objet_ptr();
                const VmMethodDescriptor *method = resolve_method(module,
                                                                  parent_dispatch
                                                                      ? object->klass->parent.get()
                                                                      : object->klass.get(),
                                                                  member_index);
                if (method == nullptr)
                {
                    throw VmRuntimeError("VM: membre introuvable '" + module.members[member_index] + "'");
                }
                if (method->is_private && !accesses_own_object(frame, receiver))
                {
                    throw VmRuntimeError("VM: accès interdit à la méthode privée '" + method->name + "'");
                }
                auto body = make_ref<VmClosureBody>();
                body->function_index = method->function_index;
                body->captures = vm_method_captures(receiver.as_objet_ptr()->klass.get(),
                                                    method->function_index);
                auto function = make_ref<LumiereFunction>();
                function->name = method->name;
                function->body = std::move(body);
                function->receiver = receiver;
                stack.push_back(Value::fonction(std::move(function)));
                break;
            }
            stack.push_back(make_bound_member(receiver,
                                              module.members[member_index],
                                              runtime_services));
            break;
        }
        case Opcode::SET_MEMBER:
        case Opcode::SET_MEMBER_LONG:
        {
            const std::size_t member_index = opcode == Opcode::SET_MEMBER_LONG
                                                 ? read_u24(chunk, ip)
                                                 : read_byte(chunk, ip);
            if (member_index >= module.members.size())
            {
                throw VmRuntimeError("VM: index de membre invalide");
            }
            const Value value = pop_value(stack);
            const Value receiver = pop_value(stack);
            if (!receiver.is_objet())
            {
                throw VmRuntimeError("VM: affectation membre sur une valeur non objet");
            }
            const ResolvedField resolved = resolve_field(module,
                                                         receiver.as_objet_ptr()->klass.get(),
                                                         member_index);
            const VmFieldDescriptor *field = resolved.descriptor;
            if (field == nullptr)
            {
                throw VmRuntimeError("VM: champ introuvable '" + module.members[member_index] + "'");
            }
            if (field->is_private && !accesses_own_object(frame, receiver))
            {
                throw VmRuntimeError("VM: affectation interdite au champ privé '" + field->name + "'");
            }
            if (field->is_fixed)
            {
                throw VmRuntimeError("VM: impossible d'affecter le champ fixe '" + field->name + "'");
            }
            if (!field->type.empty() && !matches_type(value, resolved.shape, field->type))
            {
                throw VmRuntimeError("VM: le champ '" + field->name + "' attend " + display_runtime_type(field->type));
            }
            receiver.as_objet_ptr()->fields[field->name] = value;
            stack.push_back(value);
            break;
        }
        case Opcode::LIST:
            execute_list(stack, read_byte(chunk, ip));
            break;
        case Opcode::DICTIONARY:
            execute_dictionary(stack, read_byte(chunk, ip));
            break;
        case Opcode::ENSEMBLE:
            execute_ensemble(stack, read_byte(chunk, ip));
            break;
        case Opcode::ITERATION_SNAPSHOT:
            execute_iteration_snapshot(stack);
            break;
        case Opcode::SEQUENCE_LENGTH:
            execute_sequence_length(stack);
            break;
        case Opcode::INDEX_GET:
            execute_index_get(stack);
            break;
        case Opcode::INDEX_SET:
            execute_index_set(stack, runtime_services);
            break;
        case Opcode::CAST:
        case Opcode::CAST_LONG:
        {
            const bool is_long = opcode == Opcode::CAST_LONG;
            const std::size_t index = is_long ? read_u24(chunk, ip) : read_byte(chunk, ip);
            if (index >= module.types.size())
            {
                throw VmRuntimeError("VM: index de type invalide");
            }
            execute_cast(stack, module.types[index]);
            break;
        }
        case Opcode::TYPE_CHECK:
        case Opcode::TYPE_CHECK_LONG:
        {
            const bool is_long = opcode == Opcode::TYPE_CHECK_LONG;
            const std::size_t index = is_long ? read_u24(chunk, ip) : read_byte(chunk, ip);
            if (index >= module.types.size())
            {
                throw VmRuntimeError("VM: index de type invalide");
            }
            execute_type_check(stack, module.types[index]);
            break;
        }
        case Opcode::ASSERT_TYPE:
        case Opcode::ASSERT_TYPE_LONG:
        {
            const std::size_t index = opcode == Opcode::ASSERT_TYPE_LONG
                                          ? read_u24(chunk, ip)
                                          : read_byte(chunk, ip);
            if (index >= module.annotations.size())
            {
                throw VmRuntimeError("VM: index d'annotation invalide");
            }
            const VmAnnotation &annotation = module.annotations[index];
            if (annotation.type_index >= module.types.size())
            {
                throw VmRuntimeError("VM: index de type invalide");
            }
            execute_type_assertion(stack,
                                   module.types[annotation.type_index],
                                   execution.type_shapes[annotation.type_index],
                                   annotation.context,
                                   runtime_services);
            break;
        }
        case Opcode::MATCH_ERROR:
            throw VmRuntimeError("VM: aucune branche de 'agir selon' ne correspond");
        case Opcode::POP:
            if (stack.size() <= frame.stack_base)
            {
                throw VmRuntimeError("VM: POP sur une pile vide");
            }
            stack.pop_back();
            break;
        case Opcode::RESULT_IS_SUCCESS:
        {
            const Value result = pop_value(stack);
            if (!result.is_resultat())
            {
                throw VmRuntimeError("VM: motif de résultat appliqué à une autre valeur");
            }
            stack.push_back(Value::logique(result.as_resultat()->success));
            break;
        }
        case Opcode::RESULT_FAILURE_TYPE:
        case Opcode::RESULT_FAILURE_TYPE_LONG:
        {
            const std::size_t index =
                opcode == Opcode::RESULT_FAILURE_TYPE_LONG
                    ? read_u24(chunk, ip)
                    : read_byte(chunk, ip);
            if (index >= module.types.size())
            {
                throw VmRuntimeError("VM: index de type invalide");
            }
            const Value result = pop_value(stack);
            stack.push_back(Value::logique(
                result.is_resultat() &&
                !result.as_resultat()->success &&
                matches_type_name(result.as_resultat()->payload,
                                  module.types[index])));
            break;
        }
        case Opcode::RESULT_PAYLOAD:
        {
            const Value result = pop_value(stack);
            if (!result.is_resultat())
            {
                throw VmRuntimeError("VM: extraction appliquée à une valeur non résultat");
            }
            stack.push_back(result.as_resultat()->payload);
            break;
        }
        case Opcode::PROPAGATE:
        {
            const Value result = pop_value(stack);
            if (!result.is_resultat())
            {
                throw VmRuntimeError("VM: propager exige une valeur Résultat");
            }
            if (result.as_resultat()->success)
            {
                stack.push_back(result.as_resultat()->payload);
            }
            else
            {
                Value error = result;
                if (frame.function->return_type.empty() ||
                    !matches_type_name(error,
                                       frame.function->return_type))
                {
                    throw VmRuntimeError(
                        "VM: propager exige une fonction englobante dont le retour '" +
                        frame.function->return_type + "' accepte " +
                        error.type_name());
                }
                error = error.with_trace_frame(TraceFrame{
                    frame.function->name,
                    frame.function->source_path,
                    static_cast<std::uint32_t>(frame.call_site.line),
                    static_cast<std::uint32_t>(frame.call_site.column)});
                stack.resize(frame.stack_base);
                frames.pop();
                if (frames.empty())
                {
                    return error;
                }
                stack.push_back(std::move(error));
            }
            break;
        }
        case Opcode::IGNORE_RESULT:
        {
            const Value value = pop_value(stack);
            if (!value.is_resultat())
            {
                throw VmRuntimeError("VM: ignorer exige une valeur Résultat");
            }
            break;
        }
        case Opcode::RETURN:
        {
            collect_cycles_if_due();
            Value result = stack.size() > frame.stack_base ? std::move(stack.back()) : Value::rien();
            if (result.is_resultat() && !result.as_resultat()->success)
            {
                result = result.with_trace_frame(TraceFrame{
                    frame.function->name,
                    frame.function->source_path,
                    static_cast<std::uint32_t>(frame.call_site.line),
                    static_cast<std::uint32_t>(frame.call_site.column)});
            }
            stack.resize(frame.stack_base);
            frames.pop();
            if (frames.empty())
            {
                return result;
            }
            stack.push_back(std::move(result));
            break;
        }
        }
        }
        catch (const VmRuntimeError &error)
        {
            throw build_runtime_error(error, opcode_offset);
        }
    }

    return Value::rien();
}

Value run_frames(VmExecutionState &execution,
                 const std::size_t entry_function_index,
                 std::vector<Value> entry_arguments = {},
                 std::vector<CellRef> entry_captures = {},
                 const SourceLocation entry_call_site = {})
{
    FrameStack frames;
    push_call_frame(frames,
                    execution.module,
                    entry_function_index,
                    std::move(entry_arguments),
                    0,
                    std::move(entry_captures),
                    entry_call_site);
    return execute_frames(execution, std::move(frames));
}

Value run_closure_frames(VmExecutionState &execution,
                         const std::size_t entry_function_index,
                         std::vector<RuntimeArgument> arguments,
                         std::vector<CellRef> entry_captures,
                         const SourceLocation entry_call_site)
{
    FrameStack frames;
    normalize_closure_arguments(frames,
                                execution.module,
                                entry_function_index,
                                arguments,
                                0,
                                std::move(entry_captures),
                                entry_call_site);
    return execute_frames(execution, std::move(frames));
}

} // namespace

void VM::execute(Program &program)
{
    VmCompiler compiler;
    ModuleBytecode module = compiler.compile(program);
    // Checked once here so the interpreter can read operands without checking
    // them again on every instruction.
    if (const auto problem = verify_module(module))
    {
        throw VmCompileError("VM: bytecode invalide — " + *problem);
    }
    const Value result = run(module);
    // The entry frame is gone, so anything the program left in a cycle is now
    // unreachable and this is the last chance to say so.
    collect_cycles();
    if (result.is_resultat() && !result.as_resultat()->success)
    {
        const std::optional<RuntimeSite> &origin =
            result.as_resultat()->origin;
        const auto &traced = result.as_resultat()->trace;
        std::vector<StackFrame> stack_trace;
        stack_trace.reserve(traced.size());
        for (const TraceFrame &tf : traced)
        {
            stack_trace.push_back({tf.function_name,
                                   tf.source_path,
                                   tf.line,
                                   tf.column});
        }
        throw RuntimeError(
            "principal a échoué: " +
                result.as_resultat()->payload.to_string(),
            origin.has_value()
                ? origin->source_path
                : program.source_path,
            !origin.has_value() ||
                    origin->source_path == program.source_path
                ? program.source_text
                : std::string{},
            origin.has_value()
                ? static_cast<uint32_t>(origin->line)
                : 0,
            origin.has_value()
                ? static_cast<uint32_t>(origin->column)
                : 0,
            std::move(stack_trace));
    }
}

Value VM::run(const ModuleBytecode &module)
{
    if (module.functions.empty() || module.entry_function_index >= module.functions.size())
    {
        throw VmRuntimeError("VM: module bytecode invalide");
    }

    const auto natives = native_globals();
    std::unordered_map<std::string, std::size_t> function_indices;
    function_indices.reserve(module.functions.size());
    for (std::size_t i = 0; i < module.functions.size(); ++i)
    {
        function_indices.emplace(module.functions[i].name, i);
    }

    std::vector<Value> globals(module.globals.size(), Value::rien());
    std::vector<bool> global_defined(module.globals.size(), false);
    std::vector<bool> initialized_functions(module.functions.size(), false);
    VmExecutionState execution{
        module, natives, function_indices, globals, global_defined, initialized_functions, {},
        classify_types(module.types)};
    // Initializers, entrypoint, and native callback re-entry share contracts and
    // bound native methods for the entire execution, not just one frame stack.
    execution.runtime_services.set_callback_executor([&](Value callee, const NativeArgs &args) {
        const auto function = callee.as_fonction();
        const auto body = dynamic_ref_cast<VmClosureBody>(function->body);
        if (body == nullptr || args.arguments == nullptr || body->function_index >= module.functions.size())
            throw VmRuntimeError("VM: fermeture bytecode invalide");
        return run_closure_frames(execution,
                                  body->function_index,
                                  *args.arguments,
                                  body->captures,
                                  {static_cast<std::size_t>(args.site.line),
                                   static_cast<std::size_t>(args.site.column)});
    });
    for (std::size_t i = 0; i < module.globals.size(); ++i)
    {
        const std::string &name = module.globals[i];
        if (name == "Erreur" ||
            (name.size() > 8 &&
             name.compare(
                 name.size() - 8,
                 8,
                 "::Erreur") == 0))
        {
            auto interface =
                make_ref<LumiereInterface>();
            interface->name = "Erreur";
            globals[i] =
                Value::interface(std::move(interface));
            global_defined[i] = true;
            continue;
        }
        if (const auto function = function_indices.find(name); function != function_indices.end())
        {
            auto body = make_ref<VmClosureBody>();
            body->function_index = function->second;
            auto closure = make_ref<LumiereFunction>();
            closure->name = name;
            closure->body = std::move(body);
            globals[i] = Value::fonction(std::move(closure));
            global_defined[i] = true;
        }
        else if (const auto native = natives.find(name); native != natives.end())
        {
            auto callable = make_ref<LumiereFunction>();
            callable->name = name;
            callable->min_arity = 0;
            callable->max_arity = 255;
            const NativeFunction handler = native->second;
            callable->native_handler = [handler, name](
                                           IRuntime &,
                                           const NativeArgs &args) {
                std::vector<Value> values;
                values.reserve(args.arguments->size());
                for (const RuntimeArgument &argument : *args.arguments)
                {
                    if (!argument.name.empty())
                    {
                        throw VmRuntimeError("VM: arguments nommés ne sont pas pris en charge pour '" + name + "'");
                    }
                    values.push_back(argument.value);
                }
                Value result = handler(values);
                if (name == "Échec" &&
                    result.is_resultat() &&
                    !result.as_resultat()->origin.has_value())
                {
                    result = Value::resultat(
                        false,
                        result.as_resultat()->payload,
                        args.site);
                }
                return result;
            };
            globals[i] = Value::fonction(std::move(callable));
            global_defined[i] = true;
        }
    }
    for (const std::size_t initializer : module.initializer_function_indices)
    {
        if (initializer >= initialized_functions.size())
        {
            throw VmRuntimeError("VM: index d'initialiseur invalide");
        }
        if (initialized_functions[initializer])
        {
            continue;
        }
        initialized_functions[initializer] = true;
        static_cast<void>(run_frames(execution, initializer));
    }
    return run_frames(execution, module.entry_function_index);
}

} // namespace lumiere
