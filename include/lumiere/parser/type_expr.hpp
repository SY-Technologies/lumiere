#pragma once

#include "lumiere/lexer/token.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lumiere
{

enum class TypeExprKind
{
    EMPTY,
    NAMED,
    GENERIC,
    UNION,
    INTEGER_ARGUMENT,
};

struct TypeExpr
{
    TypeExprKind kind = TypeExprKind::EMPTY;
    Token source = Token(TokenType::RIEN, "", 0, 0);
    std::string name;
    std::vector<TypeExpr> children;
    std::uint64_t integer = 0;

    [[nodiscard]] static TypeExpr named(Token name_token)
    {
        TypeExpr type;
        type.kind = TypeExprKind::NAMED;
        type.name = name_token.lexeme;
        type.source = std::move(name_token);
        return type;
    }

    [[nodiscard]] static TypeExpr generic(Token source_token,
                                          std::string base,
                                          std::vector<TypeExpr> arguments)
    {
        TypeExpr type;
        type.kind = TypeExprKind::GENERIC;
        type.source = std::move(source_token);
        type.name = std::move(base);
        type.children = std::move(arguments);
        return type;
    }

    [[nodiscard]] static TypeExpr union_of(Token source_token,
                                           std::vector<TypeExpr> alternatives)
    {
        TypeExpr type;
        type.kind = TypeExprKind::UNION;
        type.source = std::move(source_token);
        type.children = std::move(alternatives);
        return type;
    }

    [[nodiscard]] static TypeExpr integer_argument(Token token, std::uint64_t value)
    {
        TypeExpr type;
        type.kind = TypeExprKind::INTEGER_ARGUMENT;
        type.source = std::move(token);
        type.integer = value;
        return type;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return kind == TypeExprKind::EMPTY;
    }

    [[nodiscard]] std::string to_string() const
    {
        switch (kind)
        {
        case TypeExprKind::EMPTY:
            return {};
        case TypeExprKind::NAMED:
            return name;
        case TypeExprKind::INTEGER_ARGUMENT:
            return std::to_string(integer);
        case TypeExprKind::GENERIC:
        {
            std::string rendered = name + '[';
            for (std::size_t i = 0; i < children.size(); ++i)
            {
                if (i > 0)
                {
                    rendered += ',';
                }
                rendered += children[i].to_string();
            }
            rendered += ']';
            return rendered;
        }
        case TypeExprKind::UNION:
        {
            std::string rendered;
            for (std::size_t i = 0; i < children.size(); ++i)
            {
                if (i > 0)
                {
                    rendered += " | ";
                }
                rendered += children[i].to_string();
            }
            return rendered;
        }
        }
        return {};
    }

    [[nodiscard]] Token as_token() const
    {
        Token token = source;
        token.type = TokenType::IDENT;
        token.lexeme = to_string();
        return token;
    }
};

} // namespace lumiere
