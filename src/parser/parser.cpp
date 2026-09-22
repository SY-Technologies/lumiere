#include "lumiere/parser/parser.hpp"

#include <algorithm>
#include <string>

namespace lumiere
{
    namespace
    {
        bool can_appear_as_member_name(TokenType type)
        {
            switch (type)
            {
            case TokenType::IDENT:
            case TokenType::SOIT:
            case TokenType::FIXE:
            case TokenType::FONCTION:
            case TokenType::RETOURNE:
            case TokenType::CLASSE:
            case TokenType::INTERFACE:
            case TokenType::TYPE:
            case TokenType::REALISE:
            case TokenType::REMPLACE:
            case TokenType::PUBLIC:
            case TokenType::PRIVE:
            case TokenType::SI:
            case TokenType::SINON:
            case TokenType::POUR:
            case TokenType::CHAQUE:
            case TokenType::DANS:
            case TokenType::TANT_QUE:
            case TokenType::AGIR_SELON:
            case TokenType::ARRETER:
            case TokenType::CONTINUER:
            case TokenType::ICI:
            case TokenType::PARENT:
            case TokenType::EN:
            case TokenType::IMPORTER:
            case TokenType::COMME:
            case TokenType::EST:
            case TokenType::ET:
            case TokenType::OU:
            case TokenType::NON:
            case TokenType::VRAI:
            case TokenType::FAUX:
            case TokenType::RIEN:
                return true;
            default:
                return false;
            }
        }
    }

    Parser::Parser(std::vector<Token> tokens) : m_tokens(std::move(tokens)) {};

    const Token &Parser::peek() const
    {
        return m_tokens[m_current];
    };

    const Token &Parser::previous() const
    {

        int prev = m_current - 1;
        return m_tokens[prev];
    };

    bool Parser::is_at_end() const
    {

        return peek().type == TokenType::FIN_FICHIER;
    };

    bool Parser::check(TokenType type) const
    {
        if (is_at_end())
        {
            return false;
        }
        return type == m_tokens[m_current].type;
    };

    const Token &Parser::advance()
    {
        if (!is_at_end())
        {
            m_current++;
        }

        return previous();
    };
    bool Parser::match(std::initializer_list<TokenType> types)
    {
        if (is_at_end())
        {
            return false;
        }
        for (auto token_type : types)
        {
            if (check(token_type))
            {
                advance();
                return true;
            }
        }
        return false;
    }

    const Token &Parser::expect(TokenType type, const std::string &message)
    {
        if (!check(type))
        {
            error(m_tokens[m_current], message);
        }
        return advance();
    };

    [[noreturn]] void Parser::error(const Token &token, const std::string &message)
    {
        throw ParseError(token, message);
    };

    StmtList Parser::parse()
    {
        m_had_error = false;
        m_diagnostics.clear();
        StmtList stmts;
        while (!is_at_end())
        {
            const std::size_t statement_start = m_current;
            try
            {
                stmts.push_back(parse_statement());
            }
            catch (const ParseError &error)
            {
                m_had_error = true;
                const std::size_t start = error.token.start_offset;
                const std::size_t fallback_length = error.token.type == TokenType::FIN_FICHIER
                                                        ? 0
                                                        : std::max<std::size_t>(1, error.token.lexeme.size());
                const std::size_t end = error.token.end_offset > start
                                            ? error.token.end_offset
                                            : start + fallback_length;
                m_diagnostics.push_back({
                    "LUM-P0001",
                    DiagnosticSeverity::ERROR_LEVEL,
                    error.message,
                    "",
                    {start, end, error.token.line, error.token.column},
                });
                if (m_current == statement_start && !is_at_end())
                {
                    advance();
                }
                synchronize();
            }
        }
        if (m_had_error)
        {
            return {};
        }
        return stmts;
    }

    bool Parser::had_error() const
    {
        return m_had_error;
    }

    const std::vector<Diagnostic> &Parser::diagnostics() const noexcept
    {
        return m_diagnostics;
    }

    void Parser::synchronize()
    {
        while (!is_at_end())
        {
            switch (peek().type)
            {
            case TokenType::SOIT:
            case TokenType::FONCTION:
            case TokenType::CLASSE:
            case TokenType::INTERFACE:
            case TokenType::IMPORTER:
            case TokenType::SI:
            case TokenType::POUR:
            case TokenType::TANT_QUE:
            case TokenType::AGIR_SELON:
            case TokenType::RETOURNE:
            case TokenType::PUBLIC:
            case TokenType::PRIVE:
            case TokenType::REMPLACE:
                return;
            case TokenType::ACCOLADE_FERM:
                advance();
                return;
            default:
                advance();
                break;
            }
        }
    }

