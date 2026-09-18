#include "lumiere/interpreter/tree_walker/tree_walker.hpp"

#include "lumiere/interpreter/runtime/cycles.hpp"

namespace lumiere
{

void TreeWalker::visit(ExprStmt &stmt)
{
    m_result = evaluate(*stmt.expr);
}

void TreeWalker::visit(IgnorerStmt &stmt)
{
    const Value value = evaluate(*stmt.expr);
    if (!value.is_resultat())
    {
        throw_runtime_error(stmt.keyword,
                            "ignorer exige une valeur Résultat");
    }
    m_result = Value::rien();
}

void TreeWalker::visit(BlockStmt &stmt)
{
    execute_block(stmt);
}

void TreeWalker::visit(VarDeclStmt &stmt)
{
    if (m_env == nullptr)
    {
        throw_runtime_error(stmt.name, "environnement d'exécution absent");
    }

    Value value = stmt.initializer ? evaluate(*stmt.initializer) : Value::rien();
    ensure_value_matches_annotation(value, stmt.type, stmt.name, "la variable '" + stmt.name.lexeme + "'");
    if (stmt.is_fixe)
    {
        try
        {
            m_env->define_fixe(stmt.name.lexeme, std::move(value), resolved_annotation_name(stmt.type));
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
            m_env->define(stmt.name.lexeme, std::move(value), resolved_annotation_name(stmt.type));
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
        throw_runtime_error(stmt.name, "environnement d'exécution absent");
    }

    try
    {
        m_env->define_fixe(stmt.name.lexeme, Value::fonction(make_declared_function(stmt, m_self, m_env_owner)));
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
        throw_runtime_error(stmt.name, "environnement d'exécution absent");
    }

    Ref<LumiereClass> runtime_parent = nullptr;
    ClassDeclStmt *parent = nullptr;
    if (!stmt.parent.empty())
    {
        const std::string parent_name = stmt.parent.to_string();
        if (!m_env->contains(parent_name))
        {
            throw_runtime_error(stmt.parent.source, "classe parente introuvable: " + parent_name);
        }
        const Value parent_value = m_env->get(parent_name);
        if (!parent_value.is_classe())
        {
            throw_runtime_error(stmt.parent.source, "la classe parente n'est pas une classe: " + parent_name);
        }
        runtime_parent = parent_value.as_classe();
        parent = class_decl(runtime_parent);
    }

    if (parent != nullptr)
    {
        for (auto &member : stmt.members)
        {
            if (auto *method = dynamic_cast<FunctionDeclStmt *>(member.get()))
            {
                FunctionDeclStmt *parent_method = find_method_decl(runtime_parent, method->name.lexeme);
                if (method->is_remplace && parent_method == nullptr)
                {
                    throw_runtime_error(method->name, "remplace utilise sans méthode parente correspondante: " + method->name.lexeme);
                }
                if (!method->is_remplace && parent_method != nullptr)
                {
                    throw_runtime_error(method->name, "méthode parente déjà définie; utilisez remplace: " + method->name.lexeme);
                }
                if (method->is_remplace && parent_method != nullptr && !method_signatures_match(*parent_method, *method))
                {
                    throw_runtime_error(method->name, "la méthode remplacee doit conserver la même signature: " + method->name.lexeme);
                }
            }
        }
    }

    Ref<LumiereClass> runtime_class = make_runtime_class(stmt);
    validate_class_interfaces(stmt, runtime_class);

    try
    {
        m_env->define_fixe(stmt.name.lexeme, Value::classe(std::move(runtime_class)));
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
        throw_runtime_error(stmt.name, "environnement d'exécution absent");
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

void TreeWalker::visit(TypeAliasDeclStmt &stmt)
{
    m_env->define_type_alias(stmt.name.lexeme, stmt.target);
}

void TreeWalker::visit(ImportStmt &stmt)
{
    if (m_env == nullptr)
    {
        throw_runtime_error(stmt.module_name, "environnement d'exécution absent");
    }

    const std::shared_ptr<Module> module = load_module(stmt.module_name);
    if (!stmt.imported_members.empty())
    {
        for (const auto &imported_member : stmt.imported_members)
        {
            if (module->public_type_aliases.contains(imported_member.name.lexeme))
            {
                const std::string binding_name =
                    imported_member.alias.lexeme.empty()
                        ? imported_member.name.lexeme
                        : imported_member.alias.lexeme;
                m_env->define_type_alias(
                    binding_name,
                    module->type_aliases.at(imported_member.name.lexeme));
                if (const auto value = module->public_type_values.find(imported_member.name.lexeme);
                    value != module->public_type_values.end())
                    m_env->define_fixe(binding_name, value->second);
                continue;
            }
            if (module->public_members.count(imported_member.name.lexeme) == 0)
            {
                throw_runtime_error(imported_member.name, "membre non exporté ou introuvable dans le module: " + imported_member.name.lexeme);
            }

            const auto member_it = module->members.find(imported_member.name.lexeme);
            if (member_it == module->members.end())
            {
                throw_runtime_error(imported_member.name, "membre introuvable dans le module: " + imported_member.name.lexeme);
            }

            const std::string binding_name = imported_member.alias.lexeme.empty()
                                                 ? imported_member.name.lexeme
                                                 : imported_member.alias.lexeme;
            try
            {
                m_env->define_fixe(binding_name, member_it->second);
            }
            catch (const RuntimeError &error)
            {
                throw_runtime_error(imported_member.alias.lexeme.empty() ? imported_member.name : imported_member.alias,
                                    error.raw_message());
            }
        }

        return;
    }

    const std::string binding_name = stmt.alias.lexeme.empty()
                                         ? default_module_alias(stmt.module_name.lexeme)
                                         : stmt.alias.lexeme;

    auto namespace_object = make_ref<LumiereObject>();
    namespace_object->klass = nullptr;

    for (const auto &public_name : module->public_members)
    {
        auto member_it = module->members.find(public_name);
        if (member_it != module->members.end())
        {
            namespace_object->fields[public_name] = member_it->second;
            if (member_it->second.is_classe() || member_it->second.is_interface())
            {
                Token identity = stmt.module_name;
                identity.lexeme = member_it->second.is_classe()
                    ? member_it->second.as_classe()->type_identity : member_it->second.as_interface()->type_identity;
                if (!identity.lexeme.empty())
                    m_env->define_type_alias(binding_name + '.' + public_name, TypeExpr::named(identity));
            }
        }
    }
    for (const std::string &name : module->public_type_aliases)
    {
        m_env->define_type_alias(
            binding_name + '.' + name,
            module->type_aliases.at(name));
        if (const auto value = module->public_type_values.find(name);
            value != module->public_type_values.end())
            namespace_object->fields.insert_or_assign(name, value->second);
    }

    try
    {
        m_env->define_fixe(binding_name, Value::objet(std::move(namespace_object)));
    }
    catch (const RuntimeError &error)
    {
        throw_runtime_error(stmt.alias.lexeme.empty() ? stmt.module_name : stmt.alias,
                            error.raw_message());
    }
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
        // The same back edge the VM collects on: between two iterations nothing
        // is part-way through an update, so the counts the collector reads are
        // settled. Without this a loop that builds cycles grows without bound
        // until the program ends, however short-lived each cycle is.
        collect_cycles_if_due();
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
        collect_cycles_if_due();
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
                if (matches_type_name(matched_value, pattern.type))
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
            case PatternKind::RESULT_SUCCESS:
            case PatternKind::RESULT_FAILURE:
                if (matched_value.is_resultat() &&
                    matched_value.as_resultat()->success ==
                        (pattern.kind == PatternKind::RESULT_SUCCESS) &&
                    (pattern.type.empty() ||
                     matches_type_name(matched_value.as_resultat()->payload,
                                       pattern.type)))
                {
                    branch_matches = true;
                    if (pattern.name.lexeme != "_")
                    {
                        binding_name = &pattern.name;
                        binding_value = &matched_value.as_resultat()->payload;
                    }
                }
                break;
            }

            if (branch_matches)
            {
                if (branch.terminator == BranchTerminator::PROPAGER)
                {
                    throw PropagateSignal{matched_value};
                }
                if (branch.terminator == BranchTerminator::IGNORER)
                {
                    if (!matched_value.is_resultat())
                    {
                        throw_runtime_error(
                            branch.terminator_token,
                            "ignorer exige une valeur Résultat");
                    }
                    return;
                }
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
