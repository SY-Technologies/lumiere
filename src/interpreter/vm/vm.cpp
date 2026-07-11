#include "lumiere/interpreter/vm/vm.hpp"

#include "lumiere/interpreter/vm/compiler.hpp"
#include "native_globals.hpp"
#include "vm_error.hpp"
#include "lumiere/interpreter/runtime/iruntime.hpp"
#include "lumiere/interpreter/runtime/runtime_argument.hpp"
#include "lumiere/interpreter/tree_walker/runtime.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/parser/utf8.hpp"

#include <algorithm>
#include <functional>
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

std::uint8_t read_byte(const Chunk &chunk, std::size_t &ip)
{
    if (ip >= chunk.code.size())
    {
        throw VmRuntimeError("VM: lecture hors limites du bytecode");
    }
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

    Value value = stack.back();
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
    throw VmRuntimeError("VM: operation arithmetique attend des valeurs numeriques");
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
    if (left.is_liste_fixe() && right.is_liste_fixe())
    {
        const auto left_elements = left.as_liste_fixe();
        const auto right_elements = right.as_liste_fixe();
        if (left_elements->elements.size() != right_elements->elements.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < left_elements->elements.size(); ++i)
        {
            if (!values_equal(left_elements->elements[i], right_elements->elements[i]))
            {
                return false;
            }
        }
        return true;
    }

    return left == right;
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

    [[noreturn]] void raise_runtime_error(const RuntimeSite &, const std::string &message) const override
    {
        throw VmRuntimeError("VM: " + message);
    }

    bool is_equal(const Value &left, const Value &right) const override
    {
        return values_equal(left, right);
    }

    std::string to_text(const Value &value) const override
    {
        return value.to_string();
    }

    void annotate_value(const Value &value, std::string_view type_name, const RuntimeSite &) const override;

    void enforce_list_element(const std::shared_ptr<ListeData> &list,
                              const Value &value,
                              const std::string &context) const;
    void enforce_fixed_list_element(const std::shared_ptr<ListeFixeData> &list,
                                    const Value &value,
                                    const std::string &context) const;
    void enforce_dictionary_entry(const std::shared_ptr<DictData> &dictionary,
                                  const Value &key,
                                  const Value &value,
                                  const std::string &context) const;

    [[nodiscard]] std::optional<std::string> list_element_type(const std::shared_ptr<ListeData> &list) const;
    [[nodiscard]] std::optional<std::string> fixed_list_element_type(const std::shared_ptr<ListeFixeData> &list) const;
    [[nodiscard]] std::optional<std::pair<std::string, std::string>> dictionary_types(
        const std::shared_ptr<DictData> &dictionary) const;

private:
    CallbackExecutor m_callback_executor;
    mutable std::unordered_map<const ListeData *, std::string> m_list_types;
    mutable std::unordered_map<const ListeFixeData *, std::string> m_fixed_list_types;
    mutable std::unordered_map<const DictData *, std::pair<std::string, std::string>> m_dictionary_types;
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

bool matches_type_name(const Value &value, const std::string_view full_name)
{
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
        std::shared_ptr<LumiereClass> klass = value.as_objet()->klass;
        while (klass != nullptr)
        {
            if (klass->name == name || klass->interfaces.contains(std::string(name)))
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
        for (const auto &[key, entry_value] : value.as_dictionnaire()->entries)
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
        for (const Value &element : value.as_ensemble()->elements)
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
                                     const std::string_view type_name,
                                     const RuntimeSite &) const
{
    const auto arguments = annotation_arguments(type_name);
    if (value.is_liste() && arguments.size() == 1)
    {
        m_list_types[value.as_liste().get()] = std::string(arguments[0]);
    }
    else if (value.is_liste_fixe() && arguments.size() == 2)
    {
        m_fixed_list_types[value.as_liste_fixe().get()] = std::string(arguments[0]);
    }
    else if (value.is_dictionnaire() && arguments.size() == 2)
    {
        m_dictionary_types[value.as_dictionnaire().get()] = {std::string(arguments[0]), std::string(arguments[1])};
    }
}

void VmRuntimeServices::enforce_list_element(const std::shared_ptr<ListeData> &list,
                                           const Value &value,
                                           const std::string &context) const
{
    if (const auto type = list_element_type(list); type.has_value() && !matches_type_name(value, *type))
    {
        throw VmRuntimeError("VM: " + context + " attend une valeur " + *type);
    }
}

void VmRuntimeServices::enforce_fixed_list_element(const std::shared_ptr<ListeFixeData> &list,
                                                 const Value &value,
                                                 const std::string &context) const
{
    if (const auto type = fixed_list_element_type(list); type.has_value() && !matches_type_name(value, *type))
    {
        throw VmRuntimeError("VM: " + context + " attend une valeur " + *type);
    }
}

void VmRuntimeServices::enforce_dictionary_entry(const std::shared_ptr<DictData> &dictionary,
                                               const Value &key,
                                               const Value &value,
                                               const std::string &context) const
{
    const auto types = dictionary_types(dictionary);
    if (!types.has_value())
    {
        return;
    }
    if (!matches_type_name(key, types->first) || !matches_type_name(value, types->second))
    {
        throw VmRuntimeError("VM: " + context + " attend " + types->first + " -> " + types->second);
    }
}

std::optional<std::string> VmRuntimeServices::list_element_type(const std::shared_ptr<ListeData> &list) const
{
    const auto it = m_list_types.find(list.get());
    return it == m_list_types.end() ? std::nullopt : std::optional<std::string>(it->second);
}

std::optional<std::string> VmRuntimeServices::fixed_list_element_type(
    const std::shared_ptr<ListeFixeData> &list) const
{
    const auto it = m_fixed_list_types.find(list.get());
    return it == m_fixed_list_types.end() ? std::nullopt : std::optional<std::string>(it->second);
}

std::optional<std::pair<std::string, std::string>> VmRuntimeServices::dictionary_types(
    const std::shared_ptr<DictData> &dictionary) const
{
    const auto it = m_dictionary_types.find(dictionary.get());
    return it == m_dictionary_types.end()
               ? std::nullopt
               : std::optional<std::pair<std::string, std::string>>(it->second);
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
            stack.push_back(Value::entier(static_cast<std::int64_t>(operand.as_decimal())));
        }
        else if (operand.is_symbole())
        {
            stack.push_back(Value::entier(static_cast<std::int64_t>(operand.as_symbole())));
        }
        else if (operand.is_texte())
        {
            try { stack.push_back(Value::entier(std::stoll(operand.as_texte()))); }
            catch (...) { throw VmRuntimeError("VM: conversion vers Entier impossible pour une valeur de type Texte"); }
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
            try { stack.push_back(Value::decimal(std::stod(operand.as_texte()))); }
            catch (...) { throw VmRuntimeError("VM: conversion vers Decimal impossible pour une valeur de type Texte"); }
        }
        else
        {
            throw VmRuntimeError("VM: conversion explicite non prise en charge vers Decimal");
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
            if (code_point < 0 || code_point > 0x10FFFF)
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
                throw VmRuntimeError("VM: conversion vers Symbole impossible: le texte doit contenir exactement un caractere");
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

void execute_type_assertion(std::vector<Value> &stack,
                            const std::string &type_name,
                            VmRuntimeServices &runtime)
{
    if (stack.empty())
    {
        throw VmRuntimeError("VM: pile vide pendant la verification de type");
    }
    if (!matches_type_name(stack.back(), type_name))
    {
        throw VmRuntimeError("VM: valeur de type " + stack.back().type_name() +
                             " incompatible avec " + type_name);
    }
    runtime.annotate_value(stack.back(), type_name, {});
}

void execute_add(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        stack.push_back(Value::entier(left.as_entier() + right.as_entier()));
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

    throw VmRuntimeError("VM: addition attend deux valeurs numeriques ou au moins un Texte");
}

void execute_subtract(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        stack.push_back(Value::entier(left.as_entier() - right.as_entier()));
        return;
    }

    if (left.is_numeric() && right.is_numeric())
    {
        stack.push_back(Value::decimal(numeric_value(left) - numeric_value(right)));
        return;
    }

    throw VmRuntimeError("VM: soustraction attend deux valeurs numeriques");
}

void execute_multiply(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        stack.push_back(Value::entier(left.as_entier() * right.as_entier()));
        return;
    }

    if (left.is_numeric() && right.is_numeric())
    {
        stack.push_back(Value::decimal(numeric_value(left) * numeric_value(right)));
        return;
    }

    throw VmRuntimeError("VM: multiplication attend deux valeurs numeriques");
}

void execute_divide(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);

    if (left.is_entier() && right.is_entier())
    {
        if (right.as_entier() == 0)
        {
            throw VmRuntimeError("VM: division par zero");
        }
        stack.push_back(Value::entier(left.as_entier() / right.as_entier()));
        return;
    }

    if (left.is_numeric() && right.is_numeric())
    {
        const double divisor = numeric_value(right);
        if (divisor == 0.0)
        {
            throw VmRuntimeError("VM: division par zero");
        }
        stack.push_back(Value::decimal(numeric_value(left) / divisor));
        return;
    }

    throw VmRuntimeError("VM: division attend deux valeurs numeriques");
}

