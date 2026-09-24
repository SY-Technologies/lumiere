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

/**
 * @brief Unicode simple case mapping (upper codepoint, lower codepoint),
 * sorted by upper codepoint. Covers ASCII, Latin-1 Supplement, and Latin
 * Extended-A -- French, and by extension the rest of Western Europe's Latin
 * alphabets -- generated from Unicode's own case tables rather than
 * hand-derived, since Latin Extended-A alone mixes two different even/odd
 * pairing conventions across its sub-ranges and getting that wrong silently
 * is worse than the bug this replaces.
 *
 * Left out on purpose because they are not one-codepoint-to-one-codepoint:
 * U+00DF (ß, uppercases to "SS"), U+0130/U+0131 (Turkish dotted/dotless I,
 * whose default-locale uppercase collides with plain 'I'), and U+017F (ſ,
 * long s, which also uppercases to plain 'S'). Mapping any of those would
 * either change the string's length or make one letter's case reversible two
 * different ways -- both out of scope for a per-character fold.
 */
struct CasePair
{
    char32_t upper;
    char32_t lower;
};
constexpr CasePair kCasePairs[] = {
    { 0x0041, 0x0061 },  // A a
    { 0x0042, 0x0062 },  // B b
    { 0x0043, 0x0063 },  // C c
    { 0x0044, 0x0064 },  // D d
    { 0x0045, 0x0065 },  // E e
    { 0x0046, 0x0066 },  // F f
    { 0x0047, 0x0067 },  // G g
    { 0x0048, 0x0068 },  // H h
    { 0x0049, 0x0069 },  // I i
    { 0x004A, 0x006A },  // J j
    { 0x004B, 0x006B },  // K k
    { 0x004C, 0x006C },  // L l
    { 0x004D, 0x006D },  // M m
    { 0x004E, 0x006E },  // N n
    { 0x004F, 0x006F },  // O o
    { 0x0050, 0x0070 },  // P p
    { 0x0051, 0x0071 },  // Q q
    { 0x0052, 0x0072 },  // R r
    { 0x0053, 0x0073 },  // S s
    { 0x0054, 0x0074 },  // T t
    { 0x0055, 0x0075 },  // U u
    { 0x0056, 0x0076 },  // V v
    { 0x0057, 0x0077 },  // W w
    { 0x0058, 0x0078 },  // X x
    { 0x0059, 0x0079 },  // Y y
    { 0x005A, 0x007A },  // Z z
    { 0x00C0, 0x00E0 },  // À à
    { 0x00C1, 0x00E1 },  // Á á
    { 0x00C2, 0x00E2 },  // Â â
    { 0x00C3, 0x00E3 },  // Ã ã
    { 0x00C4, 0x00E4 },  // Ä ä
    { 0x00C5, 0x00E5 },  // Å å
    { 0x00C6, 0x00E6 },  // Æ æ
    { 0x00C7, 0x00E7 },  // Ç ç
    { 0x00C8, 0x00E8 },  // È è
    { 0x00C9, 0x00E9 },  // É é
    { 0x00CA, 0x00EA },  // Ê ê
    { 0x00CB, 0x00EB },  // Ë ë
    { 0x00CC, 0x00EC },  // Ì ì
    { 0x00CD, 0x00ED },  // Í í
    { 0x00CE, 0x00EE },  // Î î
    { 0x00CF, 0x00EF },  // Ï ï
    { 0x00D0, 0x00F0 },  // Ð ð
    { 0x00D1, 0x00F1 },  // Ñ ñ
    { 0x00D2, 0x00F2 },  // Ò ò
    { 0x00D3, 0x00F3 },  // Ó ó
    { 0x00D4, 0x00F4 },  // Ô ô
    { 0x00D5, 0x00F5 },  // Õ õ
    { 0x00D6, 0x00F6 },  // Ö ö
    { 0x00D8, 0x00F8 },  // Ø ø
    { 0x00D9, 0x00F9 },  // Ù ù
    { 0x00DA, 0x00FA },  // Ú ú
    { 0x00DB, 0x00FB },  // Û û
    { 0x00DC, 0x00FC },  // Ü ü
    { 0x00DD, 0x00FD },  // Ý ý
    { 0x00DE, 0x00FE },  // Þ þ
    { 0x0100, 0x0101 },  // Ā ā
    { 0x0102, 0x0103 },  // Ă ă
    { 0x0104, 0x0105 },  // Ą ą
    { 0x0106, 0x0107 },  // Ć ć
    { 0x0108, 0x0109 },  // Ĉ ĉ
    { 0x010A, 0x010B },  // Ċ ċ
    { 0x010C, 0x010D },  // Č č
    { 0x010E, 0x010F },  // Ď ď
    { 0x0110, 0x0111 },  // Đ đ
    { 0x0112, 0x0113 },  // Ē ē
    { 0x0114, 0x0115 },  // Ĕ ĕ
    { 0x0116, 0x0117 },  // Ė ė
    { 0x0118, 0x0119 },  // Ę ę
    { 0x011A, 0x011B },  // Ě ě
    { 0x011C, 0x011D },  // Ĝ ĝ
    { 0x011E, 0x011F },  // Ğ ğ
    { 0x0120, 0x0121 },  // Ġ ġ
    { 0x0122, 0x0123 },  // Ģ ģ
    { 0x0124, 0x0125 },  // Ĥ ĥ
    { 0x0126, 0x0127 },  // Ħ ħ
    { 0x0128, 0x0129 },  // Ĩ ĩ
    { 0x012A, 0x012B },  // Ī ī
    { 0x012C, 0x012D },  // Ĭ ĭ
    { 0x012E, 0x012F },  // Į į
    { 0x0132, 0x0133 },  // Ĳ ĳ
    { 0x0134, 0x0135 },  // Ĵ ĵ
    { 0x0136, 0x0137 },  // Ķ ķ
    { 0x0139, 0x013A },  // Ĺ ĺ
    { 0x013B, 0x013C },  // Ļ ļ
    { 0x013D, 0x013E },  // Ľ ľ
    { 0x013F, 0x0140 },  // Ŀ ŀ
    { 0x0141, 0x0142 },  // Ł ł
    { 0x0143, 0x0144 },  // Ń ń
    { 0x0145, 0x0146 },  // Ņ ņ
    { 0x0147, 0x0148 },  // Ň ň
    { 0x014A, 0x014B },  // Ŋ ŋ
    { 0x014C, 0x014D },  // Ō ō
    { 0x014E, 0x014F },  // Ŏ ŏ
    { 0x0150, 0x0151 },  // Ő ő
    { 0x0152, 0x0153 },  // Œ œ
    { 0x0154, 0x0155 },  // Ŕ ŕ
    { 0x0156, 0x0157 },  // Ŗ ŗ
    { 0x0158, 0x0159 },  // Ř ř
    { 0x015A, 0x015B },  // Ś ś
    { 0x015C, 0x015D },  // Ŝ ŝ
    { 0x015E, 0x015F },  // Ş ş
    { 0x0160, 0x0161 },  // Š š
    { 0x0162, 0x0163 },  // Ţ ţ
    { 0x0164, 0x0165 },  // Ť ť
    { 0x0166, 0x0167 },  // Ŧ ŧ
    { 0x0168, 0x0169 },  // Ũ ũ
    { 0x016A, 0x016B },  // Ū ū
    { 0x016C, 0x016D },  // Ŭ ŭ
    { 0x016E, 0x016F },  // Ů ů
    { 0x0170, 0x0171 },  // Ű ű
    { 0x0172, 0x0173 },  // Ų ų
    { 0x0174, 0x0175 },  // Ŵ ŵ
    { 0x0176, 0x0177 },  // Ŷ ŷ
    { 0x0178, 0x00FF },  // Ÿ ÿ
    { 0x0179, 0x017A },  // Ź ź
    { 0x017B, 0x017C },  // Ż ż
    { 0x017D, 0x017E },  // Ž ž
};

