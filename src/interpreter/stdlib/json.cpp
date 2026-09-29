#include "lumiere/interpreter/runtime/nominal_type.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/parser/utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace lumiere
{

namespace
{

// JSON.ErreurJSON carries more than the shared opération/cause/chemin shape
// stdlib_error_value() builds (ligne/colonne for parse errors), so this
// mirrors that helper's construction rather than extending it -- every other
// stdlib error type genuinely fits the shared shape; this one does not.
Value json_error(const std::string &operation,
                 const std::string &cause,
                 int line = -1,
                 int column = -1,
                 const std::string &path = {})
{
    auto klass = make_ref<LumiereClass>();
    klass->name = "JSON.ErreurJSON";
    klass->type_identity = native_nominal_type_identity(klass->name);
    auto error_interface = make_ref<LumiereInterface>();
    error_interface->name = "Erreur";
    error_interface->type_identity = "Erreur";
    klass->interfaces.emplace("Erreur", std::move(error_interface));

    auto object = make_ref<LumiereObject>();
    object->klass = std::move(klass);
    object->fields.emplace("opération", Value::texte(operation));
    object->fields.emplace("cause", Value::texte(cause));
    if (line >= 0)
    {
        object->fields.emplace("ligne", Value::entier(line));
    }
    if (column >= 0)
    {
        object->fields.emplace("colonne", Value::entier(column));
    }
    if (!path.empty())
    {
        object->fields.emplace("chemin", Value::texte(path));
    }
    return Value::objet(std::move(object));
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

struct JsonParseError
{
    std::string cause;
    int line;
    int column;
};

bool is_ascii_digit(char32_t ch)
{
    return ch >= U'0' && ch <= U'9';
}

// Scans JSON text one Unicode scalar at a time (matching the rest of this
// codebase's "scalar, not byte" convention for text) and tracks 1-based
// line/column for parse errors. JSON's own grammar is ASCII outside of
// string contents, so every structural character this scans is one byte;
// only string bodies can carry multi-byte UTF-8, and decode_one is what
// both rejects malformed input there and keeps columns counted in scalars.
class JsonParser
{
public:
    explicit JsonParser(const std::string &text) : m_text(text)
    {
    }

    Value parse_document()
    {
        skip_whitespace();
        Value result = parse_value();
        skip_whitespace();
        if (!at_end())
        {
            fail("texte superflu après la valeur JSON");
        }
        return result;
    }

private:
    const std::string &m_text;
    std::size_t m_pos = 0;
    int m_line = 1;
    int m_column = 1;

    [[noreturn]] void fail(const std::string &cause)
    {
        throw JsonParseError{cause, m_line, m_column};
    }

    bool at_end() const
    {
        return m_pos >= m_text.size();
    }

    // Decodes without consuming. 0 is never a valid JSON structural or
    // string-content scalar we branch on unescaped, so it doubles as "no
    // more input" for every caller here.
    char32_t peek()
    {
        if (at_end())
        {
            return 0;
        }
        char32_t character = 0;
        const auto next = utf8::decode_one(m_text, m_pos, character);
        if (!next)
        {
            fail("texte UTF-8 invalide");
        }
        return character;
    }

    char32_t advance()
    {
        if (at_end())
        {
            fail("fin de texte inattendue");
        }
        char32_t character = 0;
        const auto next = utf8::decode_one(m_text, m_pos, character);
        if (!next)
        {
            fail("texte UTF-8 invalide");
        }
        m_pos = *next;
        if (character == U'\n')
        {
            ++m_line;
            m_column = 1;
        }
        else
        {
            ++m_column;
        }
        return character;
    }

    void expect_ascii(char expected, const std::string &what)
    {
        if (at_end() || m_text[m_pos] != expected)
        {
            fail(what);
        }
        advance();
    }

    void skip_whitespace()
    {
        while (!at_end())
        {
            const char32_t ch = peek();
            if (ch == U' ' || ch == U'\t' || ch == U'\n' || ch == U'\r')
            {
                advance();
            }
            else
            {
                break;
            }
        }
    }

    void expect_literal(const char *literal)
    {
        for (const char *p = literal; *p != '\0'; ++p)
        {
            if (at_end() || advance() != static_cast<char32_t>(*p))
            {
                fail(std::string("littéral JSON invalide, attendu '") + literal + "'");
            }
        }
    }

    char32_t parse_hex4()
    {
        char32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (at_end())
            {
                fail("séquence \\u incomplète");
            }
            const char32_t digit = advance();
            value <<= 4;
            if (digit >= U'0' && digit <= U'9')
            {
                value |= (digit - U'0');
            }
            else if (digit >= U'a' && digit <= U'f')
            {
                value |= (digit - U'a' + 10);
            }
            else if (digit >= U'A' && digit <= U'F')
            {
                value |= (digit - U'A' + 10);
            }
            else
            {
                fail("chiffre hexadécimal attendu dans une séquence \\u");
            }
        }
        return value;
    }

    std::string parse_string()
    {
        expect_ascii('"', "chaîne attendue");
        std::string out;
        while (true)
        {
            if (at_end())
            {
                fail("chaîne non terminée");
            }
            const char32_t ch = peek();
            if (ch == U'"')
            {
                advance();
                break;
            }
            if (ch == U'\\')
            {
                advance();
                if (at_end())
                {
                    fail("séquence d'échappement incomplète");
                }
                const char32_t escape = advance();
                switch (escape)
                {
                case U'"':
                    out += '"';
                    break;
                case U'\\':
                    out += '\\';
                    break;
                case U'/':
                    out += '/';
                    break;
                case U'b':
                    out += '\b';
                    break;
                case U'f':
                    out += '\f';
                    break;
                case U'n':
                    out += '\n';
                    break;
                case U'r':
                    out += '\r';
                    break;
                case U't':
                    out += '\t';
                    break;
                case U'u':
                {
                    char32_t code = parse_hex4();
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        expect_ascii('\\', "substitut haut sans substitut bas");
                        expect_ascii('u', "substitut haut sans substitut bas");
                        const char32_t low = parse_hex4();
                        if (low < 0xDC00 || low > 0xDFFF)
                        {
                            fail("substitut bas invalide après un substitut haut");
                        }
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (code >= 0xDC00 && code <= 0xDFFF)
                    {
                        fail("substitut bas inattendu sans substitut haut");
                    }
                    out += utf8::encode_character(code);
                    break;
                }
                default:
                    fail("séquence d'échappement invalide");
                }
            }
            else if (ch < 0x20)
            {
                fail("caractère de contrôle non échappé dans une chaîne");
            }
            else
            {
                const std::size_t start = m_pos;
                advance();
                out.append(m_text, start, m_pos - start);
            }
        }
        return out;
    }

    Value parse_number()
    {
        const std::size_t start = m_pos;
        bool is_integral = true;

        if (!at_end() && peek() == U'-')
        {
            advance();
        }
        if (at_end() || !is_ascii_digit(peek()))
        {
            fail("nombre JSON invalide");
        }
        if (peek() == U'0')
        {
            advance();
            if (!at_end() && is_ascii_digit(peek()))
            {
                fail("zéro non significatif interdit dans un nombre JSON");
            }
        }
        else
        {
            while (!at_end() && is_ascii_digit(peek()))
            {
                advance();
            }
        }
        if (!at_end() && peek() == U'.')
        {
            is_integral = false;
            advance();
            if (at_end() || !is_ascii_digit(peek()))
            {
                fail("partie décimale attendue après '.'");
            }
            while (!at_end() && is_ascii_digit(peek()))
            {
                advance();
            }
        }
        if (!at_end() && (peek() == U'e' || peek() == U'E'))
        {
            is_integral = false;
            advance();
            if (!at_end() && (peek() == U'+' || peek() == U'-'))
            {
                advance();
            }
            if (at_end() || !is_ascii_digit(peek()))
            {
                fail("exposant attendu après 'e'");
            }
            while (!at_end() && is_ascii_digit(peek()))
            {
                advance();
            }
        }

        const std::string token = m_text.substr(start, m_pos - start);
        if (is_integral)
        {
            try
            {
                std::size_t consumed = 0;
                const long long parsed = std::stoll(token, &consumed);
                if (consumed != token.size())
                {
                    fail("nombre entier JSON invalide");
                }
                return Value::entier(static_cast<int64_t>(parsed));
            }
            catch (const std::out_of_range &)
            {
                // Per docs/stdlib-foundations.md: "An integral token outside
                // the Entier range fails instead of silently losing precision
                // in Décimal."
                fail("nombre entier hors des limites de Entier");
            }
        }

        const double value = std::strtod(token.c_str(), nullptr);
        if (!std::isfinite(value))
        {
            fail("nombre hors des limites représentables d'un Décimal");
        }
        return Value::decimal(value);
    }

    Value parse_value()
    {
        skip_whitespace();
        if (at_end())
        {
            fail("valeur JSON attendue");
        }
        const char32_t ch = peek();
        switch (ch)
        {
        case U'{':
            return parse_object();
        case U'[':
            return parse_array();
        case U'"':
            return Value::texte(parse_string());
        case U't':
            expect_literal("true");
            return Value::logique(true);
        case U'f':
            expect_literal("false");
            return Value::logique(false);
        case U'n':
            expect_literal("null");
            return Value::rien();
        default:
            if (ch == U'-' || is_ascii_digit(ch))
            {
                return parse_number();
            }
            fail("valeur JSON invalide");
        }
    }

    Value parse_object()
    {
        expect_ascii('{', "'{' attendu");
        auto data = make_ref<DictData>();
        std::unordered_set<std::string> seen_keys;

        skip_whitespace();
        if (!at_end() && peek() == U'}')
        {
            advance();
            return Value::dictionnaire(std::move(data));
        }

        while (true)
        {
            skip_whitespace();
            if (at_end() || peek() != U'"')
            {
                fail("clé de chaîne attendue dans un objet JSON");
            }
            std::string key = parse_string();
            // Rejecting duplicates rather than keeping the last value: a
            // silently chosen winner hides malformed input, per the "Common
            // rules" section of docs/stdlib-foundations.md.
            if (!seen_keys.insert(key).second)
            {
                fail("clé d'objet dupliquée: \"" + key + "\"");
            }
            skip_whitespace();
            expect_ascii(':', "':' attendu après une clé d'objet");
            skip_whitespace();
            Value value = parse_value();
            data->set(Value::texte(std::move(key)), std::move(value));
            skip_whitespace();
            if (at_end())
            {
                fail("objet JSON non terminé");
            }
            const char32_t next = peek();
            if (next == U',')
            {
                advance();
                continue;
            }
            if (next == U'}')
            {
                advance();
                break;
            }
            fail("',' ou '}' attendu dans un objet JSON");
        }
        return Value::dictionnaire(std::move(data));
    }

    Value parse_array()
    {
        expect_ascii('[', "'[' attendu");
        auto data = make_ref<ListeData>();

        skip_whitespace();
        if (!at_end() && peek() == U']')
        {
            advance();
            return Value::liste(std::move(data));
        }

        while (true)
        {
            skip_whitespace();
            data->elements.push_back(parse_value());
            skip_whitespace();
            if (at_end())
            {
                fail("tableau JSON non terminé");
            }
            const char32_t next = peek();
            if (next == U',')
            {
                advance();
                continue;
            }
            if (next == U']')
            {
                advance();
                break;
            }
            fail("',' ou ']' attendu dans un tableau JSON");
        }
        return Value::liste(std::move(data));
    }
};

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

struct JsonEncodeError
{
    std::string cause;
    std::string path;
};

std::string escape_json_string(const std::string &text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    std::size_t i = 0;
    while (i < text.size())
    {
        char32_t ch = 0;
        const auto next = utf8::decode_one(text, i, ch);
        // Lumiere Texte is guaranteed valid UTF-8 by construction (every
        // producer of a Texte value validates it), so this cannot fail; the
        // guard only avoids an infinite loop if that invariant is ever
        // violated elsewhere.
        if (!next)
        {
            out += "\xef\xbf\xbd"; // U+FFFD, one scalar, always advances.
            ++i;
            continue;
        }
        i = *next;
        switch (ch)
        {
        case U'"':
            out += "\\\"";
            break;
        case U'\\':
            out += "\\\\";
            break;
        case U'\b':
            out += "\\b";
            break;
        case U'\f':
            out += "\\f";
            break;
        case U'\n':
            out += "\\n";
            break;
        case U'\r':
            out += "\\r";
            break;
        case U'\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch));
                out += buffer;
            }
            else
            {
                out += utf8::encode_character(ch);
            }
        }
    }
    out += '"';
    return out;
}

struct EncodeOptions
{
    bool pretty = false;
    int indent = 0;
};

void write_indent(std::string &out, const EncodeOptions &options, int depth)
{
    if (!options.pretty)
    {
        return;
    }
    out += '\n';
    out.append(static_cast<std::size_t>(options.indent) * static_cast<std::size_t>(depth), ' ');
}

void encode_value(const Value &value,
                  std::string &out,
                  const std::string &path,
                  std::vector<const void *> &ancestors,
                  const EncodeOptions &options,
                  int depth)
{
    if (value.is_rien())
    {
        out += "null";
        return;
    }
    if (value.is_logique())
    {
        out += value.as_logique() ? "true" : "false";
        return;
    }
    if (value.is_entier())
    {
        out += std::to_string(value.as_entier());
        return;
    }
    if (value.is_decimal())
    {
        const double number = value.as_decimal();
        if (!std::isfinite(number))
        {
            throw JsonEncodeError{"infini et non_nombre ne sont pas des nombres JSON", path};
        }
        out += numeric::decimal_to_text(number);
        return;
    }
    if (value.is_texte())
    {
        out += escape_json_string(value.as_texte());
        return;
    }
    if (value.is_liste() || value.is_liste_fixe())
    {
        const void *identity = value.is_liste()
                                    ? static_cast<const void *>(value.as_liste().get())
                                    : static_cast<const void *>(value.as_liste_fixe().get());
        // A cycle is an ancestor reappearing on the current recursion path,
        // not merely a value seen before: two sibling fields legitimately
        // sharing one list is not cyclic, so this checks "currently being
        // encoded", not "ever encoded".
        if (std::find(ancestors.begin(), ancestors.end(), identity) != ancestors.end())
        {
            throw JsonEncodeError{"structure cyclique détectée", path};
        }
        ancestors.push_back(identity);
        const std::vector<Value> &elements =
            value.is_liste() ? value.as_liste()->elements : value.as_liste_fixe()->elements;
        out += '[';
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            if (i != 0)
            {
                out += ',';
            }
            write_indent(out, options, depth + 1);
            encode_value(elements[i], out, path + "[" + std::to_string(i) + "]", ancestors, options, depth + 1);
        }
        if (!elements.empty())
        {
            write_indent(out, options, depth);
        }
        out += ']';
        ancestors.pop_back();
        return;
    }
    if (value.is_dictionnaire())
    {
        const void *identity = value.as_dictionnaire().get();
        if (std::find(ancestors.begin(), ancestors.end(), identity) != ancestors.end())
        {
            throw JsonEncodeError{"structure cyclique détectée", path};
        }
        ancestors.push_back(identity);
        const auto &entries = value.as_dictionnaire()->items();
        out += '{';
        bool first = true;
        for (const auto &entry : entries)
        {
            if (!entry.first.is_texte())
            {
                throw JsonEncodeError{
                    "les clés de dictionnaire doivent être de type Texte pour l'encodage JSON", path};
            }
            if (!first)
            {
                out += ',';
            }
            first = false;
            write_indent(out, options, depth + 1);
            out += escape_json_string(entry.first.as_texte());
            out += options.pretty ? ": " : ":";
            encode_value(entry.second, out, path + "." + entry.first.as_texte(), ancestors, options, depth + 1);
        }
        if (!entries.empty())
        {
            write_indent(out, options, depth);
        }
        out += '}';
        ancestors.pop_back();
        return;
    }
    // Sets, functions, results, modules, classes, and objects: unsupported in
    // v1, per docs/stdlib-foundations.md's JSON value-mapping table.
    throw JsonEncodeError{"type non encodable en JSON: " + value.type_name(), path};
}

