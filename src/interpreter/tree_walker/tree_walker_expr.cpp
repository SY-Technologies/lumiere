#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/parser/utf8.hpp"
#include "lumiere/interpreter/runtime/numeric.hpp"

#include <functional>
#include <iostream>
#include <optional>
#include <string>

namespace lumiere
{

namespace
{

Value checked_integer(TreeWalker &runtime, const Token &site,
                      std::optional<int64_t> value)
{
    if (!value)
        runtime.raise_runtime_error(site, "valeur hors limites pour Entier");
    return Value::entier(*value);
}

std::string describe_binary_operand_types(const Value &left, const Value &right)
{
    return "types recus: " + left.type_name() + " et " + right.type_name();
}

std::string describe_expected_binary_types(const std::string &operation,
                                           const Value &left,
                                           const Value &right,
                                           const std::string &expected_types)
{
    return operation + " attend " + expected_types + "; " + describe_binary_operand_types(left, right);
}

std::string describe_expected_unary_type(const std::string &operation,
                                         const Value &operand,
                                         const std::string &expected_type)
{
    return operation + " attend une valeur de type " + expected_type +
           "; type recu: " + operand.type_name();
}

} // namespace

void TreeWalker::visit(LiteralExpr &expr)
{
    switch (expr.token.type)
    {
    case TokenType::ENTIER_LIT:
        // The lexer already refused a literal that does not fit.
        m_result = Value::entier(numeric::parse_integer_literal(expr.token.lexeme).value_or(0));
        return;
    case TokenType::DECIMAL_LIT:
        m_result = Value::decimal(std::stod(numeric::without_digit_separators(expr.token.lexeme)));
        return;
    case TokenType::TEXTE_LIT:
        m_result = Value::texte(expr.token.lexeme.substr(1, expr.token.lexeme.size() - 2));
        return;
    case TokenType::SYMBOLE_LIT:
    {
        const std::optional<char32_t> symbol_char =
            utf8::decode_single_character(std::string_view(expr.token.lexeme).substr(1, expr.token.lexeme.size() - 2));
        if (!symbol_char.has_value())
        {
            throw_runtime_error(expr.token, "symbole invalide");
        }
        m_result = Value::symbole(*symbol_char);
        return;
    }
    case TokenType::VRAI:
        m_result = Value::logique(true);
        return;
    case TokenType::FAUX:
        m_result = Value::logique(false);
        return;
    case TokenType::RIEN:
        m_result = Value::rien();
        return;
    default:
        throw_runtime_error(expr.token, "litteral non pris en charge");
    }
}

void TreeWalker::visit(IdentifierExpr &expr)
{
    if (expr.name.type == TokenType::ICI)
    {
        m_result = m_self;
        return;
    }
    if (expr.name.type == TokenType::PARENT)
    {
        throw_runtime_error(expr.name, "parent doit etre utilise avec un acces membre");
    }

    if (m_env == nullptr)
    {
        throw_runtime_error(expr.name, "environnement d'execution absent");
    }

    try
    {
        m_result = m_env->get(expr.name.lexeme);
    }
    catch (const RuntimeError &error)
    {
        throw_runtime_error(expr.name, error.raw_message());
    }
}

void TreeWalker::visit(BinaryExpr &expr)
{
    if (expr.op.type == TokenType::EGAL)
    {
        evaluate_assignment(expr);
        return;
    }

    const Value left = evaluate(*expr.left);

    if (expr.op.type == TokenType::ET)
    {
        if (!left.is_logique())
        {
            throw_runtime_error(expr.op,
                                "l'operateur 'et' attend une operande gauche de type Logique; type recu: " +
                                    left.type_name());
        }

        if (!left.as_logique())
        {
            m_result = Value::logique(false);
            return;
        }

        const Value right = evaluate(*expr.right);
        if (!right.is_logique())
        {
            throw_runtime_error(
                expr.op,
                describe_expected_binary_types("l'operateur 'et'", left, right, "deux operandes de type Logique"));
        }

        m_result = Value::logique(right.as_logique());
        return;
    }

    if (expr.op.type == TokenType::OU)
    {
        if (!left.is_logique())
        {
            throw_runtime_error(expr.op,
                                "l'operateur 'ou' attend une operande gauche de type Logique; type recu: " +
                                    left.type_name());
        }

        if (left.as_logique())
        {
            m_result = Value::logique(true);
            return;
        }

        const Value right = evaluate(*expr.right);
        if (!right.is_logique())
        {
            throw_runtime_error(
                expr.op,
                describe_expected_binary_types("l'operateur 'ou'", left, right, "deux operandes de type Logique"));
        }

        m_result = Value::logique(right.as_logique());
        return;
    }

    const Value right = evaluate(*expr.right);

    switch (expr.op.type)
    {
    case TokenType::PLUS:
        if (left.is_entier() && right.is_entier())
        {
            m_result = checked_integer(*this, expr.op, numeric::add(left.as_entier(), right.as_entier()));
            return;
        }
        if (left.is_numeric() && right.is_numeric())
        {
            m_result = Value::decimal(assert_decimal(left, expr.op) + assert_decimal(right, expr.op));
            return;
        }
        if (left.is_texte() || right.is_texte())
        {
            m_result = Value::texte(to_texte(left) + to_texte(right));
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("l'addition", left, right, "deux valeurs numeriques ou au moins un Texte"));
    case TokenType::MOINS:
        if (left.is_entier() && right.is_entier())
        {
            m_result = checked_integer(*this, expr.op, numeric::subtract(left.as_entier(), right.as_entier()));
            return;
        }
        if (left.is_numeric() && right.is_numeric())
        {
            m_result = Value::decimal(assert_decimal(left, expr.op) - assert_decimal(right, expr.op));
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("la soustraction", left, right, "deux valeurs numeriques"));
    case TokenType::ETOILE:
        if (left.is_entier() && right.is_entier())
        {
            m_result = checked_integer(*this, expr.op, numeric::multiply(left.as_entier(), right.as_entier()));
            return;
        }
        if (left.is_numeric() && right.is_numeric())
        {
            m_result = Value::decimal(assert_decimal(left, expr.op) * assert_decimal(right, expr.op));
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("la multiplication", left, right, "deux valeurs numeriques"));
    case TokenType::SLASH:
        if (left.is_entier() && right.is_entier())
        {
            if (right.as_entier() == 0)
            {
                throw_runtime_error(expr.op, "division par zéro");
            }
            m_result = checked_integer(*this, expr.op, numeric::divide(left.as_entier(), right.as_entier()));
            return;
        }
        if (left.is_numeric() && right.is_numeric())
        {
            const double divisor = assert_decimal(right, expr.op);
            if (divisor == 0.0)
            {
                throw_runtime_error(expr.op, "division par zero interdite");
            }
            m_result = Value::decimal(assert_decimal(left, expr.op) / divisor);
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("la division", left, right, "deux valeurs numeriques"));
    case TokenType::MODULO:
        if (left.is_entier() && right.is_entier())
        {
            if (right.as_entier() == 0)
            {
                throw_runtime_error(expr.op, "modulo par zéro");
            }
            m_result = checked_integer(*this, expr.op, numeric::remainder(left.as_entier(), right.as_entier()));
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("le modulo", left, right, "deux valeurs de type Entier"));
    case TokenType::EGAL_EGAL:
        m_result = Value::logique(is_equal(left, right));
        return;
    case TokenType::BANG_EGAL:
        m_result = Value::logique(!is_equal(left, right));
        return;
    case TokenType::INFERIEUR:
        if (left.is_entier() && right.is_entier())
        {
            m_result = Value::logique(left.as_entier() < right.as_entier());
            return;
        }
        if (left.is_numeric() && right.is_numeric())
        {
            m_result = Value::logique(assert_decimal(left, expr.op) < assert_decimal(right, expr.op));
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("la comparaison '<'", left, right, "deux valeurs numeriques"));
    case TokenType::INFERIEUR_EGAL:
        if (left.is_entier() && right.is_entier())
        {
            m_result = Value::logique(left.as_entier() <= right.as_entier());
            return;
        }
        if (left.is_numeric() && right.is_numeric())
        {
            m_result = Value::logique(assert_decimal(left, expr.op) <= assert_decimal(right, expr.op));
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("la comparaison '<='", left, right, "deux valeurs numeriques"));
    case TokenType::SUPERIEUR:
        if (left.is_entier() && right.is_entier())
        {
            m_result = Value::logique(left.as_entier() > right.as_entier());
            return;
        }
        if (left.is_numeric() && right.is_numeric())
        {
            m_result = Value::logique(assert_decimal(left, expr.op) > assert_decimal(right, expr.op));
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("la comparaison '>'", left, right, "deux valeurs numeriques"));
    case TokenType::SUPERIEUR_EGAL:
        if (left.is_entier() && right.is_entier())
        {
            m_result = Value::logique(left.as_entier() >= right.as_entier());
            return;
        }
        if (left.is_numeric() && right.is_numeric())
        {
            m_result = Value::logique(assert_decimal(left, expr.op) >= assert_decimal(right, expr.op));
            return;
        }
        throw_runtime_error(
            expr.op,
            describe_expected_binary_types("la comparaison '>='", left, right, "deux valeurs numeriques"));
    default:
        throw_runtime_error(expr.op, "operateur binaire non pris en charge");
    }
}

void TreeWalker::evaluate_assignment(BinaryExpr &expr)
{
    if (auto *identifier = dynamic_cast<IdentifierExpr *>(expr.left.get()))
    {
        assign_identifier(*identifier, *expr.right);
        return;
    }

    if (auto *member = dynamic_cast<MemberAccessExpr *>(expr.left.get()))
    {
        assign_member(*member, *expr.right);
        return;
    }

    if (auto *index = dynamic_cast<IndexAccessExpr *>(expr.left.get()))
    {
        assign_index(*index, *expr.right);
        return;
    }

    throw_runtime_error(expr.op, "cible d'affectation invalide: une variable, un champ ou un acces par indice est attendu");
}

void TreeWalker::assign_identifier(IdentifierExpr &target, Expr &value_expr)
{
    if (m_env == nullptr)
    {
        throw_runtime_error(target.name, "environnement d'execution absent");
    }

    Value value = evaluate(value_expr);
    try
    {
        const std::string declared_type = m_env->declared_type_of(target.name.lexeme);
        if (!declared_type.empty())
        {
            ensure_value_matches_annotation(
                value,
                Token(TokenType::IDENT, declared_type, target.name.line, target.name.column),
                target.name,
                "la variable '" + target.name.lexeme + "'");
        }
    }
    catch (const RuntimeError &error)
    {
        throw_runtime_error(target.name, error.raw_message());
    }

    try
    {
        m_env->assign(target.name.lexeme, value);
    }
    catch (const RuntimeError &error)
    {
        throw_runtime_error(target.name, error.raw_message());
    }

    m_result = std::move(value);
}

void TreeWalker::assign_member(MemberAccessExpr &target, Expr &value_expr)
{
    const auto *object_identifier = dynamic_cast<IdentifierExpr *>(target.object.get());
    const bool uses_super = object_identifier != nullptr && object_identifier->name.type == TokenType::PARENT;
    const Value object = uses_super ? m_self : evaluate(*target.object);
    if (!object.is_objet())
    {
        throw_runtime_error(target.member, "affectation de champ impossible: la cible avant '.' doit être un Objet");
    }

    auto instance = object.as_objet();
    std::shared_ptr<LumiereClass> lookup_class = instance->klass;
    if (uses_super)
    {
        if (lookup_class == nullptr || (lookup_class = parent_class(lookup_class)) == nullptr)
        {
            throw_runtime_error(target.member, "'parent' ne peut etre utilise ici: aucune classe parente n'est disponible");
        }
    }

    std::shared_ptr<LumiereClass> declaring_class;
    VarDeclStmt *field_decl = lookup_class ? find_field_decl(lookup_class, target.member.lexeme, &declaring_class) : nullptr;
    if (field_decl == nullptr)
    {
        throw_runtime_error(target.member, "champ introuvable: '" + target.member.lexeme + "'");
    }
    if (field_decl->is_prive && !access_uses_ici(*target.object))
    {
        throw_runtime_error(target.member, "acces interdit au champ prive '" + target.member.lexeme + "'");
    }

    Value value = evaluate(value_expr);
    ensure_value_matches_annotation(
        value,
        class_annotation(declaring_class, field_decl->type),
        target.member,
        "le champ '" + target.member.lexeme + "'");
    instance->fields[target.member.lexeme] = value;
    m_result = std::move(value);
}

void TreeWalker::assign_index(IndexAccessExpr &target, Expr &value_expr)
{
    const Value object = evaluate(*target.object);
    const Value key = evaluate(*target.index);
    Value value = evaluate(value_expr);

    if (object.is_liste_fixe())
    {
        throw_runtime_error(target.bracket,
                            "une ListeFixe est immuable : ses elements ne peuvent pas etre remplaces");
    }

    if (!supports_mutable_index_assignment(object))
    {
        throw_runtime_error(target.bracket,
                            "affectation par indice impossible: la cible doit etre une Liste ou un Dictionnaire");
    }

    if (object.is_liste())
    {
        const int64_t position = assert_entier(key, target.bracket);
        auto list = object.as_liste();
        if (position < 0 || static_cast<std::size_t>(position) >= list->elements.size())
        {
            throw_runtime_error(target.bracket, "indice hors limites");
        }
        enforce_list_element_constraint(list, value, target.bracket, "l'element de liste");
        list->elements[static_cast<std::size_t>(position)] = value;
        m_result = std::move(value);
        return;
    }

    if (object.is_dictionnaire())
    {
        auto dict = object.as_dictionnaire();
        enforce_dict_entry_constraint(dict, key, value, target.bracket, "l'entree du dictionnaire");
        require_dictionary_key(key, target.bracket);
        dict->set(key, value);
        m_result = std::move(value);
        return;
    }

    throw_runtime_error(target.bracket,
                        "affectation par indice impossible: la cible doit etre une Liste ou un Dictionnaire");
}

void TreeWalker::visit(DictionaryExpr &expr)
{
    auto data = std::make_shared<DictData>();
    data->reserve(expr.entries.size());

    for (auto &entry : expr.entries)
    {
        Value key = evaluate(*entry.key);
        Value value = evaluate(*entry.value);
        require_dictionary_key(key, expr.brace);
        data->set(std::move(key), std::move(value));
    }

    m_result = Value::dictionnaire(std::move(data));
}

void TreeWalker::visit(UnaryExpr &expr)
{
    const Value operand = evaluate(*expr.operand);

    switch (expr.op.type)
    {
    case TokenType::MOINS:
        if (operand.is_entier())
        {
            m_result = checked_integer(*this, expr.op, numeric::negate(operand.as_entier()));
            return;
        }
        if (operand.is_numeric())
        {
            m_result = Value::decimal(-assert_decimal(operand, expr.op));
            return;
        }
        throw_runtime_error(expr.op, describe_expected_unary_type("l'operateur unaire '-'", operand, "numerique"));
    case TokenType::NON:
        m_result = Value::logique(!is_truthy(operand));
        return;
    default:
        break;
    }

    throw_runtime_error(expr.op, "operateur unaire non pris en charge");
}

void TreeWalker::visit(CastExpr &expr)
{
    const Value operand = evaluate(*expr.operand);
    const std::string target = expr.target_type.to_string();

    if (target == "Entier")
    {
        if (operand.is_entier())
        {
            m_result = operand;
            return;
        }
        if (operand.is_decimal())
        {
            m_result = checked_integer(*this, expr.target_type.source, numeric::to_integer(operand.as_decimal()));
            return;
        }
        if (operand.is_symbole())
        {
            m_result = Value::entier(static_cast<int64_t>(operand.as_symbole()));
            return;
        }
        if (operand.is_texte())
        {
            try
            {
                std::size_t consumed = 0;
                const auto value = std::stoll(operand.as_texte(), &consumed);
                if (consumed != operand.as_texte().size())
                    throw std::invalid_argument("caractères restants");
                m_result = Value::entier(value);
                return;
            }
            catch (...)
            {
                throw_runtime_error(expr.target_type.source, "conversion vers Entier impossible pour une valeur de type Texte");
            }
        }
    }

    if (target == "Décimal" || target == "Decimal")
    {
        if (operand.is_decimal())
        {
            m_result = operand;
            return;
        }
        if (operand.is_entier())
        {
            m_result = Value::decimal(static_cast<double>(operand.as_entier()));
            return;
        }
        if (operand.is_texte())
        {
            if (const auto value = numeric::parse_decimal(operand.as_texte()))
            {
                m_result = Value::decimal(*value);
                return;
            }
            throw_runtime_error(expr.target_type.source, "conversion vers Décimal impossible pour une valeur de type Texte");
        }
    }

    if (target == "Logique")
    {
        if (operand.is_logique())
        {
            m_result = operand;
            return;
        }
        if (operand.is_texte())
        {
            if (operand.as_texte() == "vrai")
            {
                m_result = Value::logique(true);
                return;
            }
            if (operand.as_texte() == "faux")
            {
                m_result = Value::logique(false);
                return;
            }
            throw_runtime_error(expr.target_type.source, "conversion vers Logique impossible: le texte doit valoir 'vrai' ou 'faux'");
        }
    }

    if (target == "Symbole")
    {
        if (operand.is_symbole())
        {
            m_result = operand;
            return;
        }
        if (operand.is_entier())
        {
            const int64_t unicode_value = operand.as_entier();
            if (unicode_value < 0 || unicode_value > 0x10FFFF ||
                (unicode_value >= 0xD800 && unicode_value <= 0xDFFF))
            {
                throw_runtime_error(expr.target_type.source, "conversion vers Symbole impossible: le point de code Unicode est invalide");
            }
            m_result = Value::symbole(static_cast<char32_t>(unicode_value));
            return;
        }
        if (operand.is_texte())
        {
            const std::optional<char32_t> symbol_char = utf8::decode_single_character(operand.as_texte());
            if (!symbol_char.has_value())
            {
                throw_runtime_error(expr.target_type.source, "conversion vers Symbole impossible: le texte doit contenir exactement un caractere");
            }
            m_result = Value::symbole(*symbol_char);
            return;
        }
    }

    if (target == "Texte")
    {
        m_result = Value::texte(to_texte(operand));
        return;
    }

    if (target == "Universel")
    {
        m_result = operand;
        return;
    }

    throw_runtime_error(expr.target_type.source, "conversion explicite non prise en charge vers le type '" + target + "'");
}

void TreeWalker::visit(TypeCheckExpr &expr)
{
    m_result = Value::logique(matches_type_name(evaluate(*expr.operand), expr.type));
}

void TreeWalker::visit(FunctionExpr &expr)
{
    m_result = Value::fonction(make_declared_function(expr, m_self, m_env_owner));
}

void TreeWalker::visit(CallExpr &expr)
{
    if (auto *identifier = dynamic_cast<IdentifierExpr *>(expr.callee.get()))
    {
        const std::string &builtin_name = identifier->name.lexeme;
        if ((builtin_name == "afficher" ||
            builtin_name == "lire" ||
            builtin_name == "lire_entier" ||
            builtin_name == "lire_décimal" ||
            builtin_name == "lire_decimal" ||
            builtin_name == "lire_logique" ||
            builtin_name == "type_de" ||
            builtin_name == "Succès" ||
            builtin_name == "Échec") &&
            (m_env == nullptr || !m_env->contains(builtin_name)))
        {
            m_result = call_builtin(builtin_name, expr.args, expr.paren);
            return;
        }
    }

    if (auto *member = dynamic_cast<MemberAccessExpr *>(expr.callee.get()))
    {
        const auto *object_identifier = dynamic_cast<IdentifierExpr *>(member->object.get());
        if (object_identifier != nullptr &&
            object_identifier->name.lexeme == "ListeFixe" &&
            member->member.lexeme == "remplir")
        {
            if (expr.args.size() != 3)
            {
                throw_runtime_error(expr.paren, "ListeFixe.remplir requiert exactement 3 argument(s)");
            }
            for (const auto &arg : expr.args)
            {
                if (!arg.name.empty())
                {
                    throw_runtime_error(expr.paren, "ListeFixe.remplir n'accepte pas d'arguments nommes");
                }
            }

            const auto *type_expr = dynamic_cast<IdentifierExpr *>(expr.args[0].value.get());
            if (type_expr == nullptr)
            {
                throw_runtime_error(expr.paren, "ListeFixe.remplir attend un nom de type comme premier argument");
            }

            const std::string element_type = type_expr->name.lexeme;
            const Value length_value = evaluate(*expr.args[1].value);
            const int64_t length = assert_entier(length_value, expr.paren);
            if (length < 0)
            {
                throw_runtime_error(expr.paren, "la taille d'une ListeFixe ne peut pas etre negative");
            }

            const Value fill_value = evaluate(*expr.args[2].value);
            ensure_value_matches_annotation(
                fill_value,
                Token(TokenType::IDENT, element_type, type_expr->name.line, type_expr->name.column),
                expr.paren,
                "ListeFixe.remplir");

            auto data = std::make_shared<ListeFixeData>();
            data->elements.assign(static_cast<std::size_t>(length), fill_value);
            m_result = Value::liste_fixe(std::move(data));
            register_value_annotation(
                m_result,
                Token(TokenType::IDENT,
                      "ListeFixe[" + element_type + ", " + std::to_string(length) + "]",
                      expr.paren.line,
                      expr.paren.column));
            return;
        }
    }

    const Value callee = evaluate(*expr.callee);
    if (callee.is_classe())
    {
        m_result = instantiate_class(callee.as_classe(), expr.args, expr.paren);
        return;
    }
    if (!callee.is_fonction())
    {
        throw_runtime_error(expr.paren, "la valeur appelee n'est pas une fonction");
    }

    m_result = call_function(callee.as_fonction(), expr.args, expr.paren);
}

void TreeWalker::visit(ListExpr &expr)
{
    auto data = std::make_shared<ListeData>();
    data->elements.reserve(expr.elements.size());

    for (auto &element : expr.elements)
    {
        data->elements.push_back(evaluate(*element));
    }

    m_result = Value::liste(std::move(data));
}

void TreeWalker::visit(MemberAccessExpr &expr)
{
    const auto *object_identifier = dynamic_cast<IdentifierExpr *>(expr.object.get());
    const bool uses_super = object_identifier != nullptr && object_identifier->name.type == TokenType::PARENT;
    const Value object = uses_super ? m_self : evaluate(*expr.object);
    if (!object.is_objet())
    {
        const Value native_member = resolve_native_member(object, expr.member);
        if (!native_member.is_rien())
        {
            m_result = native_member;
            return;
        }

        throw_runtime_error(expr.member, "acces membre impossible: la cible avant '.' doit etre un Objet");
    }

    auto instance = object.as_objet();
    std::shared_ptr<LumiereClass> lookup_class = instance->klass;
    if (uses_super)
    {
        if (lookup_class == nullptr || (lookup_class = parent_class(lookup_class)) == nullptr)
        {
            throw_runtime_error(expr.member, "'parent' ne peut etre utilise ici: aucune classe parente n'est disponible");
        }
    }

    VarDeclStmt *field_decl = lookup_class ? find_field_decl(lookup_class, expr.member.lexeme) : nullptr;
    if (lookup_class == nullptr)
    {
        auto field_it = instance->fields.find(expr.member.lexeme);
        if (field_it != instance->fields.end())
        {
            m_result = field_it->second;
            return;
        }
    }

    if (field_decl != nullptr)
    {
        if (field_decl->is_prive && !access_uses_ici(*expr.object))
        {
            throw_runtime_error(expr.member, "acces interdit au champ prive '" + expr.member.lexeme + "'");
        }

        auto field_it = instance->fields.find(expr.member.lexeme);
        if (field_it == instance->fields.end())
        {
            throw_runtime_error(expr.member, "champ introuvable: '" + expr.member.lexeme + "'");
        }

        m_result = field_it->second;
        return;
    }

    if (lookup_class != nullptr)
    {
        std::shared_ptr<LumiereClass> declaring_class;
        if (FunctionDeclStmt *function_decl = find_method_decl(lookup_class, expr.member.lexeme, &declaring_class))
        {
            if (function_decl->is_prive && !access_uses_ici(*expr.object))
            {
                throw_runtime_error(expr.member, "acces interdit a la methode privee '" + expr.member.lexeme + "'");
            }

            m_result = Value::fonction(make_declared_function(
                *function_decl,
                object,
                class_closure_owner(declaring_class),
                class_source_identity(declaring_class)));
            return;
        }

        auto field_it = instance->fields.find(expr.member.lexeme);
        if (field_it != instance->fields.end())
        {
            m_result = field_it->second;
            return;
        }
    }

    throw_runtime_error(expr.member, "membre introuvable: '" + expr.member.lexeme + "'");
}

void TreeWalker::visit(IndexAccessExpr &expr)
{
    const Value object = evaluate(*expr.object);
    const Value index = evaluate(*expr.index);

    if (!supports_index_read(object))
    {
        throw_runtime_error(expr.bracket, "acces par indice impossible pour une valeur de type " + object.type_name());
    }

    if (object.is_liste())
    {
        const int64_t position = assert_entier(index, expr.bracket);
        auto list = object.as_liste();

        if (position < 0 || static_cast<std::size_t>(position) >= list->elements.size())
        {
            throw_runtime_error(expr.bracket, "indice hors limites");
        }

        m_result = list->elements[static_cast<std::size_t>(position)];
        return;
    }

    if (object.is_liste_fixe())
    {
        const int64_t position = assert_entier(index, expr.bracket);
        auto list = object.as_liste_fixe();

        if (position < 0 || static_cast<std::size_t>(position) >= list->elements.size())
        {
            throw_runtime_error(expr.bracket, "indice hors limites");
        }

        m_result = list->elements[static_cast<std::size_t>(position)];
        return;
    }

    if (object.is_dictionnaire())
    {
        auto dict = object.as_dictionnaire();

        if (const DictEntry *entry = dict->find(index))
        {
            m_result = entry->second;
            return;
        }

        throw_runtime_error(expr.bracket, "cle introuvable dans le Dictionnaire");
    }

    if (object.is_texte())
    {
        const int64_t position = assert_entier(index, expr.bracket);
        const std::string &text = object.as_texte();
        const std::optional<std::size_t> length = utf8::character_count(text);

        if (!length.has_value())
        {
            throw_runtime_error(expr.bracket, "texte UTF-8 invalide");
        }

        if (position < 0 || static_cast<std::size_t>(position) >= *length)
        {
            throw_runtime_error(expr.bracket, "indice hors limites");
        }

        const std::optional<char32_t> symbol_char = utf8::character_at(text, static_cast<std::size_t>(position));
        if (!symbol_char.has_value())
        {
            throw_runtime_error(expr.bracket, "texte UTF-8 invalide");
        }

        m_result = Value::symbole(*symbol_char);
        return;
    }

    throw_runtime_error(expr.bracket, "acces par indice impossible pour une valeur de type " + object.type_name());
}

void TreeWalker::visit(PropagationExpr &expr)
{
    const Value value = evaluate(*expr.operand);
    if (!value.is_resultat())
    {
        throw_runtime_error(expr.keyword,
                            "propager exige une valeur Résultat");
    }
    auto result = value.as_resultat();
    if (result->success)
    {
        m_result = result->payload;
    }
    else
    {
        throw PropagateSignal{value};
    }
}

Value TreeWalker::call_function(const std::shared_ptr<LumiereFunction> &function,
                                const std::vector<Argument> &args,
                                const Token &call_site)
{
    if (function == nullptr)
    {
        throw_runtime_error(call_site, "fonction invalide");
    }

    if (function->is_native())
    {
        const std::vector<RuntimeArgument> runtime_args = evaluate_runtime_arguments(args);
        m_result = function->native_handler(
            *this,
            NativeArgs{function->is_method() ? &function->receiver : nullptr,
                       &runtime_args,
                       RuntimeSite{m_current_source_path, static_cast<int>(call_site.line), static_cast<int>(call_site.column)}});
        return m_result;
    }

    const std::vector<RuntimeArgument> runtime_args = evaluate_runtime_arguments(args);
    return call_user_function(
        function,
        runtime_args,
        RuntimeSite{m_current_source_path, static_cast<int>(call_site.line), static_cast<int>(call_site.column)});
}

Value TreeWalker::call_user_function(const std::shared_ptr<LumiereFunction> &function,
                                     const std::vector<RuntimeArgument> &args,
                                     const RuntimeSite &call_site)
{
    if (function == nullptr)
    {
        raise_runtime_error(call_site, "fonction invalide");
    }

    FunctionDeclStmt *decl_ptr = function_decl(*function);
    FunctionExpr *expr_ptr = function_expr(*function);
    if (decl_ptr == nullptr && expr_ptr == nullptr)
    {
        raise_runtime_error(call_site, "fonction invalide");
    }

    const std::vector<Parameter> &params = decl_ptr != nullptr ? decl_ptr->params : expr_ptr->params;
    TypeExpr return_type = decl_ptr != nullptr ? decl_ptr->return_type : expr_ptr->return_type;
    // Return checks run after restoring the caller's value environment. Resolve
    // their annotation in the function's lexical environment before entering it.
    try
    {
        if (const auto closure = function_closure_owner(*function))
            return_type = closure->resolve_type_aliases(return_type);
    }
    catch (const std::invalid_argument &error)
    {
        raise_runtime_error(call_site, error.what());
    }
    Stmt *body = decl_ptr != nullptr ? decl_ptr->body.get() : expr_ptr->body.get();
    const std::string function_name = decl_ptr != nullptr ? decl_ptr->name.lexeme : "<anonyme>";

    const Token site_token(TokenType::IDENT,
                           function_name,
                           static_cast<uint32_t>(call_site.line),
                           static_cast<uint32_t>(call_site.column));

    StackFrameGuard frame(*this, make_stack_frame(function_name, m_current_source_path, site_token));

    // When a failing Résultat escapes this function (via propagation or an
    // explicit return), record this function's frame on the result so that an
    // unhandled failure produces a meaningful stack traceback.
    const auto add_propagation_frame = [&](Value error) {
        if (error.is_resultat() && !error.as_resultat()->success)
        {
            return error.with_trace_frame(TraceFrame{
                function_name,
                m_current_source_path,
                site_token.line,
                site_token.column});
        }
        return error;
    };

    Environment *previous_env = m_env;
    std::shared_ptr<Environment> previous_env_owner = m_env_owner;
    Value previous_self = m_self;
    // Start a fresh call frame whose parent is the function's saved closure.
    // Parameters and locals land in this new frame; captured names resolve
    // through the parent chain preserved by function_closure_owner(...).
    m_env_owner = std::make_shared<Environment>(function_closure_owner(*function));
    m_env = m_env_owner.get();
    m_env->set_source_identity(function_source_identity(*function));
    m_self = function->receiver;

    try
    {
        std::vector<std::optional<Value>> bound_arguments(params.size());
        std::size_t next_positional_parameter = 0;

        for (std::size_t i = 0; i < args.size(); ++i)
        {
            std::size_t target_index = params.size();

            if (!args[i].name.empty())
            {
                for (std::size_t param_index = 0; param_index < params.size(); ++param_index)
                {
                    if (params[param_index].name == args[i].name)
                    {
                        target_index = param_index;
                        break;
                    }
                }

                if (target_index == params.size())
                {
                    raise_runtime_error(call_site, "aucun parametre nomme '" + args[i].name + "'");
                }
            }
            else
            {
                while (next_positional_parameter < params.size() &&
                       bound_arguments[next_positional_parameter].has_value())
                {
                    ++next_positional_parameter;
                }

                if (next_positional_parameter == params.size())
                {
                    raise_runtime_error(call_site, "trop d'arguments fournis a l'appel de fonction");
                }

                target_index = next_positional_parameter++;
            }

            if (bound_arguments[target_index].has_value())
            {
                raise_runtime_error(call_site, "le parametre '" + params[target_index].name + "' est fourni plusieurs fois");
            }

            bound_arguments[target_index] = args[i].value;
        }

        for (std::size_t i = 0; i < params.size(); ++i)
        {
            const Parameter &parameter = params[i];
            Value argument_value = Value::rien();

            if (bound_arguments[i].has_value())
            {
                argument_value = *bound_arguments[i];
            }
            else if (parameter.default_value)
            {
                argument_value = evaluate(*parameter.default_value);
            }
            else
            {
                raise_runtime_error(call_site, "argument manquant pour le parametre '" + parameter.name + "'");
            }

            ensure_value_matches_annotation(
                argument_value,
                parameter.type,
                site_token,
                "le parametre '" + parameter.name + "'");
            m_env->define(parameter.name, std::move(argument_value), resolved_annotation_name(parameter.type));
        }

        if (body != nullptr)
        {
            execute(*body);
        }

        ensure_value_matches_annotation(
            Value::rien(),
            return_type,
            site_token,
            "la fonction '" + function_name + "'");

        m_env = previous_env;
        m_env_owner = previous_env_owner;
        m_self = previous_self;
        return Value::rien();
    }
    catch (const PropagateSignal &signal)
    {
        m_env = previous_env;
        m_env_owner = previous_env_owner;
        m_self = previous_self;
        Value error = add_propagation_frame(signal.error);
        ensure_value_matches_annotation(
            error,
            return_type,
            site_token,
            "la fonction '" + function_name + "'");
        return error;
    }
    catch (const ReturnSignal &signal)
    {
        Value return_value = add_propagation_frame(signal.value);
        m_env = previous_env;
        m_env_owner = previous_env_owner;
        m_self = previous_self;
        ensure_value_matches_annotation(
            return_value,
            return_type,
            site_token,
            "la fonction '" + function_name + "'");
        return return_value;
    }
    catch (...)
    {
        m_env = previous_env;
        m_env_owner = previous_env_owner;
        m_self = previous_self;
        throw;
    }
}

Value TreeWalker::call_builtin(const std::string &name,
                               const std::vector<Argument> &args,
                               const Token &call_site)
{
    const RuntimeSite origin{
        m_current_source_path,
        static_cast<int>(call_site.line),
        static_cast<int>(call_site.column)};
    const auto input_failure =
        [&](const std::string &operation,
            std::string cause) {
            return stdlib_failure(
                stdlib_error_value(
                    "ErreurEntrée",
                    operation,
                    std::move(cause)),
                origin);
        };

    if (name == "Succès" || name == "Échec")
    {
        if (args.size() != 1 || !args.front().name.empty())
        {
            throw_runtime_error(call_site,
                                name + " requiert exactement un argument positionnel");
        }
        return Value::resultat(
            name == "Succès",
            evaluate(*args.front().value),
            origin);
    }

    if (name == "afficher")
    {
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (!args[i].name.empty())
            {
                throw_runtime_error(call_site, "les arguments nommes ne sont pas pris en charge pour 'afficher'");
            }

            if (i > 0)
            {
                std::cout << ' ';
            }
            std::cout << to_texte(evaluate(*args[i].value));
        }
        std::cout << '\n';
        return Value::rien();
    }

