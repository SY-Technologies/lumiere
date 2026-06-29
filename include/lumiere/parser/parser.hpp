#pragma once

#include "lumiere/parser/ast.hpp"
#include "lumiere/lexer/token.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace lumiere
{

    /**
     * @brief Thrown when the parser encounters unexpected or invalid syntax.
     *
     * Contains the offending token for precise error reporting and a
     * human-readable message in French.
     */
    struct ParseError : std::exception
    {
        Token token;
        std::string message;

        ParseError(Token token, std::string message)
            : token(std::move(token)), message(std::move(message)) {}

        const char *what() const noexcept override { return message.c_str(); }
    };

    /**
     * @brief Transforms a flat list of tokens into an AST.
     *
     * Implements a recursive descent parser following the Lumière grammar.
     * Operator precedence is encoded in the call hierarchy — lower precedence
     * functions call higher precedence ones, bottoming out at parse_primary().
     *
     * On a syntax error a ParseError is thrown immediately. Error recovery
     * is not implemented. The first error terminates parsing.
     *
     */
    class Parser
    {
    public:
        /**
         * @brief Constructs the parser with a token list from the lexer.
         * @param tokens The full token list including FIN_FICHIER.
         */
        explicit Parser(std::vector<Token> tokens);

        /**
         * @brief Parses the full programme and returns a list of top-level statements.
         *
         * Catches any ParseError, prints a formatted error message to stderr,
         * and returns an empty list on failure.
         *
         * @return A StmtList representing the programme, or empty on error.
         */
        StmtList parse();

        /**
         * @brief Returns whether the last parse attempt encountered a syntax error.
         */
        bool had_error() const;

    private:
        std::vector<Token> m_tokens;
        std::size_t m_current = 0;
        bool m_had_error = false;

        /**
         * @brief Returns the current token without consuming it.
         */
        const Token &peek() const;

        /**
         * @brief Returns the token before the current position.
         */
        const Token &previous() const;

        /**
         * @brief Returns true if the current token is FIN_FICHIER.
         */
        bool is_at_end() const;

        /**
         * @brief Returns true if the current token is of the given type.
         * Does not consume.
         */
        bool check(TokenType type) const;

        /**
         * @brief Consumes and returns the current token.
         */
        const Token &advance();

        /**
         * @brief Consumes the current token if it matches any of the given types.
         * @return True if a match was found and consumed.
         */
        bool match(std::initializer_list<TokenType> types);

        /**
         * @brief Consumes the current token if it matches the expected type.
         *
         * Throws ParseError with the given message if the token does not match.
         *
         * @param type    The expected token type.
         * @param message Error message in French if the expectation fails.
         * @return The consumed token.
         */
        const Token &expect(TokenType type, const std::string &message);

        /**
         * @brief Constructs and throws a ParseError at the current position.
         */
        [[noreturn]] void error(const Token &token, const std::string &message);

        // Statement parsing

        /**
         * @brief Parses a single statement dispatching to the appropriate parser.
         */
        StmtPtr parse_statement();

        /**
         * @brief Parses a block of statements enclosed in { }.
         */
        StmtPtr parse_block();

        /**
         * @brief Parses a variable declaration — soit [fixe] name [: type] = expr.
         */
        StmtPtr parse_var_decl(bool is_public = false);

        /**
         * @brief Parses a function or method declaration.
         */
        StmtPtr parse_function_decl(bool is_prive, bool is_public, bool is_remplace);

        /**
         * @brief Parses an anonymous function expression.
         */
        ExprPtr parse_function_expr();

        /**
         * @brief Parses a class declaration.
         */
        StmtPtr parse_class_decl(bool is_public = false);

        /**
         * @brief Parses an interface declaration.
         */
        StmtPtr parse_interface_decl(bool is_public = false);

        /**
         * @brief Parses an import statement.
         */
        StmtPtr parse_import();
        Token parse_module_identifier();
        std::vector<ImportStmt::ImportedMember> parse_imported_members();

        /**
         * @brief Parses an if statement with optional sinon branch.
         */
        StmtPtr parse_if();

        /**
         * @brief Parses a pour chaque loop.
         */
        StmtPtr parse_for();

        /**
         * @brief Parses a tant que loop.
         */
        StmtPtr parse_while();

        /**
         * @brief Parses a retourne statement.
         */
        StmtPtr parse_return();

        /**
         * @brief Parses an arrêter statement.
         */
        StmtPtr parse_break();

        /**
         * @brief Parses a continuer statement.
         */
        StmtPtr parse_continue();

        /**
         * @brief Parses a lancer statement.
         */
        StmtPtr parse_throw();

        /**
         * @brief Parses an essayer / attraper / finalement block.
         */
        StmtPtr parse_try();

        /**
         * @brief Parses an agir selon statement.
         */
        StmtPtr parse_agir_selon();

        // Expression parsing — ordered low to high precedence for instance (4+(3+8))

        /**
         * @brief Parses an expression (entry point).
         */
        ExprPtr parse_expression();

        /**
         * @brief Parses assignment — x = expr.
         */
        ExprPtr parse_assignment();

        /**
         * @brief Parses logical OR — a ou b.
         */
        ExprPtr parse_or();

        /**
         * @brief Parses logical AND — a et b.
         */
        ExprPtr parse_and();

        /**
         * @brief Parses logical NOT — non a.
         */
        ExprPtr parse_not();

        /**
         * @brief Parses equality — a == b, a != b.
         */
        ExprPtr parse_equality();

        /**
         * @brief Parses comparison — a < b, a > b, a <= b, a >= b.
         */
        ExprPtr parse_comparison();

        /**
         * @brief Parses addition and subtraction — a + b, a - b.
         */
        ExprPtr parse_addition();

        /**
         * @brief Parses multiplication, division, modulo — a * b, a / b, a % b.
         */
        ExprPtr parse_multiplication();

        /**
         * @brief Parses unary operators — -a.
         */
        ExprPtr parse_unary();

        /**
         * @brief Parses cast expressions — a en Type.
         */
        ExprPtr parse_cast();

        /**
         * @brief Parses function calls, member access, and index access.
         */
        ExprPtr parse_call();

        /**
         * @brief Parses primary expressions — literals, identifiers, grouped expressions.
         */
        ExprPtr parse_primary();

        // Helpers

        /**
         * @brief Parses a comma-separated argument list inside parentheses.
         */
        std::vector<Argument> parse_arguments();

        /**
         * @brief Parses a comma-separated parameter list inside parentheses.
         */
        std::vector<Parameter> parse_parameters();

        /**
         * @brief Parses a type annotation such as Entier or Liste[Entier].
         */
        Token parse_type_annotation(const std::string &message);
    };

} // namespace lumiere
