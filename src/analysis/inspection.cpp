#include "lumiere/analysis/inspection.hpp"

#include "lumiere/analysis/analysis.hpp"
#include "lumiere/analysis/semantic_analysis.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/ast.hpp"
#include "lumiere/parser/parser.hpp"
#include "stdlib_docs.generated.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lumiere
{
namespace
{

std::string type_name(const TypeExpr &type)
{
    return type.empty() ? "Rien" : type.to_string();
}

std::string join_parameters(const std::vector<Parameter> &params)
{
    std::ostringstream output;
    for (std::size_t i = 0; i < params.size(); ++i)
    {
        if (i > 0)
        {
            output << ", ";
        }
        output << params[i].name << ": " << type_name(params[i].type);
    }
    return output.str();
}

struct Declaration
{
    std::string label;
    std::string kind;
    std::string signature;
    std::vector<std::string> parameters;
    std::string return_type;
    std::string documentation;
    std::size_t offset;
};

/// Documentation for a global builtin or stdlib module value. Built straight
/// from the embedded stdlib/*.lum sources; sheds light on a name the compiler
/// knows natively without a source declaration.
struct BuiltinDocumentation
{
    std::string module;
    std::string name;
    std::string kind;
    std::string signature;
    std::string return_type;
    std::string documentation;
};

std::string stdlib_module_name(const std::string_view file_name)
{
    static const std::unordered_map<std::string_view, std::string_view> modules = {
        {"aleatoire.lum", "Aléatoire"},
        {"chemin.lum", "Chemin"},
        {"fichier.lum", "Fichier"},
        {"luminet.lum", "LumiNet"},
        {"lumitest.lum", "LumiTest"},
        {"maths.lum", "Maths"},
        {"temps.lum", "Temps"},
        {"texte.lum", "Texte"},
    };
    const auto found = modules.find(file_name);
    return found == modules.end() ? "" : std::string(found->second);
}

std::string registration_type_name(const TypeExpr &type)
{
    return type.empty() ? "Rien" : type.to_string();
}

std::string registration_join_parameters(const std::vector<Parameter> &params)
{
    std::ostringstream output;
    for (std::size_t i = 0; i < params.size(); ++i)
    {
        if (i > 0)
        {
            output << ", ";
        }
        output << params[i].name;
        if (!params[i].type.empty())
        {
            output << " : " << params[i].type.to_string();
        }
    }
    return output.str();
}

/// Parses the embedded stdlib sources once and collects top-level module values
/// and methods. Documentation stays beside the API shape it describes.
const std::vector<BuiltinDocumentation> &stdlib_documentation()
{
    static const std::vector<BuiltinDocumentation> registry = [] {
        std::vector<BuiltinDocumentation> entries;
        for (const EmbeddedStdlibFile &file : STDLIB_DOC_FILES)
        {
            const std::string module = stdlib_module_name(file.name);
            Lexer lexer(std::string(file.source));
            Parser parser(lexer.tokenise());
            const StmtList statements = parser.parse();
            if (parser.had_error())
            {
                continue;
            }
            for (const StmtPtr &statement : statements)
            {
                if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(statement.get()))
                {
                    const std::string return_type = registration_type_name(function->return_type);
                    const bool is_constructor =
                        function->name.lexeme == "Succès" || function->name.lexeme == "Échec";
                    entries.push_back({module,
                                       function->name.lexeme,
                                       is_constructor ? "constructeur" : "fonction",
                                       function->name.lexeme + "(" +
                                           registration_join_parameters(function->params) +
                                           ") -> " + return_type,
                                       return_type,
                                       function->documentation});
                }
                else if (const auto *constant = dynamic_cast<const VarDeclStmt *>(statement.get()))
                {
                    if (!constant->is_fixe)
                    {
                        continue;
                    }
                    const std::string return_type = registration_type_name(constant->type);
                    entries.push_back({module,
                                       constant->name.lexeme,
                                       "constante",
                                       constant->name.lexeme,
                                       return_type,
                                       constant->documentation});
                }
                std::string qualified;
                std::string owner_name;
                std::string owner_kind;
                std::string owner_documentation;
                const StmtList *methods = nullptr;
                if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(statement.get()))
                {
                    owner_name = klass->name.lexeme;
                    owner_kind = "classe";
                    owner_documentation = klass->documentation;
                    qualified = owner_name + ".";
                    methods = &klass->members;
                }
                else if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(statement.get()))
                {
                    owner_name = interface->name.lexeme;
                    owner_kind = "interface";
                    owner_documentation = interface->documentation;
                    qualified = owner_name + ".";
                    methods = &interface->methods;
                }
                if (methods == nullptr)
                {
                    continue;
                }
                entries.push_back({module,
                                   owner_name,
                                   owner_kind,
                                   owner_kind + " " + owner_name,
                                   owner_name,
                                   owner_documentation});
                for (const StmtPtr &member : *methods)
                {
                    const auto *method = dynamic_cast<const FunctionDeclStmt *>(member.get());
                    if (method == nullptr)
                    {
                        continue;
                    }
                    const std::string return_type = registration_type_name(method->return_type);
                    const std::string parameters = registration_join_parameters(method->params);
                    if (module == owner_name)
                    {
                        entries.push_back({module,
                                           method->name.lexeme,
                                           "fonction",
                                           method->name.lexeme + "(texte : " + owner_name +
                                               (parameters.empty() ? "" : ", " + parameters) +
                                               ") -> " + return_type,
                                           return_type,
                                           method->documentation});
                    }
                    entries.push_back({module,
                                       qualified + method->name.lexeme,
                                       "méthode",
                                       method->name.lexeme + "(" + parameters + ") -> " + return_type,
                                       return_type,
                                       method->documentation});
                }
            }
        }
        return entries;
    }();
    return registry;
}

