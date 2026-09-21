#include "lumiere/diagnostics/runtime_messages.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/parser/utf8.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace lumiere
{

namespace
{

std::vector<std::size_t> character_offsets(IRuntime &runtime, const std::string &text,
                                          const RuntimeSite &site)
{
    std::vector<std::size_t> offsets;
    std::size_t offset = 0;
    while (offset < text.size())
    {
        offsets.push_back(offset);
        char32_t character;
        const auto next = utf8::decode_one(text, offset, character);
        if (!next)
            runtime.raise_runtime_error(site, "texte UTF-8 invalide");
        offset = *next;
    }
    offsets.push_back(text.size());
    return offsets;
}

// These trim helpers are plain C++ string utilities. They intentionally use
// std::isspace over raw bytes, so the whitespace rules here are C/C++ rules,
// not a richer Unicode-aware text model.
std::string trim_left_copy(const std::string &text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }
    return text.substr(begin);
}

std::string trim_right_copy(const std::string &text)
{
    std::size_t end = text.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }
    return text.substr(0, end);
}

std::string trim_copy(const std::string &text)
{
    return trim_right_copy(trim_left_copy(text));
}

Ref<ListeData> split_text_items(const std::string &text, const std::string &separator)
{
    auto items = make_ref<ListeData>();
    std::size_t start = 0;
    while (true)
    {
        const std::size_t pos = text.find(separator, start);
        if (pos == std::string::npos)
        {
            items->elements.push_back(Value::texte(text.substr(start)));
            break;
        }
        items->elements.push_back(Value::texte(text.substr(start, pos - start)));
        start = pos + separator.size();
    }
    return items;
}