    StmtPtr Parser::parse_statement()
    {
        const std::string documentation = consume_documentation();
        StmtPtr statement = parse_statement_core();
        attach_documentation(statement.get(), documentation);
        return statement;
    }

    std::string Parser::consume_documentation()
    {
        std::string text;
        while (check(TokenType::DOCUMENTATION))
        {
            const Token token = advance();
            if (!text.empty())
            {
                text.push_back('\n');
            }
            text += token.lexeme;
        }
        return text;
    }

    void Parser::attach_documentation(Stmt *statement, std::string documentation)
    {
        if (statement == nullptr || documentation.empty())
        {
            return;
        }
        if (auto *variable = dynamic_cast<VarDeclStmt *>(statement))
        {
            variable->documentation = std::move(documentation);
        }
        else if (auto *function = dynamic_cast<FunctionDeclStmt *>(statement))
        {
            function->documentation = std::move(documentation);
        }
        else if (auto *klass = dynamic_cast<ClassDeclStmt *>(statement))
        {
            klass->documentation = std::move(documentation);
        }
        else if (auto *interface = dynamic_cast<InterfaceDeclStmt *>(statement))
        {
            interface->documentation = std::move(documentation);
        }
        else if (auto *alias = dynamic_cast<TypeAliasDeclStmt *>(statement))
        {
            alias->documentation = std::move(documentation);
        }
    }

    StmtPtr Parser::parse_statement_core()
    {
        if (check(TokenType::SOIT))
        {
            return parse_var_decl();
        }
        if (check(TokenType::CLASSE))
        {
            return parse_class_decl();
        }
        if (check(TokenType::INTERFACE))
        {
            return parse_interface_decl();
        }
        if (check(TokenType::TYPE))
        {
            return parse_type_alias();
        }
        if (check(TokenType::IMPORTER))
        {
            return parse_import();
        }
        if (check(TokenType::SI))
        {
            return parse_if();
        };
        if (check(TokenType::POUR))
        {
            return parse_for();
        };
        if (check(TokenType::TANT_QUE))
        {
            return parse_while();
        };
        if (check(TokenType::AGIR_SELON))
        {
            return std::make_unique<ExprStmt>(parse_agir_selon());
        };
        if (check(TokenType::RETOURNE))
        {
            return parse_return();
        };
        if (check(TokenType::ARRETER))
        {
            return parse_break();
        };
        if (check(TokenType::CONTINUER))
        {
            return parse_continue();
        };
        if (check(TokenType::IGNORER))
        {
            Token keyword = advance();
            if (check(TokenType::PAREN_OUV))
            {
                error(
                    peek(),
                    "'ignorer' est suivi directement de l'expression, sans parenthèses d'appel");
            }
            ExprPtr expr = parse_expression();
            return std::make_unique<IgnorerStmt>(std::move(keyword), std::move(expr));
        };
        if (check(TokenType::ACCOLADE_OUV))
        {
            return parse_block();
        };

        // visibility and override modifiers, peek ahead for fonction
        if (check(TokenType::PRIVE) || check(TokenType::PUBLIC))
        {
            bool is_prive = peek().type == TokenType::PRIVE;
            bool is_public = peek().type == TokenType::PUBLIC;
            advance(); // consume privé or public

            if (check(TokenType::FONCTION))
            {
                return parse_function_decl(is_prive, is_public, false);
            }
            if (check(TokenType::SOIT))
            {
                return parse_var_decl(is_public);
            }
            if (check(TokenType::CLASSE))
            {
                if (is_prive)
                {
                    error(peek(), "une classe de niveau fichier ne peut pas être marquée 'privé'");
                }
                return parse_class_decl(true);
            }
            if (check(TokenType::INTERFACE))
            {
                if (is_prive)
                {
                    error(peek(), "une interface de niveau fichier ne peut pas être marquée 'privé'");
                }
                return parse_interface_decl(true);
            }
            if (check(TokenType::TYPE))
            {
                if (is_prive)
                {
                    error(peek(), "un alias de type de niveau fichier ne peut pas être marqué 'privé'");
                }
                return parse_type_alias(true);
            }

            // field declaration with visibility modifier
            if (check(TokenType::IDENT) &&
                m_tokens[m_current + 1].type == TokenType::DEUX_POINTS)
            {
                const Token &name = advance();
                advance(); // consume :
                TypeExpr type = parse_type_annotation("attendu un type de champ");
                return std::make_unique<VarDeclStmt>(name, std::move(type), false, is_prive, !is_prive, nullptr);
            }

            error(peek(), "attendu 'soit', 'fonction', 'classe', 'interface', 'type' ou champ après modificateur de visibilité");
        }

        if (check(TokenType::REMPLACE))
        {
            advance(); // consume remplace
            bool is_prive = false;
            if (check(TokenType::PRIVE))
            {
                is_prive = true;
                advance();
            }
            if (check(TokenType::FONCTION))
            {
                return parse_function_decl(is_prive, false, true);
            };
            error(peek(), "attendu 'fonction' après 'remplace'");
        }

        if (check(TokenType::FONCTION))
        {
            return parse_function_decl(false, false, false);
        }
        // field declaration inside class — IDENT : Type
        if (check(TokenType::IDENT) && m_tokens[m_current + 1].type == TokenType::DEUX_POINTS)
        {
            const Token &name = advance(); // consume name
            advance();                     // consume :
            TypeExpr type = parse_type_annotation("attendu un type de champ");
            return std::make_unique<VarDeclStmt>(name, std::move(type), false, false, true, nullptr);
        }

        // anything else is an expression statement
        ExprPtr expr = parse_expression();
        return std::make_unique<ExprStmt>(std::move(expr));
    }
    StmtPtr Parser::parse_import()
    {
        advance(); // consume "importer"

        const Token name = parse_module_identifier();
        std::vector<ImportStmt::ImportedMember> imported_members;

        Token alias(TokenType::RIEN, "", name.line, name.column);
        if (check(TokenType::POINT) && m_tokens[m_current + 1].type == TokenType::ACCOLADE_OUV)
        {
            advance(); // consume .
            imported_members = parse_imported_members();
        }
        else if (check(TokenType::COMME))
        {
            advance(); // consume "comme"
            alias = expect(TokenType::IDENT, "attendu un alias après 'comme'");
        }

        return std::make_unique<ImportStmt>(name, alias, std::move(imported_members));
    }