std::optional<Inspection> documented_inspection(const std::string_view module,
                                                const std::string_view name,
                                                const std::size_t start_offset,
                                                const std::size_t end_offset)
{
    const auto &registry = stdlib_documentation();
    const auto found = std::find_if(registry.begin(), registry.end(),
                                    [&](const BuiltinDocumentation &entry)
                                    { return entry.module == module && entry.name == name; });
    if (found == registry.end())
    {
        return std::nullopt;
    }
    Inspection inspection;
    inspection.label = name;
    inspection.kind = std::string(found->kind);
    inspection.signature = std::string(found->signature);
    inspection.return_type = std::string(found->return_type);
    inspection.documentation = std::string(found->documentation);
    inspection.start_offset = start_offset;
    inspection.end_offset = end_offset;
    return inspection;
}

std::string canonical_module_name(const std::string &name)
{
    return name == "Aleatoire" ? "Aléatoire" : name;
}

std::string default_module_alias(const std::string &module)
{
    const std::size_t separator = module.rfind('.');
    return separator == std::string::npos ? module : module.substr(separator + 1);
}

std::optional<Inspection> imported_value_inspection(const StmtList &statements,
                                                    const Token &selected)
{
    for (const StmtPtr &statement : statements)
    {
        const auto *import = dynamic_cast<const ImportStmt *>(statement.get());
        if (import == nullptr)
        {
            continue;
        }
        for (const ImportStmt::ImportedMember &member : import->imported_members)
        {
            const std::string &binding = member.alias.lexeme.empty()
                                             ? member.name.lexeme
                                             : member.alias.lexeme;
            if (binding != selected.lexeme)
            {
                continue;
            }
            auto inspection = documented_inspection(
                canonical_module_name(import->module_name.lexeme),
                member.name.lexeme,
                selected.start_offset,
                selected.end_offset);
            if (inspection.has_value())
            {
                inspection->label = binding;
            }
            return inspection;
        }
    }
    return std::nullopt;
}

bool member_path(const Expr &expression, std::string &root, std::string &path)
{
    if (const auto *identifier = dynamic_cast<const IdentifierExpr *>(&expression))
    {
        root = identifier->name.lexeme;
        return true;
    }
    const auto *member = dynamic_cast<const MemberAccessExpr *>(&expression);
    if (member == nullptr || !member_path(*member->object, root, path))
    {
        return false;
    }
    if (!path.empty())
    {
        path += '.';
    }
    path += member->member.lexeme;
    return true;
}