Value encode_document(IRuntime &runtime,
                      const Value &root,
                      const EncodeOptions &options,
                      const std::string &operation_name,
                      const RuntimeSite &call_site)
{
    (void)runtime;
    std::string out;
    std::vector<const void *> ancestors;
    try
    {
        encode_value(root, out, "$", ancestors, options, 0);
    }
    catch (const JsonEncodeError &error)
    {
        return stdlib_failure(json_error(operation_name, error.cause, -1, -1, error.path), call_site);
    }
    return stdlib_success(Value::texte(std::move(out)));
}

}

void register_json_module(Module &module)
{
    const auto &make_native_function = native_function_factory();

    auto error_class = make_ref<LumiereClass>();
    error_class->name = "JSON.ErreurJSON";
    stdlib_bind_public_value(module, "ErreurJSON", Value::classe(std::move(error_class)));

    stdlib_bind_public_function(
        module,
        make_native_function,
        "analyser",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "JSON.analyser", call_site);
            const std::string text = stdlib_expect_text(runtime, args[0].value, "JSON.analyser", call_site);
            (void)runtime;
            try
            {
                JsonParser parser(text);
                return stdlib_success(parser.parse_document());
            }
            catch (const JsonParseError &error)
            {
                return stdlib_failure(
                    json_error("analyser", error.cause, error.line, error.column), call_site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "encoder",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 1, "JSON.encoder", call_site);
            return encode_document(runtime, args[0].value, EncodeOptions{}, "encoder", call_site);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "encoder_indenté",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            stdlib_expect_positional(runtime, args, 2, "JSON.encoder_indenté", call_site);
            const int64_t spaces =
                stdlib_expect_integer(runtime, args[1].value, "JSON.encoder_indenté", call_site);
            if (spaces < 0 || spaces > 8)
            {
                runtime.raise_runtime_error(
                    call_site, "JSON.encoder_indenté attend un nombre d'espaces entre 0 et 8");
            }
            EncodeOptions options;
            options.pretty = true;
            options.indent = static_cast<int>(spaces);
            return encode_document(runtime, args[0].value, options, "encoder_indenté", call_site);
        });
}

} // namespace lumiere
