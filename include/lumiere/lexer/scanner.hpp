#pragma once

#include <vector>
#include <string>

namespace lumiere{

    class Scanner{
    public:
        explicit Scanner(std::string source);
        std::string lexeme() const;
        int  line()   const;
        int  column() const;
            /**
         * @brief Returns true if all source characters have been consumed.
         */
        bool is_at_end() const;

        /**
         * @brief Returns the current character without consuming it.
         * @return The character at m_current, or '\0' if at end.
         */
        char peek() const;

        /**
         * @brief Returns the character one ahead of current without consuming it.
         * @return The character at m_current + 1, or '\0' if at end.
         */
        char peek_next() const;

        /**
         * @brief Consumes and returns the current character, advancing m_current.
         *
         * Also increments m_column. Callers that consume a newline must
         * increment m_line and reset m_column themselves.
         *
         * @return The consumed character.
         */
        char advance();

        /**
         * @brief Consumes the current character only if it matches the expected one.
         *
         * Used for two-character tokens such as ==, !=, <=, >=, ->, .., ...
         *
         * @param expected The character to match against.
         * @return True if the character matched and was consumed, false otherwise.
         */
        bool match(char expected);

        struct State { std::size_t current; int line; int column; };
        State save()                const;
        void  restore(State saved);
        void mark_start();
        void mark_line_end();


    private:
        std::string m_source;         //< source lumière code to scan
        std::size_t  m_start   = 0;   ///< Index of the first character of the current token.
        std::size_t  m_current = 0;   ///< Index of the character currently being processed, always pointing at the next unprocessed character 
        int          m_line    = 1;   ///< Current line number (1-based).
        int          m_column  = 1;


};
}//namespace lumiere
