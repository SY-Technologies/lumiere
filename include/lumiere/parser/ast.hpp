#pragma once

#include "lumiere/lexer/token.hpp"
#include "lumiere/parser/type_expr.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lumiere
{
    // forward declarations
    struct Expr; //An expression is a piece of code that produces a value.
    struct Stmt; //A statement is a piece of code that performs an action.

    // type aliases
    using ExprPtr = std::unique_ptr<Expr>;
    using StmtPtr = std::unique_ptr<Stmt>;
    using ExprList = std::vector<ExprPtr>;
    using StmtList = std::vector<StmtPtr>;

    struct Argument
    {
        std::string name;
        ExprPtr value;
    };

    struct Parameter
    {
        std::string name;
        Token name_token;
        TypeExpr type;
        ExprPtr default_value;
    };

    //TODO: add comments in front of each struct line 

    // Forward declarations

    // Expressions
    struct LiteralExpr;
    struct IdentifierExpr;
    struct BinaryExpr;
    struct DictionaryExpr;
    struct UnaryExpr;
    struct CastExpr;
    struct TypeCheckExpr;
    struct FunctionExpr;
    struct CallExpr;
    struct ListExpr;
    struct MemberAccessExpr;
    struct IndexAccessExpr;
    struct PropagationExpr;

    // Statements
    struct BlockStmt;
    struct VarDeclStmt;
    struct FunctionDeclStmt;
    struct ClassDeclStmt;
    struct InterfaceDeclStmt;
    struct TypeAliasDeclStmt;
    struct ImportStmt;
    struct IfStmt;
    struct ForStmt;
    struct WhileStmt;
    struct ReturnStmt;
    struct BreakStmt;
    struct ContinueStmt;
    struct ExprStmt;
    struct IgnorerStmt;
    struct AgirSelonStmt;

    // Visitors 

    struct ExprVisitor
    {
        virtual ~ExprVisitor() = default;
        virtual void visit(LiteralExpr &) = 0;
        virtual void visit(IdentifierExpr &) = 0;
        virtual void visit(BinaryExpr &) = 0;
        virtual void visit(DictionaryExpr &) = 0;
        virtual void visit(UnaryExpr &) = 0;
        virtual void visit(CastExpr &) = 0;
        virtual void visit(TypeCheckExpr &) = 0;
        virtual void visit(FunctionExpr &) = 0;
        virtual void visit(CallExpr &) = 0;
        virtual void visit(ListExpr &) = 0;
        virtual void visit(MemberAccessExpr &) = 0;
        virtual void visit(IndexAccessExpr &) = 0;
        virtual void visit(PropagationExpr &) = 0;
        virtual void visit(AgirSelonStmt &) = 0;
    };

    struct StmtVisitor
    {
        virtual ~StmtVisitor() = default;
        virtual void visit(BlockStmt &) = 0;
        virtual void visit(VarDeclStmt &) = 0;
        virtual void visit(FunctionDeclStmt &) = 0;
        virtual void visit(ClassDeclStmt &) = 0;
        virtual void visit(InterfaceDeclStmt &) = 0;
        virtual void visit(TypeAliasDeclStmt &) = 0;
        virtual void visit(ImportStmt &) = 0;
        virtual void visit(IfStmt &) = 0;
        virtual void visit(ForStmt &) = 0;
        virtual void visit(WhileStmt &) = 0;
        virtual void visit(ReturnStmt &) = 0;
        virtual void visit(BreakStmt &) = 0;
        virtual void visit(ContinueStmt &) = 0;
        virtual void visit(ExprStmt &) = 0;
        virtual void visit(IgnorerStmt &) = 0;
        virtual void visit(AgirSelonStmt &) = 0;
    };

    // Base classes

    struct Expr
    {
        virtual ~Expr() = default;
        virtual void accept(ExprVisitor &v) = 0;
    };

    struct Stmt
    {
        virtual ~Stmt() = default;
        virtual void accept(StmtVisitor &v) = 0;
    };

    // Expression nodes

    struct LiteralExpr : Expr
    {
        Token token;

        explicit LiteralExpr(Token token)
            : token(std::move(token)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct IdentifierExpr : Expr
    {
        Token name; // the IDENT token

        explicit IdentifierExpr(Token name)
            : name(std::move(name)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct BinaryExpr : Expr
    {
        ExprPtr left;
        Token op;
        ExprPtr right;

        BinaryExpr(ExprPtr left, Token op, ExprPtr right)
            : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct DictionaryEntryExpr
    {
        ExprPtr key;
        ExprPtr value;
    };

    struct DictionaryExpr : Expr
    {
        Token brace; // the '{' token for error reporting
        std::vector<DictionaryEntryExpr> entries;

        DictionaryExpr(Token brace, std::vector<DictionaryEntryExpr> entries)
            : brace(std::move(brace)), entries(std::move(entries)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct UnaryExpr : Expr
    {
        Token op;
        ExprPtr operand;

        UnaryExpr(Token op, ExprPtr operand)
            : op(std::move(op)), operand(std::move(operand)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct CastExpr : Expr
    {
        ExprPtr operand;
        TypeExpr target_type;

        CastExpr(ExprPtr operand, TypeExpr target_type)
            : operand(std::move(operand)), target_type(std::move(target_type)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct TypeCheckExpr : Expr
    {
        ExprPtr operand;
        Token keyword;
        TypeExpr type;

        TypeCheckExpr(ExprPtr operand, Token keyword, TypeExpr type)
            : operand(std::move(operand)), keyword(std::move(keyword)), type(std::move(type)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct FunctionExpr : Expr
    {
        Token keyword;
        std::vector<Parameter> params;
        TypeExpr return_type;
        StmtPtr body;

        FunctionExpr(Token keyword, std::vector<Parameter> params, TypeExpr return_type, StmtPtr body)
            : keyword(std::move(keyword)),
              params(std::move(params)),
              return_type(std::move(return_type)),
              body(std::move(body)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct CallExpr : Expr
    {
        ExprPtr callee; // the function or constructor being called
        Token paren;    // the '(' token for error reporting
        std::vector<Argument> args;

        CallExpr(ExprPtr callee, Token paren, std::vector<Argument> args)
            : callee(std::move(callee)), paren(std::move(paren)), args(std::move(args)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct ListExpr : Expr
    {
        Token bracket; // the '[' token for error reporting
        ExprList elements;

        ListExpr(Token bracket, ExprList elements)
            : bracket(std::move(bracket)), elements(std::move(elements)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct MemberAccessExpr : Expr
    {
        ExprPtr object;
        Token dot;    // the '.' token
        Token member; // the member name token

        MemberAccessExpr(ExprPtr object, Token dot, Token member)
            : object(std::move(object)), dot(std::move(dot)), member(std::move(member)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    struct IndexAccessExpr : Expr
    {
        ExprPtr object;
        Token bracket; // the '[' token for error reporting
        ExprPtr index;

        IndexAccessExpr(ExprPtr object, Token bracket, ExprPtr index)
            : object(std::move(object)), bracket(std::move(bracket)), index(std::move(index)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };
    struct PropagationExpr : Expr
    {
        Token keyword; // the 'propager' keyword
        ExprPtr operand;

        PropagationExpr(Token keyword, ExprPtr operand)
            : keyword(std::move(keyword)), operand(std::move(operand)) {}

        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

    // Statement nodes 

    struct ExprStmt : Stmt
    {
        ExprPtr expr;

        explicit ExprStmt(ExprPtr expr)
            : expr(std::move(expr)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct IgnorerStmt : Stmt
    {
        Token keyword;
        ExprPtr expr;

        IgnorerStmt(Token keyword, ExprPtr expr)
            : keyword(std::move(keyword)), expr(std::move(expr)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct BlockStmt : Stmt
    {
        StmtList statements;

        explicit BlockStmt(StmtList statements)
            : statements(std::move(statements)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct VarDeclStmt : Stmt
    {
        Token name;
        TypeExpr type;
        bool is_fixe;
        bool is_prive;
        bool is_public;
        ExprPtr initializer; // nullptr if no initializer

        VarDeclStmt(Token name, TypeExpr type, bool is_fixe, bool is_prive, bool is_public, ExprPtr initializer)
            : name(std::move(name)), type(std::move(type)), is_fixe(is_fixe), is_prive(is_prive), is_public(is_public), initializer(std::move(initializer)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct FunctionDeclStmt : Stmt
    {
        Token name;
        std::vector<Parameter> params;
        TypeExpr return_type;
        StmtPtr body;
        bool is_prive;
        bool is_public;
        bool is_remplace;

        FunctionDeclStmt(Token name, std::vector<Parameter> params,
                         TypeExpr return_type, StmtPtr body,
                         bool is_prive, bool is_public, bool is_remplace)
            : name(std::move(name)), params(std::move(params)), return_type(std::move(return_type)), body(std::move(body)), is_prive(is_prive), is_public(is_public), is_remplace(is_remplace) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct ClassDeclStmt : Stmt
    {
        Token name;
        TypeExpr parent;
        bool is_public;
        std::vector<TypeExpr> interfaces;
        std::vector<StmtPtr> members;  // VarDeclStmt and FunctionDeclStmt

        ClassDeclStmt(Token name, TypeExpr parent, bool is_public,
                      std::vector<TypeExpr> interfaces, std::vector<StmtPtr> members)
            : name(std::move(name)), parent(std::move(parent)), is_public(is_public), interfaces(std::move(interfaces)), members(std::move(members)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct InterfaceDeclStmt : Stmt
    {
        Token name;
        bool is_public;
        std::vector<StmtPtr> methods; // FunctionDeclStmt with no body

        InterfaceDeclStmt(Token name, bool is_public, std::vector<StmtPtr> methods)
            : name(std::move(name)), is_public(is_public), methods(std::move(methods)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct TypeAliasDeclStmt : Stmt
    {
        Token name;
        TypeExpr target;
        bool is_public;

        TypeAliasDeclStmt(Token name, TypeExpr target, bool is_public)
            : name(std::move(name)),
              target(std::move(target)),
              is_public(is_public) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct ImportStmt : Stmt
    {
        struct ImportedMember
        {
            Token name;
            Token alias; // empty lexeme if no alias

            ImportedMember(Token name, Token alias)
                : name(std::move(name)), alias(std::move(alias)) {}
        };

        Token module_name;
        Token alias; // empty lexeme if no namespace alias
        std::vector<ImportedMember> imported_members;

        explicit ImportStmt(Token module_name, Token alias, std::vector<ImportedMember> imported_members = {})
            : module_name(std::move(module_name)), alias(std::move(alias)), imported_members(std::move(imported_members)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct IfStmt : Stmt
    {
        ExprPtr condition;
        StmtPtr then_branch; // always a BlockStmt
        StmtPtr else_branch; // nullptr if no sinon

        IfStmt(ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch)
            : condition(std::move(condition)), then_branch(std::move(then_branch)), else_branch(std::move(else_branch)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct ForStmt : Stmt
    {
        Token variable;   // the loop variable e.g. "i"
        ExprPtr iterable; // the collection or Intervalle
        StmtPtr body;     // always a BlockStmt

        ForStmt(Token variable, ExprPtr iterable, StmtPtr body)
            : variable(std::move(variable)), iterable(std::move(iterable)), body(std::move(body)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct WhileStmt : Stmt
    {
        ExprPtr condition;
        StmtPtr body; // always a BlockStmt

        WhileStmt(ExprPtr condition, StmtPtr body)
            : condition(std::move(condition)), body(std::move(body)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct ReturnStmt : Stmt
    {
        Token keyword; // for error reporting
        ExprPtr value; // nullptr if bare retourne

        ReturnStmt(Token keyword, ExprPtr value)
            : keyword(std::move(keyword)), value(std::move(value)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct BreakStmt : Stmt
    {
        Token keyword;

        explicit BreakStmt(Token keyword)
            : keyword(std::move(keyword)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    struct ContinueStmt : Stmt
    {
        Token keyword;

        explicit ContinueStmt(Token keyword)
            : keyword(std::move(keyword)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
    };

    enum class PatternKind
    {
        LITERAL,
        TYPE_BINDING,
        RIEN,
        RESULT_SUCCESS,
        RESULT_FAILURE,
    };

    struct Pattern
    {
        PatternKind kind;
        ExprPtr literal;
        Token name;
        TypeExpr type;

        explicit Pattern(ExprPtr literal)
            : kind(PatternKind::LITERAL),
              literal(std::move(literal)),
              name(TokenType::RIEN, "", 0, 0),
              type(),
              constructor(TokenType::RIEN, "", 0, 0) {}

        Pattern(Token name, TypeExpr type)
            : kind(PatternKind::TYPE_BINDING),
              literal(nullptr),
              name(std::move(name)),
              type(std::move(type)),
              constructor(TokenType::RIEN, "", 0, 0) {}

        explicit Pattern(Token rien_token)
            : kind(PatternKind::RIEN),
              literal(nullptr),
              name(std::move(rien_token)),
              type(),
              constructor(TokenType::RIEN, "", 0, 0) {}

        Pattern(PatternKind kind, Token constructor, Token binding, TypeExpr type)
            : kind(kind),
              literal(nullptr),
              name(std::move(binding)),
              type(std::move(type)),
              constructor(std::move(constructor)) {}

        Token constructor;
    };

    enum class BranchTerminator
    {
        NONE,
        PROPAGER,
        IGNORER,
    };

    struct AgirSelonBranch
    {
        std::vector<Pattern> patterns;
        StmtPtr body;
        BranchTerminator terminator;
        Token terminator_token;

        AgirSelonBranch(std::vector<Pattern> patterns, StmtPtr body)
            : patterns(std::move(patterns)), body(std::move(body)),
              terminator(BranchTerminator::NONE), terminator_token(TokenType::RIEN, "", 0, 0) {}
    };

    struct AgirSelonStmt : Stmt, Expr
    {
        Token keyword;
        ExprPtr expression;
        std::vector<AgirSelonBranch> branches;
        StmtPtr else_branch; // nullptr if no sinon branch

        AgirSelonStmt(Token keyword, ExprPtr expression,
                      std::vector<AgirSelonBranch> branches,
                      StmtPtr else_branch)
            : keyword(std::move(keyword)),
              expression(std::move(expression)),
              branches(std::move(branches)),
              else_branch(std::move(else_branch)) {}

        void accept(StmtVisitor &v) override { v.visit(*this); }
        void accept(ExprVisitor &v) override { v.visit(*this); }
    };

} // namespace lumiere