    if (name == "lire")
    {
        if (!args.empty())
        {
            throw_runtime_error(call_site, "lire n'accepte pas d'arguments");
        }

        std::string line;
        if (!std::getline(std::cin, line))
        {
            return input_failure(
                "lire",
                "fin de l'entrée");
        }
        return stdlib_success(Value::texte(std::move(line)));
    }

    if (name == "type_de")
    {
        if (args.size() != 1)
        {
            throw_runtime_error(call_site, "type_de requiert exactement 1 argument");
        }
        if (!args[0].name.empty())
        {
            throw_runtime_error(call_site, "type_de n'accepte pas d'arguments nommes");
        }

        return Value::texte(runtime_type_name(evaluate(*args[0].value)));
    }

    if (name == "lire_entier")
    {
        if (!args.empty())
        {
            throw_runtime_error(call_site, "lire_entier n'accepte pas d'arguments");
        }

        std::string line;
        if (!std::getline(std::cin, line))
        {
            return input_failure(
                "lire_entier",
                "fin de l'entrée");
        }
        try
        {
            std::size_t parsed = 0;
            const int64_t value = std::stoll(line, &parsed);
            while (parsed < line.size() && std::isspace(static_cast<unsigned char>(line[parsed])))
            {
                ++parsed;
            }
            if (parsed != line.size())
            {
                throw std::invalid_argument("caractères restants");
            }
            return stdlib_success(Value::entier(value));
        }
        catch (...)
        {
            return input_failure(
                "lire_entier",
                "entier invalide: " + line);
        }
    }

