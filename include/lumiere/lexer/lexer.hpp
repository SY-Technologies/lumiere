#pragma once

#include "lumiere/lexer/token.hpp"
#include "lumiere/lexer/tokenizer.hpp"
#include <string>
#include <vector>

namespace lumiere
{
    class Lexer
    {
    public:
        explicit Lexer(std::string source);
 
        // Consume the entire source and return all tokens.
        // The last token is always FIN_FICHIER.
        std::vector<Token> tokenise();
 
    private:
        Scanner    m_scanner;
        Tokenizer  m_tokenizer;
    };
}