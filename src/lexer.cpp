#include "lumiere/lexer.hpp"
#include <string>

namespace lumiere
{
    Lexer::Lexer(std::string source) : m_source(std::move(source)) {};

    bool Lexer::is_at_end() const
    {
        return m_current == m_source.size();
    }

    char Lexer::peek() const
    {
        if (is_at_end())
        {
            return '\0';
        }
        return m_source[m_current];
    }
    char Lexer::peek_next() const
    {
        if (is_at_end())
        {
            return '\0';
        }
        return m_source[m_current + 1];
    }

}