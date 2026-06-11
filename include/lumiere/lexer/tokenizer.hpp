#pragma once

#include <vector>
#include "lumiere/lexer/token.hpp"
#include "lumiere/lexer/scanner.hpp"

namespace lumiere
{
    class Tokenizer
    {
    public:
        explicit Tokenizer(Scanner& scanner);
            /**
         * @brief Creates a token of the given type using the current lexeme window.
         *
         * The lexeme is derived from m_source[m_start .. m_current].
         * Line and column are taken from the current lexer state.
         *
         * @param type The token type to assign.
         * @return A fully constructed Token.
         */
        Token make_token(TokenType type) const;
                /**
         * @brief Scans and returns the next token from the source.
         *
         * This is the main dispatch function. It reads one character and
         * delegates to the appropriate specialised scanner or handles the
         * token inline for simple single-character tokens.
         *
         * @return The next token in the source.
         */
        Token scan_token();

        /**
         * @brief Creates an ERREUR token with a descriptive message as the lexeme.
         *
         * @param msg A human-readable description of the error in French.
         * @return An ERREUR token at the current source position.
         */
        Token error_token(const std::string& msg) const;
                /**
         * @brief Skips whitespace and comments, advancing m_current.
         *
         * Handles:
         *   - spaces, tabs, carriage returns
         *   - newlines (increments m_line, resets m_column)
         *   - single-line comments starting with -- or //
         *   - block comments delimited by  slash start and start slash
         */
        void skip_whitespace_and_comments();

        
    private:
        Scanner& m_scanner;
        /**
         * @brief Scans a double-quoted text literal.
         *
         * Consumes characters until a closing '"' is found.
         * Returns an ERREUR token if the string is unterminated.
         *
         * @return A TEXTE_LIT token, or ERREUR if unterminated.
         */
        Token scan_string();

        /**
         * @brief Scans a single-quoted symbol literal.
         *
         * Expects exactly one Unicode code point between single quotes.
         * Returns an ERREUR token if malformed or unterminated.
         *
         * @return A SYMBOLE_LIT token, or ERREUR if malformed.
         */
        Token scan_symbol();

        /**
         * @brief Scans an integer or decimal numeric literal.
         *
         * Consumes digits and at most one '.' followed by more digits.
         * Returns ENTIER_LIT if no decimal point is found, DECIMAL_LIT otherwise.
         *
         * @return An ENTIER_LIT or DECIMAL_LIT token.
         */
        Token scan_number();

        /**
         * @brief Scans an identifier or keyword.
         *
         * Consumes all alphanumeric characters and underscores, including
         * UTF-8 multibyte sequences for accented characters. Delegates to
         * keyword_type() to determine whether the result is a keyword or IDENT.
         *
         * Also handles the two-word token TANT_QUE by peeking ahead after
         * scanning "tant".
         *
         * @return The appropriate keyword token, or IDENT.
         */
        Token scan_identifier_or_keyword();



        /**
         * @brief Maps a scanned word to its keyword TokenType.
         *
         * Performs a lookup against all reserved keywords including
         * accented French identifiers. Returns IDENT if the word is
         * not a reserved keyword.
         *
         * @param word The scanned word to look up.
         * @return The matching keyword TokenType, or IDENT.
         */
        static TokenType keyword_type(const std::string& word);

        /**
         * @brief Returns true if c is an ASCII decimal digit.
         */
        static bool is_digit(char c);

        /**
         * @brief Returns true if c is valid as the first character of an identifier.
         *
         * Accepts ASCII letters, underscore, and UTF-8 leading bytes (>= 0xC0)
         * to support accented characters at the start of identifiers.
         *
         * @param c The character to test, cast to unsigned char.
         */
        static bool is_alpha_start(unsigned char c);

        /**
         * @brief Returns true if c is valid as a continuation character of an identifier.
         *
         * Accepts ASCII letters, digits, underscore, and any UTF-8 multibyte
         * byte (>= 0x80) to support accented characters within identifiers.
         *
         * @param c The character to test, cast to unsigned char.
         */
        static bool is_alpha_continue(unsigned char c);


    };
} // namespace lumiere