std::string replace_all_copy(const std::string &text,
                             const std::string &needle,
                             const std::string &replacement)
{
    std::string result = text;
    std::size_t position = 0;
    while ((position = result.find(needle, position)) != std::string::npos)
    {
        result.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
    return result;
}

std::string replace_first_copy(const std::string &text,
                               const std::string &needle,
                               const std::string &replacement)
{
    std::string result = text;
    const std::size_t position = result.find(needle);
    if (position != std::string::npos)
    {
        result.replace(position, needle.size(), replacement);
    }
    return result;
}

Value execute_texte_operation(IRuntime &runtime,
                              const std::string &text,
                              const std::string &operation,
                              const std::vector<RuntimeArgument> &args,
                              const RuntimeSite &call_site)
{
    if (operation == "taille")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.taille", call_site);
        const auto count = utf8::character_count(text);
        if (!count)
            runtime.raise_runtime_error(call_site, "texte UTF-8 invalide");
        return Value::entier(static_cast<int64_t>(*count));
    }
    if (operation == "est_vide")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.est_vide", call_site);
        return Value::logique(text.empty());
    }
    if (operation == "contient")
    {
        stdlib_expect_positional(runtime, args, 1, "Texte.contient", call_site);
        const std::string needle = stdlib_expect_text(runtime, args[0].value, "Texte.contient", call_site);
        return Value::logique(text.find(needle) != std::string::npos);
    }
    if (operation == "index_de")
    {
        stdlib_expect_positional(runtime, args, 1, "Texte.index_de", call_site);
        const std::string needle = stdlib_expect_text(runtime, args[0].value, "Texte.index_de", call_site);
        const std::size_t pos = text.find(needle);
        if (pos == std::string::npos)
            return Value::entier(-1);
        const auto count = utf8::character_count(std::string_view(text).substr(0, pos));
        if (!count || !utf8::character_count(needle))
            runtime.raise_runtime_error(call_site, "texte UTF-8 invalide");
        return Value::entier(static_cast<int64_t>(*count));
    }
    if (operation == "commence_par")
    {
        stdlib_expect_positional(runtime, args, 1, "Texte.commence_par", call_site);
        const std::string prefix = stdlib_expect_text(runtime, args[0].value, "Texte.commence_par", call_site);
        return Value::logique(text.rfind(prefix, 0) == 0);
    }
    if (operation == "finit_par")
    {
        stdlib_expect_positional(runtime, args, 1, "Texte.finit_par", call_site);
        const std::string suffix = stdlib_expect_text(runtime, args[0].value, "Texte.finit_par", call_site);
        return Value::logique(text.size() >= suffix.size() &&
                              text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0);
    }
    if (operation == "separer")
    {
        stdlib_expect_positional(runtime, args, 1, "Texte.separer", call_site);
        const std::string separator = stdlib_expect_text(runtime, args[0].value, "Texte.separer", call_site);
        if (separator.empty())
        {
            runtime.raise_runtime_error(call_site, "Texte.separer attend un séparateur non vide");
        }
        Value result = Value::liste(split_text_items(text, separator));
        runtime.annotate_value(result, "Liste[Texte]", call_site);
        return result;
    }
    if (operation == "separer_lignes")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.separer_lignes", call_site);
        Value result = Value::liste(split_text_items(text, "\n"));
        runtime.annotate_value(result, "Liste[Texte]", call_site);
        return result;
    }
    if (operation == "remplacer")
    {
        stdlib_expect_positional(runtime, args, 2, "Texte.remplacer", call_site);
        const std::string needle = stdlib_expect_text(runtime, args[0].value, "Texte.remplacer", call_site);
        const std::string replacement = stdlib_expect_text(runtime, args[1].value, "Texte.remplacer", call_site);
        if (needle.empty())
        {
            runtime.raise_runtime_error(call_site, "Texte.remplacer attend une cible non vide");
        }
        return Value::texte(replace_first_copy(text, needle, replacement));
    }
    if (operation == "remplacer_tout")
    {
        stdlib_expect_positional(runtime, args, 2, "Texte.remplacer_tout", call_site);
        const std::string needle = stdlib_expect_text(runtime, args[0].value, "Texte.remplacer_tout", call_site);
        const std::string replacement = stdlib_expect_text(runtime, args[1].value, "Texte.remplacer_tout", call_site);
        if (needle.empty())
        {
            runtime.raise_runtime_error(call_site, "Texte.remplacer_tout attend une cible non vide");
        }
        return Value::texte(replace_all_copy(text, needle, replacement));
    }
    if (operation == "elaguer")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.elaguer", call_site);
        return Value::texte(trim_copy(text));
    }
    if (operation == "elaguer_gauche")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.elaguer_gauche", call_site);
        return Value::texte(trim_left_copy(text));
    }
    if (operation == "elaguer_droite")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.elaguer_droite", call_site);
        return Value::texte(trim_right_copy(text));
    }
    if (operation == "minuscules")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.minuscules", call_site);
        std::string result = text;
        for (char &ch : result)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return Value::texte(std::move(result));
    }
    if (operation == "majuscules")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.majuscules", call_site);
        std::string result = text;
        for (char &ch : result)
        {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return Value::texte(std::move(result));
    }
    if (operation == "inverser")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.inverser", call_site);
        const auto offsets = character_offsets(runtime, text, call_site);
        std::string reversed;
        reversed.reserve(text.size());
        for (std::size_t i = offsets.size() - 1; i > 0; --i)
            reversed.append(text, offsets[i - 1], offsets[i] - offsets[i - 1]);
        return Value::texte(std::move(reversed));
    }
    if (operation == "repeter")
    {
        stdlib_expect_positional(runtime, args, 1, "Texte.repeter", call_site);
        const int64_t count = stdlib_expect_integer(runtime, args[0].value, "Texte.repeter", call_site);
        if (count < 0)
        {
            runtime.raise_runtime_error(call_site, "Texte.repeter attend un nombre non négatif");
        }
        constexpr int64_t kMaxRepeatBytes = 10 * 1024 * 1024;
        if (text.empty() || count == 0)
            return Value::texte("");
        if (count > kMaxRepeatBytes / static_cast<int64_t>(text.size()))
        {
            runtime.raise_runtime_error(call_site, "Texte.repeter: le résultat dépasse la taille maximale autorisee");
        }
        std::string result;
        result.reserve(text.size() * static_cast<std::size_t>(count));
        for (int64_t i = 0; i < count; ++i)
        {
            result += text;
        }
        return Value::texte(std::move(result));
    }
    if (operation == "inserer")
    {
        stdlib_expect_positional(runtime, args, 2, "Texte.inserer", call_site);
        const int64_t position = stdlib_expect_integer(runtime, args[0].value, "Texte.inserer", call_site);
        const std::string fragment = stdlib_expect_text(runtime, args[1].value, "Texte.inserer", call_site);
        const auto offsets = character_offsets(runtime, text, call_site);
        if (position < 0 || static_cast<std::size_t>(position) >= offsets.size())
        {
            runtime.raise_runtime_error(call_site, "position d'insertion hors limites");
        }
        std::string result = text;
        if (!utf8::character_count(fragment))
            runtime.raise_runtime_error(call_site, "texte UTF-8 invalide");
        result.insert(offsets[static_cast<std::size_t>(position)], fragment);
        return Value::texte(std::move(result));
    }
    if (operation == "supprimer")
    {
        stdlib_expect_positional(runtime, args, 2, "Texte.supprimer", call_site);
        const int64_t debut = stdlib_expect_integer(runtime, args[0].value, "Texte.supprimer", call_site);
        const int64_t longueur = stdlib_expect_integer(runtime, args[1].value, "Texte.supprimer", call_site);
        const auto offsets = character_offsets(runtime, text, call_site);
        const auto count = offsets.size() - 1;
        if (debut < 0 || longueur < 0 || static_cast<std::size_t>(debut) > count)
        {
            runtime.raise_runtime_error(call_site, "suppression hors limites");
        }
        std::string result = text;
        const auto begin = static_cast<std::size_t>(debut);
        const auto length = std::min(static_cast<std::size_t>(longueur), count - begin);
        result.erase(offsets[begin], offsets[begin + length] - offsets[begin]);
        return Value::texte(std::move(result));
    }
    if (operation == "sous_texte")
    {
        stdlib_expect_positional_range(runtime, args, 1, 2, "Texte.sous_texte", call_site);
        const int64_t debut = stdlib_expect_integer(runtime, args[0].value, "Texte.sous_texte", call_site);
        const auto offsets = character_offsets(runtime, text, call_site);
        const auto count = offsets.size() - 1;
        if (debut < 0 || static_cast<std::size_t>(debut) > count)
        {
            runtime.raise_runtime_error(call_site, "indice de debut hors limites");
        }
        if (args.size() == 1)
        {
            return Value::texte(text.substr(offsets[static_cast<std::size_t>(debut)]));
        }
        const int64_t longueur = stdlib_expect_integer(runtime, args[1].value, "Texte.sous_texte", call_site);
        if (longueur < 0)
        {
            runtime.raise_runtime_error(call_site, "longueur négative interdite");
        }
        if (static_cast<std::size_t>(longueur) > count - static_cast<std::size_t>(debut))
        {
            runtime.raise_runtime_error(call_site, "sous_texte: la longueur dépasse la taille du texte");
        }
        const auto begin = static_cast<std::size_t>(debut);
        const auto end = begin + static_cast<std::size_t>(longueur);
        return Value::texte(text.substr(offsets[begin], offsets[end] - offsets[begin]));
    }
    if (operation == "en_entier")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.en_entier", call_site);
        try
        {
            std::size_t consumed = 0;
            const long long value = std::stoll(text, &consumed);
            if (consumed != text.size())
            {
                return stdlib_failure(
                    stdlib_error_value(
                        "Texte.ErreurConversion",
                        "en_entier",
                        "le texte ne représente pas un Entier valide"),
                    call_site);
            }
            return stdlib_success(
                Value::entier(static_cast<int64_t>(value)));
        }
        catch (const std::exception &)
        {
            return stdlib_failure(
                stdlib_error_value(
                    "Texte.ErreurConversion",
                    "en_entier",
                    "le texte ne représente pas un Entier valide"),
                call_site);
        }
    }
    if (operation == "en_decimal")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.en_decimal", call_site);
        if (const auto value = numeric::parse_decimal(text))
        {
            return stdlib_success(Value::decimal(*value));
        }
        return stdlib_failure(
            stdlib_error_value(
                "Texte.ErreurConversion",
                "en_decimal",
                "le texte ne représente pas un Décimal valide"),
            call_site);
    }
    if (operation == "en_logique")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.en_logique", call_site);
        if (text == "vrai")
        {
            return stdlib_success(Value::logique(true));
        }
        if (text == "faux")
        {
            return stdlib_success(Value::logique(false));
        }
        return stdlib_failure(
            stdlib_error_value(
                "Texte.ErreurConversion",
                "en_logique",
                "le texte doit valoir 'vrai' ou 'faux'"),
            call_site);
    }

    runtime.raise_runtime_error(call_site, messages::membre_introuvable(operation, "Texte"));
    return Value::rien();
}

