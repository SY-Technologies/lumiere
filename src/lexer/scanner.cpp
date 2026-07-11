#include "lumiere/lexer/scanner.hpp"
#include <string>

namespace lumiere
{
    Scanner::Scanner(std::string source) : m_source(std::move(source)) {};
    bool Scanner::is_at_end() const
    {
        return m_current == m_source.size();
    }

    char Scanner::peek() const
    {
        if (is_at_end())
        {
            return '\0';
        }
        return m_source[m_current];
    }

    char Scanner::peek_next() const
    {
        if (m_current + 1 >= m_source.size())
        {
            return '\0';
        }
        return m_source[m_current + 1];
    }

    char Scanner::advance()
    {
        if (!is_at_end())
        {
            m_column++;
            return m_source[m_current++];
        }
        return '\0';
    }

    bool Scanner::match(char expected)
    {
        if (is_at_end())
        {
            return false;
        }
        if (m_source[m_current] != expected)
        {
            return false;
        }

        m_column++;
        m_current++;

        return true;
    }
    std::string Scanner::lexeme() const
    {
        return m_source.substr(m_start, m_current - m_start);
    }
    int Scanner::column() const
    {
        return m_column;
    }
    int Scanner::line() const
    {
        return m_line;
    }
    void Scanner::mark_start()
    {
        m_start = m_current;
        m_start_line = m_line;
        m_start_column = m_column;
    }
    int Scanner::start_line() const
    {
        return m_start_line;
    }
    int Scanner::start_column() const
    {
        return m_start_column;
    }
    std::size_t Scanner::start_offset() const
    {
        return m_start;
    }
    std::size_t Scanner::current_offset() const
    {
        return m_current;
    }
    Scanner::State Scanner::save() const
    {
        return State{.current = m_current, .line = m_line, .column = m_column};
    }
    void Scanner::restore(State saved)
    {
        m_column = saved.column;
        m_current = saved.current;
        m_line = saved.line;
    }
    void Scanner::mark_line_end()
    {
        m_column = 0;
        m_line++;
    }
}
