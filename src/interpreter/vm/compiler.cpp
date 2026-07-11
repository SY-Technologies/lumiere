 #include "lumiere/interpreter/vm/compiler.hpp"

#include "ast_to_lir.hpp"
#include "lir_to_bytecode.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/source_file.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace lumiere
{
namespace
{

struct LinkedUnit
{
    std::unordered_map<std::string, std::size_t> exports;
    std::vector<std::size_t> initializers;
};

struct LinkContext
{
    LirModule module;
    std::unordered_map<std::string, LinkedUnit> loaded;
    std::unordered_set<std::string> loading;
    std::size_t next_prefix = 0;
};

struct CollectedImport
{
    ImportStmt *statement = nullptr;
    bool top_level = false;
};

void collect_imports_from_expr(Expr &expr, std::vector<CollectedImport> &imports);

void collect_imports_from_stmt(Stmt &stmt,
                               std::vector<CollectedImport> &imports,
                               const bool top_level = false)
{
    if (auto *import = dynamic_cast<ImportStmt *>(&stmt))
    {
        imports.push_back({import, top_level});
    }
    else if (auto *block = dynamic_cast<BlockStmt *>(&stmt))
    {
        for (auto &child : block->statements)
        {
            collect_imports_from_stmt(*child, imports);
        }
    }
    else if (auto *expression = dynamic_cast<ExprStmt *>(&stmt))
    {
        collect_imports_from_expr(*expression->expr, imports);
    }
    else if (auto *variable = dynamic_cast<VarDeclStmt *>(&stmt))
    {
        if (variable->initializer)
        {
            collect_imports_from_expr(*variable->initializer, imports);
        }
    }
    else if (auto *function = dynamic_cast<FunctionDeclStmt *>(&stmt))
    {
        for (Parameter &parameter : function->params)
        {
            if (parameter.default_value)
            {
                collect_imports_from_expr(*parameter.default_value, imports);
            }
        }
        if (function->body)
        {
            collect_imports_from_stmt(*function->body, imports);
        }
    }
    else if (auto *klass = dynamic_cast<ClassDeclStmt *>(&stmt))
    {
        for (auto &member : klass->members)
        {
            collect_imports_from_stmt(*member, imports);
        }
    }
    else if (auto *condition = dynamic_cast<IfStmt *>(&stmt))
    {
        collect_imports_from_expr(*condition->condition, imports);
        collect_imports_from_stmt(*condition->then_branch, imports);
        if (condition->else_branch)
        {
            collect_imports_from_stmt(*condition->else_branch, imports);
        }
    }
    else if (auto *loop = dynamic_cast<ForStmt *>(&stmt))
    {
        collect_imports_from_expr(*loop->iterable, imports);
        collect_imports_from_stmt(*loop->body, imports);
    }
    else if (auto *loop = dynamic_cast<WhileStmt *>(&stmt))
    {
        collect_imports_from_expr(*loop->condition, imports);
        collect_imports_from_stmt(*loop->body, imports);
    }
    else if (auto *result = dynamic_cast<ReturnStmt *>(&stmt))
    {
        if (result->value)
        {
            collect_imports_from_expr(*result->value, imports);
        }
    }
    else if (auto *thrown = dynamic_cast<ThrowStmt *>(&stmt))
    {
        collect_imports_from_expr(*thrown->value, imports);
    }
    else if (auto *attempt = dynamic_cast<TryStmt *>(&stmt))
    {
        collect_imports_from_stmt(*attempt->body, imports);
        for (CatchClause &clause : attempt->catch_clauses)
        {
            collect_imports_from_stmt(*clause.body, imports);
        }
        if (attempt->finally_body)
        {
            collect_imports_from_stmt(*attempt->finally_body, imports);
        }
    }
    else if (auto *match = dynamic_cast<AgirSelonStmt *>(&stmt))
    {
        collect_imports_from_expr(*match->expression, imports);
        for (AgirSelonBranch &branch : match->branches)
        {
            for (Pattern &pattern : branch.patterns)
            {
                if (pattern.literal)
                {
                    collect_imports_from_expr(*pattern.literal, imports);
                }
            }
            collect_imports_from_stmt(*branch.body, imports);
        }
        if (match->else_branch)
        {
            collect_imports_from_stmt(*match->else_branch, imports);
        }
    }
}

void collect_imports_from_expr(Expr &expr, std::vector<CollectedImport> &imports)
{
    if (auto *binary = dynamic_cast<BinaryExpr *>(&expr))
    {
        collect_imports_from_expr(*binary->left, imports);
        collect_imports_from_expr(*binary->right, imports);
    }
    else if (auto *dictionary = dynamic_cast<DictionaryExpr *>(&expr))
    {
        for (DictionaryEntryExpr &entry : dictionary->entries)
        {
            collect_imports_from_expr(*entry.key, imports);
            collect_imports_from_expr(*entry.value, imports);
        }
    }
    else if (auto *unary = dynamic_cast<UnaryExpr *>(&expr))
    {
        collect_imports_from_expr(*unary->operand, imports);
    }
    else if (auto *cast = dynamic_cast<CastExpr *>(&expr))
    {
        collect_imports_from_expr(*cast->operand, imports);
    }
    else if (auto *check = dynamic_cast<TypeCheckExpr *>(&expr))
    {
        collect_imports_from_expr(*check->operand, imports);
    }
    else if (auto *function = dynamic_cast<FunctionExpr *>(&expr))
    {
        for (Parameter &parameter : function->params)
        {
            if (parameter.default_value)
            {
                collect_imports_from_expr(*parameter.default_value, imports);
            }
        }
        collect_imports_from_stmt(*function->body, imports);
    }
    else if (auto *call = dynamic_cast<CallExpr *>(&expr))
    {
        collect_imports_from_expr(*call->callee, imports);
        for (Argument &argument : call->args)
        {
            collect_imports_from_expr(*argument.value, imports);
        }
    }
    else if (auto *list = dynamic_cast<ListExpr *>(&expr))
    {
        for (auto &element : list->elements)
        {
            collect_imports_from_expr(*element, imports);
        }
    }
    else if (auto *member = dynamic_cast<MemberAccessExpr *>(&expr))
    {
        collect_imports_from_expr(*member->object, imports);
    }
    else if (auto *index = dynamic_cast<IndexAccessExpr *>(&expr))
    {
        collect_imports_from_expr(*index->object, imports);
        collect_imports_from_expr(*index->index, imports);
    }
}

std::filesystem::path resolve_module_path(const std::filesystem::path &source_path,
                                          const std::string &module_name)
{
    std::filesystem::path candidate = source_path.parent_path();
    std::size_t start = 0;
    while (start < module_name.size())
    {
        const std::size_t end = module_name.find('.', start);
        const bool last = end == std::string::npos;
        candidate /= module_name.substr(start, last ? std::string::npos : end - start);
        if (last)
        {
            candidate += SOURCE_FILE_EXTENSION;
            break;
        }
        start = end + 1;
    }
    return std::filesystem::exists(candidate) ? candidate : std::filesystem::path{};
}

Program parse_module(const std::filesystem::path &path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw VmCompileError("VM: impossible d'ouvrir le module " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string source = buffer.str();
    Lexer lexer(source);
    Parser parser(lexer.tokenise());
    StmtList statements = parser.parse();
    if (parser.had_error())
    {
        throw VmCompileError("VM: erreur de syntaxe dans le module " + path.filename().replace_extension().string());
    }
    return Program{std::move(statements), path.string(), std::move(source)};
}

LirOperand remap_operand(const LirOperand operand,
                         const std::vector<std::size_t> &constants,
                         const std::vector<std::size_t> &globals,
                         const std::vector<std::size_t> &functions,
                         const std::vector<std::size_t> &types,
                         const std::vector<std::size_t> &members,
                         const std::vector<std::size_t> &classes,
                         const std::vector<std::size_t> &interfaces,
                         const std::vector<std::size_t> &argument_names,
                         const std::vector<std::size_t> &namespaces)
{
    switch (operand.kind)
    {
    case LirOperandKind::IR_OPERAND_CONSTANT: return LirOperand::constant(constants.at(operand.index));
    case LirOperandKind::IR_OPERAND_GLOBAL: return LirOperand::global(globals.at(operand.index));
    case LirOperandKind::IR_OPERAND_FUNCTION: return LirOperand::function(functions.at(operand.index));
    case LirOperandKind::IR_OPERAND_TYPE: return LirOperand::type(types.at(operand.index));
    case LirOperandKind::IR_OPERAND_MEMBER: return LirOperand::member(members.at(operand.index));
    case LirOperandKind::IR_OPERAND_CLASS: return LirOperand::klass(classes.at(operand.index));
    case LirOperandKind::IR_OPERAND_INTERFACE: return LirOperand::interface(interfaces.at(operand.index));
    case LirOperandKind::IR_OPERAND_ARGUMENT_NAME:
        return LirOperand::argument_name(argument_names.at(operand.index));
    case LirOperandKind::IR_OPERAND_NAMESPACE: return LirOperand::name_space(namespaces.at(operand.index));
    case LirOperandKind::IR_OPERAND_LOCAL:
    case LirOperandKind::IR_OPERAND_TEMP:
    case LirOperandKind::IR_OPERAND_BLOCK:
    case LirOperandKind::IR_OPERAND_CAPTURE:
        return operand;
    }
    throw VmCompileError("VM: operande LIR inconnue pendant l'edition de liens");
}

struct MergeResult
{
    std::vector<std::size_t> globals;
    std::vector<std::size_t> initializers;
};

MergeResult merge_module(LirModule &target, const LirModule &source, const std::string &prefix)
{
    std::vector<std::size_t> constants;
    for (const LirConstant &constant : source.constants)
    {
        constants.push_back(target.add_constant(constant.value, constant.display));
    }

    std::vector<std::size_t> globals;
    static const std::unordered_set<std::string> core_globals{
        "afficher", "afficher_inline", "lire", "lire_entier", "lire_décimal",
        "lire_decimal", "lire_logique", "type_de"};
    for (const LirGlobal &global : source.globals)
    {
        globals.push_back(target.add_global(
            core_globals.contains(global.name) || global.name.starts_with('@') ? global.name : prefix + global.name));
    }

    std::vector<std::size_t> types;
    for (const LirType &type : source.types)
    {
        types.push_back(target.add_type(type.name));
    }
    std::vector<std::size_t> members;
    for (const LirMember &member : source.members)
    {
        members.push_back(target.add_member(member.name));
    }
    std::vector<std::size_t> argument_names;
    for (const std::string &name : source.argument_names)
    {
        argument_names.push_back(target.add_argument_name(name));
    }

    const std::size_t function_offset = target.functions.size();
    std::vector<std::size_t> functions(source.functions.size());
    for (std::size_t i = 0; i < functions.size(); ++i)
    {
        functions[i] = function_offset + i;
    }
    const std::size_t class_offset = target.classes.size();
    std::vector<std::size_t> classes(source.classes.size());
    for (std::size_t i = 0; i < classes.size(); ++i)
    {
        classes[i] = class_offset + i;
    }
    const std::size_t interface_offset = target.interfaces.size();
    std::vector<std::size_t> interfaces(source.interfaces.size());
    for (std::size_t i = 0; i < interfaces.size(); ++i)
    {
        interfaces[i] = interface_offset + i;
    }
    const std::size_t namespace_offset = target.namespaces.size();
    std::vector<std::size_t> namespaces(source.namespaces.size());
    for (std::size_t i = 0; i < namespaces.size(); ++i)
    {
        namespaces[i] = namespace_offset + i;
    }

    for (const LirClassDescriptor &source_class : source.classes)
    {
        LirClassDescriptor klass = source_class;
        if (!klass.parent.empty())
        {
            klass.parent = prefix + klass.parent;
        }
        for (std::string &interface : klass.interfaces)
        {
            interface = prefix + interface;
        }
        for (LirMethodDescriptor &method : klass.methods)
        {
            method.function_index = functions.at(method.function_index);
            for (LirOperand &capture : method.capture_sources)
            {
                capture = remap_operand(capture, constants, globals, functions, types, members, classes, interfaces,
                                        argument_names, namespaces);
            }
        }
        target.classes.push_back(std::move(klass));
    }
    target.interfaces.insert(target.interfaces.end(), source.interfaces.begin(), source.interfaces.end());
    for (const LirNamespaceDescriptor &source_namespace : source.namespaces)
    {
        LirNamespaceDescriptor name_space = source_namespace;
        for (LirNamespaceMember &member : name_space.members)
        {
            member.global_index = globals.at(member.global_index);
        }
        target.namespaces.push_back(std::move(name_space));
    }

    for (const LirFunction &source_function : source.functions)
    {
        LirFunction &function = target.append_function(prefix + source_function.name);
        function.params = source_function.params;
        function.source_path = source_function.source_path;
        function.source_text = source_function.source_text;
        function.locals = source_function.locals;
        function.source_arity = source_function.source_arity;
        function.optional_params = source_function.optional_params;
        function.temps = source_function.temps;
        function.entry_block = source_function.entry_block;
        for (const LirCapture &capture : source_function.captures)
        {
            function.captures.push_back({capture.index,
                                         capture.name,
                                         remap_operand(capture.source, constants, globals, functions, types,
                                                       members, classes, interfaces, argument_names, namespaces)});
        }
        for (const LirBlock &source_block : source_function.blocks)
        {
            LirBlock &block = function.append_block();
            for (const LirInstruction &source_instruction : source_block.instructions)
            {
                std::vector<LirOperand> operands;
                for (const LirOperand operand : source_instruction.operands)
                {
                    operands.push_back(remap_operand(operand, constants, globals, functions, types, members, classes,
                                                     interfaces, argument_names, namespaces));
                }
                block.instructions.push_back(LirInstruction::make(
                    source_instruction.opcode,
                    remap_operand(source_instruction.destination, constants, globals, functions, types,
                                  members, classes, interfaces, argument_names, namespaces),
                    std::move(operands), source_instruction.source));
            }
            std::vector<LirOperand> term_operands;
            for (const LirOperand operand : source_block.terminator->operands)
            {
                term_operands.push_back(remap_operand(operand, constants, globals, functions, types, members, classes,
                                                      interfaces, argument_names, namespaces));
            }
            block.terminator = std::make_unique<LirTerminator>(LirTerminator{
                source_block.terminator->kind, std::move(term_operands), source_block.terminator->source});
        }
    }

    MergeResult result;
    result.globals = std::move(globals);
    for (const std::size_t initializer : source.initializer_functions)
    {
        result.initializers.push_back(functions.at(initializer));
    }
    return result;
}

std::string default_alias(const std::string &module_name)
{
    const std::size_t dot = module_name.rfind('.');
    return dot == std::string::npos ? module_name : module_name.substr(dot + 1);
}

std::optional<LinkedUnit> link_builtin(LinkContext &context, const std::string &module_name)
{
    const std::string key = "builtin:" + module_name;
    if (const auto loaded = context.loaded.find(key); loaded != context.loaded.end())
    {
        return loaded->second;
    }

    Module module;
    module.name = module_name;
    if (!register_builtin_module(module))
    {
        return std::nullopt;
    }

    LinkedUnit result;
    const std::size_t initializer_index = context.module.functions.size();
    LirFunction &initializer = context.module.append_function("@builtin::" + module_name + "::__module_init__");
    initializer.entry_block = initializer.append_block().index;
    LirBlock &block = initializer.block(initializer.entry_block);
    std::size_t temp_index = 0;
    for (const std::string &name : module.public_members)
    {
        const auto member = module.members.find(name);
        if (member == module.members.end())
        {
            continue;
        }
        const std::size_t global = context.module.add_global("@builtin::" + module_name + "::" + name);
        const std::size_t constant = context.module.add_constant(member->second, "<builtin " + module_name + "." + name + ">");
        const LirOperand value = LirOperand::temp(temp_index++);
        initializer.temps.push_back(value.index);
        block.instructions.push_back(LirInstruction::make(
            LirOpcode::IR_OP_CONSTANT, value, {LirOperand::constant(constant)}));
        block.instructions.push_back(LirInstruction::make(
            LirOpcode::IR_OP_STORE_GLOBAL, LirOperand::temp(0),
            {LirOperand::global(global), value}));
        result.exports[name] = global;
    }
    block.terminator = std::make_unique<LirTerminator>(LirTerminator::return_nil());
    result.initializers.push_back(initializer_index);
    context.loaded.emplace(key, result);
    return result;
}

LinkedUnit link_unit(LinkContext &context,
                     Program &program,
                     const std::filesystem::path &path,
                     const std::string &prefix,
                     bool root)
{
    const std::string key = std::filesystem::weakly_canonical(path).string();
    if (!root)
    {
        if (const auto loaded = context.loaded.find(key); loaded != context.loaded.end())
        {
            return loaded->second;
        }
        if (!context.loading.insert(key).second)
        {
            throw VmCompileError("VM: cycle d'import detecte pour le module " + path.string());
        }
    }

    struct ImportResolution
    {
        ImportStmt *statement = nullptr;
        LinkedUnit unit;
        bool top_level = false;
    };
    std::vector<ImportResolution> imports;
    std::vector<CollectedImport> collected_imports;
    for (auto &statement : program.statements)
    {
        collect_imports_from_stmt(*statement, collected_imports, true);
    }
    for (const CollectedImport &collected : collected_imports)
    {
        ImportStmt *import = collected.statement;
        if (const auto builtin = link_builtin(context, import->module_name.lexeme); builtin.has_value())
        {
            imports.push_back({import, *builtin, collected.top_level});
            continue;
        }
        const std::filesystem::path module_path = resolve_module_path(path, import->module_name.lexeme);
        if (module_path.empty())
        {
            throw VmCompileError("VM: module introuvable: " + import->module_name.lexeme);
        }
        Program imported = parse_module(module_path);
        const std::string imported_prefix = "@M" + std::to_string(context.next_prefix++) + "::";
        imports.push_back({import,
                           link_unit(context, imported, module_path, imported_prefix, false),
                           collected.top_level});
    }

    ResolvedVmImports resolved_imports;
    for (const ImportResolution &resolution : imports)
    {
        ResolvedVmImport resolved;
        for (const auto &[name, global] : resolution.unit.exports)
        {
            resolved.export_symbols[name] = context.module.globals.at(global).name;
        }
        for (const std::size_t initializer : resolution.unit.initializers)
        {
            const std::string &function_name = context.module.functions.at(initializer).name;
            static_cast<void>(context.module.add_global(function_name));
            resolved.initializer_symbols.push_back(function_name);
        }
        resolved_imports.emplace(resolution.statement, std::move(resolved));
    }
    AstToLir lowerer;
    LirModule local = lowerer.lower(program, resolved_imports);
    const MergeResult merged = merge_module(context.module, local, prefix);

    std::vector<std::size_t> own_initializers = merged.initializers;
    const bool has_top_level_import = std::any_of(imports.begin(), imports.end(), [](const ImportResolution &import) {
        return import.top_level;
    });
    if (has_top_level_import)
    {
        const std::size_t binding_index = context.module.functions.size();
        LirFunction &binding = context.module.append_function(prefix + "__import_bindings__");
        binding.entry_block = binding.append_block().index;
        LirBlock &block = binding.block(binding.entry_block);
        std::size_t temp = 0;
        for (const ImportResolution &resolution : imports)
        {
            if (!resolution.top_level)
            {
                continue;
            }
            ImportStmt &import = *resolution.statement;
            if (!import.imported_members.empty())
            {
                for (const ImportStmt::ImportedMember &member : import.imported_members)
                {
                    const auto exported = resolution.unit.exports.find(member.name.lexeme);
                    if (exported == resolution.unit.exports.end())
                    {
                        throw VmCompileError("VM: membre non exporte ou introuvable dans le module: " +
                                             member.name.lexeme);
                    }
                    const std::string binding_name = member.alias.lexeme.empty() ? member.name.lexeme : member.alias.lexeme;
                    const auto local_global = std::find_if(local.globals.begin(), local.globals.end(), [&](const LirGlobal &global) {
                        return global.name == binding_name;
                    });
                    const std::size_t target = local_global == local.globals.end()
                                                   ? context.module.add_global(prefix + binding_name)
                                                   : merged.globals.at(local_global->index);
                    const LirOperand value = LirOperand::temp(temp++);
                    binding.temps.push_back(value.index);
                    block.instructions.push_back(LirInstruction::make(
                        LirOpcode::IR_OP_LOAD_GLOBAL, value, {LirOperand::global(exported->second)}));
                    block.instructions.push_back(LirInstruction::make(
                        LirOpcode::IR_OP_STORE_GLOBAL, LirOperand::temp(0),
                        {LirOperand::global(target), value}));
                }
            }
            else
            {
                LirNamespaceDescriptor descriptor;
                for (const auto &[name, global] : resolution.unit.exports)
                {
                    descriptor.members.push_back({name, global});
                }
                const std::size_t descriptor_index = context.module.namespaces.size();
                context.module.namespaces.push_back(std::move(descriptor));
                const std::string alias = import.alias.lexeme.empty()
                                              ? default_alias(import.module_name.lexeme)
                                              : import.alias.lexeme;
                const auto local_global = std::find_if(local.globals.begin(), local.globals.end(), [&](const LirGlobal &global) {
                    return global.name == alias;
                });
                const std::size_t target = local_global == local.globals.end()
                                               ? context.module.add_global(prefix + alias)
                                               : merged.globals.at(local_global->index);
                const LirOperand value = LirOperand::temp(temp++);
                binding.temps.push_back(value.index);
                block.instructions.push_back(LirInstruction::make(
                    LirOpcode::IR_OP_NAMESPACE, value, {LirOperand::name_space(descriptor_index)}));
                block.instructions.push_back(LirInstruction::make(
                    LirOpcode::IR_OP_STORE_GLOBAL, LirOperand::temp(0),
                    {LirOperand::global(target), value}));
            }
        }
        block.terminator = std::make_unique<LirTerminator>(LirTerminator::return_nil());
        own_initializers.insert(own_initializers.begin(), binding_index);
    }

    LinkedUnit result;
    for (const ImportResolution &resolution : imports)
    {
        if (!resolution.top_level)
        {
            continue;
        }
        result.initializers.insert(result.initializers.end(),
                                   resolution.unit.initializers.begin(),
                                   resolution.unit.initializers.end());
    }
    result.initializers.insert(result.initializers.end(), own_initializers.begin(), own_initializers.end());
    for (auto &statement : program.statements)
    {
        std::string name;
        bool is_public = false;
        if (auto *variable = dynamic_cast<VarDeclStmt *>(statement.get()))
        {
            name = variable->name.lexeme;
            is_public = variable->is_public;
        }
        else if (auto *function = dynamic_cast<FunctionDeclStmt *>(statement.get()))
        {
            name = function->name.lexeme;
            is_public = function->is_public;
        }
        else if (auto *klass = dynamic_cast<ClassDeclStmt *>(statement.get()))
        {
            name = klass->name.lexeme;
            is_public = klass->is_public;
        }
        else if (auto *interface = dynamic_cast<InterfaceDeclStmt *>(statement.get()))
        {
            name = interface->name.lexeme;
            is_public = interface->is_public;
        }
        if (!is_public)
        {
            continue;
        }
        const auto global = std::find_if(local.globals.begin(), local.globals.end(), [&](const LirGlobal &candidate) {
            return candidate.name == name;
        });
        if (global != local.globals.end())
        {
            result.exports[name] = merged.globals.at(global->index);
        }
    }

    if (!root)
    {
        context.loading.erase(key);
        context.loaded.emplace(key, result);
    }
    else
    {
        context.module.initializer_functions = result.initializers;
    }
    return result;
}

} // namespace

LirModule VmCompiler::lower_to_lir(Program &program)
{
    LinkContext context;
    context.module.name = program.source_path.empty() ? "__module__" : program.source_path;
    const std::filesystem::path source_path =
        program.source_path.empty()
            ? std::filesystem::current_path() / ("__module__" + std::string(SOURCE_FILE_EXTENSION))
            : std::filesystem::path(program.source_path);
    static_cast<void>(link_unit(context, program, source_path, "", true));

    return std::move(context.module);
}

ModuleBytecode VmCompiler::compile(Program &program)
{
    const FunctionDeclStmt *principal = nullptr;
    for (auto &statement : program.statements)
    {
        auto *function = dynamic_cast<FunctionDeclStmt *>(statement.get());
        if (function != nullptr && function->name.lexeme == "principal")
        {
            principal = function;
        }
    }
    if (principal == nullptr)
    {
        throw VmCompileError("VM: aucun point d'entree 'principal' n'a ete trouve");
    }
    if (!principal->params.empty())
    {
        throw VmCompileError("VM: 'principal' ne doit pas encore prendre d'arguments");
    }

    LirModule module = lower_to_lir(program);
    std::size_t entry = 0;
    bool found = false;
    for (std::size_t i = 0; i < module.functions.size(); ++i)
    {
        if (module.functions[i].name == "principal")
        {
            entry = i;
            found = true;
            break;
        }
    }
    if (!found)
    {
        throw VmCompileError("VM: point d'entree bytecode introuvable");
    }

    LirToBytecode emitter;
    return emitter.emit(module, entry);
}

ModuleBytecode VmCompiler::compile_for_inspection(Program &program)
{
    LirModule module = lower_to_lir(program);
    if (module.functions.empty())
    {
        throw VmCompileError("VM: aucun corps bytecode a inspecter");
    }

    std::size_t entry = 0;
    for (std::size_t i = 0; i < module.functions.size(); ++i)
    {
        if (module.functions[i].name == "principal")
        {
            entry = i;
            break;
        }
    }
    LirToBytecode emitter;
    return emitter.emit(module, entry);
}

} // namespace lumiere
