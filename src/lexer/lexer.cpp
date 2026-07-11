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
        m_diagnostics.clear();
        std::vector<Token> tokens;

        while (!m_scanner.is_at_end())
        {
            m_tokenizer.skip_whitespace_and_comments();
            if (m_scanner.is_at_end())
            {
                break;
            }

            m_scanner.mark_start();
            Token token = m_tokenizer.scan_token();
            if (token.type == TokenType::ERREUR)
            {
                m_diagnostics.push_back({
                    "LUM-L0001",
                    DiagnosticSeverity::ERROR_LEVEL,
                    token.lexeme,
                    "",
                    {token.start_offset, token.end_offset, token.start_line, token.start_column},
                });
            }
            tokens.push_back(std::move(token));
        }

        tokens.push_back(Token(TokenType::FIN_FICHIER,
                               "",
                               m_scanner.line(),
                               m_scanner.column(),
                               m_scanner.current_offset(),
                               m_scanner.current_offset()));
        return tokens;
    }

    const std::vector<Diagnostic> &Lexer::diagnostics() const noexcept
    {
        return m_diagnostics;
    }
}