void execute_modulo(std::vector<Value> &stack)
{
    const Value right = pop_value(stack);
    const Value left = pop_value(stack);
    if (!left.is_entier() || !right.is_entier())
    {
        throw VmRuntimeError("VM: modulo attend deux valeurs de type Entier");
    }
    if (right.as_entier() == 0)
    {
        throw VmRuntimeError("VM: modulo par zero");
    }
    stack.push_back(Value::entier(left.as_entier() % right.as_entier()));
}

void execute_negate(std::vector<Value> &stack)
{
    const Value operand = pop_value(stack);

    if (operand.is_entier())
    {
        stack.push_back(Value::entier(-operand.as_entier()));
        return;
    }

    if (operand.is_decimal())
    {
        stack.push_back(Value::decimal(-operand.as_decimal()));
        return;
    }

    throw VmRuntimeError("VM: negation attend une valeur numerique");
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
void execute_numeric_compare(std::vector<Value> &stack, Predicate predicate, const char *message)
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

    throw VmRuntimeError(message);
}

void execute_list(std::vector<Value> &stack, const std::size_t length)
{
    if (stack.size() < length)
    {
        throw VmRuntimeError("VM: pile insuffisante pour construire une liste");
    }

    auto data = std::make_shared<ListeData>();
    data->elements.reserve(length);

    const std::size_t start = stack.size() - length;
    for (std::size_t i = start; i < stack.size(); ++i)
    {
        data->elements.push_back(stack[i]);
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

    auto data = std::make_shared<DictData>();
    data->entries.reserve(entry_count);

    const std::size_t start = stack.size() - value_count;
    for (std::size_t i = start; i < stack.size(); i += 2)
    {
        data->entries.emplace_back(stack[i], stack[i + 1]);
    }

    stack.resize(start);
    stack.push_back(Value::dictionnaire(std::move(data)));
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

    throw VmRuntimeError("VM: longueur demandee sur une valeur non iterable");
}

void execute_index_get(std::vector<Value> &stack)
{
    const Value index = pop_value(stack);
    const Value sequence = pop_value(stack);
    if (sequence.is_dictionnaire())
    {
        for (const auto &entry : sequence.as_dictionnaire()->entries)
        {
            if (values_equal(entry.first, index))
            {
                stack.push_back(entry.second);
                return;
            }
        }
        throw VmRuntimeError("VM: cle introuvable dans le Dictionnaire");
    }

    if (!index.is_entier())
    {
        throw VmRuntimeError("VM: l'index de sequence doit etre un Entier");
    }

    const int64_t raw_index = index.as_entier();
    if (raw_index < 0)
    {
        throw VmRuntimeError("VM: indice negatif");
    }
    const std::size_t offset = static_cast<std::size_t>(raw_index);

    if (sequence.is_liste())
    {
        if (offset >= sequence.as_liste()->elements.size())
        {
            throw VmRuntimeError("VM: indice hors limites");
        }
        stack.push_back(sequence.as_liste()->elements[offset]);
        return;
    }
    if (sequence.is_liste_fixe())
    {
        if (offset >= sequence.as_liste_fixe()->elements.size())
        {
            throw VmRuntimeError("VM: indice hors limites");
        }
        stack.push_back(sequence.as_liste_fixe()->elements[offset]);
        return;
    }
    if (sequence.is_texte())
    {
        const auto character = utf8::character_at(sequence.as_texte(), offset);
        if (!character.has_value())
        {
            throw VmRuntimeError("VM: indice hors limites");
        }
        stack.push_back(Value::symbole(*character));
        return;
    }

    throw VmRuntimeError("VM: indexation demandee sur une valeur non iterable");
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
            throw VmRuntimeError("VM: l'index de liste doit etre un Entier");
        }
        const std::int64_t raw_index = index.as_entier();
        if (raw_index < 0 || static_cast<std::size_t>(raw_index) >= object.as_liste()->elements.size())
        {
            throw VmRuntimeError("VM: index de liste hors limites");
        }
        runtime.enforce_list_element(object.as_liste(), value, "l'affectation de liste");
        object.as_liste()->elements[static_cast<std::size_t>(raw_index)] = value;
        stack.push_back(value);
        return;
    }

    if (object.is_liste_fixe())
    {
        if (!index.is_entier())
        {
            throw VmRuntimeError("VM: l'index de liste fixe doit etre un Entier");
        }
        const std::int64_t raw_index = index.as_entier();
        if (raw_index < 0 || static_cast<std::size_t>(raw_index) >= object.as_liste_fixe()->elements.size())
        {
            throw VmRuntimeError("VM: index de liste fixe hors limites");
        }
        runtime.enforce_fixed_list_element(object.as_liste_fixe(), value, "l'affectation de liste fixe");
        object.as_liste_fixe()->elements[static_cast<std::size_t>(raw_index)] = value;
        stack.push_back(value);
        return;
    }

    if (object.is_dictionnaire())
    {
        auto dictionary = object.as_dictionnaire();
        runtime.enforce_dictionary_entry(dictionary, index, value, "l'affectation de dictionnaire");
        for (auto &entry : dictionary->entries)
        {
            if (values_equal(entry.first, index))
            {
                entry.second = value;
                stack.push_back(value);
                return;
            }
        }
        dictionary->entries.emplace_back(index, value);
        stack.push_back(value);
        return;
    }

    throw VmRuntimeError("VM: affectation par indice impossible pour ce type");
}