char32_t to_lower_codepoint(const char32_t character)
{
    const auto it = std::lower_bound(std::begin(kCasePairs), std::end(kCasePairs), character,
                                     [](const CasePair &pair, const char32_t value) { return pair.upper < value; });
    return (it != std::end(kCasePairs) && it->upper == character) ? it->lower : character;
}

char32_t to_upper_codepoint(const char32_t character)
{
    const auto it = std::find_if(std::begin(kCasePairs), std::end(kCasePairs),
                                 [character](const CasePair &pair) { return pair.lower == character; });
    return (it != std::end(kCasePairs)) ? it->upper : character;
}

/**
 * @brief Applies a codepoint-level case mapping to every character of @p text.
 *
 * `std::tolower`/`std::toupper` only ever covered ASCII; every byte at or
 * above 0x80 in a UTF-8 string is part of a multi-byte character, not a
 * Latin-1 byte, so mapping byte by byte left accented letters untouched (and,
 * outside the "C" locale, risked corrupting a byte inside a multi-byte
 * sequence into invalid UTF-8). This instead decodes one whole character at a
 * time, maps its codepoint, and re-encodes it.
 */
std::string map_case(IRuntime &runtime, const std::string &text, const RuntimeSite &site,
                     char32_t (*map_codepoint)(char32_t))
{
    std::string result;
    result.reserve(text.size());
    std::size_t offset = 0;
    while (offset < text.size())
    {
        char32_t character;
        const auto next = utf8::decode_one(text, offset, character);
        if (!next)
            runtime.raise_runtime_error(site, "texte UTF-8 invalide");
        result += utf8::encode_character(map_codepoint(character));
        offset = *next;
    }
    return result;
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
        return Value::texte(map_case(runtime, text, call_site, to_lower_codepoint));
    }
    if (operation == "majuscules")
    {
        stdlib_expect_positional(runtime, args, 0, "Texte.majuscules", call_site);
        return Value::texte(map_case(runtime, text, call_site, to_upper_codepoint));
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
        if (const auto value = numeric::parse_integer(text))
        {
            return stdlib_success(Value::entier(*value));
        }
        return stdlib_failure(
            stdlib_error_value(
                "Texte.ErreurConversion",
                "en_entier",
                "le texte ne représente pas un Entier valide"),
            call_site);
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