    Token Parser::parse_module_identifier()
    {
        const Token &first = expect(TokenType::IDENT, "attendu le nom du module après 'importer'");
        std::string module_name = first.lexeme;

        while (check(TokenType::POINT) && m_tokens[m_current + 1].type == TokenType::IDENT)
        {
            advance();
            const Token &segment = expect(TokenType::IDENT, "attendu un segment de package après '.'");
            module_name += ".";
            module_name += segment.lexeme;
        }

        return Token(TokenType::IDENT, std::move(module_name), first.line, first.column);
    }

    std::vector<ImportStmt::ImportedMember> Parser::parse_imported_members()
    {
        expect(TokenType::ACCOLADE_OUV, "attendu '{' pour ouvrir la liste des imports sélectifs");

        std::vector<ImportStmt::ImportedMember> members;
        do
        {
            const Token &name = expect(TokenType::IDENT, "attendu un nom membre dans l'import sélectif");
            Token alias(TokenType::RIEN, "", name.line, name.column);

            if (check(TokenType::COMME))
            {
                advance();
                alias = expect(TokenType::IDENT, "attendu un alias après 'comme'");
            }

            members.emplace_back(name, alias);
        } while (match({TokenType::VIRGULE}));

        expect(TokenType::ACCOLADE_FERM, "attendu '}' après la liste des imports sélectifs");
        return members;
    }

    StmtPtr Parser::parse_break()
    {
        const Token &keyword = advance(); // consume arrêter
        return std::make_unique<BreakStmt>(keyword);
    }

    StmtPtr Parser::parse_continue()
    {
        const Token &keyword = advance(); // consume continuer
        return std::make_unique<ContinueStmt>(keyword);
    }

    StmtPtr Parser::parse_return()
    {
        const Token &keyword = advance(); // consume retourne

        // bare retourne with no value
        if (is_at_end() || check(TokenType::ACCOLADE_FERM))
        {
            return std::make_unique<ReturnStmt>(keyword, nullptr);
        }

        ExprPtr value = parse_expression();
        return std::make_unique<ReturnStmt>(keyword, std::move(value));
    }

    StmtPtr Parser::parse_block()
    {
        expect(TokenType::ACCOLADE_OUV, "attendu '{' pour ouvrir le bloc");

        StmtList statements;
        while (!is_at_end() && !check(TokenType::ACCOLADE_FERM))
        {
            statements.push_back(parse_statement());
        }

        expect(TokenType::ACCOLADE_FERM, "attendu '}' pour fermer le bloc");
        return std::make_unique<BlockStmt>(std::move(statements));
    }
    StmtPtr Parser::parse_var_decl(bool is_public)
    {
        advance(); // consume soit

        bool is_fixe = false;
        if (check(TokenType::FIXE))
        {
            is_fixe = true;
            advance(); // consume fixe
        }

        const Token &name = expect(TokenType::IDENT, "attendu un nom de variable après 'soit'");

        // optional type annotation — : Type
        TypeExpr type;
        if (check(TokenType::DEUX_POINTS))
        {
            advance(); // consume :
            type = parse_type_annotation("attendu un type après ':'");
        }

        // optional initializer — = expr
        ExprPtr initializer = nullptr;
        if (check(TokenType::EGAL))
        {
            advance(); // consume =
            initializer = parse_expression();
        }

        return std::make_unique<VarDeclStmt>(name, std::move(type), is_fixe, false, is_public, std::move(initializer));
    }