std::optional<Inspection> imported_member_inspection(const StmtList &statements,
                                                     const MemberAccessExpr &access)
{
    std::string root;
    std::string path;
    if (!member_path(*access.object, root, path))
    {
        return std::nullopt;
    }
    if (!path.empty())
    {
        path += '.';
    }
    path += access.member.lexeme;

    for (const StmtPtr &statement : statements)
    {
        const auto *import = dynamic_cast<const ImportStmt *>(statement.get());
        if (import == nullptr || !import->imported_members.empty())
        {
            continue;
        }
        const std::string alias = import->alias.lexeme.empty()
                                      ? default_module_alias(import->module_name.lexeme)
                                      : import->alias.lexeme;
        if (root == alias)
        {
            return documented_inspection(
                canonical_module_name(import->module_name.lexeme), path,
                access.member.start_offset, access.member.end_offset);
        }
    }
    return std::nullopt;
}

std::optional<Inspection> builtin_inspection(const std::string &name,
                                             const std::size_t start_offset,
                                             const std::size_t end_offset)
{
    if (auto core = documented_inspection("", name, start_offset, end_offset))
    {
        return core;
    }

    // Preserve hover for incomplete editor buffers that omit imports. Only use
    // this compatibility fallback when the name is unique across modules.
    const BuiltinDocumentation *match = nullptr;
    for (const BuiltinDocumentation &entry : stdlib_documentation())
    {
        if (entry.name != name || entry.name.find('.') != std::string::npos)
        {
            continue;
        }
        if (match != nullptr)
        {
            return std::nullopt;
        }
        match = &entry;
    }
    return match == nullptr
               ? std::nullopt
               : documented_inspection(match->module, name, start_offset, end_offset);
}

void collect_statements(const StmtList &statements, std::vector<Declaration> &declarations);

void push_declaration(std::vector<Declaration> &declarations, Declaration declaration)
{
    declarations.push_back(std::move(declaration));
}

void collect_statement(const Stmt &statement, std::vector<Declaration> &declarations)
{
    if (const auto *variable = dynamic_cast<const VarDeclStmt *>(&statement))
    {
        const std::string qualifier = variable->is_fixe ? "fixe " : "soit ";
        std::string signature = qualifier + variable->name.lexeme;
        if (!variable->type.empty())
        {
            signature += ": " + variable->type.to_string();
        }
        push_declaration(declarations, {variable->name.lexeme,
                                        "variable",
                                        signature,
                                        {},
                                        variable->type.empty() ? "" : variable->type.to_string(),
                                        variable->documentation,
                                        variable->name.start_offset});
    }
    else if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(&statement))
    {
        std::vector<std::string> parameters;
        parameters.reserve(function->params.size());
        for (const Parameter &parameter : function->params)
        {
            parameters.push_back(parameter.name + " : " + type_name(parameter.type));
        }
        push_declaration(declarations, {function->name.lexeme,
                                        "fonction",
                                        "fonction " + function->name.lexeme + "(" + join_parameters(function->params) + ")" +
                                            " -> " + type_name(function->return_type),
                                        std::move(parameters),
                                        type_name(function->return_type),
                                        function->documentation,
                                        function->name.start_offset});
        if (function->body != nullptr)
        {
            collect_statement(*function->body, declarations);
        }
    }
    else if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(&statement))
    {
        push_declaration(declarations, {klass->name.lexeme,
                                        "classe",
                                        "classe " + klass->name.lexeme,
                                        {},
                                        klass->name.lexeme,
                                        klass->documentation,
                                        klass->name.start_offset});
        collect_statements(klass->members, declarations);
    }
    else if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(&statement))
    {
        push_declaration(declarations, {interface->name.lexeme,
                                        "interface",
                                        "interface " + interface->name.lexeme,
                                        {},
                                        interface->name.lexeme,
                                        interface->documentation,
                                        interface->name.start_offset});
        collect_statements(interface->methods, declarations);
    }
    else if (const auto *alias = dynamic_cast<const TypeAliasDeclStmt *>(&statement))
    {
        push_declaration(declarations, {alias->name.lexeme,
                                        "alias de type",
                                        "type " + alias->name.lexeme + " = " + type_name(alias->target),
                                        {},
                                        type_name(alias->target),
                                        alias->documentation,
                                        alias->name.start_offset});
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
        push_declaration(declarations, {loop->variable.lexeme,
                                        "variable de boucle",
                                        "variable de boucle " + loop->variable.lexeme,
                                        {},
                                        "Entier",
                                        {},
                                        loop->variable.start_offset});
        collect_statement(*loop->body, declarations);
    }
    else if (const auto *loop = dynamic_cast<const WhileStmt *>(&statement))
    {
        collect_statement(*loop->body, declarations);
    }
}