    if (name == "lire_décimal" || name == "lire_decimal")
    {
        if (!args.empty())
        {
            throw_runtime_error(call_site, "lire_décimal n'accepte pas d'arguments");
        }

        std::string line;
        if (!std::getline(std::cin, line))
        {
            return input_failure(
                "lire_décimal",
                "fin de l'entrée");
        }
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
        {
            line.pop_back();
        }
        if (const auto value = numeric::parse_decimal(line))
        {
            return stdlib_success(Value::decimal(*value));
        }
        return input_failure(
            "lire_décimal",
            "décimal invalide: " + line);
    }

    if (name == "lire_logique")
    {
        if (!args.empty())
        {
            throw_runtime_error(call_site, "lire_logique n'accepte pas d'arguments");
        }

        std::string line;
        if (!std::getline(std::cin, line))
        {
            return input_failure(
                "lire_logique",
                "fin de l'entrée");
        }
        if (line == "vrai")
        {
            return stdlib_success(Value::logique(true));
        }
        if (line == "faux")
        {
            return stdlib_success(Value::logique(false));
        }
        return input_failure(
            "lire_logique",
            "logique invalide: " + line);
    }

    throw_runtime_error(call_site, "fonction native inconnue: " + name);
}

Value TreeWalker::instantiate_class(const std::shared_ptr<LumiereClass> &klass,
                                    const std::vector<Argument> &args,
                                    const Token &call_site)
{
    ClassDeclStmt *klass_decl = class_decl(klass);
    if (klass_decl == nullptr)
    {
        throw_runtime_error(call_site, "classe invalide");
    }

    auto object = std::make_shared<LumiereObject>();
    object->klass = klass;

    struct FieldInfo
    {
        VarDeclStmt *declaration;
        Token annotation;
    };
    std::unordered_map<std::string, FieldInfo> fields_by_name;
    std::vector<std::string> field_order;

    std::function<void(const std::shared_ptr<LumiereClass> &)> collect_fields = [&](const std::shared_ptr<LumiereClass> &current) {
        if (current == nullptr)
        {
            return;
        }

        if (std::shared_ptr<LumiereClass> parent = parent_class(current))
        {
            collect_fields(parent);
        }

        ClassDeclStmt *current_decl = class_decl(current);
        if (current_decl == nullptr)
        {
            return;
        }

        for (auto &member : current_decl->members)
        {
            if (auto *field = dynamic_cast<VarDeclStmt *>(member.get()))
            {
                fields_by_name.insert_or_assign(field->name.lexeme, FieldInfo{field, class_annotation(current, field->type)});
                field_order.push_back(field->name.lexeme);
            }
        }
    };

    collect_fields(klass);

    std::unordered_set<std::string> seen;
    std::vector<std::string> deduped_order;
    for (const auto &name : field_order)
    {
        if (!seen.count(name))
        {
            deduped_order.push_back(name);
            seen.insert(name);
        }
    }
    field_order = std::move(deduped_order);

    std::unordered_set<std::string> assigned_fields;
    std::size_t positional_index = 0;

    for (const auto &arg : args)
    {
        std::string field_name;
        if (!arg.name.empty())
        {
            field_name = arg.name;
        }
        else
        {
            if (positional_index >= field_order.size())
            {
                throw_runtime_error(call_site, "trop d'arguments fournis pour la construction de " + klass_decl->name.lexeme);
            }
            field_name = field_order[positional_index++];
        }

        if (!fields_by_name.count(field_name))
        {
            throw_runtime_error(call_site, "champ inconnu lors de la construction: " + field_name);
        }
        if (assigned_fields.count(field_name))
        {
            throw_runtime_error(call_site, "champ initialise plusieurs fois: " + field_name);
        }

        object->fields[field_name] = evaluate(*arg.value);
        ensure_value_matches_annotation(
            object->fields[field_name],
            fields_by_name.at(field_name).annotation,
            call_site,
            "le champ '" + field_name + "'");
        assigned_fields.insert(field_name);
    }

    for (const auto &[field_name, field] : fields_by_name)
    {
        const auto *field_decl = field.declaration;
        if (!assigned_fields.count(field_name))
        {
            if (field_decl->initializer)
            {
                object->fields[field_name] = evaluate(*field_decl->initializer);
            }
            else
            {
                object->fields[field_name] = Value::rien();
            }
            ensure_value_matches_annotation(
                object->fields[field_name],
                field.annotation,
                field_decl->name,
                "le champ '" + field_name + "'");
        }
    }

    return Value::objet(std::move(object));
}

} // namespace lumiere
