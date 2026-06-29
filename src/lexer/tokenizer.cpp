#include <vector>
#include <unordered_map>
#include "lumiere/lexer/tokenizer.hpp"
#include "lumiere/lexer/scanner.hpp"

namespace lumiere
{
    Tokenizer::Tokenizer(Scanner &scanner) : m_scanner(scanner) {};
    Token Tokenizer::make_token(TokenType type) const
    {
        std::string lexeme(m_scanner.lexeme());
        return Token(type, std::move(lexeme), m_scanner.line(), m_scanner.column());
    }

    Token Tokenizer::error_token(const std::string &msg) const
    {
        return Token(TokenType::ERREUR, msg, m_scanner.line(), m_scanner.column());
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
            return make_token(TokenType::SLASH);

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
    Token Tokenizer::scan_string()
    {
        while (!m_scanner.is_at_end() && m_scanner.peek() != '"')
        {
            if (m_scanner.peek() == '\n')
            {
                m_scanner.mark_line_end();
            }
            m_scanner.advance();
        }

        if (m_scanner.is_at_end())
        {
            return error_token("texte non terminé — '\"' attendu");
        }

        m_scanner.advance(); // consume closing "
        return make_token(TokenType::TEXTE_LIT);
    }
    Token Tokenizer::scan_symbol()
    {
        if (m_scanner.is_at_end())
        {
            return error_token("symbole non terminé — caractère attendu");
        }

        // consume the character — could be a UTF-8 multibyte sequence
        m_scanner.advance();

        // consume any remaining bytes of a multibyte sequence
        while (!m_scanner.is_at_end() && (static_cast<unsigned char>(m_scanner.peek()) >= 0x80))
        {
            m_scanner.advance();
        }

        if (m_scanner.is_at_end() || m_scanner.peek() != '\'')
        {
            return error_token("symbole non terminé — \"'\" attendu");
        }

        m_scanner.advance(); // consume closing '
        return make_token(TokenType::SYMBOLE_LIT);
    }
    Token Tokenizer::scan_number()
    {
        while (is_digit(m_scanner.peek()))
        {
            m_scanner.advance();
        }

        // check for decimal point followed by more digits
        if (m_scanner.peek() == '.' && is_digit(m_scanner.peek_next()))
        {
            m_scanner.advance(); // consume '.'
            while (is_digit(m_scanner.peek()))
            {
                m_scanner.advance();
            }
            return make_token(TokenType::DECIMAL_LIT);
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
            {"réalise", TokenType::REALISE},
            {"remplace", TokenType::REMPLACE},
            {"public", TokenType::PUBLIC},
            {"privé", TokenType::PRIVE},

            // control flow
            {"si", TokenType::SI},
            {"sinon", TokenType::SINON},
            {"pour", TokenType::POUR},
            {"chaque", TokenType::CHAQUE},
            {"dans", TokenType::DANS},
            {"arrêter", TokenType::ARRETER},
            {"continuer", TokenType::CONTINUER},

            // error handling
            {"essayer", TokenType::ESSAYER},
            {"attraper", TokenType::ATTRAPER},
            {"finalement", TokenType::FINALEMENT},
            {"lancer", TokenType::LANCER},

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
        };

        auto it = keywords.find(word);
        return (it != keywords.end()) ? it->second : TokenType::IDENT;
    }
}
