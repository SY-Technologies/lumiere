#include "lumiere/analysis/inspection.hpp"

#include "lumiere/analysis/analysis.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/ast.hpp"

#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lumiere
{
namespace
{

struct Declaration
{
    std::string label;
    std::string detail;
    std::size_t offset;
};

std::string type_name(const Token &token)
{
    return token.lexeme.empty() ? "Rien" : token.lexeme;
}

std::string function_signature(const FunctionDeclStmt &function)
{
    std::ostringstream output;
    output << "fonction " << function.name.lexeme << '(';
    for (std::size_t i = 0; i < function.params.size(); ++i)
    {
        if (i > 0)
        {
            output << ", ";
        }
        output << function.params[i].name << ": " << type_name(function.params[i].type_token);
    }
    output << ") -> " << type_name(function.return_type);
    return output.str();
}

void collect_statements(const StmtList &statements, std::vector<Declaration> &declarations);

void collect_statement(const Stmt &statement, std::vector<Declaration> &declarations)
{
    if (const auto *variable = dynamic_cast<const VarDeclStmt *>(&statement))
    {
        const std::string qualifier = variable->is_fixe ? "fixe " : "soit ";
        const std::string annotation = variable->type_token.lexeme.empty()
            ? ""
            : ": " + variable->type_token.lexeme;
        declarations.push_back({variable->name.lexeme,
                                qualifier + variable->name.lexeme + annotation,
                                variable->name.start_offset});
    }
    else if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(&statement))
    {
        declarations.push_back({function->name.lexeme, function_signature(*function), function->name.start_offset});
        if (function->body != nullptr)
        {
            collect_statement(*function->body, declarations);
        }
    }
    else if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(&statement))
    {
        declarations.push_back({klass->name.lexeme, "classe " + klass->name.lexeme, klass->name.start_offset});
        collect_statements(klass->members, declarations);
    }
    else if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(&statement))
    {
        declarations.push_back({interface->name.lexeme,
                                "interface " + interface->name.lexeme,
                                interface->name.start_offset});
        collect_statements(interface->methods, declarations);
    }
    else if (const auto *block = dynamic_cast<const BlockStmt *>(&statement))
    {
        collect_statements(block->statements, declarations);
    }
    else if (const auto *conditional = dynamic_cast<const IfStmt *>(&statement))
    {
        collect_statement(*conditional->then_branch, declarations);
        if (conditional->else_branch != nullptr)
        {
            collect_statement(*conditional->else_branch, declarations);
        }
    }
    else if (const auto *loop = dynamic_cast<const ForStmt *>(&statement))
    {
        declarations.push_back({loop->variable.lexeme, "variable de boucle " + loop->variable.lexeme,
                                loop->variable.start_offset});
        collect_statement(*loop->body, declarations);
    }
    else if (const auto *loop = dynamic_cast<const WhileStmt *>(&statement))
    {
        collect_statement(*loop->body, declarations);
    }
    else if (const auto *attempt = dynamic_cast<const TryStmt *>(&statement))
    {
        collect_statement(*attempt->body, declarations);
        for (const CatchClause &clause : attempt->catch_clauses)
        {
            declarations.push_back({clause.variable.lexeme,
                                    clause.variable.lexeme + ": " + clause.type_token.lexeme,
                                    clause.variable.start_offset});
            collect_statement(*clause.body, declarations);
        }
        if (attempt->finally_body != nullptr)
        {
            collect_statement(*attempt->finally_body, declarations);
        }
    }
}

void collect_statements(const StmtList &statements, std::vector<Declaration> &declarations)
{
    for (const StmtPtr &statement : statements)
    {
        collect_statement(*statement, declarations);
    }
}

std::optional<std::string_view> keyword_detail(const TokenType type)
{
    static const std::unordered_map<TokenType, std::string_view> details = {
        {TokenType::SOIT, "Déclare une variable."},
        {TokenType::FIXE, "Rend une déclaration non réassignable."},
        {TokenType::FONCTION, "Déclare une fonction."},
        {TokenType::RETOURNE, "Termine la fonction et retourne une valeur."},
        {TokenType::CLASSE, "Déclare une classe."},
        {TokenType::INTERFACE, "Déclare un contrat d'interface."},
        {TokenType::REALISE, "Indique les interfaces réalisées par une classe."},
        {TokenType::SI, "Exécute une branche lorsque sa condition est vraie."},
        {TokenType::SINON, "Définit la branche alternative d'une condition."},
        {TokenType::POUR, "Commence une boucle d'itération."},
        {TokenType::TANT_QUE, "Répète un bloc tant que sa condition est vraie."},
        {TokenType::IMPORTER, "Importe un module Lumiere."},
        {TokenType::LANCER, "Lance une erreur."},
        {TokenType::ESSAYER, "Commence un bloc de gestion d'erreur."},
        {TokenType::ATTRAPER, "Intercepte une erreur compatible."},
    };
    const auto found = details.find(type);
    if (found == details.end())
    {
        return std::nullopt;
    }
    return found->second;
}

std::string escape_json(const std::string &value)
{
    std::string escaped;
    for (const char character : value)
    {
        if (character == '"' || character == '\\')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

} // namespace

std::optional<Inspection> inspect_source(const std::string &source, const std::size_t byte_offset)
{
    Lexer lexer(source);
    const std::vector<Token> tokens = lexer.tokenise();
    const Token *selected = nullptr;
    for (const Token &token : tokens)
    {
        if (byte_offset >= token.start_offset && byte_offset < token.end_offset)
        {
            selected = &token;
            break;
        }
    }
    if (selected == nullptr)
    {
        return std::nullopt;
    }

    if (const auto detail = keyword_detail(selected->type); detail.has_value())
    {
        return Inspection{selected->lexeme, std::string(*detail), selected->start_offset, selected->end_offset};
    }
    if (selected->type != TokenType::IDENT)
    {
        return std::nullopt;
    }

    AnalysisResult analysis = analyze_source(source);
    if (analysis.has_errors())
    {
        return std::nullopt;
    }
    std::vector<Declaration> declarations;
    collect_statements(analysis.statements, declarations);

    const Declaration *best = nullptr;
    for (const Declaration &declaration : declarations)
    {
        if (declaration.label == selected->lexeme &&
            (best == nullptr || (declaration.offset <= selected->start_offset && declaration.offset > best->offset)))
        {
            best = &declaration;
        }
    }
    if (best == nullptr)
    {
        return std::nullopt;
    }
    return Inspection{best->label, best->detail, selected->start_offset, selected->end_offset};
}

std::string inspection_to_json(const std::optional<Inspection> &inspection)
{
    if (!inspection.has_value())
    {
        return "{\"protocolVersion\":1,\"inspection\":null}";
    }
    std::ostringstream output;
    output << "{\"protocolVersion\":1,\"inspection\":{";
    output << "\"label\":\"" << escape_json(inspection->label) << "\",";
    output << "\"detail\":\"" << escape_json(inspection->detail) << "\",";
    output << "\"range\":{\"start\":" << inspection->start_offset
           << ",\"end\":" << inspection->end_offset << "}}}";
    return output.str();
}

} // namespace lumiere