void collect_statements(const StmtList &statements, std::vector<Declaration> &declarations)
{
    for (const StmtPtr &statement : statements)
    {
        collect_statement(*statement, declarations);
    }
}

const MemberAccessExpr *find_member_access_expr(const Expr *expression, const std::size_t offset)
{
    if (expression == nullptr)
    {
        return nullptr;
    }
    if (const auto *member = dynamic_cast<const MemberAccessExpr *>(expression))
    {
        if (offset >= member->member.start_offset && offset < member->member.end_offset)
        {
            return member;
        }
        return find_member_access_expr(member->object.get(), offset);
    }
    if (const auto *call = dynamic_cast<const CallExpr *>(expression))
    {
        if (const MemberAccessExpr *result = find_member_access_expr(call->callee.get(), offset))
        {
            return result;
        }
        for (const Argument &argument : call->args)
        {
            if (const MemberAccessExpr *result = find_member_access_expr(argument.value.get(), offset))
            {
                return result;
            }
        }
        return nullptr;
    }
    if (const auto *binary = dynamic_cast<const BinaryExpr *>(expression))
    {
        if (const MemberAccessExpr *result = find_member_access_expr(binary->left.get(), offset))
        {
            return result;
        }
        return find_member_access_expr(binary->right.get(), offset);
    }
    if (const auto *unary = dynamic_cast<const UnaryExpr *>(expression))
    {
        return find_member_access_expr(unary->operand.get(), offset);
    }
    if (const auto *index = dynamic_cast<const IndexAccessExpr *>(expression))
    {
        if (const MemberAccessExpr *result = find_member_access_expr(index->object.get(), offset))
        {
            return result;
        }
        return find_member_access_expr(index->index.get(), offset);
    }
    if (const auto *propagation = dynamic_cast<const PropagationExpr *>(expression))
    {
        return find_member_access_expr(propagation->operand.get(), offset);
    }
    if (const auto *cast = dynamic_cast<const CastExpr *>(expression))
    {
        return find_member_access_expr(cast->operand.get(), offset);
    }
    if (const auto *check = dynamic_cast<const TypeCheckExpr *>(expression))
    {
        return find_member_access_expr(check->operand.get(), offset);
    }
    if (const auto *list = dynamic_cast<const ListExpr *>(expression))
    {
        for (const ExprPtr &element : list->elements)
        {
            if (const MemberAccessExpr *result = find_member_access_expr(element.get(), offset))
            {
                return result;
            }
        }
        return nullptr;
    }
    if (const auto *dictionary = dynamic_cast<const DictionaryExpr *>(expression))
    {
        for (const DictionaryEntryExpr &entry : dictionary->entries)
        {
            if (const MemberAccessExpr *result = find_member_access_expr(entry.key.get(), offset))
            {
                return result;
            }
            if (const MemberAccessExpr *result = find_member_access_expr(entry.value.get(), offset))
            {
                return result;
            }
        }
        return nullptr;
    }
    return nullptr;
}

