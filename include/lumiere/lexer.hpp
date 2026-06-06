#pragma once

#include "lumiere/token.hpp"
#include <string>
#include <vector>

namespace lumiere {

/**
 * @brief Transforms raw Lumière source code into a flat list of tokens.
 *
 * The lexer operates in a single pass over the source string, emitting one
 * token at a time. It never throws — unexpected characters produce an ERREUR
 * token and scanning continues, allowing multiple errors to be reported in
 * one pass.
 *
 * Usage:
 *   Lexer lexer(source);
 *   std::vector<Token> tokens = lexer.tokenise();
 */
class Lexer {
public:
    /**
     * @brief Constructs the lexer with the full source string.
     * @param source The raw Lumière source code to tokenise.
     */
    explicit Lexer(std::string source);

    /**
     * @brief Tokenises the entire source and returns all tokens.
     *
     * The returned vector always ends with a FIN_FICHIER token.
     * ERREUR tokens may appear anywhere in the list if invalid
     * characters were encountered.
     *
     * @return A flat list of tokens representing the source.
     */
    std::vector<Token> tokenise();

private:
    std::string  m_source;
    std::size_t  m_start   = 0;   ///< Index of the first character of the current token.
    std::size_t  m_current = 0;   ///< Index of the character currently being processed.
    int          m_line    = 1;   ///< Current line number (1-based).
    int          m_column  = 1;   ///< Current column number (1-based).


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

    // ── Token factories ───────────────────────────────────────────────────────

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
     * @brief Creates an ERREUR token with a descriptive message as the lexeme.
     *
     * @param msg A human-readable description of the error in French.
     * @return An ERREUR token at the current source position.
     */
    Token error_token(const std::string& msg) const;

    // -- Scanners --
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
     * @brief Skips whitespace and comments, advancing m_current.
     *
     * Handles:
     *   - spaces, tabs, carriage returns
     *   - newlines (increments m_line, resets m_column)
     *   - single-line comments starting with -- or //
     *   - block comments delimited by  slash start and start slash
     */
    void skip_whitespace_and_comments();

    // ── Helpers ───────────────────────────────────────────────────────────────

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