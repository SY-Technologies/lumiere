#include <filesystem>
#include <fstream>
#include <sstream>
#include "lumiere/interpreter/stdlib/modules.hpp"
#include "lumiere/interpreter/tree_walker/tree_walker.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/parser.hpp"

namespace lumiere
{

    std::shared_ptr<Module> TreeWalker::load_builtin_module(const std::string &module_name) const
    {
        auto module = std::make_shared<Module>();
        module->name = module_name;
        auto state = std::make_shared<TreeWalkerModuleState>();
        state->environment = std::make_shared<Environment>();
        module->state = state;

        if (module_name == "Chemin")
        {
            register_chemin_module(*module, [this](LumiereFunction::NativeHandler handler)
                                   { return make_native_function(std::move(handler)); });
            return module;
        }

        if (module_name == "Fichier")
        {
            register_fichier_module(*module, [this](LumiereFunction::NativeHandler handler)
                                    { return make_native_function(std::move(handler)); });
            return module;
        }

        if (module_name == "Texte")
        {
            register_texte_module(*module, [this](LumiereFunction::NativeHandler handler)
                                  { return make_native_function(std::move(handler)); });
            return module;
        }

        if (module_name == "Maths")
        {
            register_maths_module(*module, [this](LumiereFunction::NativeHandler handler)
                                  { return make_native_function(std::move(handler)); });
            return module;
        }

        if (module_name == "Temps")
        {
            register_temps_module(*module, [this](LumiereFunction::NativeHandler handler)
                                  { return make_native_function(std::move(handler)); });
            return module;
        }

        if (module_name == "Aléatoire" || module_name == "Aleatoire")
        {
            register_aleatoire_module(*module, [this](LumiereFunction::NativeHandler handler)
                                      { return make_native_function(std::move(handler)); });
            return module;
        }

        if (module_name == "LumiNet")
        {
            register_luminet_module(*module, [this](LumiereFunction::NativeHandler handler)
                                    { return make_native_function(std::move(handler)); });
            return module;
        }

        if (module_name == "LumiTest")
        {
            auto lumitest_state = std::make_shared<LumiTestModuleState>();
            lumitest_state->options = m_lumitest_options;
            register_lumitest_module(*module, [this](LumiereFunction::NativeHandler handler)
                                     { return make_native_function(std::move(handler)); }, lumitest_state);
            return module;
        }

        return nullptr;
    }

    std::filesystem::path TreeWalker::resolve_module_path(const std::string &module_name) const
    {
        std::vector<std::filesystem::path> search_roots = m_import_paths;

        if (!m_current_source_path.empty())
        {
            const std::filesystem::path current_path(m_current_source_path);
            if (current_path.has_parent_path())
            {
                search_roots.push_back(current_path.parent_path());
            }
        }

        for (const auto &root : search_roots)
        {
            std::filesystem::path candidate = root;
            std::size_t segment_start = 0;
            while (segment_start < module_name.size())
            {
                const std::size_t segment_end = module_name.find('.', segment_start);
                const bool is_last = segment_end == std::string::npos;
                const std::string segment = module_name.substr(
                    segment_start,
                    is_last ? std::string::npos : segment_end - segment_start);

                if (is_last)
                {
                    candidate /= segment + ".lum";
                    break;
                }

                candidate /= segment;
                segment_start = segment_end + 1;
            }

            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
        }

        return {};
    }

    std::string TreeWalker::default_module_alias(const std::string &module_name) const
    {
        const std::size_t last_dot = module_name.rfind('.');
        if (last_dot == std::string::npos)
        {
            return module_name;
        }

        return module_name.substr(last_dot + 1);
    }

