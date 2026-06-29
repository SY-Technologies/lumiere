#pragma once
#include <sstream>
#include <string>
#include "lumiere/parser/ast.hpp"



namespace lumiere{
// a utility class for printing AST nodes
class AstPrinter : public ExprVisitor, public StmtVisitor
    {
    public:
        std::string print(Stmt &stmt)
        {
            m_out.str("");
            m_out.clear();
            stmt.accept(*this);
            return m_out.str();
        }

        std::string print(Expr &expr)
        {
            m_out.str("");
            m_out.clear();
            expr.accept(*this);
            return m_out.str();
        }

    private:
        std::ostringstream m_out;

        //Helpers

        void print_expr(Expr *expr)
        {
            if (expr)
            {
                expr->accept(*this);
            }
            else
            {
                m_out << "_";
            }
        }

        void print_stmt(Stmt *stmt)
        {
            if (stmt)
            {
                stmt->accept(*this);
            }
            else
            {
                m_out << "_";
            }
        }

        //Expression visitors
        void visit(LiteralExpr &e) override
        {
            m_out << "(literal " << e.token.lexeme << ")";
        }

        void visit(IdentifierExpr &e) override
        {
            m_out << "(ident " << e.name.lexeme << ")";
        }

        void visit(BinaryExpr &e) override
        {
            m_out << "(binary " << e.op.lexeme << " ";
            print_expr(e.left.get());
            m_out << " ";
            print_expr(e.right.get());
            m_out << ")";
        }

        void visit(DictionaryExpr &dict_expr) override
        {
            m_out << "(dict";
            for (auto &entry : dict_expr.entries)
            {
                m_out << " (entry ";
                print_expr(entry.key.get());
                m_out << " ";
                print_expr(entry.value.get());
                m_out << ")";
            }
            m_out << ")";
        }

        void visit(UnaryExpr &e) override
        {
            m_out << "(unary " << e.op.lexeme << " ";
            print_expr(e.operand.get());
            m_out << ")";
        }

        void visit(CastExpr &e) override
        {
            m_out << "(cast ";
            print_expr(e.operand.get());
            m_out << " " << e.target_type.lexeme << ")";
        }

        void visit(TypeCheckExpr &e) override
        {
            m_out << "(type-check ";
            print_expr(e.operand.get());
            m_out << " " << e.type_token.lexeme << ")";
        }

        void visit(FunctionExpr &e) override
        {
            m_out << "(func-expr [";
            for (std::size_t i = 0; i < e.params.size(); ++i)
            {
                if (i > 0)
                {
                    m_out << " ";
                }
                m_out << "(" << e.params[i].name
                      << " " << e.params[i].type_token.lexeme << ")";
            }
            m_out << "] "
                  << (e.return_type.lexeme.empty() ? "_" : e.return_type.lexeme) << " ";
            print_stmt(e.body.get());
            m_out << ")";
        }

        void visit(CallExpr &e) override
        {
            m_out << "(call ";
            print_expr(e.callee.get());
            m_out << " (args";
            for (auto &arg : e.args)
            {
                m_out << " (arg " << (arg.name.empty() ? "_" : arg.name) << " ";
                print_expr(arg.value.get());
                m_out << ")";
            }
            m_out << "))";
        }

        void visit(ListExpr &e) override
        {
            m_out << "(list";
            for (auto &element : e.elements)
            {
                m_out << " ";
                print_expr(element.get());
            }
            m_out << ")";
        }

        void visit(MemberAccessExpr &e) override
        {
            m_out << "(member ";
            print_expr(e.object.get());
            m_out << " " << e.member.lexeme << ")";
        }

        void visit(IndexAccessExpr &e) override
        {
            m_out << "(index ";
            print_expr(e.object.get());
            m_out << " ";
            print_expr(e.index.get());
            m_out << ")";
        }

        //Statement visitors

        void visit(ExprStmt &s) override
        {
            m_out << "(expr ";
            print_expr(s.expr.get());
            m_out << ")";
        }

        void visit(BlockStmt &s) override
        {
            m_out << "(block";
            for (auto &stmt : s.statements)
            {
                m_out << " ";
                print_stmt(stmt.get());
            }
            m_out << ")";
        }

        void visit(VarDeclStmt &s) override
        {
            m_out << "(var-decl "
                  << s.name.lexeme << " "
                  << (s.type_token.lexeme.empty() ? "_" : s.type_token.lexeme) << " "
                  << (s.is_fixe ? "true" : "false") << " "
                  << (s.is_public ? "public" : (s.is_prive ? "private" : "internal")) << " ";
            print_expr(s.initializer.get());
            m_out << ")";
        }

        void visit(FunctionDeclStmt &s) override
        {
            m_out << "(func-decl " << s.name.lexeme << " "
                  << (s.is_public ? "public" : (s.is_prive ? "private" : "internal")) << " [";
            for (std::size_t i = 0; i < s.params.size(); ++i)
            {
                if (i > 0)
                {
                    m_out << " ";
                }
                m_out << "(" << s.params[i].name
                      << " " << s.params[i].type_token.lexeme << ")";
            }
            m_out << "] "
                  << (s.return_type.lexeme.empty() ? "_" : s.return_type.lexeme) << " ";
            print_stmt(s.body.get());
            m_out << ")";
        }

