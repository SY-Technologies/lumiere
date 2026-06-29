#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

namespace lumiere
{

void TreeWalker::visit(ExprStmt &stmt)
{
    m_result = evaluate(*stmt.expr);
}

void TreeWalker::visit(BlockStmt &stmt)
{
    execute_block(stmt);
}

void TreeWalker::visit(VarDeclStmt &stmt)
{
    if (m_env == nullptr)
    {
        throw_runtime_error(stmt.name, "environnement d'execution absent");
    }

    Value value = stmt.initializer ? evaluate(*stmt.initializer) : Value::rien();
    ensure_value_matches_annotation(value, stmt.type_token, stmt.name, "la variable '" + stmt.name.lexeme + "'");
    if (stmt.is_fixe)
    {
        try
        {
            m_env->define_fixe(stmt.name.lexeme, std::move(value), stmt.type_token.lexeme);
        }
        catch (const RuntimeError &error)
        {
            throw_runtime_error(stmt.name, error.raw_message());
        }
    }
    else
    {
        try
        {
            m_env->define(stmt.name.lexeme, std::move(value), stmt.type_token.lexeme);
        }
        catch (const RuntimeError &error)
        {
            throw_runtime_error(stmt.name, error.raw_message());
        }
    }
}

void TreeWalker::visit(FunctionDeclStmt &stmt)
{
    if (m_env == nullptr)
    {
        throw_runtime_error(stmt.name, "environnement d'execution absent");
    }

    try
    {
        m_env->define_fixe(stmt.name.lexeme, Value::fonction(make_declared_function(stmt, m_self, m_env)));
    }
    catch (const RuntimeError &error)
    {
        throw_runtime_error(stmt.name, error.raw_message());
    }
}

void TreeWalker::visit(ClassDeclStmt &stmt)
{
    if (m_env == nullptr)
    {
        throw_runtime_error(stmt.name, "environnement d'execution absent");
    }

    ClassDeclStmt *parent = nullptr;
    if (!stmt.parent.lexeme.empty())
    {
        if (!m_env->contains(stmt.parent.lexeme))
        {
            throw_runtime_error(stmt.parent, "classe parente introuvable: " + stmt.parent.lexeme);
        }
        const Value parent_value = m_env->get(stmt.parent.lexeme);
        if (!parent_value.is_classe())
        {
            throw_runtime_error(stmt.parent, "la classe parente n'est pas une classe: " + stmt.parent.lexeme);
        }
        parent = class_decl(parent_value.as_classe());
    }

    if (parent != nullptr)
    {
        for (auto &member : stmt.members)
        {
            if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
            {
                FunctionDeclStmt *parent_method = find_method_decl(*parent, method->name.lexeme);
                if (method->is_remplace && parent_method == nullptr)
                {
                    throw_runtime_error(method->name, "remplace utilise sans methode parente correspondante: " + method->name.lexeme);
                }
                if (!method->is_remplace && parent_method != nullptr)
                {
                    throw_runtime_error(method->name, "methode parente deja definie; utilisez remplace: " + method->name.lexeme);
                }
            }
        }
    }

    validate_class_interfaces(stmt);

    try
    {
        m_env->define_fixe(stmt.name.lexeme, Value::classe(make_runtime_class(stmt)));
    }
    catch (const RuntimeError &error)
    {
        throw_runtime_error(stmt.name, error.raw_message());
    }
}

void TreeWalker::visit(InterfaceDeclStmt &stmt)
{
    if (m_env == nullptr)
    {
        throw_runtime_error(stmt.name, "environnement d'execution absent");
    }

    try
    {
        m_env->define_fixe(stmt.name.lexeme, Value::interface(make_runtime_interface(stmt)));
    }
    catch (const RuntimeError &error)
    {
        throw_runtime_error(stmt.name, error.raw_message());
    }
}

void TreeWalker::visit(ImportStmt &stmt)
{
    if (m_env == nullptr)
    {
        throw_runtime_error(stmt.module_name, "environnement d'execution absent");
    }

    const std::shared_ptr<Module> module = load_module(stmt.module_name);
    if (!stmt.imported_members.empty())
    {
        for (const auto &imported_member : stmt.imported_members)
        {
            if (module->public_members.count(imported_member.name.lexeme) == 0)
            {
                throw_runtime_error(imported_member.name, "membre non exporte ou introuvable dans le module: " + imported_member.name.lexeme);
            }

            const auto member_it = module->members.find(imported_member.name.lexeme);
            if (member_it == module->members.end())
            {
                throw_runtime_error(imported_member.name, "membre introuvable dans le module: " + imported_member.name.lexeme);
            }

            const std::string binding_name = imported_member.alias.lexeme.empty()
                                                 ? imported_member.name.lexeme
                                                 : imported_member.alias.lexeme;
            m_env->define_fixe(binding_name, member_it->second);
        }

        return;
    }

    const std::string binding_name = stmt.alias.lexeme.empty()
                                         ? default_module_alias(stmt.module_name.lexeme)
                                         : stmt.alias.lexeme;

    auto namespace_object = std::make_shared<LumiereObject>();
    namespace_object->klass = nullptr;

    for (const auto &public_name : module->public_members)
    {
        auto member_it = module->members.find(public_name);
        if (member_it != module->members.end())
        {
            namespace_object->fields[public_name] = member_it->second;
        }
    }

    m_env->define_fixe(binding_name, Value::objet(std::move(namespace_object)));
}

void TreeWalker::visit(IfStmt &stmt)
{
    if (is_truthy(evaluate(*stmt.condition)))
    {
        execute(*stmt.then_branch);
        return;
    }

    if (stmt.else_branch)
    {
        execute(*stmt.else_branch);
    }
}

void TreeWalker::visit(ForStmt &stmt)
{
    const std::vector<Value> items = enumerate_iterable(evaluate(*stmt.iterable), stmt.variable);

    for (const Value &item : items)
    {
        ScopeGuard guard(m_env, m_env_owner);
        m_env->define(stmt.variable.lexeme, item);

        try
        {
            execute(*stmt.body);
        }
        catch (const ContinueSignal &)
        {
            continue;
        }
        catch (const BreakSignal &)
        {
            break;
        }
    }
}

void TreeWalker::visit(WhileStmt &stmt)
{
    while (is_truthy(evaluate(*stmt.condition)))
    {
        try
        {
            execute(*stmt.body);
        }
        catch (const ContinueSignal &)
        {
            continue;
        }
        catch (const BreakSignal &)
        {
            break;
        }
    }
}

void TreeWalker::visit(ReturnStmt &stmt)
{
    throw ReturnSignal{stmt.value ? evaluate(*stmt.value) : Value::rien()};
}

void TreeWalker::visit(BreakStmt &)
{
    throw BreakSignal{};
}

void TreeWalker::visit(ContinueStmt &)
{
    throw ContinueSignal{};
}

void TreeWalker::visit(ThrowStmt &stmt)
{
    throw ThrownSignal{
        evaluate(*stmt.value),
        m_current_source_path,
        m_current_source_text,
        stmt.keyword.line,
        stmt.keyword.column,
        m_stack_trace,
    };
}

void TreeWalker::visit(TryStmt &stmt)
{
    auto run_finally = [&]() {
        if (stmt.finally_body)
        {
            execute(*stmt.finally_body);
        }
    };

    try
    {
        execute(*stmt.body);
    }
    catch (const ReturnSignal &signal)
    {
        run_finally();
        throw;
    }
    catch (const BreakSignal &)
    {
        run_finally();
        throw;
    }
    catch (const ContinueSignal &)
    {
        run_finally();
        throw;
    }
    catch (const ThrownSignal &signal)
    {
        bool handled = false;

        for (auto &clause : stmt.catch_clauses)
        {
            if (!matches_catch_clause(clause, signal.value))
            {
                continue;
            }

            execute_branch_with_optional_binding(*clause.body, &clause.variable, &signal.value);
            handled = true;
            break;
        }

        if (!handled)
        {
            run_finally();
            throw;
        }
    }
    catch (const RuntimeError &error)
    {
        bool handled = false;
        const Value thrown_value = Value::texte(error.what());

        for (auto &clause : stmt.catch_clauses)
        {
            if (!matches_catch_clause(clause, thrown_value))
            {
                continue;
            }

            execute_branch_with_optional_binding(*clause.body, &clause.variable, &thrown_value);
            handled = true;
            break;
        }

        if (!handled)
        {
            run_finally();
            throw;
        }
    }

    run_finally();
}

void TreeWalker::visit(AgirSelonStmt &stmt)
{
    const Value matched_value = evaluate(*stmt.expression);

    for (auto &branch : stmt.branches)
    {
        const Token *binding_name = nullptr;
        const Value *binding_value = nullptr;
        bool branch_matches = false;

        for (auto &pattern : branch.patterns)
        {
            switch (pattern.kind)
            {
            case PatternKind::LITERAL:
                if (pattern.literal && is_equal(matched_value, evaluate(*pattern.literal)))
                {
                    branch_matches = true;
                }
                break;
            case PatternKind::TYPE_BINDING:
                if (matches_type_name(matched_value, pattern.type_token))
                {
                    branch_matches = true;
                    binding_name = &pattern.name;
                    binding_value = &matched_value;
                }
                break;
            case PatternKind::RIEN:
                if (matched_value.is_rien())
                {
                    branch_matches = true;
                }
                break;
            }

            if (branch_matches)
            {
                execute_branch_with_optional_binding(*branch.body, binding_name, binding_value);
                return;
            }
        }
    }

    if (stmt.else_branch)
    {
        execute_branch_with_optional_binding(*stmt.else_branch, nullptr, nullptr);
        return;
    }

    throw_runtime_error(stmt.keyword, "aucune branche de 'agir selon' ne correspond");
}

}
