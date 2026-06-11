#include "lumiere/lexer/lexer.hpp"
#include "lumiere/lexer/scanner.hpp"
#include "lumiere/lexer/token.hpp"
#include <string>

namespace lumiere
{
    Lexer::Lexer(std::string source)
        : m_scanner(std::move(source)), m_tokenizer(m_scanner)
    {
    }

    std::vector<Token> Lexer::tokenise()
    {
        std::vector<Token> tokens;

        while (!m_scanner.is_at_end())
        {
            m_tokenizer.skip_whitespace_and_comments();
            if (m_scanner.is_at_end())
            {
                break;
            }

            m_scanner.mark_start();
            tokens.push_back(m_tokenizer.scan_token());
        }

        tokens.push_back(Token(TokenType::FIN_FICHIER, "", m_scanner.line(), m_scanner.column()));
        return tokens;
    }
}