    StmtPtr Parser::parse_if()
    {
        advance(); // consume si

        // optional parentheses around condition
        bool has_parens = check(TokenType::PAREN_OUV);
        if (has_parens)
        {
            advance();
        } // consume (

        ExprPtr condition = parse_expression();

        if (has_parens)
        {
            expect(TokenType::PAREN_FERM, "attendu ')' après la condition");
        }

        StmtPtr then_branch = parse_block();

        // optional sinon or sinon si
        StmtPtr else_branch = nullptr;
        if (check(TokenType::SINON))
        {
            advance(); // consume sinon

            if (check(TokenType::SI))
            {
                else_branch = parse_if();
            } // sinon si — recursive
            else
            {
                else_branch = parse_block();
            }
        }

        return std::make_unique<IfStmt>(
            std::move(condition),
            std::move(then_branch),
            std::move(else_branch));
    }
    StmtPtr Parser::parse_while()
    {
        advance(); // consume tant que

        // optional parentheses around condition
        bool has_parens = check(TokenType::PAREN_OUV);
        if (has_parens)
        {
            advance(); // consume (
        }

        ExprPtr condition = parse_expression();

        if (has_parens)
        {
            expect(TokenType::PAREN_FERM, "attendu ')' après la condition");
        }

        StmtPtr body = parse_block();

        return std::make_unique<WhileStmt>(
            std::move(condition),
            std::move(body));
    }

    StmtPtr Parser::parse_for()
    {
        advance(); // consume pour

        expect(TokenType::CHAQUE, "attendu 'chaque' après 'pour'");

        const Token &variable = expect(TokenType::IDENT, "attendu un nom de variable après 'chaque'");

        expect(TokenType::DANS, "attendu 'dans' après le nom de variable");

        ExprPtr iterable = parse_expression();

        StmtPtr body = parse_block();

        return std::make_unique<ForStmt>(
            variable,
            std::move(iterable),
            std::move(body));
    }
    std::unique_ptr<AgirSelonStmt> Parser::parse_agir_selon()
    {
        const Token &keyword = advance(); // consume agir selon
        ExprPtr matched_expr = parse_expression();

        expect(TokenType::ACCOLADE_OUV, "attendu '{' après l'expression de 'agir selon'");

        std::vector<AgirSelonBranch> branches;
        StmtPtr else_branch = nullptr;

        while (!is_at_end() && !check(TokenType::ACCOLADE_FERM))
        {
            if (check(TokenType::SINON))
            {
                advance(); // consume sinon
                expect(TokenType::FLECHE, "attendu '->' après 'sinon'");

                if (check(TokenType::ACCOLADE_OUV))
                {
                    else_branch = parse_block();
                }
                else
                {
                    else_branch = std::make_unique<ExprStmt>(parse_expression());
                }
                break;
            }

            std::vector<Pattern> patterns;
            do
            {
                if (check(TokenType::IDENT) &&
                    (peek().lexeme == "Succès" || peek().lexeme == "Échec") &&
                    m_tokens[m_current + 1].type == TokenType::PAREN_OUV)
                {
                    const Token constructor = advance();
                    advance(); // consume (
                    const Token binding = expect(
                        TokenType::IDENT,
                        "attendu un nom ou '_' dans le motif de résultat");
                    TypeExpr narrowed_type;
                    if (match({TokenType::DEUX_POINTS}))
                    {
                        narrowed_type =
                            parse_type_annotation("attendu un type d'erreur");
                    }
                    expect(TokenType::PAREN_FERM,
                           "attendu ')' après le motif de résultat");
                    patterns.emplace_back(
                        constructor.lexeme == "Succès"
                            ? PatternKind::RESULT_SUCCESS
                            : PatternKind::RESULT_FAILURE,
                        constructor,
                        binding,
                        std::move(narrowed_type));
                }
                else if (check(TokenType::RIEN))
                {
                    patterns.emplace_back(advance());
                }
                else if (check(TokenType::IDENT) && m_tokens[m_current + 1].type == TokenType::DEUX_POINTS)
                {
                    const Token &name = advance();
                    advance(); // consume :
                    TypeExpr type = parse_type_annotation("attendu un type de motif");
                    patterns.emplace_back(name, std::move(type));
                }
                else
                {
                    patterns.emplace_back(parse_expression());
                }
            } while (match({TokenType::VIRGULE}));

            expect(TokenType::FLECHE, "attendu '->' après le motif");

            AgirSelonBranch branch(std::move(patterns), nullptr);
            if (check(TokenType::PROPAGER))
            {
                branch.terminator = BranchTerminator::PROPAGER;
                branch.terminator_token = advance();
            }
            else if (check(TokenType::IGNORER))
            {
                branch.terminator = BranchTerminator::IGNORER;
                branch.terminator_token = advance();
            }
            else if (check(TokenType::ACCOLADE_OUV))
            {
                branch.body = parse_block();
            }
            else if (check(TokenType::RETOURNE))
            {
                branch.body = parse_return();
            }
            else
            {
                branch.body = std::make_unique<ExprStmt>(parse_expression());
            }

            branches.push_back(std::move(branch));
        }

        expect(TokenType::ACCOLADE_FERM, "attendu '}' après 'agir selon'");

        return std::make_unique<AgirSelonStmt>(
            keyword,
            std::move(matched_expr),
            std::move(branches),
            std::move(else_branch));
    }

