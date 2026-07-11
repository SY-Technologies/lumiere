#pragma once

#include "lumiere/lexer/token.hpp"
#include "lumiere/lexer/tokenizer.hpp"
#include "lumiere/diagnostics/diagnostic.hpp"
#include <string>
#include <vector>

namespace lumiere
{
    class Lexer
    {
    public:
        explicit Lexer(std::string source);
 
        /**
         * @brief Consumes the entire source and returns its tokens.
         *
         * The final token is always FIN_FICHIER. Invalid input is preserved as
         * ERREUR tokens and recorded as structured diagnostics.
         */
        std::vector<Token> tokenise();

        /**
         * @brief Returns diagnostics produced by the latest tokenise() call.
         *
         * The reference remains valid until this lexer is destroyed or
         * tokenise() is called again.
         */
        const std::vector<Diagnostic> &diagnostics() const noexcept;
 
    private:
        Scanner    m_scanner;
        Tokenizer  m_tokenizer;
        std::vector<Diagnostic> m_diagnostics;
    };
}