const MemberAccessExpr *find_member_access_stmt(const Stmt &statement, const std::size_t offset)
{
    if (const auto *expression = dynamic_cast<const ExprStmt *>(&statement))
    {
        return find_member_access_expr(expression->expr.get(), offset);
    }
    if (const auto *ignorer = dynamic_cast<const IgnorerStmt *>(&statement))
    {
        return find_member_access_expr(ignorer->expr.get(), offset);
    }
    if (const auto *variable = dynamic_cast<const VarDeclStmt *>(&statement))
    {
        return find_member_access_expr(variable->initializer.get(), offset);
    }
    if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(&statement))
    {
        if (function->body != nullptr)
        {
            if (const MemberAccessExpr *result = find_member_access_stmt(*function->body, offset))
            {
                return result;
            }
        }
        return nullptr;
    }
    if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(&statement))
    {
        for (const StmtPtr &member : klass->members)
        {
            if (const MemberAccessExpr *result = find_member_access_stmt(*member, offset))
            {
                return result;
            }
        }
        return nullptr;
    }
    if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(&statement))
    {
        for (const StmtPtr &member : interface->methods)
        {
            if (const MemberAccessExpr *result = find_member_access_stmt(*member, offset))
            {
                return result;
            }
        }
        return nullptr;
    }
    if (const auto *block = dynamic_cast<const BlockStmt *>(&statement))
    {
        for (const StmtPtr &child : block->statements)
        {
            if (const MemberAccessExpr *result = find_member_access_stmt(*child, offset))
            {
                return result;
            }
        }
        return nullptr;
    }
    if (const auto *conditional = dynamic_cast<const IfStmt *>(&statement))
    {
        if (const MemberAccessExpr *result = find_member_access_expr(conditional->condition.get(), offset))
        {
            return result;
        }
        if (const MemberAccessExpr *result = find_member_access_stmt(*conditional->then_branch, offset))
        {
            return result;
        }
        if (conditional->else_branch != nullptr)
        {
            return find_member_access_stmt(*conditional->else_branch, offset);
        }
        return nullptr;
    }
    if (const auto *loop = dynamic_cast<const ForStmt *>(&statement))
    {
        if (const MemberAccessExpr *result = find_member_access_expr(loop->iterable.get(), offset))
        {
            return result;
        }
        return find_member_access_stmt(*loop->body, offset);
    }
    if (const auto *loop = dynamic_cast<const WhileStmt *>(&statement))
    {
        if (const MemberAccessExpr *result = find_member_access_expr(loop->condition.get(), offset))
        {
            return result;
        }
        return find_member_access_stmt(*loop->body, offset);
    }
    if (const auto *agir = dynamic_cast<const AgirSelonStmt *>(&statement))
    {
        if (const MemberAccessExpr *result = find_member_access_expr(agir->expression.get(), offset))
        {
            return result;
        }
        for (const AgirSelonBranch &branch : agir->branches)
        {
            if (const MemberAccessExpr *result = find_member_access_stmt(*branch.body, offset))
            {
                return result;
            }
        }
        if (agir->else_branch != nullptr)
        {
            return find_member_access_stmt(*agir->else_branch, offset);
        }
        return nullptr;
    }
    if (const auto *ret = dynamic_cast<const ReturnStmt *>(&statement))
    {
        return find_member_access_expr(ret->value.get(), offset);
    }
    return nullptr;
}

std::optional<Inspection> declaration_inspection_from_stmt(const Stmt &statement)
{
    if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(&statement))
    {
        std::vector<std::string> parameters;
        parameters.reserve(function->params.size());
        for (const Parameter &parameter : function->params)
        {
            parameters.push_back(parameter.name + " : " + type_name(parameter.type));
        }
        return Inspection{function->name.lexeme,
                          "méthode",
                          "fonction " + function->name.lexeme + "(" + join_parameters(function->params) + ")" +
                              " -> " + type_name(function->return_type),
                          std::move(parameters),
                          type_name(function->return_type),
                          function->documentation,
                          function->name.start_offset,
                          function->name.end_offset};
    }
    if (const auto *variable = dynamic_cast<const VarDeclStmt *>(&statement))
    {
        const std::string qualifier = variable->is_fixe ? "fixe " : "soit ";
        std::string signature = qualifier + variable->name.lexeme;
        if (!variable->type.empty())
        {
            signature += ": " + variable->type.to_string();
        }
return Inspection{variable->name.lexeme,
                           "champ",
                           signature,
                          {},
                          variable->type.empty() ? "" : variable->type.to_string(),
                          variable->documentation,
                          variable->name.start_offset,
                          variable->name.end_offset};
    }
    return std::nullopt;
}

const Stmt *find_class_member_statement(const ClassDeclStmt &klass, const std::string_view name)
{
    for (const StmtPtr &member : klass.members)
    {
        if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(member.get()))
        {
            if (function->name.lexeme == name)
            {
                return member.get();
            }
        }
        else if (const auto *variable = dynamic_cast<const VarDeclStmt *>(member.get()))
        {
            if (variable->name.lexeme == name)
            {
                return member.get();
            }
        }
    }
    return nullptr;
}