    StmtPtr Parser::parse_function_decl(bool is_prive, bool is_public, bool is_remplace)
    {
        advance(); // consume fonction

        const Token &name = expect(TokenType::IDENT, "attendu un nom de fonction");

        expect(TokenType::PAREN_OUV, "attendu '(' après le nom de fonction");
        std::vector<Parameter> params = parse_parameters();
        expect(TokenType::PAREN_FERM, "attendu ')' après les paramètres");

        // optional return type — -> Type
        TypeExpr return_type;
        if (check(TokenType::FLECHE))
        {
            advance(); // consume ->
            return_type = parse_type_annotation("attendu un type de retour après '->'");
        }

        // interface methods have no body
        StmtPtr body = nullptr;
        if (check(TokenType::ACCOLADE_OUV))
        {
            body = parse_block();
        }

        return std::make_unique<FunctionDeclStmt>(
            name,
            std::move(params),
            return_type,
            std::move(body),
            is_prive,
            is_public,
            is_remplace);
    }

    ExprPtr Parser::parse_function_expr()
    {
        const Token keyword = advance(); // consume fonction

        expect(TokenType::PAREN_OUV, "attendu '(' après 'fonction'");
        std::vector<Parameter> params = parse_parameters();
        expect(TokenType::PAREN_FERM, "attendu ')' après les paramètres");

        TypeExpr return_type;
        if (check(TokenType::FLECHE))
        {
            advance();
            return_type = parse_type_annotation("attendu un type de retour après '->'");
        }

        StmtPtr body;
        if (check(TokenType::ACCOLADE_OUV))
        {
            body = parse_block();
        }
        else
        {
            body = std::make_unique<ExprStmt>(parse_expression());
        }

        return std::make_unique<FunctionExpr>(keyword, std::move(params), return_type, std::move(body));
    }

    std::vector<Parameter> Parser::parse_parameters()
    {
        std::vector<Parameter> params;

        if (check(TokenType::PAREN_FERM))
        {
            return params; // empty parameter list
        }

        do
        {
            const Token &name = expect(TokenType::IDENT, "attendu un nom de paramètre");
            expect(TokenType::DEUX_POINTS, "attendu ':' après le nom du paramètre");
            TypeExpr type = parse_type_annotation("attendu un type de paramètre");

            // optional default value
            ExprPtr default_value = nullptr;
            if (check(TokenType::EGAL))
            {
                advance(); // consume =
                default_value = parse_expression();
            }

            params.push_back(Parameter{name.lexeme, name, std::move(type), std::move(default_value)});
        } while (match({TokenType::VIRGULE}));

        return params;
    }

    TypeExpr Parser::parse_type_annotation(const std::string &message)
    {
        return parse_union_type(message);
    }

    TypeExpr Parser::parse_union_type(const std::string &message)
    {
        TypeExpr first = parse_generic_type(message);
        if (!check(TokenType::PIPE))
        {
            return first;
        }

        std::vector<TypeExpr> alternatives;
        alternatives.push_back(std::move(first));
        while (match({TokenType::PIPE}))
        {
            alternatives.push_back(parse_generic_type("attendu un type après '|'"));
        }

        Token source = alternatives.front().source;
        source.end_offset = alternatives.back().source.end_offset;
        source.lexeme.clear();
        return TypeExpr::union_of(std::move(source), std::move(alternatives));
    }

