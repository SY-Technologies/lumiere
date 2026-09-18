#include "lumiere/interpreter/runtime/numeric.hpp"
#include "ast_to_lir.hpp"

#include "lumiere/interpreter/vm/compiler.hpp"
#include "lumiere/lexer/token.hpp"
#include "lumiere/parser/ast.hpp"
#include "lumiere/parser/utf8.hpp"
#include "lumiere/interpreter/runtime/nominal_type.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lumiere
{
namespace
{

std::string runtime_type_name(const TypeExpr &type,
                              const TypeAliasTable &aliases,
                              bool resolve_nominal = true)
{
    try
    {
        return resolve_type_aliases(type, aliases, {}, resolve_nominal).to_string();
    }
    catch (const std::invalid_argument &error)
    {
        throw VmCompileError("VM: " + std::string(error.what()));
    }
}

// Maps source tokens into LIR source coordinates.
LirSourceLocation lir_loc(const Token &token)
{
    return LirSourceLocation{token.line, token.column};
}

LirSourceLocation lir_loc(const TypeExpr &type)
{
    return lir_loc(type.source);
}

std::string token_label(const Token &token)
{
    return token.lexeme.empty() ? token.to_string() : token.lexeme;
}

[[noreturn]] void invalid_ast(const std::string &what, const Token &token)
{
    std::ostringstream out;
    out << "VM: forme AST invalide pour " << what;
    if (!token.lexeme.empty())
    {
        out << " près de '" << token.lexeme << "'";
    }
    out << " (" << token.line << ":" << token.column << ")";
    throw VmCompileError(out.str());
}

// Converts the currently supported literal AST subset into runtime values
// that can be stored in the LIR and later copied into the bytecode constant
// pool if the chosen opcode encoding requires it.
Value literal_value(const Token &token)
{
    switch (token.type)
    {
    case TokenType::ENTIER_LIT:
        // The lexer already refused a literal that does not fit.
        return Value::entier(numeric::parse_integer_literal(token.lexeme).value_or(0));
    case TokenType::DECIMAL_LIT:
        return Value::decimal(std::stod(numeric::without_digit_separators(token.lexeme)));
    case TokenType::TEXTE_LIT:
        return Value::texte(token.lexeme.substr(1, token.lexeme.size() - 2));
    case TokenType::SYMBOLE_LIT:
    {
        const std::optional<char32_t> character =
            utf8::decode_single_character(std::string_view(token.lexeme).substr(1, token.lexeme.size() - 2));
        if (!character.has_value())
        {
            throw VmCompileError("VM: symbole invalide");
        }
        return Value::symbole(*character);
    }
    case TokenType::VRAI:
        return Value::logique(true);
    case TokenType::FAUX:
        return Value::logique(false);
    case TokenType::RIEN:
        return Value::rien();
    default:
        invalid_ast("le littéral '" + token_label(token) + "'", token);
    }
}

// Lowers one function into explicit basic blocks and value-producing LIR.
class FunctionLowerer final : public ExprVisitor, public StmtVisitor
{
public:
    using FunctionTable = std::unordered_map<std::string, FunctionDeclStmt *>;
    using GlobalTypeTable = std::unordered_map<std::string, std::string>;
    using NominalValueTable = std::unordered_map<std::string, std::string>;

    FunctionLowerer(LirModule &module,
                    LirFunction &function,
                    const FunctionTable &functions,
                    const std::vector<Parameter> &params,
                    const TypeExpr &return_type,
                    FunctionLowerer *parent = nullptr,
                    const GlobalTypeTable *global_types = nullptr,
                    const std::unordered_set<std::string> *fixed_globals = nullptr,
                    bool module_scope = false,
                    std::size_t parameter_offset = 0,
                    const ResolvedVmImports *imports = nullptr,
                    const TypeAliasTable *type_aliases = nullptr,
                    const NominalValueTable *nominal_type_values = nullptr)
        : m_module(module),
          m_function(function),
          m_functions(functions),
          m_return_type(type_aliases == nullptr
                            ? return_type.to_string()
                            : runtime_type_name(return_type, *type_aliases)),
          m_parent(parent),
          m_global_types(global_types),
          m_fixed_globals(fixed_globals),
          m_module_scope(module_scope),
          m_parameter_offset(parameter_offset),
          m_imports(imports),
          m_type_aliases(type_aliases == nullptr ? TypeAliasTable{} : *type_aliases),
          m_nominal_type_values(nominal_type_values == nullptr
                                    ? NominalValueTable{}
                                    : *nominal_type_values)
    {
        m_function.return_type = m_return_type;
        m_current_block = m_function.append_block().index;
        m_function.entry_block = m_current_block;

        begin_scope();
        for (const LirNamedValue &param : m_function.params)
        {
            declare_binding(param.name, param.index);
        }
        for (std::size_t i = 0; i < params.size(); ++i)
        {
            m_local_types.emplace(
                i + m_parameter_offset,
                type_name(params[i].type));
        }
    }

    // Lowers a function body and installs an implicit return_nil when control
    // reaches the end of the block without an explicit return.
    void lower(Stmt &body, const std::vector<Parameter> &params)
    {
        lower_default_parameters(params);
        lower_parameter_types(params);
        body.accept(*this);
        if (!current_block().is_terminated())
        {
            if (m_return_type.empty())
            {
                m_function.set_terminator(m_current_block, LirTerminator::return_nil());
            }
            else
            {
                const LirOperand nil = constant_nil(Token{TokenType::RIEN, "rien", 0, 0});
                m_function.set_terminator(m_current_block,
                                          LirTerminator::return_value(assert_type(nil, m_return_type, {}, current_function_context())));
            }
        }
    }

    void lower_module_statements(const std::vector<Stmt *> &statements)
    {
        for (Stmt *statement : statements)
        {
            statement->accept(*this);
        }
        if (!current_block().is_terminated())
        {
            m_function.set_terminator(m_current_block, LirTerminator::return_nil());
        }
    }

    void visit(LiteralExpr &expr) override
    {
        const std::size_t constant_index = m_module.add_constant(literal_value(expr.token), expr.token.lexeme);
        m_last_value = emit_value(LirOpcode::IR_OP_CONSTANT, {LirOperand::constant(constant_index)}, lir_loc(expr.token));
    }

    void visit(IdentifierExpr &expr) override
    {
        if (expr.name.type == TokenType::PARENT)
        {
            const auto self = lookup_local("ici");
            if (!self.has_value())
            {
                throw VmCompileError("VM: 'parent' ne peut être utilise hors d'une méthode");
            }
            m_last_value = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                      {LirOperand::local(*self)},
                                      lir_loc(expr.name));
            return;
        }
        const auto local = lookup_local(expr.name.lexeme);
        if (local.has_value())
        {
            m_last_value = emit_value(LirOpcode::IR_OP_LOAD_LOCAL, {LirOperand::local(*local)}, lir_loc(expr.name));
            return;
        }

        if (const auto capture = resolve_capture(expr.name.lexeme); capture.has_value())
        {
            m_last_value = emit_value(LirOpcode::IR_OP_LOAD_CAPTURE,
                                      {LirOperand::capture(capture->operand.index)},
                                      lir_loc(expr.name));
            return;
        }

        const std::size_t global_index = m_module.add_global(expr.name.lexeme);
        m_last_value = emit_value(LirOpcode::IR_OP_LOAD_GLOBAL, {LirOperand::global(global_index)}, lir_loc(expr.name));
    }

    void visit(BinaryExpr &expr) override
    {
        if (expr.op.type == TokenType::EGAL)
        {
            if (auto *index = dynamic_cast<IndexAccessExpr *>(expr.left.get()))
            {
                m_last_value = emit_value(LirOpcode::IR_OP_INDEX_SET,
                                          {lower_expr(*index->object),
                                           lower_expr(*index->index),
                                           lower_expr(*expr.right)},
                                          lir_loc(index->bracket));
                return;
            }
            if (auto *member = dynamic_cast<MemberAccessExpr *>(expr.left.get()))
            {
                const LirOperand object = lower_expr(*member->object);
                const LirOperand value = lower_expr(*expr.right);
                emit_effect(LirOpcode::IR_OP_SET_MEMBER,
                            {object, LirOperand::member(m_module.add_member(member->member.lexeme)), value},
                            lir_loc(member->member));
                m_last_value = value;
                return;
            }

            auto *identifier = dynamic_cast<IdentifierExpr *>(expr.left.get());
            if (identifier == nullptr)
            {
                invalid_ast("l'affectation", expr.op);
            }

            const auto local = lookup_local(identifier->name.lexeme);
            if (local.has_value())
            {
                if (m_fixed_locals.contains(*local))
                {
                    throw VmCompileError("VM: impossible d'affecter la variable fixe '" + identifier->name.lexeme + "'");
                }

                LirOperand assigned_value = lower_expr(*expr.right);
                if (const auto type = m_local_types.find(*local); type != m_local_types.end() && !type->second.empty())
                {
                    assigned_value = assert_type(assigned_value, type->second, lir_loc(expr.op),
                                                 variable_context(identifier->name.lexeme));
                }
                emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                            {LirOperand::local(*local), assigned_value},
                            lir_loc(expr.op));
                m_last_value = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                          {LirOperand::local(*local)},
                                          lir_loc(identifier->name));
                return;
            }

            if (const auto capture = resolve_capture(identifier->name.lexeme); capture.has_value())
            {
                if (capture->is_fixed)
                {
                    throw VmCompileError("VM: impossible d'affecter la variable fixe capturee '" +
                                         identifier->name.lexeme + "'");
                }
                LirOperand assigned_value = lower_expr(*expr.right);
                if (!capture->type.empty())
                {
                    assigned_value = assert_type(assigned_value, capture->type, lir_loc(expr.op),
                                                 variable_context(identifier->name.lexeme));
                }
                emit_effect(LirOpcode::IR_OP_STORE_CAPTURE,
                            {capture->operand, assigned_value},
                            lir_loc(expr.op));
                m_last_value = emit_value(LirOpcode::IR_OP_LOAD_CAPTURE,
                                          {capture->operand},
                                          lir_loc(identifier->name));
                return;
            }

            if (m_fixed_globals != nullptr && m_fixed_globals->contains(identifier->name.lexeme))
            {
                throw VmCompileError("VM: impossible d'affecter la variable globale fixe '" +
                                     identifier->name.lexeme + "'");
            }
            const bool declared_variable = m_global_types != nullptr &&
                                           m_global_types->contains(identifier->name.lexeme);
            if (!declared_variable && !m_functions.contains(identifier->name.lexeme))
            {
                throw VmCompileError("VM: affectation a une variable globale non déclarée '" +
                                     identifier->name.lexeme + "'");
            }
            LirOperand assigned = lower_expr(*expr.right);
            if (m_global_types != nullptr)
            {
                const auto type = m_global_types->find(identifier->name.lexeme);
                if (type != m_global_types->end() && !type->second.empty())
                {
                    assigned = assert_type(assigned, type->second, lir_loc(expr.op),
                                           variable_context(identifier->name.lexeme));
                }
            }
            const std::size_t global = m_module.add_global(identifier->name.lexeme);
            emit_effect(LirOpcode::IR_OP_STORE_GLOBAL,
                        {LirOperand::global(global), assigned},
                        lir_loc(expr.op));
            m_last_value = emit_value(LirOpcode::IR_OP_LOAD_GLOBAL,
                                      {LirOperand::global(global)},
                                      lir_loc(identifier->name));
            return;
        }

        if (expr.op.type == TokenType::ET || expr.op.type == TokenType::OU)
        {
            m_last_value = lower_short_circuit(expr);
            return;
        }

        LirOpcode opcode {};
        switch (expr.op.type)
        {
        case TokenType::PLUS:
            opcode = LirOpcode::IR_OP_ADD;
            break;
        case TokenType::MOINS:
            opcode = LirOpcode::IR_OP_SUBTRACT;
            break;
        case TokenType::ETOILE:
            opcode = LirOpcode::IR_OP_MULTIPLY;
            break;
        case TokenType::SLASH:
            opcode = LirOpcode::IR_OP_DIVIDE;
            break;
        case TokenType::MODULO:
            opcode = LirOpcode::IR_OP_MODULO;
            break;
        case TokenType::EGAL_EGAL:
            opcode = LirOpcode::IR_OP_EQUAL;
            break;
        case TokenType::BANG_EGAL:
            opcode = LirOpcode::IR_OP_NOT_EQUAL;
            break;
        case TokenType::INFERIEUR:
            opcode = LirOpcode::IR_OP_LESS;
            break;
        case TokenType::INFERIEUR_EGAL:
            opcode = LirOpcode::IR_OP_LESS_EQUAL;
            break;
        case TokenType::SUPERIEUR:
            opcode = LirOpcode::IR_OP_GREATER;
            break;
        case TokenType::SUPERIEUR_EGAL:
            opcode = LirOpcode::IR_OP_GREATER_EQUAL;
            break;
        default:
            invalid_ast("l'expression binaire", expr.op);
        }

        std::vector<LirOperand> operands;
        operands.push_back(lower_expr(*expr.left));
        operands.push_back(lower_expr(*expr.right));
        m_last_value = emit_value(opcode, std::move(operands), lir_loc(expr.op));
    }
    void visit(SetExpr &expr) override
    {
        if (expr.elements.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
        {
            throw VmCompileError("VM: trop d'éléments dans un littéral d'ensemble");
        }

        std::vector<LirOperand> operands;
        operands.reserve(expr.elements.size());
        for (auto &element : expr.elements)
        {
            operands.push_back(lower_expr(*element));
        }

        m_last_value = emit_value(LirOpcode::IR_OP_ENSEMBLE, std::move(operands), lir_loc(expr.brace));
    }

    void visit(DictionaryExpr &expr) override
    {
        if (expr.entries.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
        {
            throw VmCompileError("VM: trop d'entrées dans un littéral de dictionnaire");
        }

        std::vector<LirOperand> operands;
        operands.reserve(expr.entries.size() * 2);
        for (auto &entry : expr.entries)
        {
            operands.push_back(lower_expr(*entry.key));
            operands.push_back(lower_expr(*entry.value));
        }

        m_last_value = emit_value(LirOpcode::IR_OP_DICTIONARY, std::move(operands), lir_loc(expr.brace));
    }
    void visit(UnaryExpr &expr) override
    {
        if (expr.op.type != TokenType::MOINS)
        {
            if (expr.op.type == TokenType::NON)
            {
                m_last_value = emit_value(LirOpcode::IR_OP_NOT,
                                          {lower_expr(*expr.operand)},
                                          lir_loc(expr.op));
                return;
            }
            invalid_ast("l'expression unaire", expr.op);
        }

        m_last_value = emit_value(LirOpcode::IR_OP_NEGATE,
                                  {lower_expr(*expr.operand)},
                                  lir_loc(expr.op));
    }
    void visit(CastExpr &expr) override
    {
        const std::size_t type_index = m_module.add_type(type_name(expr.target_type));
        m_last_value = emit_value(LirOpcode::IR_OP_CAST,
                                  {lower_expr(*expr.operand), LirOperand::type(type_index)},
                                  lir_loc(expr.target_type));
    }
    void visit(TypeCheckExpr &expr) override
    {
        const std::size_t type_index = m_module.add_type(type_name(expr.type));
        m_last_value = emit_value(LirOpcode::IR_OP_TYPE_CHECK,
                                  {lower_expr(*expr.operand), LirOperand::type(type_index)},
                                  lir_loc(expr.keyword));
    }
    void visit(FunctionExpr &expr) override
    {
        m_last_value = lower_closure("$lambda" + std::to_string(m_module.functions.size()),
                                     expr.params,
                                     expr.return_type,
                                     *expr.body,
                                     lir_loc(expr.keyword));
    }
    void visit(ListExpr &expr) override
    {
        if (expr.elements.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
        {
            throw VmCompileError("VM: trop d'éléments dans un littéral de liste");
        }

        std::vector<LirOperand> operands;
        operands.reserve(expr.elements.size());
        for (auto &element : expr.elements)
        {
            operands.push_back(lower_expr(*element));
        }

        m_last_value = emit_value(LirOpcode::IR_OP_LIST, std::move(operands), lir_loc(expr.bracket));
    }
    void visit(MemberAccessExpr &expr) override
    {
        const auto *identifier = dynamic_cast<IdentifierExpr *>(expr.object.get());
        const LirOpcode opcode = identifier != nullptr && identifier->name.type == TokenType::PARENT
                                     ? LirOpcode::IR_OP_GET_PARENT
                                     : LirOpcode::IR_OP_GET_MEMBER;
        m_last_value = emit_value(opcode,
                                  {lower_expr(*expr.object),
                                   LirOperand::member(m_module.add_member(expr.member.lexeme))},
                                  lir_loc(expr.member));
    }
    void visit(IndexAccessExpr &expr) override
    {
        m_last_value = emit_value(LirOpcode::IR_OP_INDEX_GET,
                                  {lower_expr(*expr.object), lower_expr(*expr.index)},
                                  lir_loc(expr.bracket));
    }

    void visit(PropagationExpr &expr) override
    {
        m_last_value = emit_value(LirOpcode::IR_OP_PROPAGATE,
                                  {lower_expr(*expr.operand)},
                                  lir_loc(expr.keyword));
    }

    void visit(IgnorerStmt &stmt) override
    {
        const LirOperand value = lower_expr(*stmt.expr);
        emit_effect(LirOpcode::IR_OP_IGNORE_RESULT,
                    {value},
                    lir_loc(stmt.keyword));
    }

    void visit(CallExpr &expr) override
    {
        auto *callee = dynamic_cast<IdentifierExpr *>(expr.callee.get());
        if (auto *member = dynamic_cast<MemberAccessExpr *>(expr.callee.get()))
        {
            std::vector<LirOperand> operands;
            operands.push_back(lower_expr(*member->object));
            operands.push_back(LirOperand::member(m_module.add_member(member->member.lexeme)));
            for (const Argument &arg : expr.args)
            {
                append_call_argument(operands, arg.name, lower_expr(*arg.value));
            }
            if (expr.args.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
            {
                throw VmCompileError("VM: trop d'arguments dans un appel membre");
            }
            const auto *identifier = dynamic_cast<IdentifierExpr *>(member->object.get());
            const LirOpcode opcode = identifier != nullptr && identifier->name.type == TokenType::PARENT
                                         ? LirOpcode::IR_OP_CALL_PARENT
                                         : LirOpcode::IR_OP_CALL_MEMBER;
            m_last_value = emit_value(opcode,
                                      std::move(operands),
                                      lir_loc(member->member));
            return;
        }
        if (callee == nullptr || lookup_local(callee->name.lexeme).has_value() ||
            resolve_capture(callee->name.lexeme).has_value())
        {
            std::vector<LirOperand> operands{lower_expr(*expr.callee)};
            for (const Argument &arg : expr.args)
            {
                append_call_argument(operands, arg.name, lower_expr(*arg.value));
            }
            if (expr.args.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
            {
                throw VmCompileError("VM: trop d'arguments dans un appel");
            }
            m_last_value = emit_value(LirOpcode::IR_OP_CALL, std::move(operands), lir_loc(expr.paren));
            return;
        }

        const std::size_t global_index = m_module.add_global(callee->name.lexeme);
        const auto declared_function = m_functions.find(callee->name.lexeme);
        if (declared_function == m_functions.end())
        {
            std::vector<LirOperand> operands{LirOperand::global(global_index)};
            for (const Argument &arg : expr.args)
            {
                append_call_argument(operands, arg.name, lower_expr(*arg.value));
            }
            if (expr.args.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
            {
                throw VmCompileError("VM: trop d'arguments dans un appel");
            }
            m_last_value = emit_value(LirOpcode::IR_OP_CALL_GLOBAL, std::move(operands), lir_loc(expr.paren));
            return;
        }

        const std::vector<Parameter> &params = declared_function->second->params;
        std::vector<std::optional<std::size_t>> bound_argument(params.size());
        std::vector<std::size_t> argument_targets;
        argument_targets.reserve(expr.args.size());
        std::size_t next_positional = 0;

        for (const Argument &arg : expr.args)
        {
            std::size_t target = params.size();
            if (!arg.name.empty())
            {
                for (std::size_t i = 0; i < params.size(); ++i)
                {
                    if (params[i].name == arg.name)
                    {
                        target = i;
                        break;
                    }
                }
                if (target == params.size())
                {
                    throw VmCompileError("VM: aucun paramètre nommé '" + arg.name + "'");
                }
            }
            else
            {
                while (next_positional < params.size() && bound_argument[next_positional].has_value())
                {
                    ++next_positional;
                }
                if (next_positional == params.size())
                {
                    throw VmCompileError("VM: trop d'arguments fournis a l'appel de '" + callee->name.lexeme + "'");
                }
                target = next_positional++;
            }

            if (bound_argument[target].has_value())
            {
                throw VmCompileError("VM: le paramètre '" + params[target].name + "' est fourni plusieurs fois");
            }
            const std::size_t argument_local = allocate_hidden_local("$arg");
            bound_argument[target] = argument_local;
            argument_targets.push_back(argument_local);
        }

        for (std::size_t i = 0; i < expr.args.size(); ++i)
        {
            const LirOperand value = lower_expr(*expr.args[i].value);
            emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                        {LirOperand::local(argument_targets[i]), value},
                        lir_loc(expr.paren));
        }

        std::vector<LirOperand> operands{LirOperand::global(global_index)};
        for (std::size_t i = 0; i < params.size(); ++i)
        {
            if (bound_argument[i].has_value())
            {
                append_call_argument(operands,
                                     "",
                                     emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                {LirOperand::local(*bound_argument[i])},
                                                lir_loc(expr.paren)));
            }
            else
            {
                if (!params[i].default_value)
                {
                    throw VmCompileError("VM: argument manquant pour le paramètre '" + params[i].name + "'");
                }
                append_call_argument(operands, "", constant_nil(expr.paren));
            }
        }

        for (std::size_t i = 0; i < params.size(); ++i)
        {
            if (!params[i].default_value)
            {
                continue;
            }
            const bool provided = bound_argument[i].has_value();
            const std::size_t constant_index = m_module.add_constant(Value::logique(provided), provided ? "vrai" : "faux");
            append_call_argument(operands,
                                 "",
                                 emit_value(LirOpcode::IR_OP_CONSTANT,
                                            {LirOperand::constant(constant_index)},
                                            lir_loc(expr.paren)));
        }

        if ((operands.size() - 1) / 2 > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
        {
            throw VmCompileError("VM: trop d'arguments ABI dans un appel");
        }
        m_last_value = emit_value(LirOpcode::IR_OP_CALL_GLOBAL, std::move(operands), lir_loc(expr.paren));
    }

    void visit(BlockStmt &stmt) override
    {
        const bool was_module_scope = m_module_scope;
        m_module_scope = false;
        begin_scope();
        for (auto &statement : stmt.statements)
        {
            if (current_block().is_terminated())
            {
                break;
            }
            statement->accept(*this);
        }
        leave_scope();
        m_module_scope = was_module_scope;
    }

    void visit(ExprStmt &stmt) override
    {
        const LirOperand value = lower_expr(*stmt.expr);
        emit_effect(LirOpcode::IR_OP_DISCARD, {value}, {});
    }

    void visit(ReturnStmt &stmt) override
    {
        const LirOperand value = stmt.value ? lower_expr(*stmt.value) : constant_nil(stmt.keyword);
        const LirOperand checked = m_return_type.empty()
                                       ? value
                                       : assert_type(value, m_return_type, lir_loc(stmt.keyword), current_function_context());
        m_function.set_terminator(
            m_current_block,
            LirTerminator::return_value(checked, lir_loc(stmt.keyword)));
    }

    void visit(VarDeclStmt &stmt) override
    {
        if (m_module_scope)
        {
            LirOperand value = stmt.initializer ? lower_expr(*stmt.initializer) : constant_nil(stmt.name);
            if (!stmt.type.empty())
            {
                value = assert_type(value, type_name(stmt.type), lir_loc(stmt.name),
                                   variable_context(stmt.name.lexeme));
            }
            emit_effect(LirOpcode::IR_OP_STORE_GLOBAL,
                        {LirOperand::global(m_module.add_global(stmt.name.lexeme)), value},
                        lir_loc(stmt.name));
            return;
        }
        if (has_binding_in_current_scope(stmt.name.lexeme))
        {
            throw VmCompileError("VM: variable locale dupliquée '" + stmt.name.lexeme + "'");
        }

        const std::size_t local_index = m_next_local_index++;
        m_function.locals.push_back({local_index, stmt.name.lexeme});
        declare_binding(stmt.name.lexeme, local_index);

        LirOperand initial_value = stmt.initializer
            ? lower_expr(*stmt.initializer)
            : constant_nil(stmt.name);
        if (!stmt.type.empty())
        {
            m_local_types.emplace(local_index, type_name(stmt.type));
            initial_value = assert_type(initial_value, type_name(stmt.type), lir_loc(stmt.name),
                                        variable_context(stmt.name.lexeme));
        }
        if (stmt.is_fixe)
        {
            m_fixed_locals.insert(local_index);
        }
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(local_index), initial_value},
                    lir_loc(stmt.name));
    }
    void visit(FunctionDeclStmt &stmt) override
    {
        if (has_binding_in_current_scope(stmt.name.lexeme))
        {
            throw VmCompileError("VM: fonction locale dupliquée '" + stmt.name.lexeme + "'");
        }
        const std::size_t local_index = m_next_local_index++;
        m_function.locals.push_back({local_index, stmt.name.lexeme});
        declare_binding(stmt.name.lexeme, local_index);
        m_fixed_locals.insert(local_index);
        const LirOperand closure = lower_closure(stmt.name.lexeme,
                                                 stmt.params,
                                                 stmt.return_type,
                                                 *stmt.body,
                                                 lir_loc(stmt.name));
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(local_index), closure},
                    lir_loc(stmt.name));
    }
    void visit(ClassDeclStmt &stmt) override
    {
        if (m_module_scope)
        {
            for (std::size_t i = 0; i < m_module.classes.size(); ++i)
            {
                if (m_module.classes[i].name == stmt.name.lexeme)
                {
                    const LirOperand value = emit_class_value(i, stmt);
                    emit_effect(LirOpcode::IR_OP_STORE_GLOBAL,
                                {LirOperand::global(m_module.add_global(stmt.name.lexeme)), value},
                                lir_loc(stmt.name));
                    return;
                }
            }
            throw VmCompileError("VM: descripteur de classe introuvable '" + stmt.name.lexeme + "'");
        }

        if (has_binding_in_current_scope(stmt.name.lexeme))
        {
            throw VmCompileError("VM: classe locale dupliquée '" + stmt.name.lexeme + "'");
        }
        const std::size_t local = m_next_local_index++;
        m_function.locals.push_back({local, stmt.name.lexeme});
        declare_binding(stmt.name.lexeme, local);
        m_fixed_locals.insert(local);

        LirClassDescriptor descriptor;
        descriptor.name = stmt.name.lexeme;
        descriptor.type_identity = nominal_type_identity(m_function.source_path, stmt.name);
        Token identity = stmt.name;
        identity.lexeme = descriptor.type_identity;
        m_type_aliases.insert_or_assign(stmt.name.lexeme, TypeExpr::named(identity));
        descriptor.parent = type_name(stmt.parent);
        for (const TypeExpr &interface : stmt.interfaces)
        {
            descriptor.interfaces.push_back(type_name(interface));
        }
        for (auto &member : stmt.members)
        {
            if (auto *field = dynamic_cast<VarDeclStmt *>(member.get()))
            {
                descriptor.fields.push_back({field->name.lexeme,
                                             type_name(field->type),
                                             field->is_prive,
                                             field->is_fixe});
            }
            else if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
            {
                descriptor.methods.push_back(lower_method(stmt.name.lexeme, *method));
            }
        }
        const std::size_t descriptor_index = m_module.classes.size();
        m_module.classes.push_back(std::move(descriptor));
        const LirOperand value = emit_class_value(descriptor_index, stmt);
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(local), value},
                    lir_loc(stmt.name));
    }
    void visit(InterfaceDeclStmt &stmt) override
    {
        if (m_module_scope)
        {
            for (std::size_t i = 0; i < m_module.interfaces.size(); ++i)
            {
                if (m_module.interfaces[i].name == stmt.name.lexeme)
                {
                    const LirOperand value = emit_value(LirOpcode::IR_OP_INTERFACE,
                                                        {LirOperand::interface(i)},
                                                        lir_loc(stmt.name));
                    emit_effect(LirOpcode::IR_OP_STORE_GLOBAL,
                                {LirOperand::global(m_module.add_global(stmt.name.lexeme)), value},
                                lir_loc(stmt.name));
                    return;
                }
            }
            throw VmCompileError("VM: descripteur d'interface introuvable '" + stmt.name.lexeme + "'");
        }

        if (has_binding_in_current_scope(stmt.name.lexeme))
        {
            throw VmCompileError("VM: interface locale dupliquée '" + stmt.name.lexeme + "'");
        }
        LirInterfaceDescriptor descriptor;
        descriptor.name = stmt.name.lexeme;
        descriptor.type_identity = nominal_type_identity(m_function.source_path, stmt.name);
        Token identity = stmt.name;
        identity.lexeme = descriptor.type_identity;
        m_type_aliases.insert_or_assign(stmt.name.lexeme, TypeExpr::named(identity));
        for (auto &member : stmt.methods)
        {
            auto *method = dynamic_cast<FunctionDeclStmt *>(member.get());
            if (method == nullptr)
            {
                continue;
            }
            LirInterfaceMethodDescriptor signature;
            signature.name = method->name.lexeme;
            signature.return_type = type_name(method->return_type);
            for (const Parameter &parameter : method->params)
            {
                signature.parameter_types.push_back(type_name(parameter.type));
            }
            descriptor.methods.push_back(std::move(signature));
        }
        const std::size_t descriptor_index = m_module.interfaces.size();
        m_module.interfaces.push_back(std::move(descriptor));
        const std::size_t local = m_next_local_index++;
        m_function.locals.push_back({local, stmt.name.lexeme});
        declare_binding(stmt.name.lexeme, local);
        m_fixed_locals.insert(local);
        const LirOperand value = emit_value(LirOpcode::IR_OP_INTERFACE,
                                            {LirOperand::interface(descriptor_index)},
                                            lir_loc(stmt.name));
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(local), value},
                    lir_loc(stmt.name));
    }
    void visit(TypeAliasDeclStmt &stmt) override
    {
        const TypeExpr resolved = resolve_type_aliases(stmt.target, m_type_aliases);
        if (resolved.kind == TypeExprKind::NAMED &&
            resolved.name.find('@') != std::string::npos)
        {
            std::string value_name = runtime_type_name(stmt.target, m_type_aliases, false);
            for (auto value = m_nominal_type_values.find(value_name);
                 value != m_nominal_type_values.end();
                 value = m_nominal_type_values.find(value_name))
                value_name = value->second;
            m_nominal_type_values.insert_or_assign(stmt.name.lexeme,
                                                   std::move(value_name));
        }
        m_type_aliases.insert_or_assign(stmt.name.lexeme, resolved);
    }
    void visit(ImportStmt &stmt) override
    {
        if (m_imports == nullptr || !m_imports->contains(&stmt))
        {
            throw VmCompileError("VM: import non résolu '" + stmt.module_name.lexeme + "'");
        }
        const ResolvedVmImport &resolved = m_imports->at(&stmt);
        for (const std::string &initializer : resolved.initializer_symbols)
        {
            emit_effect(LirOpcode::IR_OP_INIT_GLOBAL,
                        {LirOperand::global(m_module.add_global(initializer))},
                        lir_loc(stmt.module_name));
        }
        const auto bind_local = [&](const std::string &name, const LirOperand value, const Token &site)
        {
            if (has_binding_in_current_scope(name))
            {
                throw VmCompileError("VM: liaison d'import dupliquée '" + name + "'");
            }
            const std::size_t local = m_next_local_index++;
            m_function.locals.push_back({local, name});
            declare_binding(name, local);
            m_fixed_locals.insert(local);
            emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                        {LirOperand::local(local), value},
                        lir_loc(site));
        };

        if (!stmt.imported_members.empty())
        {
            for (const ImportStmt::ImportedMember &member : stmt.imported_members)
            {
                if (const auto type = resolved.export_types.find(member.name.lexeme); type != resolved.export_types.end())
                {
                    m_type_aliases.insert_or_assign(member.alias.lexeme.empty() ? member.name.lexeme : member.alias.lexeme,
                                                  type->second);
                    if (!resolved.export_symbols.contains(member.name.lexeme) &&
                        !resolved.export_type_symbols.contains(member.name.lexeme))
                        continue;
                }
                auto symbol = resolved.export_symbols.find(member.name.lexeme);
                const auto type_symbol = resolved.export_type_symbols.find(member.name.lexeme);
                const std::string *symbol_name = symbol == resolved.export_symbols.end()
                    ? (type_symbol == resolved.export_type_symbols.end() ? nullptr : &type_symbol->second)
                    : &symbol->second;
                if (symbol_name == nullptr)
                {
                    throw VmCompileError("VM: membre non exporté ou introuvable dans le module: " + member.name.lexeme);
                }
                const LirOperand value = emit_value(
                    LirOpcode::IR_OP_LOAD_GLOBAL,
                    {LirOperand::global(m_module.add_global(*symbol_name))},
                    lir_loc(member.name));
                bind_local(member.alias.lexeme.empty() ? member.name.lexeme : member.alias.lexeme,
                           value,
                           member.alias.lexeme.empty() ? member.name : member.alias);
            }
            return;
        }

        LirNamespaceDescriptor descriptor;
        for (const auto &[name, symbol] : resolved.export_symbols)
        {
            descriptor.members.push_back({name, m_module.add_global(symbol)});
        }
        for (const auto &[name, symbol] : resolved.export_type_symbols)
        {
            if (!resolved.export_symbols.contains(name))
                descriptor.members.push_back({name, m_module.add_global(symbol)});
        }
        const std::size_t descriptor_index = m_module.namespaces.size();
        m_module.namespaces.push_back(std::move(descriptor));
        const LirOperand value = emit_value(LirOpcode::IR_OP_NAMESPACE,
                                            {LirOperand::name_space(descriptor_index)},
                                            lir_loc(stmt.module_name));
        const std::string alias = stmt.alias.lexeme.empty()
                                      ? stmt.module_name.lexeme.substr(stmt.module_name.lexeme.rfind('.') + 1)
                                      : stmt.alias.lexeme;
        bind_local(alias, value, stmt.alias.lexeme.empty() ? stmt.module_name : stmt.alias);
        for (const auto &[name, type] : resolved.export_types)
            m_type_aliases.insert_or_assign(alias + '.' + name, type);
    }
    void visit(IfStmt &stmt) override
    {
        const LirOperand condition = lower_expr(*stmt.condition);
        const std::size_t then_block = m_function.append_block().index;
        const std::size_t else_block = stmt.else_branch
            ? m_function.append_block().index
            : m_function.append_block().index;
        const std::size_t merge_block = m_function.append_block().index;

        m_function.set_terminator(m_current_block,
                                  LirTerminator::branch(condition, then_block, else_block, lir_loc(Token{TokenType::SI, "si", 0, 0})));

        m_current_block = then_block;
        stmt.then_branch->accept(*this);
        if (!current_block().is_terminated())
        {
            m_function.set_terminator(m_current_block, LirTerminator::jump(merge_block));
        }

        m_current_block = else_block;
        if (stmt.else_branch)
        {
            stmt.else_branch->accept(*this);
        }
        if (!current_block().is_terminated())
        {
            m_function.set_terminator(m_current_block, LirTerminator::jump(merge_block));
        }

        m_current_block = merge_block;
    }
    void visit(ForStmt &stmt) override
    {
        const LirOperand iterable = lower_expr(*stmt.iterable);
        const LirOperand snapshot = emit_value(LirOpcode::IR_OP_ITERATION_SNAPSHOT,
                                               {iterable},
                                               lir_loc(stmt.variable));
        const std::size_t iterable_local = allocate_hidden_local("$iter");
        const std::size_t index_local = allocate_hidden_local("$index");
        const std::size_t item_local = m_next_local_index++;

        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(iterable_local), snapshot},
                    lir_loc(stmt.variable));

        const std::size_t zero_constant = m_module.add_constant(Value::entier(0), "0");
        const LirOperand zero = emit_value(LirOpcode::IR_OP_CONSTANT,
                                           {LirOperand::constant(zero_constant)},
                                           lir_loc(stmt.variable));
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(index_local), zero},
                    lir_loc(stmt.variable));

        const std::size_t condition_block = m_function.append_block().index;
        const std::size_t body_block = m_function.append_block().index;
        const std::size_t increment_block = m_function.append_block().index;
        const std::size_t exit_block = m_function.append_block().index;

        m_function.set_terminator(m_current_block,
                                  LirTerminator::jump(condition_block, lir_loc(stmt.variable)));

        m_current_block = condition_block;
        const LirOperand current_index = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                    {LirOperand::local(index_local)},
                                                    lir_loc(stmt.variable));
        const LirOperand sequence = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                               {LirOperand::local(iterable_local)},
                                               lir_loc(stmt.variable));
        const LirOperand length = emit_value(LirOpcode::IR_OP_SEQUENCE_LENGTH,
                                             {sequence},
                                             lir_loc(stmt.variable));
        const LirOperand condition = emit_value(LirOpcode::IR_OP_LESS,
                                                {current_index, length},
                                                lir_loc(stmt.variable));
        m_function.set_terminator(m_current_block,
                                  LirTerminator::branch(condition,
                                                        body_block,
                                                        exit_block,
                                                        lir_loc(stmt.variable)));

        m_current_block = body_block;
        const LirOperand body_sequence = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                    {LirOperand::local(iterable_local)},
                                                    lir_loc(stmt.variable));
        const LirOperand body_index = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                 {LirOperand::local(index_local)},
                                                 lir_loc(stmt.variable));
        const LirOperand item = emit_value(LirOpcode::IR_OP_INDEX_GET,
                                           {body_sequence, body_index},
                                           lir_loc(stmt.variable));
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(item_local), item},
                    lir_loc(stmt.variable));

        begin_scope();
        m_function.locals.push_back({item_local, stmt.variable.lexeme});
        declare_binding(stmt.variable.lexeme, item_local);
        m_loop_stack.push_back({increment_block, exit_block});
        stmt.body->accept(*this);
        m_loop_stack.pop_back();
        leave_scope();
        // The loop variable and everything the body declares are new bindings on
        // each turn, so their slots start each turn empty.
        clear_body_locals(body_block, item_local, stmt.variable);
        if (!current_block().is_terminated())
        {
            m_function.set_terminator(m_current_block, LirTerminator::jump(increment_block));
        }

        m_current_block = increment_block;
        const LirOperand increment_index = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                      {LirOperand::local(index_local)},
                                                      lir_loc(stmt.variable));
        const std::size_t one_constant = m_module.add_constant(Value::entier(1), "1");
        const LirOperand one = emit_value(LirOpcode::IR_OP_CONSTANT,
                                          {LirOperand::constant(one_constant)},
                                          lir_loc(stmt.variable));
        const LirOperand next_index = emit_value(LirOpcode::IR_OP_ADD,
                                                 {increment_index, one},
                                                 lir_loc(stmt.variable));
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(index_local), next_index},
                    lir_loc(stmt.variable));
        m_function.set_terminator(m_current_block, LirTerminator::jump(condition_block));

        m_current_block = exit_block;
    }
    void visit(WhileStmt &stmt) override
    {
        const std::size_t condition_block = m_function.append_block().index;
        const std::size_t body_block = m_function.append_block().index;
        const std::size_t exit_block = m_function.append_block().index;

        m_function.set_terminator(m_current_block,
                                  LirTerminator::jump(condition_block,
                                                      lir_loc(Token{TokenType::TANT_QUE, "tant que", 0, 0})));

        m_current_block = condition_block;
        const LirOperand condition = lower_expr(*stmt.condition);
        m_function.set_terminator(m_current_block,
                                  LirTerminator::branch(condition,
                                                        body_block,
                                                        exit_block,
                                                        lir_loc(Token{TokenType::TANT_QUE, "tant que", 0, 0})));

        m_current_block = body_block;
        const std::size_t first_body_local = m_next_local_index;
        m_loop_stack.push_back({condition_block, exit_block});
        stmt.body->accept(*this);
        m_loop_stack.pop_back();
        clear_body_locals(body_block,
                          first_body_local,
                          Token{TokenType::TANT_QUE, "tant que", 0, 0});
        if (!current_block().is_terminated())
        {
            m_function.set_terminator(m_current_block, LirTerminator::jump(condition_block));
        }

        m_current_block = exit_block;
    }
    void visit(BreakStmt &stmt) override
    {
        if (m_loop_stack.empty())
        {
            throw VmCompileError("VM: 'arreter' hors d'une boucle");
        }

        m_function.set_terminator(m_current_block,
                                  LirTerminator::jump(m_loop_stack.back().break_block, lir_loc(stmt.keyword)));
    }
    void visit(ContinueStmt &stmt) override
    {
        if (m_loop_stack.empty())
        {
            throw VmCompileError("VM: 'continuer' hors d'une boucle");
        }

        m_function.set_terminator(m_current_block,
                                  LirTerminator::jump(m_loop_stack.back().continue_block, lir_loc(stmt.keyword)));
    }
    void visit(AgirSelonStmt &stmt) override
    {
        const std::size_t matched_local = allocate_hidden_local("$match");
        const std::size_t result_local = allocate_hidden_local("$match_result");
        const LirOperand matched_value = lower_expr(*stmt.expression);
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(matched_local), matched_value},
                    lir_loc(stmt.keyword));
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(result_local), constant_nil(stmt.keyword)},
                    lir_loc(stmt.keyword));

        const std::size_t exit_block = m_function.append_block().index;

        for (AgirSelonBranch &branch : stmt.branches)
        {
            for (Pattern &pattern : branch.patterns)
            {
                const std::size_t body_block = m_function.append_block().index;
                const std::size_t next_pattern_block = m_function.append_block().index;
                const LirOperand candidate = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                        {LirOperand::local(matched_local)},
                                                        lir_loc(stmt.keyword));

                LirOperand condition;
                switch (pattern.kind)
                {
                case PatternKind::LITERAL:
                    condition = emit_value(LirOpcode::IR_OP_EQUAL,
                                           {candidate, lower_expr(*pattern.literal)},
                                           lir_loc(stmt.keyword));
                    break;
                case PatternKind::TYPE_BINDING:
                {
                    const std::size_t type_index = m_module.add_type(type_name(pattern.type));
                    condition = emit_value(LirOpcode::IR_OP_TYPE_CHECK,
                                           {candidate, LirOperand::type(type_index)},
                                           lir_loc(pattern.type));
                    break;
                }
                case PatternKind::RIEN:
                    condition = emit_value(LirOpcode::IR_OP_EQUAL,
                                           {candidate, constant_nil(pattern.name)},
                                           lir_loc(pattern.name));
                    break;
                case PatternKind::RESULT_SUCCESS:
                    condition = emit_value(LirOpcode::IR_OP_RESULT_IS_SUCCESS,
                                           {candidate},
                                           lir_loc(pattern.constructor));
                    break;
                case PatternKind::RESULT_FAILURE:
                    if (!pattern.type.empty())
                    {
                        const std::size_t type_index =
                            m_module.add_type(type_name(pattern.type));
                        condition = emit_value(
                            LirOpcode::IR_OP_RESULT_FAILURE_TYPE,
                            {candidate, LirOperand::type(type_index)},
                            lir_loc(pattern.constructor));
                    }
                    else
                    {
                        const LirOperand success = emit_value(
                            LirOpcode::IR_OP_RESULT_IS_SUCCESS,
                            {candidate},
                            lir_loc(pattern.constructor));
                        condition = emit_value(LirOpcode::IR_OP_NOT,
                                               {success},
                                               lir_loc(pattern.constructor));
                    }
                    break;
                }

                m_function.set_terminator(m_current_block,
                                          LirTerminator::branch(condition,
                                                                body_block,
                                                                next_pattern_block,
                                                                lir_loc(stmt.keyword)));

                m_current_block = body_block;
                begin_scope();
                if (pattern.kind == PatternKind::TYPE_BINDING ||
                    ((pattern.kind == PatternKind::RESULT_SUCCESS ||
                      pattern.kind == PatternKind::RESULT_FAILURE) &&
                     pattern.name.lexeme != "_"))
                {
                    const std::size_t binding_local = m_next_local_index++;
                    m_function.locals.push_back({binding_local, pattern.name.lexeme});
                    declare_binding(pattern.name.lexeme, binding_local);
                    LirOperand binding_value = emit_value(
                        LirOpcode::IR_OP_LOAD_LOCAL,
                        {LirOperand::local(matched_local)},
                        lir_loc(pattern.name));
                    if (pattern.kind == PatternKind::RESULT_SUCCESS ||
                        pattern.kind == PatternKind::RESULT_FAILURE)
                    {
                        binding_value = emit_value(
                            LirOpcode::IR_OP_RESULT_PAYLOAD,
                            {binding_value},
                            lir_loc(pattern.name));
                    }
                    emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                                {LirOperand::local(binding_local), binding_value},
                                lir_loc(pattern.name));
                }
                if (branch.terminator == BranchTerminator::PROPAGER)
                {
                    leave_scope();
                    LirOperand matched = emit_value(
                        LirOpcode::IR_OP_LOAD_LOCAL,
                        {LirOperand::local(matched_local)},
                        lir_loc(stmt.keyword));
                    if (!m_return_type.empty())
                    {
                        matched = assert_type(
                            matched,
                            m_return_type,
                            lir_loc(branch.terminator_token),
                            current_function_context());
                    }
                    m_function.set_terminator(m_current_block,
                                              LirTerminator::return_value(
                                                  matched,
                                                  lir_loc(branch.terminator_token)));
                }
                else if (branch.terminator == BranchTerminator::IGNORER)
                {
                    leave_scope();
                    const LirOperand matched = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                          {LirOperand::local(matched_local)},
                                                          lir_loc(stmt.keyword));
                    emit_effect(LirOpcode::IR_OP_IGNORE_RESULT, {matched}, lir_loc(stmt.keyword));
                    m_function.set_terminator(m_current_block,
                                              LirTerminator::jump(exit_block, lir_loc(stmt.keyword)));
                }
                else
                {
                    if (auto *expression = dynamic_cast<ExprStmt *>(branch.body.get()))
                    {
                        const LirOperand value = lower_expr(*expression->expr);
                        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                                    {LirOperand::local(result_local), value},
                                    lir_loc(stmt.keyword));
                    }
                    else
                    {
                        branch.body->accept(*this);
                    }
                    leave_scope();
                }
                if (!current_block().is_terminated())
                {
                    m_function.set_terminator(m_current_block,
                                              LirTerminator::jump(exit_block, lir_loc(stmt.keyword)));
                }

                m_current_block = next_pattern_block;
            }
        }

        if (stmt.else_branch)
        {
            if (auto *expression = dynamic_cast<ExprStmt *>(stmt.else_branch.get()))
            {
                const LirOperand value = lower_expr(*expression->expr);
                emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                            {LirOperand::local(result_local), value},
                            lir_loc(stmt.keyword));
            }
            else
            {
                stmt.else_branch->accept(*this);
            }
            if (!current_block().is_terminated())
            {
                m_function.set_terminator(m_current_block,
                                          LirTerminator::jump(exit_block, lir_loc(stmt.keyword)));
            }
        }
        else
        {
            emit_effect(LirOpcode::IR_OP_MATCH_ERROR, {}, lir_loc(stmt.keyword));
            m_function.set_terminator(m_current_block, LirTerminator::return_nil(lir_loc(stmt.keyword)));
        }

        m_current_block = exit_block;
        m_last_value = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                  {LirOperand::local(result_local)},
                                  lir_loc(stmt.keyword));
    }