void bind_texte_module_adapter(Module &module,
                               const NativeFunctionFactory &make_native_function,
                               const std::string &name)
{
    stdlib_bind_public_function(
        module,
        make_native_function,
        name,
        [name](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            if (args.empty())
            {
                runtime.raise_runtime_error(native_args.site, "Texte." + name + " attend au moins 1 argument");
            }
            if (!args[0].name.empty())
            {
                runtime.raise_runtime_error(native_args.site, "Texte." + name + " n'accepte pas d'arguments nommés");
            }
            const std::string text = stdlib_expect_text(runtime, args[0].value, "Texte." + name, native_args.site);
            std::vector<RuntimeArgument> remaining(args.begin() + 1, args.end());
            return execute_texte_operation(runtime, text, name, remaining, native_args.site);
        });
}

} // namespace

Value execute_texte_member(IRuntime &runtime,
                           const Value &receiver,
                           const std::string_view member_name,
                           const std::vector<RuntimeArgument> &args,
                           const RuntimeSite &call_site)
{
    if (!receiver.is_texte())
    {
        runtime.raise_runtime_error(call_site, "une valeur Texte est attendue pour l'appel membre");
    }
    return execute_texte_operation(runtime,
                                   receiver.as_texte(),
                                   std::string(member_name),
                                   args,
                                   call_site);
}