    TypeExpr Parser::parse_generic_type(const std::string &message)
    {
        Token first = expect(TokenType::IDENT, message);
        std::string name = first.lexeme;
        Token source = first;

        while (match({TokenType::POINT}))
        {
            const Token &part = expect(TokenType::IDENT, "attendu un nom de type après '.'");
            name += '.' + part.lexeme;
            source.end_offset = part.end_offset;
        }

        if (!match({TokenType::CROCHET_OUV}))
        {
            source.lexeme = name;
            return TypeExpr::named(std::move(source));
        }

        if (check(TokenType::CROCHET_FERM))
        {
            error(peek(), "un type générique requiert au moins un argument");
        }

        std::vector<TypeExpr> arguments;
        do
        {
            if (check(TokenType::ENTIER_LIT))
            {
                Token integer_token = advance();
                try
                {
                    arguments.push_back(TypeExpr::integer_argument(
                        integer_token,
                        static_cast<std::uint64_t>(std::stoull(integer_token.lexeme))));
                }
                catch (const std::exception &)
                {
                    error(integer_token, "argument entier de type hors limites");
                }
            }
            else
            {
                arguments.push_back(parse_union_type("attendu un argument de type"));
            }

            if (check(TokenType::CROCHET_FERM))
            {
                break;
            }
            expect(TokenType::VIRGULE, "attendu ',' ou ']' après l'argument de type");
            if (check(TokenType::CROCHET_FERM))
            {
                error(peek(), "argument de type attendu après ','");
            }
        } while (true);

        const Token &closing = expect(TokenType::CROCHET_FERM, "attendu ']' pour fermer le type générique");
        source.end_offset = closing.end_offset;
        source.lexeme.clear();
        return TypeExpr::generic(std::move(source), std::move(name), std::move(arguments));
    }

    std::vector<Argument> Parser::parse_arguments()
    {
        std::vector<Argument> args;

        if (check(TokenType::PAREN_FERM))
        {
            return args; // empty argument list
        }

        do
        {
            Argument arg{"", peek(), nullptr};

            // check for named argument — name: value
            if (can_appear_as_member_name(peek().type) &&
                m_tokens[m_current + 1].type == TokenType::DEUX_POINTS)
            {
                arg.name = peek().lexeme;
                advance(); // consume name
                advance(); // consume :
            }

            arg.value = parse_expression();
            args.push_back(std::move(arg));
        } while (match({TokenType::VIRGULE}));

        return args;
    }

    StmtPtr Parser::parse_class_decl(bool is_public)
    {
        advance(); // consume classe

        const Token &name = expect(TokenType::IDENT, "attendu un nom de classe");

        // optional parent class — : Parent
        TypeExpr parent;
        if (check(TokenType::DEUX_POINTS))
        {
            advance(); // consume :
            parent = parse_generic_type("attendu un nom de classe parente après ':'");
        }

        // optional interfaces — réalise A, B, C
        std::vector<TypeExpr> interfaces;
        if (check(TokenType::REALISE))
        {
            advance(); // consume réalise
            do
            {
                interfaces.push_back(
                    parse_generic_type("attendu un nom d'interface après 'réalise'"));
            } while (match({TokenType::VIRGULE}));
        }

        // class body
        expect(TokenType::ACCOLADE_OUV, "attendu '{' pour ouvrir le corps de la classe");

        std::vector<StmtPtr> members;
        while (!is_at_end() && !check(TokenType::ACCOLADE_FERM))
        {
            members.push_back(parse_statement());
        }

        expect(TokenType::ACCOLADE_FERM, "attendu '}' pour fermer le corps de la classe");

        return std::make_unique<ClassDeclStmt>(
            name,
            parent,
            is_public,
            std::move(interfaces),
            std::move(members));
    }

    StmtPtr Parser::parse_interface_decl(bool is_public)
    {
        advance(); // consume interface

        const Token &name = expect(TokenType::IDENT, "attendu un nom d'interface");

        expect(TokenType::ACCOLADE_OUV, "attendu '{' pour ouvrir le corps de l'interface");

        std::vector<StmtPtr> methods;
        while (!is_at_end() && !check(TokenType::ACCOLADE_FERM))
        {
            // interface only allows function signatures — no visibility modifiers
            if (!check(TokenType::FONCTION))
            {
                error(peek(), "attendu 'fonction' dans le corps de l'interface");
            }

            methods.push_back(parse_function_decl(false, true, false));
        }

        expect(TokenType::ACCOLADE_FERM, "attendu '}' pour fermer le corps de l'interface");

        return std::make_unique<InterfaceDeclStmt>(name, is_public, std::move(methods));
    }

    StmtPtr Parser::parse_type_alias(bool is_public)
    {
        advance(); // consume type
        const Token &name = expect(TokenType::IDENT, "attendu un nom d'alias de type");
        expect(TokenType::EGAL, "attendu '=' après le nom de l'alias");
        TypeExpr target = parse_type_annotation("attendu le type ciblé par l'alias");
        return std::make_unique<TypeAliasDeclStmt>(name, std::move(target), is_public);
    }