void require_member_arity(const std::string &signature,
                          const std::vector<Value> &args,
                          const std::size_t expected)
{
    if (args.size() != expected)
    {
        throw VmRuntimeError("VM: " + signature + " attend " + std::to_string(expected) + " argument(s)");
    }
}

std::int64_t member_integer(const Value &value, const std::string &signature)
{
    if (!value.is_entier())
    {
        throw VmRuntimeError("VM: " + signature + " attend un Entier");
    }
    return value.as_entier();
}

std::string member_text(const Value &value, const std::string &signature)
{
    if (!value.is_texte())
    {
        throw VmRuntimeError("VM: " + signature + " attend un Texte");
    }
    return value.as_texte();
}

Value execute_sequence_member(const std::vector<Value> &elements,
                              const std::string &family,
                              const std::string &member,
                              const std::vector<Value> &args)
{
    if (member == "taille")
    {
        require_member_arity(family + ".taille", args, 0);
        return Value::entier(static_cast<std::int64_t>(elements.size()));
    }
    if (member == "vide")
    {
        require_member_arity(family + ".vide", args, 0);
        return Value::logique(elements.empty());
    }
    if (member == "contient")
    {
        require_member_arity(family + ".contient", args, 1);
        for (const Value &element : elements)
        {
            if (values_equal(element, args[0]))
            {
                return Value::logique(true);
            }
        }
        return Value::logique(false);
    }
    if (member == "joindre")
    {
        require_member_arity(family + ".joindre", args, 1);
        const std::string separator = member_text(args[0], family + ".joindre");
        std::string result;
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            if (i > 0)
            {
                result += separator;
            }
            result += elements[i].to_string();
        }
        return Value::texte(std::move(result));
    }
    return Value::rien();
}

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

    std::vector<Value> args;
    args.reserve(runtime_args.size());
    for (const RuntimeArgument &argument : runtime_args)
    {
        if (!argument.name.empty())
        {
            throw VmRuntimeError("VM: " + receiver.type_name() + "." + member +
                                 " n'accepte pas d'arguments nommes");
        }
        args.push_back(argument.value);
    }

    if (receiver.is_liste())
    {
        auto list = receiver.as_liste();
        const Value common = execute_sequence_member(list->elements, "Liste", member, args);
        if (!common.is_rien())
        {
            return common;
        }
        if (member == "ajouter")
        {
            require_member_arity("Liste.ajouter", args, 1);
            runtime.enforce_list_element(list, args[0], "Liste.ajouter");
            list->elements.push_back(args[0]);
            return Value::entier(static_cast<std::int64_t>(list->elements.size()));
        }
        if (member == "inserer")
        {
            require_member_arity("Liste.inserer", args, 2);
            const std::int64_t position = member_integer(args[0], "Liste.inserer");
            if (position < 0 || static_cast<std::size_t>(position) > list->elements.size())
            {
                throw VmRuntimeError("VM: indice d'insertion hors limites");
            }
            runtime.enforce_list_element(list, args[1], "Liste.inserer");
            list->elements.insert(list->elements.begin() + position, args[1]);
            return Value::entier(position);
        }
        if (member == "retirer_a")
        {
            require_member_arity("Liste.retirer_a", args, 1);
            const std::int64_t position = member_integer(args[0], "Liste.retirer_a");
            if (position < 0 || static_cast<std::size_t>(position) >= list->elements.size())
            {
                throw VmRuntimeError("VM: indice hors limites");
            }
            Value removed = list->elements[static_cast<std::size_t>(position)];
            list->elements.erase(list->elements.begin() + position);
            return removed;
        }
        if (member == "en_liste_fixe")
        {
            require_member_arity("Liste.en_liste_fixe", args, 1);
            const std::int64_t length = member_integer(args[0], "Liste.en_liste_fixe");
            if (length < 0 || static_cast<std::size_t>(length) != list->elements.size())
            {
                throw VmRuntimeError("VM: Liste.en_liste_fixe requiert une liste de taille exacte");
            }
            auto fixed = std::make_shared<ListeFixeData>();
            fixed->elements = list->elements;
            Value result = Value::liste_fixe(std::move(fixed));
            if (const auto type = runtime.list_element_type(list); type.has_value())
            {
                runtime.annotate_value(result,
                                       "ListeFixe[" + *type + "," + std::to_string(length) + "]",
                                       site);
            }
            return result;
        }
    }

    if (receiver.is_liste_fixe())
    {
        auto list = receiver.as_liste_fixe();
        const Value common = execute_sequence_member(list->elements, "ListeFixe", member, args);
        if (!common.is_rien())
        {
            return common;
        }
        if (member == "en_liste")
        {
            require_member_arity("ListeFixe.en_liste", args, 0);
            auto dynamic = std::make_shared<ListeData>();
            dynamic->elements = list->elements;
            Value result = Value::liste(std::move(dynamic));
            if (const auto type = runtime.fixed_list_element_type(list); type.has_value())
            {
                runtime.annotate_value(result, "Liste[" + *type + "]", site);
            }
            return result;
        }
    }

    if (receiver.is_dictionnaire())
    {
        auto dictionary = receiver.as_dictionnaire();
        if (member == "taille")
        {
            require_member_arity("Dictionnaire.taille", args, 0);
            return Value::entier(static_cast<std::int64_t>(dictionary->entries.size()));
        }
        if (member == "vide")
        {
            require_member_arity("Dictionnaire.vide", args, 0);
            return Value::logique(dictionary->entries.empty());
        }
        if (member == "contient")
        {
            require_member_arity("Dictionnaire.contient", args, 1);
            for (const auto &entry : dictionary->entries)
            {
                if (values_equal(entry.first, args[0]))
                {
                    return Value::logique(true);
                }
            }
            return Value::logique(false);
        }
        if (member == "cles" || member == "valeurs")
        {
            require_member_arity("Dictionnaire." + member, args, 0);
            auto result = std::make_shared<ListeData>();
            for (const auto &entry : dictionary->entries)
            {
                result->elements.push_back(member == "cles" ? entry.first : entry.second);
            }
            Value list_result = Value::liste(std::move(result));
            if (const auto types = runtime.dictionary_types(dictionary); types.has_value())
            {
                runtime.annotate_value(list_result,
                                       "Liste[" + (member == "cles" ? types->first : types->second) + "]",
                                       site);
            }
            return list_result;
        }
        if (member == "paires")
        {
            require_member_arity("Dictionnaire.paires", args, 0);
            auto pairs = std::make_shared<ListeData>();
            for (const auto &entry : dictionary->entries)
            {
                auto pair = std::make_shared<ListeFixeData>();
                pair->elements = {entry.first, entry.second};
                pairs->elements.push_back(Value::liste_fixe(std::move(pair)));
            }
            return Value::liste(std::move(pairs));
        }
        if (member == "retirer")
        {
            require_member_arity("Dictionnaire.retirer", args, 1);
            for (auto it = dictionary->entries.begin(); it != dictionary->entries.end(); ++it)
            {
                if (values_equal(it->first, args[0]))
                {
                    Value removed = it->second;
                    dictionary->entries.erase(it);
                    return removed;
                }
            }
            throw VmRuntimeError("VM: cle introuvable dans le dictionnaire");
        }
    }

    throw VmRuntimeError("VM: membre introuvable '" + member + "' pour " + receiver.type_name());
}