const Stmt *find_interface_member_statement(const InterfaceDeclStmt &interface, const std::string_view name)
{
    for (const StmtPtr &member : interface.methods)
    {
        if (const auto *function = dynamic_cast<const FunctionDeclStmt *>(member.get()))
        {
            if (function->name.lexeme == name)
            {
                return member.get();
            }
        }
    }
    return nullptr;
}

/// Resolves `objet.membre` where objet's type is a user-declared class or
/// interface, by locating the type's declaration and its member.
const Stmt *find_type_declaration(const StmtList &statements, const std::string_view type_name)
{
    for (const StmtPtr &statement : statements)
    {
        if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(statement.get()))
        {
            if (klass->name.lexeme == type_name)
            {
                return statement.get();
            }
        }
        else if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(statement.get()))
        {
            if (interface->name.lexeme == type_name)
            {
                return statement.get();
            }
        }
        else if (const auto *alias = dynamic_cast<const TypeAliasDeclStmt *>(statement.get()))
        {
            if (alias->name.lexeme == type_name)
            {
                return statement.get();
            }
        }
    }
    return nullptr;
}

std::optional<Inspection> member_declaration_inspection(const SemanticModel &model,
                                                         const StmtList &statements,
                                                         const MemberAccessExpr &access,
                                                         const Token *selected)
{
    const std::string member_name = access.member.lexeme;
    const SemanticTypeRef *object_type = model.type_of(*access.object);
    if (object_type == nullptr)
    {
        return std::nullopt;
    }
    std::string type_name = std::string((*object_type)->display());
    const Stmt *type_declaration = find_type_declaration(statements, type_name);
    if (type_declaration == nullptr)
    {
        return std::nullopt;
    }
    const auto apply_offsets = [&](Inspection &result)
    {
        result.start_offset = selected->start_offset;
        result.end_offset = selected->end_offset;
    };
    if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(type_declaration))
    {
        if (const Stmt *member = find_class_member_statement(*klass, member_name))
        {
            std::optional<Inspection> result = declaration_inspection_from_stmt(*member);
            if (result.has_value())
            {
                apply_offsets(*result);
            }
            return result;
        }
    }
    if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(type_declaration))
    {
        if (const Stmt *member = find_interface_member_statement(*interface, member_name))
        {
            std::optional<Inspection> result = declaration_inspection_from_stmt(*member);
            if (result.has_value())
            {
                apply_offsets(*result);
            }
            return result;
        }
    }
    return std::nullopt;
}