    ExprPtr Parser::parse_expression()
    {
        return parse_assignment();
    }

    ExprPtr Parser::parse_assignment()
    {
        ExprPtr left = parse_or();

        if (check(TokenType::EGAL))
        {
            Token op = advance(); // consume =

            // assignment is right-associative — recurse on the right side
            ExprPtr right = parse_assignment();

            // left side must be a valid assignment target
            if (dynamic_cast<IdentifierExpr *>(left.get()) ||
                dynamic_cast<MemberAccessExpr *>(left.get()) ||
                dynamic_cast<IndexAccessExpr *>(left.get()))
            {
                return std::make_unique<BinaryExpr>(
                    std::move(left),
                    std::move(op),
                    std::move(right));
            }

            error(op, "cible d'affectation invalide");
        }

        return left;
    }

    ExprPtr Parser::parse_or()
    {
        ExprPtr left = parse_and();

        while (match({TokenType::OU}))
        {
            Token op = previous();
            if (check(TokenType::PROPAGER))
            {
                Token keyword = advance();
                left = std::make_unique<PropagationExpr>(
                    std::move(keyword),
                    std::move(left));
                break;
            }
            ExprPtr right = parse_and();
            left = std::make_unique<BinaryExpr>(
                std::move(left),
                std::move(op),
                std::move(right));
        }

        return left;
    }

    ExprPtr Parser::parse_and()
    {
        ExprPtr left = parse_not();

        while (match({TokenType::ET}))
        {
            Token op = previous();
            ExprPtr right = parse_not();
            left = std::make_unique<BinaryExpr>(
                std::move(left),
                std::move(op),
                std::move(right));
        }

        return left;
    }

    ExprPtr Parser::parse_not()
    {
        if (match({TokenType::NON}))
        {
            Token op = previous();
            ExprPtr operand = parse_not(); // recursive — handles "non non a"
            return std::make_unique<UnaryExpr>(std::move(op), std::move(operand));
        }

        return parse_equality();
    }
    ExprPtr Parser::parse_equality()
    {
        ExprPtr left = parse_comparison();

        while (match({TokenType::EGAL_EGAL, TokenType::BANG_EGAL}))
        {
            Token op = previous();
            ExprPtr right = parse_comparison();
            left = std::make_unique<BinaryExpr>(
                std::move(left),
                std::move(op),
                std::move(right));
        }

        return left;
    }

    ExprPtr Parser::parse_comparison()
    {
        ExprPtr left = parse_addition();

        while (true)
        {
            if (match({TokenType::INFERIEUR,
                       TokenType::INFERIEUR_EGAL,
                       TokenType::SUPERIEUR,
                       TokenType::SUPERIEUR_EGAL}))
            {
                Token op = previous();
                ExprPtr right = parse_addition();
                left = std::make_unique<BinaryExpr>(
                    std::move(left),
                    std::move(op),
                    std::move(right));
                continue;
            }

            if (match({TokenType::EST}))
            {
                Token keyword = previous();
                TypeExpr type = parse_type_annotation("attendu un type après 'est'");
                left = std::make_unique<TypeCheckExpr>(std::move(left), std::move(keyword), std::move(type));
                continue;
            }

            break;
        }

        return left;
    }
    ExprPtr Parser::parse_addition()
    {
        ExprPtr left = parse_multiplication();

        while (match({TokenType::PLUS, TokenType::MOINS}))
        {
            Token op = previous();
            ExprPtr right = parse_multiplication();
            left = std::make_unique<BinaryExpr>(
                std::move(left),
                std::move(op),
                std::move(right));
        }

        return left;
    }

    ExprPtr Parser::parse_multiplication()
    {
        ExprPtr left = parse_unary();

        while (match({TokenType::ETOILE, TokenType::SLASH, TokenType::MODULO}))
        {
            Token op = previous();
            ExprPtr right = parse_unary();
            left = std::make_unique<BinaryExpr>(
                std::move(left),
                std::move(op),
                std::move(right));
        }

        return left;
    }

    ExprPtr Parser::parse_unary()
    {
        if (match({TokenType::MOINS}))
        {
            Token op = previous();
            ExprPtr operand = parse_unary(); // recursive — handles "--a" edge case
            return std::make_unique<UnaryExpr>(std::move(op), std::move(operand));
        }

        return parse_cast();
    }

    ExprPtr Parser::parse_cast()
    {
        ExprPtr expr = parse_call();

        while (match({TokenType::EN}))
        {
            TypeExpr target_type = parse_type_annotation("attendu un type après 'en'");
            expr = std::make_unique<CastExpr>(std::move(expr), std::move(target_type));
        }

        return expr;
    }

