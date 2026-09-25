#include <vector>
#include <unordered_map>
#include <cstdint>
#include <sstream>
#include "lumiere/lexer/tokenizer.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"
#include "lumiere/lexer/scanner.hpp"
#include "lumiere/parser/utf8.hpp"

namespace lumiere
{
    namespace
    {
        std::string normalize_block_documentation(const std::string &raw)
        {
            std::istringstream input(raw);
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(input, line))
            {
                const std::size_t content = line.find_first_not_of(" \t\r");
                line = content == std::string::npos ? "" : line.substr(content);
                if (!line.empty() && line.front() == '*')
                {
                    line.erase(0, 1);
                    if (!line.empty() && line.front() == ' ')
                    {
                        line.erase(0, 1);
                    }
                }
                const std::size_t end = line.find_last_not_of(" \t\r");
                line = end == std::string::npos ? "" : line.substr(0, end + 1);
                lines.push_back(std::move(line));
            }
            while (!lines.empty() && lines.front().empty())
            {
                lines.erase(lines.begin());
            }
            while (!lines.empty() && lines.back().empty())
            {
                lines.pop_back();
            }

            std::ostringstream output;
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                if (i > 0)
                {
                    output << '\n';
                }
                output << lines[i];
            }
            return output.str();
        }
    }

    Tokenizer::Tokenizer(Scanner &scanner) : m_scanner(scanner) {};
    Token Tokenizer::make_token(TokenType type) const
    {
        std::string lexeme(m_scanner.lexeme());
        return Token(type,
                     std::move(lexeme),
                     m_scanner.start_line(),
                     m_scanner.start_column(),
                     m_scanner.start_offset(),
                     m_scanner.current_offset());
    }

    Token Tokenizer::error_token(const std::string &msg) const
    {
        return Token(TokenType::ERREUR,
                     msg,
                     m_scanner.start_line(),
                     m_scanner.start_column(),
                     m_scanner.start_offset(),
                     m_scanner.current_offset());
    }

    bool Tokenizer::at_documentation_comment()
    {
        const Scanner::State saved = m_scanner.save();
        const bool is_block_doc =
            m_scanner.match('/') && m_scanner.match('*') && m_scanner.peek() == '*';
        m_scanner.restore(saved);
        return is_block_doc;
    }

    Token Tokenizer::scan_documentation()
    {
        // The first '/' was consumed by scan_token(). Consume the opening
        // stars, while allowing the compact empty form `/**/`.
        m_scanner.advance();
        if (!(m_scanner.peek() == '*' && m_scanner.peek_next() == '/'))
        {
            m_scanner.advance();
        }

        const std::size_t start_offset = m_scanner.start_offset();
        const uint32_t start_line = static_cast<uint32_t>(m_scanner.start_line());
        const uint32_t start_column = static_cast<uint32_t>(m_scanner.start_column());
        std::string raw;
        while (!m_scanner.is_at_end() &&
               !(m_scanner.peek() == '*' && m_scanner.peek_next() == '/'))
        {
            if (m_scanner.peek() == '\n')
            {
                m_scanner.mark_line_end();
            }
            raw.push_back(m_scanner.peek());
            m_scanner.advance();
        }
        if (!m_scanner.is_at_end())
        {
            m_scanner.advance();
            m_scanner.advance();
        }
        return Token(TokenType::DOCUMENTATION,
                     normalize_block_documentation(raw),
                     start_line,
                     start_column,
                     start_offset,
                     m_scanner.current_offset());
    }
    void Tokenizer::skip_whitespace_and_comments()
    {
        while (!Tokenizer::m_scanner.is_at_end())
        {
            char c = m_scanner.peek();

            switch (c)
            {
            case ' ':
            case '\t':
            case '\r':
                m_scanner.advance();
                break;

            case '\n':
                m_scanner.mark_line_end();
                m_scanner.advance();
                break;

            case '/':
                if (m_scanner.peek_next() == '/')
                {
                    // single-line comment // consume until end of line
                    while (!m_scanner.is_at_end() && m_scanner.peek() != '\n')
                    {
                        m_scanner.advance();
                    }
                }
                else if (m_scanner.peek_next() == '*')
                {
                    if (at_documentation_comment())
                    {
                        return;
                    }
                    // block comment /* ... */
                    m_scanner.advance(); // consume /
                    m_scanner.advance(); // consume *
                    while (!m_scanner.is_at_end())
                    {
                        if (m_scanner.peek() == '\n')
                        {
                            m_scanner.mark_line_end();
                        }
                        if (m_scanner.peek() == '*' && m_scanner.peek_next() == '/')
                        {
                            m_scanner.advance(); // consume *
                            m_scanner.advance(); // consume /
                            break;
                        }
                        m_scanner.advance();
                    }
                }
                else
                {
                    return; // it's a SLASH operator, not a comment
                }
                break;

            default:
                return; // non-whitespace, non-comment — real token starts here
            }
        }
    }

    Token Tokenizer::scan_token()
    {
        char c = m_scanner.advance();

        switch (c)
        {
        // ── Single character tokens
        case '(':
            return make_token(TokenType::PAREN_OUV);
        case ')':
            return make_token(TokenType::PAREN_FERM);
        case '{':
            return make_token(TokenType::ACCOLADE_OUV);
        case '}':
            return make_token(TokenType::ACCOLADE_FERM);
        case '[':
            return make_token(TokenType::CROCHET_OUV);
        case ']':
            return make_token(TokenType::CROCHET_FERM);
        case ',':
            return make_token(TokenType::VIRGULE);
        case '%':
            return make_token(TokenType::MODULO);
        case '*':
            return make_token(TokenType::ETOILE);
        case '+':
            return make_token(TokenType::PLUS);
        case '&':
            return make_token(TokenType::AMPERSAND);
        case '|':
            return make_token(TokenType::PIPE);
        case '/':
            // skip_whitespace_and_comments() leaves documentation comments for
            // the tokenizer. Ordinary comments were already consumed there.
            return m_scanner.peek() == '*' && m_scanner.peek_next() == '*'
                       ? scan_documentation()
                       : make_token(TokenType::SLASH);

        // ── One or two character tokens
        case '=':
            return make_token(m_scanner.match('=') ? TokenType::EGAL_EGAL : TokenType::EGAL);
        case '!':
            return m_scanner.match('=') ? make_token(TokenType::BANG_EGAL) : error_token("caractère inattendu: '!'");
        case '<':
            return make_token(m_scanner.match('=') ? TokenType::INFERIEUR_EGAL : TokenType::INFERIEUR);
        case '>':
            return make_token(m_scanner.match('=') ? TokenType::SUPERIEUR_EGAL : TokenType::SUPERIEUR);
        case ':':
            return make_token(TokenType::DEUX_POINTS);

        // ── Minus or arrow
        case '-':
            return make_token(m_scanner.match('>') ? TokenType::FLECHE : TokenType::MOINS);

        // ── Dot, range
        case '.':
            return make_token(TokenType::POINT);

        // ── Literals
        case '"':
            return scan_string();
        case '\'':
            return scan_symbol();

        // ── Numbers
        default:
            if (is_digit(c))
            {
                return scan_number();
            }
            if (is_alpha_start(static_cast<unsigned char>(c)))
            {
                return scan_identifier_or_keyword();
            }
            return error_token("caractère inattendu: '" + std::string(1, c) + "'");
        }
    }
    // Escapes recognised by both scan_string() and scan_symbol(): \n, \t,
    // \r, \\, \", \', \0, and \u{XXXXXX} for any Unicode scalar value.
    // Called with the scanner positioned right after the backslash.
    bool Tokenizer::decode_escape_sequence(std::string &out, std::string &error_message)
    {
        if (m_scanner.is_at_end())
        {
            error_message = "échappement invalide — un caractère est attendu après '\\'";
            return false;
        }
        const char c = m_scanner.advance();
        switch (c)
        {
        case 'n':
            out.push_back('\n');
            return true;
        case 't':
            out.push_back('\t');
            return true;
        case 'r':
            out.push_back('\r');
            return true;
        case '\\':
            out.push_back('\\');
            return true;
        case '"':
            out.push_back('"');
            return true;
        case '\'':
            out.push_back('\'');
            return true;
        case '0':
            out.push_back('\0');
            return true;
        case 'u':
            return decode_unicode_escape(out, error_message);
        default:
            error_message = std::string("échappement invalide — '\\") + c + "' n'est pas reconnu";
            return false;
        }
    }

    // \u{XXXXXX} -- 1 to 6 hexadecimal digits naming a Unicode scalar value,
    // encoded into `out` as UTF-8. Called with the scanner positioned right
    // after the 'u'.
    bool Tokenizer::decode_unicode_escape(std::string &out, std::string &error_message)
    {
        if (m_scanner.is_at_end() || m_scanner.peek() != '{')
        {
            error_message = "échappement invalide — '\\u' attend '{' suivi de chiffres hexadécimaux";
            return false;
        }
        m_scanner.advance(); // consume '{'

        uint32_t code_point = 0;
        int digit_count = 0;
        while (!m_scanner.is_at_end() && m_scanner.peek() != '}')
        {
            const char digit = m_scanner.peek();
            int value = 0;
            if (digit >= '0' && digit <= '9')
                value = digit - '0';
            else if (digit >= 'a' && digit <= 'f')
                value = 10 + (digit - 'a');
            else if (digit >= 'A' && digit <= 'F')
                value = 10 + (digit - 'A');
            else
            {
                error_message = "échappement invalide — '\\u{...}' n'accepte que des chiffres hexadécimaux";
                return false;
            }
            if (digit_count == 6)
            {
                error_message = "échappement invalide — '\\u{...}' ne peut pas dépasser six chiffres hexadécimaux";
                return false;
            }
            code_point = (code_point << 4) | static_cast<uint32_t>(value);
            ++digit_count;
            m_scanner.advance();
        }

        if (digit_count == 0)
        {
            error_message = "échappement invalide — '\\u{}' attend au moins un chiffre hexadécimal";
            return false;
        }
        if (m_scanner.is_at_end())
        {
            error_message = "échappement invalide — '}' attendu";
            return false;
        }
        m_scanner.advance(); // consume '}'

        if (code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF))
        {
            error_message = "échappement invalide — '\\u{...}' dépasse les points de code Unicode valides";
            return false;
        }

        out += utf8::encode_character(static_cast<char32_t>(code_point));
        return true;
    }

    Token Tokenizer::scan_string()
    {
        std::string decoded;
        while (!m_scanner.is_at_end() && m_scanner.peek() != '"')
        {
            const char c = m_scanner.peek();
            if (c == '\n')
            {
                m_scanner.mark_line_end();
            }
            if (c == '\\')
            {
                m_scanner.advance(); // consume backslash
                std::string error_message;
                if (!decode_escape_sequence(decoded, error_message))
                {
                    return error_token(error_message);
                }
                continue;
            }
            decoded.push_back(c);
            m_scanner.advance();
        }

        if (m_scanner.is_at_end())
        {
            return error_token("texte non terminé — '\"' attendu");
        }

        m_scanner.advance(); // consume closing "
        Token token = make_token(TokenType::TEXTE_LIT);
        token.decoded = std::move(decoded);
        return token;
    }
    Token Tokenizer::scan_symbol()
    {
        if (m_scanner.is_at_end())
        {
            return error_token("symbole non terminé — caractère attendu");
        }

        std::string decoded;
        while (!m_scanner.is_at_end() && m_scanner.peek() != '\'')
        {
            const char c = m_scanner.peek();
            if (c == '\n')
            {
                return error_token("symbole non terminé — \"'\" attendu");
            }
            if (c == '\\')
            {
                m_scanner.advance(); // consume backslash
                std::string error_message;
                if (!decode_escape_sequence(decoded, error_message))
                {
                    return error_token(error_message);
                }
                continue;
            }
            decoded.push_back(c);
            m_scanner.advance();
        }

        if (m_scanner.is_at_end() || m_scanner.peek() != '\'')
        {
            return error_token("symbole non terminé — \"'\" attendu");
        }

        if (!utf8::decode_single_character(decoded).has_value())
        {
            return error_token("symbole invalide — un seul caractère Unicode est attendu");
        }

        m_scanner.advance(); // consume closing '
        Token token = make_token(TokenType::SYMBOLE_LIT);
        token.decoded = std::move(decoded);
        return token;
    }
    Token Tokenizer::scan_number()
    {
        // A digit separator is only a separator between two digits, so a leading or
        // trailing underscore is left behind for the check at the end to report.
        const auto consume_digits = [this]() {
            while (is_digit(m_scanner.peek()) ||
                   (m_scanner.peek() == '_' && is_digit(m_scanner.peek_next())))
            {
                m_scanner.advance();
            }
        };

        consume_digits();

        // check for decimal point followed by more digits
        bool is_decimal = false;
        if (m_scanner.peek() == '.' && is_digit(m_scanner.peek_next()))
        {
            is_decimal = true;
            m_scanner.advance(); // consume '.'
            consume_digits();
        }

        // An exponent belongs to the number only when digits follow it, through an
        // optional sign. Anything else leaves the 'e' to start an identifier, which
        // the trailing check below then reports.
        if (m_scanner.peek() == 'e' || m_scanner.peek() == 'E')
        {
            const Scanner::State before_exponent = m_scanner.save();
            m_scanner.advance(); // consume 'e'
            if (m_scanner.peek() == '+' || m_scanner.peek() == '-')
            {
                m_scanner.advance();
            }
            if (is_digit(m_scanner.peek()))
            {
                is_decimal = true;
                consume_digits();
            }
            else
            {
                m_scanner.restore(before_exponent);
            }
        }

        // A letter touching the end of a number is always a mistake, and reading it
        // as a separate identifier hid the missing exponent syntax behind a runtime
        // "variable introuvable".
        if (is_alpha_start(static_cast<unsigned char>(m_scanner.peek())))
        {
            return error_token("nombre invalide — un caractère ne peut pas suivre immédiatement un nombre");
        }

        if (is_decimal)
        {
            // Same reason as the Entier check below: reported here, where the
            // source location still exists. Without it a literal the type
            // cannot hold reached the conversion and surfaced as "erreur: stod".
            if (!numeric::parse_decimal_literal(std::string(m_scanner.lexeme())))
            {
                return error_token(
                    "décimal hors limites — un littéral Décimal doit tenir entre "
                    "4.9406564584124654e-324 et 1.7976931348623157e+308 en valeur absolue");
            }
            return make_token(TokenType::DECIMAL_LIT);
        }

        // Reported here rather than at conversion, where the source location is gone.
        if (!numeric::parse_integer_literal(m_scanner.lexeme()))
        {
            return error_token(
                "entier hors limites — un littéral Entier ne peut pas dépasser 9223372036854775807");
        }
        return make_token(TokenType::ENTIER_LIT);
    }
    Token Tokenizer::scan_identifier_or_keyword()
    {
        while (!m_scanner.is_at_end() && is_alpha_continue(static_cast<unsigned char>(m_scanner.peek())))
        {
            m_scanner.advance();
        }

        std::string word(m_scanner.lexeme());

        // special case: "tant" must be followed by whitespace and "que"
        if (word == "tant")
        {
            auto saved = m_scanner.save();

            // skip whitespace between tant and que
            while (!m_scanner.is_at_end() && (m_scanner.peek() == ' ' || m_scanner.peek() == '\t'))
            {
                m_scanner.advance();
            }

            if (!m_scanner.is_at_end() && m_scanner.peek() == 'q' && m_scanner.peek_next() == 'u')
            {
                m_scanner.advance(); // q
                m_scanner.advance(); // u
                if (!m_scanner.is_at_end() && m_scanner.peek() == 'e')
                {
                    m_scanner.advance(); // e
                    // make sure "que" is not part of a longer identifier
                    if (m_scanner.is_at_end() || !is_alpha_continue(static_cast<unsigned char>(m_scanner.peek())))
                    {
                        return make_token(TokenType::TANT_QUE);
                    }
                }
            }

            // not followed by "que" — restore position and fall through to error
            m_scanner.restore(saved);
            return error_token("'tant' sans 'que' — vouliez-vous dire 'tant que' ?");
        }

        // special case: "agir" must be followed by whitespace and "selon"
        if (word == "agir")
        {
            auto saved = m_scanner.save();

            while (!m_scanner.is_at_end() && (m_scanner.peek() == ' ' || m_scanner.peek() == '\t'))
            {
                m_scanner.advance();
            }

            if (!m_scanner.is_at_end() && m_scanner.peek() == 's')
            {
                const std::string expected = "selon";
                bool matches = true;

                for (char ch : expected)
                {
                    if (m_scanner.is_at_end() || m_scanner.peek() != ch)
                    {
                        matches = false;
                        break;
                    }
                    m_scanner.advance();
                }

                if (matches && (m_scanner.is_at_end() || !is_alpha_continue(static_cast<unsigned char>(m_scanner.peek()))))
                {
                    return make_token(TokenType::AGIR_SELON);
                }
            }

            m_scanner.restore(saved);
            return error_token("'agir' sans 'selon' — vouliez-vous dire 'agir selon' ?");
        }

        return make_token(keyword_type(word));
    }

    bool Tokenizer::is_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    bool Tokenizer::is_alpha_start(unsigned char c)
    {
        return std::isalpha(c) || c == '_' || c >= 0xC0;
    }

    bool Tokenizer::is_alpha_continue(unsigned char c)
    {
        return std::isalnum(c) || c == '_' || c >= 0x80;
    }

    TokenType Tokenizer::keyword_type(const std::string &word)
    {
        static const std::unordered_map<std::string, TokenType> keywords = {
            // declarations
            {"soit", TokenType::SOIT},
            {"fixe", TokenType::FIXE},
            {"fonction", TokenType::FONCTION},
            {"retourne", TokenType::RETOURNE},
            {"classe", TokenType::CLASSE},
            {"interface", TokenType::INTERFACE},
            {"type", TokenType::TYPE},
            {"réalise", TokenType::REALISE},
            {"realise", TokenType::REALISE},
            {"remplace", TokenType::REMPLACE},
            {"public", TokenType::PUBLIC},
            {"privé", TokenType::PRIVE},
            {"prive", TokenType::PRIVE},

            // control flow
            {"si", TokenType::SI},
            {"sinon", TokenType::SINON},
            {"pour", TokenType::POUR},
            {"chaque", TokenType::CHAQUE},
            {"dans", TokenType::DANS},
            {"arrêter", TokenType::ARRETER},
            {"arreter", TokenType::ARRETER},
            {"continuer", TokenType::CONTINUER},

            // literals
            {"vrai", TokenType::VRAI},
            {"faux", TokenType::FAUX},
            {"rien", TokenType::RIEN},

            // other
            {"ici", TokenType::ICI},
            {"parent", TokenType::PARENT},
            {"en", TokenType::EN},
            {"importer", TokenType::IMPORTER},
            {"comme", TokenType::COMME},
            {"est", TokenType::EST},

            // logical operators
            {"et", TokenType::ET},
            {"ou", TokenType::OU},
            {"non", TokenType::NON},

            // result handling
            {"propager", TokenType::PROPAGER},
            {"ignorer", TokenType::IGNORER},
        };

        auto it = keywords.find(word);
        return (it != keywords.end()) ? it->second : TokenType::IDENT;
    }
}