private:
    struct LoopContext
    {
        std::size_t continue_block = 0;
        std::size_t break_block = 0;
    };

    struct BindingSource
    {
        LirOperand operand;
        std::string type;
        bool is_fixed = false;
    };

    LirModule &m_module;
    LirFunction &m_function;
    const FunctionTable &m_functions;
    std::string m_return_type;
    FunctionLowerer *m_parent = nullptr;
    const GlobalTypeTable *m_global_types = nullptr;
    const std::unordered_set<std::string> *m_fixed_globals = nullptr;
    bool m_module_scope = false;
    std::size_t m_parameter_offset = 0;
    const ResolvedVmImports *m_imports = nullptr;
    TypeAliasTable m_type_aliases;
    std::vector<TypeAliasTable> m_type_alias_scopes;
    NominalValueTable m_nominal_type_values;
    std::vector<NominalValueTable> m_nominal_type_value_scopes;
    LirOperand m_last_value = LirOperand::temp(0);
    std::size_t m_next_temp = 0;
    std::size_t m_current_block = 0;
    std::size_t m_next_local_index = m_function.params.size();
    std::size_t m_next_hidden_local = 0;
    std::unordered_map<std::string, std::vector<std::size_t>> m_locals_by_name;
    std::unordered_map<std::size_t, std::string> m_local_types;
    std::unordered_set<std::size_t> m_fixed_locals;
    std::unordered_map<std::string, BindingSource> m_captures_by_name;
    std::vector<LoopContext> m_loop_stack;
    std::vector<std::vector<std::string>> m_scope_stack;

    [[nodiscard]] LirBlock &current_block()
    {
        return m_function.block(m_current_block);
    }

    void append_call_argument(std::vector<LirOperand> &operands,
                              const std::string &name,
                              const LirOperand value)
    {
        operands.push_back(LirOperand::argument_name(m_module.add_argument_name(name)));
        operands.push_back(value);
    }

    [[nodiscard]] LirOperand load_named_value(const Token &name)
    {
        Token part = name;
        auto dot = name.lexeme.find('.');
        part.lexeme = name.lexeme.substr(0, dot);
        ExprPtr expression = std::make_unique<IdentifierExpr>(part);
        while (dot != std::string::npos)
        {
            const auto end = name.lexeme.find('.', dot + 1);
            part.lexeme = name.lexeme.substr(dot + 1, end == std::string::npos ? end : end - dot - 1);
            expression = std::make_unique<MemberAccessExpr>(std::move(expression), name, part);
            dot = end;
        }
        return lower_expr(*expression);
    }

    [[nodiscard]] LirOperand emit_class_value(const std::size_t descriptor_index,
                                              ClassDeclStmt &stmt)
    {
        std::vector<LirOperand> operands{LirOperand::klass(descriptor_index)};
        if (!stmt.parent.empty())
        {
            operands.push_back(load_named_value(stmt.parent.as_token()));
        }
        for (const TypeExpr &interface : stmt.interfaces)
        {
            Token resolved = interface.as_token();
            resolved.lexeme = runtime_type_name(interface, m_type_aliases, false);
            for (auto value = m_nominal_type_values.find(resolved.lexeme);
                 value != m_nominal_type_values.end();
                 value = m_nominal_type_values.find(resolved.lexeme))
                resolved.lexeme = value->second;
            operands.push_back(load_named_value(resolved));
        }
        return emit_value(LirOpcode::IR_OP_CLASS, std::move(operands), lir_loc(stmt.name));
    }

    [[nodiscard]] LirMethodDescriptor lower_method(const std::string &class_name,
                                                   FunctionDeclStmt &method)
    {
        const std::size_t function_index = m_module.functions.size();
        LirFunction &function = m_module.append_function(class_name + "." + method.name.lexeme);
        function.source_path = m_function.source_path;
        function.source_text = m_function.source_text;
        function.source_arity = method.params.size();
        function.params.push_back({0, "ici"});
        for (std::size_t i = 0; i < method.params.size(); ++i)
        {
            function.params.push_back({i + 1, method.params[i].name});
            function.optional_params.push_back(static_cast<bool>(method.params[i].default_value));
        }
        std::size_t flag = method.params.size() + 1;
        for (const Parameter &parameter : method.params)
        {
            if (parameter.default_value)
            {
                function.params.push_back({flag++, "$provided_" + parameter.name});
            }
        }

        FunctionLowerer lowerer(m_module,
                                function,
                                m_functions,
                                method.params,
                                method.return_type,
                                this,
                                m_global_types,
                                m_fixed_globals,
                                false,
                                1,
                                m_imports,
                                &m_type_aliases,
                                &m_nominal_type_values);
        lowerer.lower(*method.body, method.params);

        LirMethodDescriptor descriptor;
        descriptor.name = method.name.lexeme;
        descriptor.function_index = function_index;
        descriptor.return_type = type_name(method.return_type);
        descriptor.is_private = method.is_prive;
        descriptor.is_override = method.is_remplace;
        for (const Parameter &parameter : method.params)
        {
            descriptor.parameter_types.push_back(type_name(parameter.type));
        }
        for (const LirCapture &capture : function.captures)
        {
            descriptor.capture_sources.push_back(capture.source);
        }
        return descriptor;
    }

    // Temps are kept explicit in LIR so intermediate values remain visible
    // during dumps and debugging.
    [[nodiscard]] LirOperand allocate_temp()
    {
        const std::size_t temp_index = m_next_temp++;
        m_function.temps.push_back(temp_index);
        return LirOperand::temp(temp_index);
    }

    // Puts a CLEAR_LOCALS at the top of a loop body covering every slot the body
    // allocated. It goes in after lowering because only then is the range known,
    // and at the front because the body's first act must be to start from empty
    // slots -- including the loop variable, which is stored just after it.
    //
    // Reusing one slot across iterations is what a frame naturally does, and it
    // is invisible until a closure captures that slot: the cell is created once
    // and every closure made later in the loop shares it, so all of them see the
    // last iteration's value. The tree walker gives each iteration its own
    // environment and so its own binding; this is how the VM agrees.
    void clear_body_locals(const std::size_t body_block,
                           const std::size_t first_local,
                           const Token &site)
    {
        const std::size_t count = m_next_local_index - first_local;
        if (count == 0)
        {
            return;
        }
        std::vector<LirInstruction> &instructions = m_function.blocks[body_block].instructions;
        instructions.insert(instructions.begin(),
                            LirInstruction::make(LirOpcode::IR_OP_CLEAR_LOCALS,
                                                 LirOperand::temp(0),
                                                 {LirOperand::local(first_local),
                                                  LirOperand::local(count)},
                                                 lir_loc(site)));
    }

    [[nodiscard]] std::string variable_context(const std::string &name) const
    {
        return "la variable '" + name + "'";
    }

    [[nodiscard]] std::string current_function_context() const
    {
        return "la fonction '" + m_function.name + "'";
    }

    [[nodiscard]] std::size_t allocate_hidden_local()
    {
        const std::size_t local_index = m_next_local_index++;
        m_function.locals.push_back({local_index, "$logic" + std::to_string(m_next_hidden_local++)});
        return local_index;
    }

    [[nodiscard]] std::size_t allocate_hidden_local(const std::string &prefix)
    {
        const std::size_t local_index = m_next_local_index++;
        m_function.locals.push_back({local_index, prefix + std::to_string(m_next_hidden_local++)});
        return local_index;
    }

    void lower_default_parameters(const std::vector<Parameter> &params)
    {
        std::size_t flag_index = params.size() + m_parameter_offset;
        for (std::size_t param_index = 0; param_index < params.size(); ++param_index)
        {
            const Parameter &param = params[param_index];
            if (!param.default_value)
            {
                continue;
            }

            const std::size_t default_block = m_function.append_block().index;
            const std::size_t ready_block = m_function.append_block().index;
            const LirOperand provided = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                   {LirOperand::local(flag_index++)},
                                                   lir_loc(param.type));
            m_function.set_terminator(m_current_block,
                                      LirTerminator::branch(provided,
                                                            ready_block,
                                                            default_block,
                                                            lir_loc(param.type)));

            m_current_block = default_block;
            const LirOperand default_value = lower_expr(*param.default_value);
            emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                        {LirOperand::local(param_index + m_parameter_offset), default_value},
                        lir_loc(param.type));
            m_function.set_terminator(m_current_block,
                                      LirTerminator::jump(ready_block, lir_loc(param.type)));
            m_current_block = ready_block;
        }
    }

    void lower_parameter_types(const std::vector<Parameter> &params)
    {
        for (std::size_t i = 0; i < params.size(); ++i)
        {
            const LirOperand value = emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                                                {LirOperand::local(i + m_parameter_offset)},
                                                lir_loc(params[i].type));
            const LirOperand checked = assert_type(value,
                                                   type_name(params[i].type),
                                                   lir_loc(params[i].type),
                                                   "le paramètre '" + params[i].name + "'");
            emit_effect(LirOpcode::IR_OP_DISCARD, {checked}, lir_loc(params[i].type));
        }
    }

    [[nodiscard]] LirOperand assert_type(const LirOperand value,
                                         const std::string &type_name,
                                         const LirSourceLocation source,
                                         const std::string &context)
    {
        // The operand names an annotation, not a bare type: the failure message
        // has to say what asked for the type, the way the tree walker does.
        const std::size_t type_index = m_module.add_type(type_name);
        const std::size_t annotation_index = m_module.add_annotation(type_index, context);
        return emit_value(LirOpcode::IR_OP_ASSERT_TYPE,
                          {value, LirOperand::annotation(annotation_index)},
                          source);
    }

    void begin_scope()
    {
        m_scope_stack.emplace_back();
        m_type_alias_scopes.push_back(m_type_aliases);
        m_nominal_type_value_scopes.push_back(m_nominal_type_values);
    }

    void leave_scope()
    {
        for (auto it = m_scope_stack.back().rbegin(); it != m_scope_stack.back().rend(); ++it)
        {
            auto binding = m_locals_by_name.find(*it);
            if (binding == m_locals_by_name.end())
            {
                continue;
            }

            binding->second.pop_back();
            if (binding->second.empty())
            {
                m_locals_by_name.erase(binding);
            }
        }
        m_scope_stack.pop_back();
        m_type_aliases = std::move(m_type_alias_scopes.back());
        m_type_alias_scopes.pop_back();
        m_nominal_type_values = std::move(m_nominal_type_value_scopes.back());
        m_nominal_type_value_scopes.pop_back();
    }

    void declare_binding(const std::string &name, const std::size_t local_index)
    {
        m_locals_by_name[name].push_back(local_index);
        m_scope_stack.back().push_back(name);
    }

    [[nodiscard]] bool has_binding_in_current_scope(const std::string &name) const
    {
        for (const std::string &current_name : m_scope_stack.back())
        {
            if (current_name == name)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<std::size_t> lookup_local(const std::string &name) const
    {
        const auto local = m_locals_by_name.find(name);
        if (local == m_locals_by_name.end() || local->second.empty())
        {
            return std::nullopt;
        }
        return local->second.back();
    }

    [[nodiscard]] std::optional<BindingSource> resolve_for_child(const std::string &name)
    {
        if (const auto local = lookup_local(name); local.has_value())
        {
            const auto type = m_local_types.find(*local);
            return BindingSource{LirOperand::local(*local),
                                 type == m_local_types.end() ? std::string{} : type->second,
                                 m_fixed_locals.contains(*local)};
        }
        if (const auto existing = m_captures_by_name.find(name); existing != m_captures_by_name.end())
        {
            return existing->second;
        }
        if (m_parent == nullptr)
        {
            return std::nullopt;
        }
        const auto source = m_parent->resolve_for_child(name);
        return source.has_value() ? std::optional<BindingSource>(add_capture(name, *source)) : std::nullopt;
    }

    [[nodiscard]] std::optional<BindingSource> resolve_capture(const std::string &name)
    {
        if (const auto existing = m_captures_by_name.find(name); existing != m_captures_by_name.end())
        {
            return existing->second;
        }
        if (m_parent == nullptr)
        {
            return std::nullopt;
        }
        const auto source = m_parent->resolve_for_child(name);
        return source.has_value() ? std::optional<BindingSource>(add_capture(name, *source)) : std::nullopt;
    }

    [[nodiscard]] BindingSource add_capture(const std::string &name, const BindingSource &source)
    {
        if (m_function.captures.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()))
        {
            throw VmCompileError("VM: trop de variables capturees dans '" + m_function.name + "'");
        }
        const std::size_t index = m_function.captures.size();
        m_function.captures.push_back({index, name, source.operand});
        BindingSource capture{LirOperand::capture(index), source.type, source.is_fixed};
        m_captures_by_name.emplace(name, capture);
        return capture;
    }

    [[nodiscard]] LirOperand lower_closure(const std::string &name,
                                           const std::vector<Parameter> &params,
                                           const TypeExpr &return_type,
                                           Stmt &body,
                                           const LirSourceLocation source)
    {
        const std::size_t function_index = m_module.functions.size();
        LirFunction &nested = m_module.append_function(name);
        nested.source_path = m_function.source_path;
        nested.source_text = m_function.source_text;
        nested.source_arity = params.size();
        for (std::size_t i = 0; i < params.size(); ++i)
        {
            nested.params.push_back({i, params[i].name});
            nested.optional_params.push_back(static_cast<bool>(params[i].default_value));
        }
        std::size_t flag_index = params.size();
        for (const Parameter &param : params)
        {
            if (param.default_value)
            {
                nested.params.push_back({flag_index++, "$provided_" + param.name});
            }
        }

        FunctionLowerer lowerer(m_module,
                                nested,
                                m_functions,
                                params,
                                return_type,
                                this,
                                m_global_types,
                                m_fixed_globals,
                                false,
                                0,
                                m_imports,
                                &m_type_aliases,
                                &m_nominal_type_values);
        lowerer.lower(body, params);

        std::vector<LirOperand> operands{LirOperand::function(function_index)};
        for (const LirCapture &capture : nested.captures)
        {
            operands.push_back(capture.source);
        }
        return emit_value(LirOpcode::IR_OP_CLOSURE, std::move(operands), source);
    }

    void emit_store_constant_bool(const std::size_t local_index, const bool value, const LirSourceLocation source)
    {
        const std::size_t constant_index = m_module.add_constant(Value::logique(value), value ? "vrai" : "faux");
        const LirOperand bool_value = emit_value(LirOpcode::IR_OP_CONSTANT,
                                                 {LirOperand::constant(constant_index)},
                                                 source);
        emit_effect(LirOpcode::IR_OP_STORE_LOCAL,
                    {LirOperand::local(local_index), bool_value},
                    source);
    }

    [[nodiscard]] LirOperand lower_short_circuit(BinaryExpr &expr)
    {
        const std::size_t result_local = allocate_hidden_local();
        const LirOperand left_value = lower_expr(*expr.left);

        const std::size_t rhs_eval_block = m_function.append_block().index;
        const std::size_t short_circuit_block = m_function.append_block().index;
        const std::size_t rhs_true_block = m_function.append_block().index;
        const std::size_t rhs_false_block = m_function.append_block().index;
        const std::size_t merge_block = m_function.append_block().index;

        if (expr.op.type == TokenType::ET)
        {
            m_function.set_terminator(m_current_block,
                                      LirTerminator::branch(left_value,
                                                            rhs_eval_block,
                                                            short_circuit_block,
                                                            lir_loc(expr.op)));
        }
        else
        {
            m_function.set_terminator(m_current_block,
                                      LirTerminator::branch(left_value,
                                                            short_circuit_block,
                                                            rhs_eval_block,
                                                            lir_loc(expr.op)));
        }

        m_current_block = short_circuit_block;
        emit_store_constant_bool(result_local, expr.op.type == TokenType::OU, lir_loc(expr.op));
        m_function.set_terminator(m_current_block, LirTerminator::jump(merge_block, lir_loc(expr.op)));

        m_current_block = rhs_eval_block;
        const LirOperand right_value = lower_expr(*expr.right);
        m_function.set_terminator(m_current_block,
                                  LirTerminator::branch(right_value,
                                                        rhs_true_block,
                                                        rhs_false_block,
                                                        lir_loc(expr.op)));

        m_current_block = rhs_true_block;
        emit_store_constant_bool(result_local, true, lir_loc(expr.op));
        m_function.set_terminator(m_current_block, LirTerminator::jump(merge_block, lir_loc(expr.op)));

        m_current_block = rhs_false_block;
        emit_store_constant_bool(result_local, false, lir_loc(expr.op));
        m_function.set_terminator(m_current_block, LirTerminator::jump(merge_block, lir_loc(expr.op)));

        m_current_block = merge_block;
        return emit_value(LirOpcode::IR_OP_LOAD_LOCAL,
                          {LirOperand::local(result_local)},
                          lir_loc(expr.op));
    }

    [[nodiscard]] LirOperand constant_nil(const Token &token)
    {
        const std::size_t constant_index = m_module.add_constant(Value::rien(), "rien");
        return emit_value(LirOpcode::IR_OP_CONSTANT,
                          {LirOperand::constant(constant_index)},
                          lir_loc(token));
    }

    // Emits a value-producing LIR instruction and returns the temp that now
    // names its result.
    [[nodiscard]] LirOperand emit_value(LirOpcode opcode,
                                        std::vector<LirOperand> operands,
                                        LirSourceLocation source)
    {
        const LirOperand destination = allocate_temp();
        m_function.append_instruction(
            m_current_block,
            LirInstruction::make(opcode, destination, std::move(operands), source));
        return destination;
    }

    // Emits an effect-only LIR instruction whose result is intentionally
    // discarded by the language semantics.
    void emit_effect(LirOpcode opcode,
                     std::vector<LirOperand> operands,
                     LirSourceLocation source)
    {
        m_function.append_instruction(
            m_current_block,
            LirInstruction::make(opcode, LirOperand::temp(0), std::move(operands), source));
    }

    [[nodiscard]] LirOperand lower_expr(Expr &expr)
    {
        expr.accept(*this);
        return m_last_value;
    }

    [[nodiscard]] std::string type_name(const TypeExpr &type) const
    {
        return runtime_type_name(type, m_type_aliases);
    }
};

} // namespace