    ExprPtr Parser::parse_call()
    {
        ExprPtr expr = parse_primary();

        while (true)
        {
            if (match({TokenType::PAREN_OUV}))
            {
                // function or constructor call — foo(args)
                Token paren = previous();
                std::vector<Argument> args = parse_arguments();
                expect(TokenType::PAREN_FERM, "attendu ')' après les arguments");
                expr = std::make_unique<CallExpr>(
                    std::move(expr),
                    std::move(paren),
                    std::move(args));
            }
            else if (match({TokenType::POINT}))
            {
                // member access — obj.member
                Token dot = previous();
                if (!can_appear_as_member_name(peek().type))
                {
                    error(peek(), "attendu un nom de membre après '.'");
                }
                const Token member = advance();
                expr = std::make_unique<MemberAccessExpr>(
                    std::move(expr),
                    std::move(dot),
                    member);
            }
            else if (match({TokenType::CROCHET_OUV}))
            {
                // index access — obj[index]
                Token bracket = previous();
                ExprPtr index = parse_expression();
                expect(TokenType::CROCHET_FERM, "attendu ']' après l'index");
                expr = std::make_unique<IndexAccessExpr>(
                    std::move(expr),
                    std::move(bracket),
                    std::move(index));
            }
            else
            {
                break;
            }
        }

        return expr;
    }
    ExprPtr Parser::parse_primary()
    {
        // ── Literals
        if (match({TokenType::ENTIER_LIT,
                   TokenType::DECIMAL_LIT,
                   TokenType::TEXTE_LIT,
                   TokenType::SYMBOLE_LIT,
                   TokenType::VRAI,
                   TokenType::FAUX,
                   TokenType::RIEN}))
        {
            return std::make_unique<LiteralExpr>(previous());
        }

        // ── Identifier
        if (match({TokenType::IDENT}))
        {
            return std::make_unique<IdentifierExpr>(previous());
        }

        // ── Anonymous function expression
        if (check(TokenType::FONCTION))
        {
            return parse_function_expr();
        }

        if (check(TokenType::AGIR_SELON))
        {
            return parse_agir_selon();
        }

        // ── Ici
        if (match({TokenType::ICI}))
        {
            return std::make_unique<IdentifierExpr>(previous());
        }

        // ── Parent
        if (match({TokenType::PARENT}))
        {
            return std::make_unique<IdentifierExpr>(previous());
        }

        // ── List literal — [a, b, c]
        if (match({TokenType::CROCHET_OUV}))
        {
            Token bracket = previous();
            ExprList elements;

            if (!check(TokenType::CROCHET_FERM))
            {
                do
                {
                    elements.push_back(parse_expression());
                } while (match({TokenType::VIRGULE}));
            }

            expect(TokenType::CROCHET_FERM, "attendu ']' après la liste");
            return std::make_unique<ListExpr>(std::move(bracket), std::move(elements));
        }

        // ── Dictionary literal — {clé: valeur} — or set literal — {élément, ...}
        // The first entry decides: a ':' after it means a dictionary. An empty
        // '{}' is the empty dictionary; the empty set is written [].en_ensemble().
        if (match({TokenType::ACCOLADE_OUV}))
        {
            Token brace = previous();

            if (match({TokenType::ACCOLADE_FERM}))
            {
                return std::make_unique<DictionaryExpr>(std::move(brace), std::vector<DictionaryEntryExpr>{});
            }

            ExprPtr first = parse_expression();

            if (match({TokenType::DEUX_POINTS}))
            {
                std::vector<DictionaryEntryExpr> entries;
                entries.push_back(DictionaryEntryExpr{std::move(first), parse_expression()});
                while (match({TokenType::VIRGULE}))
                {
                    ExprPtr key = parse_expression();
                    expect(TokenType::DEUX_POINTS, "attendu ':' après la clé du dictionnaire");
                    entries.push_back(DictionaryEntryExpr{std::move(key), parse_expression()});
                }
                expect(TokenType::ACCOLADE_FERM, "attendu '}' après le dictionnaire");
                return std::make_unique<DictionaryExpr>(std::move(brace), std::move(entries));
            }

            ExprList elements;
            elements.push_back(std::move(first));
            while (match({TokenType::VIRGULE}))
            {
                elements.push_back(parse_expression());
            }
            expect(TokenType::ACCOLADE_FERM, "attendu '}' après l'ensemble");
            return std::make_unique<SetExpr>(std::move(brace), std::move(elements));
        }

        // ── Grouped expression — (expr)
        if (match({TokenType::PAREN_OUV}))
        {
            ExprPtr expr = parse_expression();
            expect(TokenType::PAREN_FERM, "attendu ')' après l'expression");
            return expr;
        }

        // ── Nothing matched
        error(peek(), "expression attendue, trouvé '" + peek().lexeme + "'");
    }
} // namespace lumiere
