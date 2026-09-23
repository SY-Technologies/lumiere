#include "lumiere/interpreter/runtime/conversions.hpp"

#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/runtime/nominal_type.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"
#include "lumiere/parser/utf8.hpp"

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace lumiere
{
namespace
{

[[noreturn]] void fail(IRuntime &runtime, const RuntimeSite &site, const std::string &message)
{
    runtime.raise_runtime_error(site, message);
    // The interface forbids returning; every caller below relies on it.
    std::abort();
}

// The operand's type has no conversion to the target at all. Naming both is
// what the conversion of an unreadable text already did, so every refusal of
// `en` now reads the same way.
[[noreturn]] void refuse(IRuntime &runtime, const RuntimeSite &site,
                         const Value &operand, const std::string_view target)
{
    fail(runtime, site, messages::conversion_impossible(display_runtime_type(target), operand.type_name()));
}

Value to_entier(IRuntime &runtime, const Value &operand, const RuntimeSite &site)
{
    if (operand.is_entier())
    {
        return operand;
    }
    if (operand.is_decimal())
    {
        const auto value = numeric::to_integer(operand.as_decimal());
        if (!value)
        {
            fail(runtime, site, "valeur hors limites pour Entier");
        }
        return Value::entier(*value);
    }
    if (operand.is_symbole())
    {
        return Value::entier(static_cast<std::int64_t>(operand.as_symbole()));
    }
    if (operand.is_texte())
    {
        const std::string &text = operand.as_texte();
        std::size_t consumed = 0;
        std::int64_t value = 0;
        try
        {
            value = std::stoll(text, &consumed);
        }
        catch (const std::logic_error &)
        {
            consumed = 0;
        }
        if (consumed == 0 || consumed != text.size())
        {
            fail(runtime, site, messages::conversion_impossible("Entier", "Texte"));
        }
        return Value::entier(value);
    }
    refuse(runtime, site, operand, "Entier");
}

Value to_decimal(IRuntime &runtime, const Value &operand, const RuntimeSite &site)
{
    if (operand.is_decimal())
    {
        return operand;
    }
    if (operand.is_entier())
    {
        return Value::decimal(static_cast<double>(operand.as_entier()));
    }
    if (operand.is_texte())
    {
        const auto value = numeric::parse_decimal(operand.as_texte());
        if (!value)
        {
            fail(runtime, site, messages::conversion_impossible("Décimal", "Texte"));
        }
        return Value::decimal(*value);
    }
    refuse(runtime, site, operand, "Décimal");
}

Value to_logique(IRuntime &runtime, const Value &operand, const RuntimeSite &site)
{
    if (operand.is_logique())
    {
        return operand;
    }
    if (operand.is_texte())
    {
        if (operand.as_texte() == "vrai")
        {
            return Value::logique(true);
        }
        if (operand.as_texte() == "faux")
        {
            return Value::logique(false);
        }
        fail(runtime, site, "conversion vers Logique impossible: le texte doit valoir 'vrai' ou 'faux'");
    }
    refuse(runtime, site, operand, "Logique");
}

Value to_symbole(IRuntime &runtime, const Value &operand, const RuntimeSite &site)
{
    if (operand.is_symbole())
    {
        return operand;
    }
    if (operand.is_entier())
    {
        const std::int64_t code_point = operand.as_entier();
        if (code_point < 0 || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF))
        {
            fail(runtime, site, "conversion vers Symbole impossible: le point de code Unicode est invalide");
        }
        return Value::symbole(static_cast<char32_t>(code_point));
    }
    if (operand.is_texte())
    {
        const auto character = utf8::decode_single_character(operand.as_texte());
        if (!character)
        {
            fail(runtime, site, "conversion vers Symbole impossible: le texte doit contenir exactement un caractère");
        }
        return Value::symbole(*character);
    }
    refuse(runtime, site, operand, "Symbole");
}

} // namespace

bool is_conversion_target(const std::string_view type_name)
{
    return type_name == "Entier" || type_name == "Décimal" || type_name == "Decimal" ||
           type_name == "Logique" || type_name == "Symbole" || type_name == "Texte" ||
           type_name == "Universel";
}

Value convert(IRuntime &runtime, const Value &operand, const std::string_view target, const RuntimeSite &site)
{
    if (target == "Entier")
    {
        return to_entier(runtime, operand, site);
    }
    if (target == "Décimal" || target == "Decimal")
    {
        return to_decimal(runtime, operand, site);
    }
    if (target == "Logique")
    {
        return to_logique(runtime, operand, site);
    }
    if (target == "Symbole")
    {
        return to_symbole(runtime, operand, site);
    }
    if (target == "Texte")
    {
        return Value::texte(runtime.to_text(operand));
    }
    if (target == "Universel")
    {
        return operand;
    }
    // Analysis refuses every other target, so a program cannot get here; a
    // module built some other way still gets a sentence rather than a crash.
    refuse(runtime, site, operand, target);
}

} // namespace lumiere