LirModule AstToLir::lower(Program &program, const ResolvedVmImports &imports,
                        const TypeAliasTable &imported_types)
{
    LirModule module;
    module.name = program.source_path.empty() ? "__module__" : program.source_path;
    FunctionLowerer::FunctionTable functions;
    FunctionLowerer::GlobalTypeTable global_types;
    FunctionLowerer::NominalValueTable nominal_type_values;
    TypeAliasTable type_aliases = imported_types;
    for (const auto &statement : program.statements)
    {
        if (const auto *klass = dynamic_cast<const ClassDeclStmt *>(statement.get()))
        {
            Token identity = klass->name;
            identity.lexeme = nominal_type_identity(program.source_path, klass->name);
            type_aliases.insert_or_assign(klass->name.lexeme, TypeExpr::named(identity));
        }
        else if (const auto *interface = dynamic_cast<const InterfaceDeclStmt *>(statement.get()))
        {
            Token identity = interface->name;
            identity.lexeme = nominal_type_identity(program.source_path, interface->name);
            type_aliases.insert_or_assign(interface->name.lexeme, TypeExpr::named(identity));
        }
    }
    std::unordered_set<std::string> fixed_globals;
    struct FunctionWork
    {
        FunctionDeclStmt *declaration = nullptr;
        std::size_t function_index = 0;
        bool is_method = false;
    };
    std::vector<FunctionWork> functions_to_lower;
    std::vector<Stmt *> initializer_statements;

    for (auto &statement : program.statements)
    {
        if (auto *alias =
                dynamic_cast<TypeAliasDeclStmt *>(statement.get()))
        {
            type_aliases.insert_or_assign(alias->name.lexeme, alias->target);
        }
    }
    for (auto &statement : program.statements)
    {
        if (const auto *alias =
                dynamic_cast<const TypeAliasDeclStmt *>(statement.get()))
        {
            const TypeExpr resolved = resolve_type_aliases(alias->target, type_aliases);
            if (resolved.kind == TypeExprKind::NAMED &&
                resolved.name.find('@') != std::string::npos)
            {
                nominal_type_values.insert_or_assign(
                    alias->name.lexeme,
                    runtime_type_name(alias->target, type_aliases, false));
            }
            type_aliases.insert_or_assign(
                alias->name.lexeme,
                resolved);
        }
    }

    for (auto &statement : program.statements)
    {
        if (dynamic_cast<TypeAliasDeclStmt *>(statement.get()) != nullptr)
        {
            continue;
        }
        if (dynamic_cast<ImportStmt *>(statement.get()) != nullptr)
        {
            continue;
        }
        auto *function = dynamic_cast<FunctionDeclStmt *>(statement.get());
        if (function != nullptr)
        {
            if (function->params.size() > 255)
            {
                throw VmCompileError("VM: trop de paramètres dans '" + function->name.lexeme + "'");
            }
            if (function->body == nullptr)
            {
                throw VmCompileError("VM: la fonction '" + function->name.lexeme + "' n'a pas de corps");
            }
            if (!functions.emplace(function->name.lexeme, function).second)
            {
                throw VmCompileError("VM: fonction globale dupliquée '" + function->name.lexeme + "'");
            }
            const std::size_t function_index = module.functions.size();
            functions_to_lower.push_back({function, function_index, false});
            fixed_globals.insert(function->name.lexeme);
            static_cast<void>(module.add_global(function->name.lexeme));
            static_cast<void>(module.append_function(function->name.lexeme));
            module.functions.back().source_path = program.source_path;
            module.functions.back().source_text = program.source_text;
            continue;
        }

        if (auto *interface = dynamic_cast<InterfaceDeclStmt *>(statement.get()))
        {
            LirInterfaceDescriptor descriptor;
            descriptor.name = interface->name.lexeme;
            descriptor.type_identity = nominal_type_identity(program.source_path, interface->name);
            for (auto &member : interface->methods)
            {
                auto *method = dynamic_cast<FunctionDeclStmt *>(member.get());
                if (method == nullptr)
                {
                    continue;
                }
                LirInterfaceMethodDescriptor signature;
                signature.name = method->name.lexeme;
                signature.return_type =
                    runtime_type_name(method->return_type, type_aliases);
                for (const Parameter &parameter : method->params)
                {
                    signature.parameter_types.push_back(
                        runtime_type_name(parameter.type, type_aliases));
                }
                descriptor.methods.push_back(std::move(signature));
            }
            module.interfaces.push_back(std::move(descriptor));
            fixed_globals.insert(interface->name.lexeme);
            static_cast<void>(module.add_global(interface->name.lexeme));
            initializer_statements.push_back(interface);
            continue;
        }

        if (auto *klass = dynamic_cast<ClassDeclStmt *>(statement.get()))
        {
            LirClassDescriptor descriptor;
            descriptor.name = klass->name.lexeme;
            descriptor.type_identity = nominal_type_identity(program.source_path, klass->name);
            descriptor.parent = runtime_type_name(klass->parent, type_aliases);
            for (const TypeExpr &interface : klass->interfaces)
            {
                descriptor.interfaces.push_back(
                    runtime_type_name(interface, type_aliases));
            }
            for (auto &member : klass->members)
            {
                if (auto *field = dynamic_cast<VarDeclStmt *>(member.get()))
                {
                    descriptor.fields.push_back({field->name.lexeme,
                                                 runtime_type_name(field->type,
                                                                   type_aliases),
                                                 field->is_prive,
                                                 field->is_fixe});
                }
                else if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
                {
                    const std::size_t method_index = module.functions.size();
                    LirMethodDescriptor method_descriptor;
                    method_descriptor.name = method->name.lexeme;
                    method_descriptor.function_index = method_index;
                    method_descriptor.return_type =
                        runtime_type_name(method->return_type, type_aliases);
                    method_descriptor.is_private = method->is_prive;
                    method_descriptor.is_override = method->is_remplace;
                    for (const Parameter &parameter : method->params)
                    {
                        method_descriptor.parameter_types.push_back(
                            runtime_type_name(parameter.type, type_aliases));
                    }
                    descriptor.methods.push_back(std::move(method_descriptor));
                    functions_to_lower.push_back({method, method_index, true});
                    static_cast<void>(module.append_function(klass->name.lexeme + "." + method->name.lexeme));
                    module.functions.back().source_path = program.source_path;
                    module.functions.back().source_text = program.source_text;
                }
            }
            module.classes.push_back(std::move(descriptor));
            fixed_globals.insert(klass->name.lexeme);
            static_cast<void>(module.add_global(klass->name.lexeme));
            initializer_statements.push_back(klass);
            continue;
        }

        if (auto *variable = dynamic_cast<VarDeclStmt *>(statement.get()))
        {
            if (!global_types.emplace(
                    variable->name.lexeme,
                    runtime_type_name(variable->type, type_aliases)).second)
            {
                throw VmCompileError("VM: variable globale dupliquée '" + variable->name.lexeme + "'");
            }
            if (variable->is_fixe)
            {
                fixed_globals.insert(variable->name.lexeme);
            }
            static_cast<void>(module.add_global(variable->name.lexeme));
            initializer_statements.push_back(variable);
            continue;
        }

        initializer_statements.push_back(statement.get());
    }

    for (const FunctionWork &work : functions_to_lower)
    {
        FunctionDeclStmt *function = work.declaration;
        LirFunction &lir_function = module.functions[work.function_index];
        const std::size_t parameter_offset = work.is_method ? 1 : 0;
        lir_function.source_arity = function->params.size();
        if (work.is_method)
        {
            lir_function.params.push_back({0, "ici"});
        }
        for (std::size_t param_index = 0; param_index < function->params.size(); ++param_index)
        {
            lir_function.params.push_back({param_index + parameter_offset, function->params[param_index].name});
            lir_function.optional_params.push_back(static_cast<bool>(function->params[param_index].default_value));
        }
        std::size_t flag_index = function->params.size() + parameter_offset;
        for (const Parameter &param : function->params)
        {
            if (param.default_value)
            {
                lir_function.params.push_back({flag_index++, "$provided_" + param.name});
            }
        }

        FunctionLowerer lowerer(module,
                                lir_function,
                                functions,
                                function->params,
                                function->return_type,
                                nullptr,
                                &global_types,
                                &fixed_globals,
                                false,
                                parameter_offset,
                                &imports,
                                &type_aliases,
                                &nominal_type_values);
        lowerer.lower(*function->body, function->params);
    }

    if (!initializer_statements.empty())
    {
        LirFunction &initializer = module.append_function("__module_init__");
        initializer.source_path = program.source_path;
        initializer.source_text = program.source_text;
        FunctionLowerer lowerer(module,
                                initializer,
                                functions,
                                {},
                                TypeExpr{},
                                nullptr,
                                &global_types,
                                &fixed_globals,
                                true,
                                0,
                                &imports,
                                &type_aliases,
                                &nominal_type_values);
        lowerer.lower_module_statements(initializer_statements);
        module.initializer_functions.push_back(module.functions.size() - 1);
    }

    return module;
}

} // namespace lumiere