void register_texte_module(Module &module)
{
    const auto &make_native_function = native_function_factory();
    auto error_class = make_ref<LumiereClass>();
    error_class->name = "Texte.ErreurConversion";
    stdlib_bind_public_value(
        module,
        "ErreurConversion",
        Value::classe(std::move(error_class)));
    for (const char *name : {"taille", "est_vide", "contient", "index_de", "commence_par", "finit_par",
                             "separer", "separer_lignes", "remplacer", "elaguer", "elaguer_gauche",
                             "elaguer_droite", "minuscules", "majuscules", "inverser", "repeter",
                             "inserer", "supprimer", "sous_texte", "en_entier", "en_decimal", "en_logique",
                             "remplacer_tout"})
    {
        bind_texte_module_adapter(module, make_native_function, name);
    }

    stdlib_bind_public_function(
        module,
        make_native_function,
        "joindre",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 2, "Texte.joindre", native_args.site);
            const Value values = args[0].value;
            const std::string separator = stdlib_expect_text(runtime, args[1].value, "Texte.joindre", native_args.site);
            if (!values.is_liste())
            {
                runtime.raise_runtime_error(native_args.site, "Texte.joindre attend une Liste");
            }
            std::string out;
            const auto list = values.as_liste();
            for (std::size_t i = 0; i < list->elements.size(); ++i)
            {
                if (i > 0)
                {
                    out += separator;
                }
                out += stdlib_expect_text(runtime, list->elements[i], "Texte.joindre", native_args.site);
            }
            return Value::texte(std::move(out));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "convertir_entier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Texte.convertir_entier", native_args.site);
            return Value::texte(std::to_string(stdlib_expect_integer(runtime, args[0].value, "Texte.convertir_entier", native_args.site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "convertir_decimal",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Texte.convertir_decimal", native_args.site);
            // The language has one way of writing a Décimal, and this is not a
            // second one. A stream's default is six significant digits, so this
            // turned 123456789.125 into "1.23457e+08", 0.1 + 0.2 into "0.3",
            // and 2.0 into "2" -- losing precision, reporting a number that was
            // not computed, and losing the type -- while afficher printed all
            // three correctly. Both engines were wrong in the same way, so
            // nothing comparing them could see it.
            return Value::texte(numeric::decimal_to_text(
                stdlib_expect_decimal(runtime, args[0].value, "Texte.convertir_decimal", native_args.site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "convertir_logique",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            stdlib_expect_positional(runtime, args, 1, "Texte.convertir_logique", native_args.site);
            if (!args[0].value.is_logique())
            {
                runtime.raise_runtime_error(native_args.site, "Texte.convertir_logique attend un Logique");
            }
            return Value::texte(args[0].value.as_logique() ? "vrai" : "faux");
        });
}

bool try_resolve_texte_native_member(const Value &object,
                                     std::string_view member_name,
                                     const NativeMethodFactory &make_native_method,
                                     Value &result)
{
    if (!object.is_texte())
    {
        return false;
    }

    static const std::unordered_set<std::string> operations = {
        "taille",         "est_vide",      "contient",      "index_de",     "commence_par", "finit_par",
        "separer",        "separer_lignes","remplacer",     "remplacer_tout","elaguer",      "elaguer_gauche",
        "elaguer_droite", "minuscules",    "majuscules",    "inverser",     "repeter",      "inserer",
        "supprimer",      "sous_texte",    "en_entier",     "en_decimal",   "en_logique"};

    if (!operations.contains(std::string(member_name)))
    {
        return false;
    }

    const std::string text = object.as_texte();
    result = Value::fonction(make_native_method(
        object,
        [text, operation = std::string(member_name)](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            return execute_texte_operation(runtime, text, operation, *native_args.arguments, native_args.site);
        }));
    return true;
}

} // namespace lumiere