Value VmRuntimeServices::call(Value callee, const NativeArgs &args)
{
    if (!callee.is_fonction())
    {
        throw VmRuntimeError("VM: la valeur n'est pas appelable");
    }
    if (callee.as_fonction()->is_native())
    {
        return callee.as_fonction()->native_handler(*this, args);
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
    auto function = std::make_shared<LumiereFunction>();
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

struct CallFrame
{
    struct ExceptionHandler
    {
        std::size_t target = 0;
        std::size_t stack_depth = 0;
    };

    const FunctionBytecode *function = nullptr;
    std::size_t ip = 0;
    std::size_t stack_base = 0;
    std::vector<std::shared_ptr<Value>> locals;
    std::vector<std::shared_ptr<Value>> captures;
    std::vector<ExceptionHandler> handlers;
    SourceLocation call_site {};
};

struct VmClosureBody final : RuntimeFunctionBody
{
    std::size_t function_index = 0;
    std::vector<std::shared_ptr<Value>> captures;
};

struct VmClassBody final : RuntimeClassBody
{
    std::size_t descriptor_index = 0;
    std::unordered_map<std::size_t, std::vector<std::shared_ptr<Value>>> method_captures;
};

struct VmInterfaceBody final : RuntimeInterfaceBody
{
    std::size_t descriptor_index = 0;
};

const VmClassDescriptor *class_descriptor(const ModuleBytecode &module,
                                          const std::shared_ptr<LumiereClass> &klass)
{
    if (klass == nullptr)
    {
        return nullptr;
    }
    const auto body = std::dynamic_pointer_cast<VmClassBody>(klass->body);
    if (body == nullptr || body->descriptor_index >= module.classes.size())
    {
        return nullptr;
    }
    return &module.classes[body->descriptor_index];
}

const VmMethodDescriptor *find_vm_method(const ModuleBytecode &module,
                                         std::shared_ptr<LumiereClass> klass,
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
        klass = klass->parent;
    }
    return nullptr;
}

const VmFieldDescriptor *find_vm_field(const ModuleBytecode &module,
                                       std::shared_ptr<LumiereClass> klass,
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
        klass = klass->parent;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Value>> find_vm_method_captures(std::shared_ptr<LumiereClass> klass,
                                                            const std::size_t function_index)
{
    while (klass != nullptr)
    {
        const auto body = std::dynamic_pointer_cast<VmClassBody>(klass->body);
        if (body != nullptr)
        {
            const auto captures = body->method_captures.find(function_index);
            if (captures != body->method_captures.end())
            {
                return captures->second;
            }
        }
        klass = klass->parent;
    }
    return {};
}

void collect_vm_fields(const ModuleBytecode &module,
                       const std::shared_ptr<LumiereClass> &klass,
                       std::vector<const VmFieldDescriptor *> &fields)
{
    if (klass == nullptr)
    {
        return;
    }
    collect_vm_fields(module, klass->parent, fields);
    const VmClassDescriptor *descriptor = class_descriptor(module, klass);
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
                           const std::shared_ptr<LumiereClass> &klass,
                           const std::vector<RuntimeArgument> &arguments)
{
    std::vector<const VmFieldDescriptor *> fields;
    collect_vm_fields(module, klass, fields);
    if (arguments.size() > fields.size())
    {
        throw VmRuntimeError("VM: trop d'arguments pour construire '" + klass->name + "'");
    }
    auto object = std::make_shared<LumiereObject>();
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
            throw VmRuntimeError("VM: le champ '" + field->name + "' attend " + field->type);
        }
        object->fields[field->name] = value;
    }
    return Value::objet(std::move(object));
}