std::optional<Inspection> qualified_member_inspection(const std::string &object_type,
                                                       const std::string &member_name,
                                                       const std::size_t start_offset,
                                                       const std::size_t end_offset)
{
    const std::string &qualified = object_type + "." + member_name;
    const auto &registry = stdlib_documentation();
    const auto found = std::find_if(registry.begin(), registry.end(),
                                    [&](const BuiltinDocumentation &entry)
                                    { return entry.kind == "méthode" && entry.name == qualified; });
    if (found == registry.end())
    {
        return std::nullopt;
    }
    Inspection inspection;
    inspection.label = member_name;
    inspection.kind = "méthode";
    inspection.signature = std::string(found->signature);
    inspection.parameters = {};
    inspection.return_type = std::string(found->return_type);
    inspection.documentation = std::string(found->documentation);
    inspection.start_offset = start_offset;
    inspection.end_offset = end_offset;
    return inspection;
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
        {TokenType::TYPE, "Déclare un alias de type."},
        {TokenType::IMPORTER, "Importe un module Lumiere."},
{TokenType::SI, "Exécute une branche lorsque sa condition est vraie."},
        {TokenType::SINON, "Définit la branche alternative d'une condition."},
        {TokenType::POUR, "Commence une boucle d'itération."},
        {TokenType::TANT_QUE, "Répète un bloc tant que sa condition est vraie."},
        {TokenType::ARRETER, "Interrompt immédiatement une boucle."},
        {TokenType::CONTINUER, "Passe immédiatement à l'itération suivante d'une boucle."},
        {TokenType::AGIR_SELON, "Fait correspondre une valeur à des motifs selon des branches."},
        {TokenType::IGNORER, "Écarte explicitement une valeur Résultat."},
        {TokenType::PROPAGER, "Propage l'échec d'une expression Résultat."},
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

Inspection declaration_to_inspection(const Declaration &declaration)
{
    Inspection inspection;
    inspection.label = declaration.label;
    inspection.kind = declaration.kind;
    inspection.signature = declaration.signature;
    inspection.parameters = declaration.parameters;
    inspection.return_type = declaration.return_type;
    inspection.documentation = declaration.documentation;
    return inspection;
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
        return Inspection{selected->lexeme, "mot-clé", selected->lexeme, {}, "",
                          std::string(*detail), selected->start_offset, selected->end_offset};
    }
    if (selected->type != TokenType::IDENT)
    {
        // Fall back to documentation for operators such as `propager` that are
        // lexed as keywords handled above, or to nothing for the rest.
        return std::nullopt;
    }

    std::optional<Inspection> builtin = builtin_inspection(selected->lexeme,
                                                           selected->start_offset,
                                                           selected->end_offset);

    AnalysisResult analysis = analyze_source(source);
    if (auto imported = imported_value_inspection(analysis.statements, *selected))
    {
        return imported;
    }

    // If the cursor sits on `objet.membre`, resolve the member against the
    // object's inferred static type (stdlib methods) or its class/interface
    // declaration (user-defined members).
    const MemberAccessExpr *member_access = nullptr;
    for (const StmtPtr &statement : analysis.statements)
    {
        if (const MemberAccessExpr *result = find_member_access_stmt(*statement, selected->start_offset))
        {
            member_access = result;
            break;
        }
    }
    if (member_access != nullptr)
    {
        if (auto imported = imported_member_inspection(analysis.statements, *member_access))
        {
            return imported;
        }
    }
    if (analysis.has_errors())
    {
        return builtin;
    }
    if (member_access != nullptr)
    {
        const SemanticAnalysis semantics =
            analyze_semantics(analysis.statements);
        const std::string member_name = member_access->member.lexeme;
        std::optional<Inspection> member_inspection;
        if (const SemanticTypeRef *object_type =
                semantics.model.type_of(*member_access->object))
        {
            if (const auto by_type =
                    qualified_member_inspection(
                        std::string((*object_type)->name()),
                        member_name,
                        selected->start_offset,
                        selected->end_offset);
                by_type.has_value())
            {
                member_inspection = by_type;
            }
            else if (const auto by_user =
                         member_declaration_inspection(
                             semantics.model, analysis.statements,
                             *member_access, selected);
                     by_user.has_value())
            {
                member_inspection = by_user;
            }
        }
        if (member_inspection.has_value())
        {
            return member_inspection;
        }
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
    if (best != nullptr)
    {
        Inspection inspection = declaration_to_inspection(*best);
        inspection.start_offset = selected->start_offset;
        inspection.end_offset = selected->end_offset;
        return inspection;
    }
    return builtin;
}

std::string inspection_to_json(const std::optional<Inspection> &inspection)
{
    if (!inspection.has_value())
    {
        return "{\"protocolVersion\":2,\"inspection\":null}";
    }
    std::ostringstream output;
    output << "{\"protocolVersion\":2,\"inspection\":{";
    output << "\"label\":\"" << escape_json(inspection->label) << "\",";
    output << "\"kind\":\"" << escape_json(inspection->kind) << "\",";
    output << "\"signature\":\"" << escape_json(inspection->signature) << "\",";
    output << "\"parameters\":[";
    for (std::size_t i = 0; i < inspection->parameters.size(); ++i)
    {
        if (i > 0)
        {
            output << ",";
        }
        output << "\"" << escape_json(inspection->parameters[i]) << "\"";
    }
    output << "],";
    output << "\"returnType\":\"" << escape_json(inspection->return_type) << "\",";
    output << "\"documentation\":\"" << escape_json(inspection->documentation) << "\",";
    output << "\"range\":{\"start\":" << inspection->start_offset
           << ",\"end\":" << inspection->end_offset << "}}}";
    return output.str();
}

} // namespace lumiere