    std::shared_ptr<Module> TreeWalker::execute_module(const std::string &module_name,
                                                       const std::filesystem::path &path,
                                                       std::shared_ptr<StmtList> statements,
                                                       std::string source_text)
    {
        auto module = std::make_shared<Module>();
        module->name = module_name;
        auto state = std::make_shared<TreeWalkerModuleState>();
        state->environment = std::make_shared<Environment>();
        module->state = state;

        Environment *previous_env = m_env;
        std::shared_ptr<Environment> previous_env_owner = m_env_owner;
        const Value previous_self = m_self;
        const std::string previous_source_path = m_current_source_path;
        const std::string previous_source_text = m_current_source_text;

        // Run module top-level code in its own environment so its declarations
        // do not leak directly into the importer's current scope.
        m_env = module_environment(module);
        m_env_owner = state->environment;
        m_self = Value::rien();
        m_current_source_path = path.string();
        m_current_source_text = std::move(source_text);

        try
        {
            for (auto &statement : *statements)
            {
                execute(*statement);

                // After execution, copy top-level declarations into the
                // module export table. Import resolution reads from members,
                // not directly from the module's Environment object.
                if (auto *var_decl = dynamic_cast<VarDeclStmt *>(statement.get()))
                {
                    module->members[var_decl->name.lexeme] = m_env->get(var_decl->name.lexeme);
                    if (var_decl->is_public)
                    {
                        module->public_members.insert(var_decl->name.lexeme);
                    }
                }
                else if (auto *function_decl = dynamic_cast<FunctionDeclStmt *>(statement.get()))
                {
                    module->members[function_decl->name.lexeme] = m_env->get(function_decl->name.lexeme);
                    if (function_decl->is_public)
                    {
                        module->public_members.insert(function_decl->name.lexeme);
                    }
                }
                else if (auto *class_decl = dynamic_cast<ClassDeclStmt *>(statement.get()))
                {
                    module->members[class_decl->name.lexeme] = m_env->get(class_decl->name.lexeme);
                    if (class_decl->is_public)
                    {
                        module->public_members.insert(class_decl->name.lexeme);
                    }
                }
                else if (auto *interface_decl = dynamic_cast<InterfaceDeclStmt *>(statement.get()))
                {
                    module->members[interface_decl->name.lexeme] = m_env->get(interface_decl->name.lexeme);
                    if (interface_decl->is_public)
                    {
                        module->public_members.insert(interface_decl->name.lexeme);
                    }
                }
            }
        }
        catch (...)
        {
            m_env = previous_env;
            m_env_owner = previous_env_owner;
            m_self = previous_self;
            m_current_source_path = previous_source_path;
            m_current_source_text = previous_source_text;
            throw;
        }

        m_env = previous_env;
        m_env_owner = previous_env_owner;
        m_self = previous_self;
        m_current_source_path = previous_source_path;
        m_current_source_text = previous_source_text;
        return module;
    }

    std::shared_ptr<Module> TreeWalker::load_module(const Token &module_name_token)
    {
        const std::string &module_name = module_name_token.lexeme;
        const std::string builtin_cache_key = "builtin:" + module_name;
        if (auto it = m_modules.find(builtin_cache_key); it != m_modules.end())
        {
            return it->second;
        }

        if (std::shared_ptr<Module> builtin = load_builtin_module(module_name))
        {
            m_modules[builtin_cache_key] = builtin;
            return builtin;
        }

        const std::filesystem::path path = resolve_module_path(module_name);
        if (path.empty())
        {
            throw_runtime_error(module_name_token, "module introuvable: " + module_name);
        }

        const std::string cache_key = std::filesystem::weakly_canonical(path).string();
        if (auto it = m_modules.find(cache_key); it != m_modules.end())
        {
            return it->second;
        }

        if (m_loading_modules.count(cache_key) != 0)
        {
            throw_runtime_error(module_name_token, "cycle d'import detecte pour le module " + module_name);
        }

        m_loading_modules.insert(cache_key);
        try
        {
            std::ifstream file(path);
            if (!file.is_open())
            {
                throw_runtime_error(module_name_token, "impossible d'ouvrir le module " + module_name);
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            const std::string source_text = buffer.str();

            Lexer lexer(source_text);
            Parser parser(lexer.tokenise());
            auto statements = std::make_shared<StmtList>(parser.parse());
            if (parser.had_error())
            {
                throw_runtime_error(module_name_token, "erreur de syntaxe dans le module " + module_name);
            }
            m_loaded_module_programmes.push_back(statements);

            auto module = execute_module(module_name, path, statements, source_text);
            m_modules[cache_key] = module;
            m_loading_modules.erase(cache_key);
            return module;
        }
        catch (...)
        {
            m_loading_modules.erase(cache_key);
            throw;
        }
    }

} // namespace lumiere