CallFrame make_call_frame(const ModuleBytecode &module,
                          const std::size_t function_index,
                          const std::vector<Value> &args,
                          const std::size_t stack_base,
                          std::vector<std::shared_ptr<Value>> captures = {},
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
    if (captures.size() != function.capture_count)
    {
        throw VmRuntimeError("VM: nombre de captures invalide pour '" + function.name + "'");
    }

    CallFrame frame;
    frame.function = &function;
    frame.stack_base = stack_base;
    frame.locals.reserve(function.local_slot_count);
    for (std::size_t i = 0; i < function.local_slot_count; ++i)
    {
        frame.locals.push_back(std::make_shared<Value>(Value::rien()));
    }
    frame.captures = std::move(captures);
    frame.call_site = call_site;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        *frame.locals[i] = args[i];
    }
    return frame;
}

std::vector<Value> normalize_closure_arguments(const FunctionBytecode &function,
                                               const std::vector<RuntimeArgument> &args)
{
    if (args.size() > function.source_arity)
    {
        throw VmRuntimeError("VM: trop d'arguments pour '" + function.name + "'");
    }

    std::vector<Value> normalized;
    normalized.reserve(function.arity);
    for (std::size_t i = 0; i < function.source_arity; ++i)
    {
        if (i < args.size())
        {
            normalized.push_back(args[i].value);
        }
        else
        {
            if (i >= function.optional_params.size() || !function.optional_params[i])
            {
                throw VmRuntimeError("VM: argument manquant pour '" + function.name + "'");
            }
            normalized.push_back(Value::rien());
        }
    }
    for (std::size_t i = 0; i < function.source_arity; ++i)
    {
        if (i < function.optional_params.size() && function.optional_params[i])
        {
            normalized.push_back(Value::logique(i < args.size()));
        }
    }
    return normalized;
}

struct VmExecutionState
{
    const ModuleBytecode &module;
    const std::unordered_map<std::string, NativeFunction> &natives;
    const std::unordered_map<std::string, std::size_t> &function_indices;
    std::vector<Value> &globals;
    std::vector<bool> &global_defined;
    std::vector<bool> &initialized_functions;
};