        void visit(ClassDeclStmt &s) override
        {
            m_out << "(class-decl " << s.name.lexeme << " "
                  << (s.is_public ? "public" : "internal") << " "
                  << (s.parent.lexeme.empty() ? "_" : s.parent.lexeme) << " [";
            for (std::size_t i = 0; i < s.interfaces.size(); ++i)
            {
                if (i > 0)
                {
                    m_out << " ";
                }
                m_out << s.interfaces[i].lexeme;
            }
            m_out << "] (block";
            for (auto &member : s.members)
            {
                m_out << " ";
                print_stmt(member.get());
            }
            m_out << "))";
        }

        void visit(InterfaceDeclStmt &s) override
        {
            m_out << "(interface-decl " << s.name.lexeme << " "
                  << (s.is_public ? "public" : "internal") << " (block";
            for (auto &method : s.methods)
            {
                m_out << " ";
                print_stmt(method.get());
            }
            m_out << "))";
        }

        void visit(ImportStmt &s) override
        {
            m_out << "(import " << s.module_name.lexeme;
            if (!s.imported_members.empty())
            {
                m_out << ".{";
                for (std::size_t i = 0; i < s.imported_members.size(); ++i)
                {
                    if (i > 0)
                    {
                        m_out << ", ";
                    }
                    m_out << s.imported_members[i].name.lexeme;
                    if (!s.imported_members[i].alias.lexeme.empty())
                    {
                        m_out << " comme " << s.imported_members[i].alias.lexeme;
                    }
                }
                m_out << "}";
            }
            else if (!s.alias.lexeme.empty())
            {
                m_out << " comme " << s.alias.lexeme;
            }
            m_out << ")";
        }

        void visit(IfStmt &s) override
        {
            m_out << "(if ";
            print_expr(s.condition.get());
            m_out << " ";
            print_stmt(s.then_branch.get());
            m_out << " ";
            print_stmt(s.else_branch.get());
            m_out << ")";
        }

        void visit(ForStmt &s) override
        {
            m_out << "(for " << s.variable.lexeme << " ";
            print_expr(s.iterable.get());
            m_out << " ";
            print_stmt(s.body.get());
            m_out << ")";
        }

        void visit(WhileStmt &s) override
        {
            m_out << "(while ";
            print_expr(s.condition.get());
            m_out << " ";
            print_stmt(s.body.get());
            m_out << ")";
        }

        void visit(ReturnStmt &s) override
        {
            m_out << "(return ";
            print_expr(s.value.get());
            m_out << ")";
        }

        void visit(BreakStmt &) override
        {
            m_out << "(break)";
        }

        void visit(ContinueStmt &) override
        {
            m_out << "(continue)";
        }

        void visit(ThrowStmt &s) override
        {
            m_out << "(throw ";
            print_expr(s.value.get());
            m_out << ")";
        }

        void visit(TryStmt &s) override
        {
            m_out << "(try ";
            print_stmt(s.body.get());
            m_out << " [";
            for (std::size_t i = 0; i < s.catch_clauses.size(); ++i)
            {
                if (i > 0)
                {
                    m_out << " ";
                }
                auto &c = s.catch_clauses[i];
                m_out << "(catch " << c.variable.lexeme
                      << " " << c.type_token.lexeme << " ";
                print_stmt(c.body.get());
                m_out << ")";
            }
            m_out << "] ";
            print_stmt(s.finally_body.get());
            m_out << ")";
        }

        void visit(AgirSelonStmt &s) override
        {
            m_out << "(agir-selon ";
            print_expr(s.expression.get());
            m_out << " [";
            for (std::size_t i = 0; i < s.branches.size(); ++i)
            {
                if (i > 0)
                {
                    m_out << " ";
                }
                auto &branch = s.branches[i];
                m_out << "(branch [";
                for (std::size_t j = 0; j < branch.patterns.size(); ++j)
                {
                    if (j > 0)
                    {
                        m_out << " ";
                    }
                    auto &pattern = branch.patterns[j];
                    switch (pattern.kind)
                    {
                    case PatternKind::LITERAL:
                        m_out << "(literal ";
                        print_expr(pattern.literal.get());
                        m_out << ")";
                        break;
                    case PatternKind::TYPE_BINDING:
                        m_out << "(typed " << pattern.name.lexeme << " " << pattern.type_token.lexeme << ")";
                        break;
                    case PatternKind::RIEN:
                        m_out << "(rien)";
                        break;
                    }
                }
                m_out << "] ";
                print_stmt(branch.body.get());
                m_out << ")";
            }
            m_out << "] ";
            print_stmt(s.else_branch.get());
            m_out << ")";
        }
    };
} // namespace lumiere