Value run_frames(VmExecutionState &execution,
                 const std::size_t entry_function_index,
                 std::vector<Value> entry_arguments = {},
                 std::vector<std::shared_ptr<Value>> entry_captures = {},
                 const SourceLocation entry_call_site = {})
{
    const ModuleBytecode &module = execution.module;
    const auto &natives = execution.natives;
    const auto &function_indices = execution.function_indices;
    auto &globals = execution.globals;
    auto &global_defined = execution.global_defined;
    auto &initialized_functions = execution.initialized_functions;
    std::vector<Value> stack;
    std::vector<CallFrame> frames;
    VmRuntimeServices runtime_services;
    runtime_services.set_callback_executor([&](Value callee, const NativeArgs &args) {
        const auto function = callee.as_fonction();
        const auto body = std::dynamic_pointer_cast<VmClosureBody>(function->body);
        if (body == nullptr || args.arguments == nullptr)
        {
            throw VmRuntimeError("VM: fermeture bytecode invalide");
        }
        std::vector<Value> values = normalize_closure_arguments(module.functions[body->function_index],
                                                                *args.arguments);
        return run_frames(execution,
                          body->function_index,
                          std::move(values),
                          body->captures,
                          {static_cast<std::size_t>(args.site.line),
                           static_cast<std::size_t>(args.site.column)});
    });
    frames.push_back(make_call_frame(module,
                                     entry_function_index,
                                     entry_arguments,
                                     0,
                                     std::move(entry_captures),
                                     entry_call_site));

    const auto route_exception = [&stack, &frames](const Value &thrown) {
        while (!frames.empty())
        {
            CallFrame &target_frame = frames.back();
            if (!target_frame.handlers.empty())
            {
                const CallFrame::ExceptionHandler handler = target_frame.handlers.back();
                target_frame.handlers.pop_back();
                stack.resize(handler.stack_depth);
                stack.push_back(thrown);
                target_frame.ip = handler.target;
                return true;
            }
            stack.resize(target_frame.stack_base);
            frames.pop_back();
        }
        return false;
    };
    const auto build_runtime_error = [&frames](std::string message, const std::size_t opcode_offset)
    {
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
        std::vector<StackFrame> trace;
        trace.reserve(frames.size());
        for (const CallFrame &frame : frames)
        {
            trace.push_back({frame.function->name,
                             frame.function->source_path,
                             static_cast<std::uint32_t>(frame.call_site.line),
                             static_cast<std::uint32_t>(frame.call_site.column)});
        }
        return RuntimeError(std::move(message),
                            current.function->source_path,
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
            throw VmRuntimeError("VM: fonction terminee sans RETURN");
        }

        opcode_offset = ip;
        const Opcode opcode = read_opcode(chunk, ip);
        const SourceLocation dispatch_location = opcode_offset < chunk.locations.size()
                                                     ? chunk.locations[opcode_offset]
                                                     : SourceLocation{};
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
                throw VmRuntimeError("VM: variable globale introuvable: " + module.globals[index]);
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
                throw VmRuntimeError("VM: variable globale introuvable: " + module.globals[index]);
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
            const auto body = std::dynamic_pointer_cast<VmClosureBody>(globals[index].as_fonction()->body);
            if (body == nullptr || body->function_index >= initialized_functions.size())
            {
                throw VmRuntimeError("VM: corps d'initialiseur de module invalide");
            }
            if (initialized_functions[body->function_index])
            {
                break;
            }
            initialized_functions[body->function_index] = true;
            frames.push_back(make_call_frame(module,
                                             body->function_index,
                                             {},
                                             stack.size(),
                                             {},
                                             dispatch_location));
            break;
        }
        case Opcode::GET_LOCAL:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= locals.size())
            {
                throw VmRuntimeError("VM: index local invalide");
            }
            stack.push_back(*locals[index]);
            break;
        }
        case Opcode::SET_LOCAL:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= locals.size())
            {
                throw VmRuntimeError("VM: index local invalide");
            }
            *locals[index] = pop_value(stack);
            break;
        }
        case Opcode::GET_CAPTURE:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= frame.captures.size())
            {
                throw VmRuntimeError("VM: index de capture invalide");
            }
            stack.push_back(*frame.captures[index]);
            break;
        }
        case Opcode::SET_CAPTURE:
        {
            const std::uint8_t index = read_byte(chunk, ip);
            if (index >= frame.captures.size())
            {
                throw VmRuntimeError("VM: index de capture invalide");
            }
            *frame.captures[index] = pop_value(stack);
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
            auto body = std::make_shared<VmClosureBody>();
            body->function_index = function_index;
            body->captures.reserve(capture_count);
            for (std::size_t i = 0; i < capture_count; ++i)
            {
                const bool from_capture = read_byte(chunk, ip) != 0;
                const std::uint8_t source_index = read_byte(chunk, ip);
                const auto &source = from_capture ? frame.captures : frame.locals;
                if (source_index >= source.size())
                {
                    throw VmRuntimeError("VM: source de capture invalide");
                }
                body->captures.push_back(source[source_index]);
            }
            auto closure = std::make_shared<LumiereFunction>();
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
            auto klass = std::make_shared<LumiereClass>();
            klass->name = descriptor.name;
            auto body = std::make_shared<VmClassBody>();
            body->descriptor_index = descriptor_index;
            for (const VmMethodDescriptor &method : descriptor.methods)
            {
                auto &captures = body->method_captures[method.function_index];
                for (const VmMethodDescriptor::CaptureSource &source : method.capture_sources)
                {
                    const auto &cells = source.from_capture ? frame.captures : frame.locals;
                    if (source.index >= cells.size())
                    {
                        throw VmRuntimeError("VM: source de capture de methode invalide");
                    }
                    captures.push_back(cells[source.index]);
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
                klass->interfaces[interface_name] = interface_values[i].as_interface();
            }
            for (const VmMethodDescriptor &method : descriptor.methods)
            {
                const VmMethodDescriptor *parent_method = find_vm_method(module, klass->parent, method.name);
                if (method.is_override && parent_method == nullptr)
                {
                    throw VmRuntimeError("VM: remplace utilise sans methode parente correspondante: " + method.name);
                }
                if (!method.is_override && parent_method != nullptr)
                {
                    throw VmRuntimeError("VM: methode parente deja definie; utilisez remplace: " + method.name);
                }
                if (parent_method != nullptr &&
                    (parent_method->parameter_types != method.parameter_types ||
                     parent_method->return_type != method.return_type))
                {
                    throw VmRuntimeError("VM: la methode remplacee doit conserver la meme signature: " + method.name);
                }
            }
            for (const auto &[interface_name, interface] : klass->interfaces)
            {
                const auto interface_body = std::dynamic_pointer_cast<VmInterfaceBody>(interface->body);
                if (interface_body == nullptr || interface_body->descriptor_index >= module.interfaces.size())
                {
                    throw VmRuntimeError("VM: interface non compatible avec le backend: " + interface_name);
                }
                for (const VmInterfaceMethodDescriptor &required :
                     module.interfaces[interface_body->descriptor_index].methods)
                {
                    const VmMethodDescriptor *implemented = find_vm_method(module, klass, required.name);
                    if (implemented == nullptr)
                    {
                        throw VmRuntimeError("VM: la classe " + descriptor.name +
                                             " ne realise pas la methode requise " + interface_name + "." + required.name);
                    }
                    if (implemented->parameter_types != required.parameter_types ||
                        implemented->return_type != required.return_type)
                    {
                        throw VmRuntimeError("VM: la methode " + descriptor.name + "." + required.name +
                                             " ne respecte pas la signature requise par l'interface " + interface_name);
                    }
                    if (implemented->is_private)
                    {
                        throw VmRuntimeError("VM: la methode " + descriptor.name + "." + required.name +
                                             " ne peut pas etre privee car elle realise l'interface " + interface_name);
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
            auto interface = std::make_shared<LumiereInterface>();
            interface->name = module.interfaces[descriptor_index].name;
            auto body = std::make_shared<VmInterfaceBody>();
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
            auto name_space = std::make_shared<LumiereObject>();
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
        case Opcode::TRY_BEGIN:
        {
            const std::size_t target = read_u16(chunk, ip);
            if (target >= chunk.code.size())
            {
                throw VmRuntimeError("VM: gestionnaire d'exception invalide");
            }
            frame.handlers.push_back({target, stack.size()});
            break;
        }
        case Opcode::TRY_END:
            if (frame.handlers.empty())
            {
                throw VmRuntimeError("VM: TRY_END sans gestionnaire");
            }
            frame.handlers.pop_back();
            break;
        case Opcode::THROW:
        {
            const Value thrown = pop_value(stack);
            RuntimeError unhandled = build_runtime_error("exception non attrapee: " + thrown.to_string(),
                                                         opcode_offset);
            if (!route_exception(thrown))
            {
                throw unhandled;
            }
            break;
        }
        case Opcode::JUMP:
        {
            const std::size_t target = read_u16(chunk, ip);
            if (target >= chunk.code.size())
            {
                throw VmRuntimeError("VM: cible de saut invalide");
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
                                    "VM: comparaison '<' attend deux valeurs numeriques");
            break;
        case Opcode::LESS_EQUAL:
            execute_numeric_compare(stack, [](const auto left, const auto right) { return left <= right; },
                                    "VM: comparaison '<=' attend deux valeurs numeriques");
            break;
        case Opcode::GREATER:
            execute_numeric_compare(stack, [](const auto left, const auto right) { return left > right; },
                                    "VM: comparaison '>' attend deux valeurs numeriques");
            break;
        case Opcode::GREATER_EQUAL:
            execute_numeric_compare(stack, [](const auto left, const auto right) { return left >= right; },
                                    "VM: comparaison '>=' attend deux valeurs numeriques");
            break;
        case Opcode::CALL:
        {
            const std::uint8_t arity = read_byte(chunk, ip);
            std::vector<std::string> argument_names;
            argument_names.reserve(arity);
            for (std::size_t i = 0; i < arity; ++i)
            {
                const std::size_t name_index = read_u16(chunk, ip);
                if (name_index >= module.argument_names.size())
                {
                    throw VmRuntimeError("VM: index de nom d'argument invalide");
                }
                argument_names.push_back(module.argument_names[name_index]);
            }
            if (stack.size() < frame.stack_base + static_cast<std::size_t>(arity) + 1)
            {
                throw VmRuntimeError("VM: pile insuffisante pour l'appel");
            }

            const std::size_t callee_index = stack.size() - static_cast<std::size_t>(arity) - 1;
            const Value callee = stack[callee_index];
            std::vector<RuntimeArgument> call_args;
            call_args.reserve(arity);
            for (std::size_t i = callee_index + 1; i < stack.size(); ++i)
            {
                call_args.push_back({argument_names[i - callee_index - 1], stack[i]});
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
            const auto function = callee.is_fonction() ? callee.as_fonction() : nullptr;
            if (function != nullptr && !function->is_native())
            {
                auto body = std::dynamic_pointer_cast<VmClosureBody>(function->body);
                if (body == nullptr)
                {
                    throw VmRuntimeError("VM: corps de fonction non compatible avec le backend VM");
                }
                const FunctionBytecode &target = module.functions[body->function_index];
                std::vector<Value> values = normalize_closure_arguments(target, call_args);
                if (function->is_method())
                {
                    values.insert(values.begin(), function->receiver);
                }
                frames.push_back(make_call_frame(module,
                                                 body->function_index,
                                                 values,
                                                 stack.size(),
                                                 body->captures,
                                                 dispatch_location));
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
            std::vector<std::string> argument_names;
            argument_names.reserve(arity);
            for (std::size_t i = 0; i < arity; ++i)
            {
                const std::size_t name_index = read_u16(chunk, ip);
                if (name_index >= module.argument_names.size())
                {
                    throw VmRuntimeError("VM: index de nom d'argument invalide");
                }
                argument_names.push_back(module.argument_names[name_index]);
            }
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
                call_args.push_back({argument_names[i], stack[args_start + i]});
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
                std::vector<Value> values;
                values.reserve(call_args.size());
                for (const RuntimeArgument &argument : call_args)
                {
                    values.push_back(argument.value);
                }
                frames.push_back(make_call_frame(module,
                                                 direct->second,
                                                 values,
                                                 stack.size(),
                                                 {},
                                                 dispatch_location));
                break;
            }
            if (global_defined[global_index] && globals[global_index].is_fonction())
            {
                const auto function = globals[global_index].as_fonction();
                if (function->is_native())
                {
                    RuntimeSite site;
                    stack.push_back(runtime_services.call(globals[global_index],
                                                        NativeArgs{nullptr, &call_args, site}));
                    break;
                }
                const auto body = std::dynamic_pointer_cast<VmClosureBody>(function->body);
                if (body == nullptr)
                {
                    throw VmRuntimeError("VM: corps de fonction globale incompatible");
                }
                std::vector<Value> values = normalize_closure_arguments(module.functions[body->function_index],
                                                                        call_args);
                frames.push_back(make_call_frame(module,
                                                 body->function_index,
                                                 values,
                                                 stack.size(),
                                                 body->captures,
                                                 dispatch_location));
                break;
            }
            const auto native = natives.find(name);
            if (native != natives.end())
            {
                for (const RuntimeArgument &argument : call_args)
                {
                    if (!argument.name.empty())
                    {
                        throw VmRuntimeError("VM: " + name + " n'accepte pas d'arguments nommes");
                    }
                }
                std::vector<Value> values;
                values.reserve(call_args.size());
                for (const RuntimeArgument &argument : call_args)
                {
                    values.push_back(argument.value);
                }
                stack.push_back(native->second(values));
                break;
            }

            throw VmRuntimeError("VM: fonction globale inconnue '" + name + "'");
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
            std::vector<std::string> argument_names;
            argument_names.reserve(arity);
            for (std::size_t i = 0; i < arity; ++i)
            {
                const std::size_t name_index = read_u16(chunk, ip);
                if (name_index >= module.argument_names.size())
                {
                    throw VmRuntimeError("VM: index de nom d'argument invalide");
                }
                argument_names.push_back(module.argument_names[name_index]);
            }
            if (member_index >= module.members.size())
            {
                throw VmRuntimeError("VM: index de membre invalide");
            }
            if (stack.size() < frame.stack_base + static_cast<std::size_t>(arity) + 1)
            {
                throw VmRuntimeError("VM: pile insuffisante pour l'appel membre");
            }

            const std::size_t receiver_index = stack.size() - arity - 1;
            const Value receiver = stack[receiver_index];
            std::vector<RuntimeArgument> args;
            args.reserve(arity);
            for (std::size_t i = 0; i < arity; ++i)
            {
                args.push_back({argument_names[i], stack[receiver_index + 1 + i]});
            }
            stack.resize(receiver_index);

            if (receiver.is_objet())
            {
                const auto field = receiver.as_objet()->fields.find(module.members[member_index]);
                if (field != receiver.as_objet()->fields.end())
                {
                    if (!field->second.is_fonction())
                    {
                        throw VmRuntimeError("VM: le membre '" + module.members[member_index] + "' n'est pas appelable");
                    }
                    const auto function = field->second.as_fonction();
                    if (function->is_native())
                    {
                        RuntimeSite site;
                        stack.push_back(runtime_services.call(field->second,
                                                            NativeArgs{nullptr, &args, site}));
                        break;
                    }
                    const auto body = std::dynamic_pointer_cast<VmClosureBody>(function->body);
                    if (body == nullptr)
                    {
                        throw VmRuntimeError("VM: corps de membre incompatible");
                    }
                    std::vector<Value> values = normalize_closure_arguments(module.functions[body->function_index],
                                                                            args);
                    frames.push_back(make_call_frame(module,
                                                     body->function_index,
                                                     values,
                                                     stack.size(),
                                                     body->captures,
                                                     dispatch_location));
                    break;
                }
                const VmMethodDescriptor *method = find_vm_method(module,
                                                                  parent_dispatch
                                                                      ? receiver.as_objet()->klass->parent
                                                                      : receiver.as_objet()->klass,
                                                                  module.members[member_index]);
                if (method == nullptr)
                {
                    throw VmRuntimeError("VM: methode introuvable '" + module.members[member_index] + "'");
                }
                const bool private_access = !frame.locals.empty() &&
                                            frame.function->name.find('.') != std::string::npos &&
                                            frame.locals[0]->is_objet() &&
                                            frame.locals[0]->as_objet() == receiver.as_objet();
                if (method->is_private && !private_access)
                {
                    throw VmRuntimeError("VM: acces interdit a la methode privee '" + method->name + "'");
                }
                const FunctionBytecode &target = module.functions[method->function_index];
                std::vector<Value> method_args = normalize_closure_arguments(target, args);
                method_args.insert(method_args.begin(), receiver);
                frames.push_back(make_call_frame(module,
                                                 method->function_index,
                                                 method_args,
                                                 stack.size(),
                                                 find_vm_method_captures(receiver.as_objet()->klass,
                                                                         method->function_index),
                                                 dispatch_location));
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
                throw VmRuntimeError("VM: pile insuffisante pour l'acces membre");
            }
            const Value receiver = pop_value(stack);
            if (receiver.is_objet())
            {
                const auto field = receiver.as_objet()->fields.find(module.members[member_index]);
                if (field != receiver.as_objet()->fields.end())
                {
                    const VmFieldDescriptor *descriptor = find_vm_field(module,
                                                                        receiver.as_objet()->klass,
                                                                        module.members[member_index]);
                    const bool private_access = !frame.locals.empty() &&
                                                frame.function->name.find('.') != std::string::npos &&
                                                frame.locals[0]->is_objet() &&
                                                frame.locals[0]->as_objet() == receiver.as_objet();
                    if (descriptor != nullptr && descriptor->is_private && !private_access)
                    {
                        throw VmRuntimeError("VM: acces interdit au champ prive '" + descriptor->name + "'");
                    }
                    stack.push_back(field->second);
                    break;
                }
                const VmMethodDescriptor *method = find_vm_method(module,
                                                                  parent_dispatch
                                                                      ? receiver.as_objet()->klass->parent
                                                                      : receiver.as_objet()->klass,
                                                                  module.members[member_index]);
                if (method == nullptr)
                {
                    throw VmRuntimeError("VM: membre introuvable '" + module.members[member_index] + "'");
                }
                const bool private_access = !frame.locals.empty() &&
                                            frame.function->name.find('.') != std::string::npos &&
                                            frame.locals[0]->is_objet() &&
                                            frame.locals[0]->as_objet() == receiver.as_objet();
                if (method->is_private && !private_access)
                {
                    throw VmRuntimeError("VM: acces interdit a la methode privee '" + method->name + "'");
                }
                auto body = std::make_shared<VmClosureBody>();
                body->function_index = method->function_index;
                body->captures = find_vm_method_captures(receiver.as_objet()->klass,
                                                         method->function_index);
                auto function = std::make_shared<LumiereFunction>();
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
            const VmFieldDescriptor *field = find_vm_field(module,
                                                           receiver.as_objet()->klass,
                                                           module.members[member_index]);
            if (field == nullptr)
            {
                throw VmRuntimeError("VM: champ introuvable '" + module.members[member_index] + "'");
            }
            const bool private_access = !frame.locals.empty() &&
                                        frame.function->name.find('.') != std::string::npos &&
                                        frame.locals[0]->is_objet() &&
                                        frame.locals[0]->as_objet() == receiver.as_objet();
            if (field->is_private && !private_access)
            {
                throw VmRuntimeError("VM: affectation interdite au champ prive '" + field->name + "'");
            }
            if (field->is_fixed)
            {
                throw VmRuntimeError("VM: impossible d'affecter le champ fixe '" + field->name + "'");
            }
            if (!field->type.empty() && !matches_type_name(value, field->type))
            {
                throw VmRuntimeError("VM: le champ '" + field->name + "' attend " + field->type);
            }
            receiver.as_objet()->fields[field->name] = value;
            stack.push_back(value);
            break;
        }
        case Opcode::LIST:
            execute_list(stack, read_byte(chunk, ip));
            break;
        case Opcode::DICTIONARY:
            execute_dictionary(stack, read_byte(chunk, ip));
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
            if (index >= module.types.size())
            {
                throw VmRuntimeError("VM: index de type invalide");
            }
            execute_type_assertion(stack, module.types[index], runtime_services);
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
        case Opcode::RETURN:
        {
            Value result = stack.size() > frame.stack_base ? stack.back() : Value::rien();
            stack.resize(frame.stack_base);
            frames.pop_back();
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
            RuntimeError unhandled = build_runtime_error(error.what(), opcode_offset);
            if (!route_exception(Value::texte(error.what())))
            {
                throw unhandled;
            }
        }
    }

    return Value::rien();
}

} // namespace

void VM::execute(Program &program)
{
    VmCompiler compiler;
    ModuleBytecode module = compiler.compile(program);
    static_cast<void>(run(module));
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
        module, natives, function_indices, globals, global_defined, initialized_functions};
    for (std::size_t i = 0; i < module.globals.size(); ++i)
    {
        const std::string &name = module.globals[i];
        if (const auto function = function_indices.find(name); function != function_indices.end())
        {
            auto body = std::make_shared<VmClosureBody>();
            body->function_index = function->second;
            auto closure = std::make_shared<LumiereFunction>();
            closure->name = name;
            closure->body = std::move(body);
            globals[i] = Value::fonction(std::move(closure));
            global_defined[i] = true;
        }
        else if (const auto native = natives.find(name); native != natives.end())
        {
            auto callable = std::make_shared<LumiereFunction>();
            callable->name = name;
            callable->min_arity = 0;
            callable->max_arity = 255;
            const NativeFunction handler = native->second;
            callable->native_handler = [handler, name](IRuntime &, const NativeArgs &args) {
                std::vector<Value> values;
                values.reserve(args.arguments->size());
                for (const RuntimeArgument &argument : *args.arguments)
                {
                    if (!argument.name.empty())
                    {
                        throw VmRuntimeError("VM: arguments nommes ne sont pas pris en charge pour '" + name + "'");
                    }
                    values.push_back(argument.value);
                }
                return handler(values);
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
