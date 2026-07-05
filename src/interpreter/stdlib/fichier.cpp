#include "lumiere/interpreter/stdlib/helpers.hpp"
#include "lumiere/interpreter/stdlib/modules.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace lumiere
{

namespace
{

std::string format_file_time_utc(const std::filesystem::file_time_type &time)
{
    // Lumiere exposes file timestamps as stable UTC text so tests and callers
    // do not depend on the machine's local timezone configuration.
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t raw_time = std::chrono::system_clock::to_time_t(system_time);
    std::tm utc_time{};
#ifdef _WIN32
    gmtime_s(&utc_time, &raw_time);
#else
    const std::tm *utc_time_ptr = std::gmtime(&raw_time);
    if (utc_time_ptr != nullptr)
    {
        utc_time = *utc_time_ptr;
    }
#endif

    std::ostringstream out;
    out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace

void register_fichier_module(Module &module, const NativeFunctionFactory &make_native_function)
{
    stdlib_bind_public_function(
        module,
        make_native_function,
        "existe",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.existe", call_site);
            return Value::logique(std::filesystem::exists(path));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "lire_texte",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.lire_texte", call_site);
            std::ifstream file(path);
            if (!file.is_open())
            {
                runtime.raise_runtime_error(call_site, "Fichier.lire_texte a echoue: impossible d'ouvrir le fichier demande");
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            return Value::texte(buffer.str());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_fichier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            return Value::logique(std::filesystem::is_regular_file(stdlib_expect_path_arg(runtime, args, "Fichier.est_fichier", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_dossier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            return Value::logique(std::filesystem::is_directory(stdlib_expect_path_arg(runtime, args, "Fichier.est_dossier", call_site)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "taille",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.taille", call_site);
            try
            {
                return Value::entier(static_cast<int64_t>(std::filesystem::file_size(path)));
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.taille", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "modifie_le",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.modifie_le", call_site);
            try
            {
                return Value::texte(format_file_time_utc(std::filesystem::last_write_time(path)));
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.modifie_le", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "lire_lignes",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.lire_lignes", call_site);
            std::ifstream file(path);
            if (!file.is_open())
            {
                runtime.raise_runtime_error(call_site, "Fichier.lire_lignes a echoue: impossible d'ouvrir le fichier demande");
            }

            auto lines = std::make_shared<ListeData>();
            std::string line;
            while (std::getline(file, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                lines->elements.push_back(Value::texte(line));
            }
            return Value::liste(std::move(lines));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "ecrire_texte",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto [path_text, content] = stdlib_expect_two_text_args(runtime, args, "Fichier.ecrire_texte", "chemin", "contenu", call_site);
            std::ofstream file(path_text, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                runtime.raise_runtime_error(call_site, "Fichier.ecrire_texte a echoue: impossible d'ouvrir le fichier cible");
            }
            file << content;
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "ajouter_texte",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto [path_text, content] = stdlib_expect_two_text_args(runtime, args, "Fichier.ajouter_texte", "chemin", "contenu", call_site);
            std::ofstream file(path_text, std::ios::binary | std::ios::app);
            if (!file.is_open())
            {
                runtime.raise_runtime_error(call_site, "Fichier.ajouter_texte a echoue: impossible d'ouvrir le fichier cible");
            }
            file << content;
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "ecrire_lignes",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            if (args.size() != 2 || !args[0].name.empty() || !args[1].name.empty())
            {
                runtime.raise_runtime_error(call_site, "Fichier.ecrire_lignes attend exactement deux arguments positionnels");
            }
            if (!args[0].value.is_texte())
            {
                runtime.raise_runtime_error(call_site, "Fichier.ecrire_lignes attend un chemin de type Texte");
            }
            if (!args[1].value.is_liste())
            {
                runtime.raise_runtime_error(call_site, "Fichier.ecrire_lignes attend une liste de lignes");
            }

            const auto list = args[1].value.as_liste();
            std::ofstream file(args[0].value.as_texte(), std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                runtime.raise_runtime_error(call_site, "Fichier.ecrire_lignes a echoue: impossible d'ouvrir le fichier cible");
            }

            for (std::size_t i = 0; i < list->elements.size(); ++i)
            {
                if (!list->elements[i].is_texte())
                {
                    runtime.raise_runtime_error(call_site, "Fichier.ecrire_lignes attend une liste contenant uniquement des valeurs de type Texte");
                }
                if (i > 0)
                {
                    file << "\n";
                }
                file << list->elements[i].as_texte();
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "creer_dossiers",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.creer_dossiers", call_site);
            try
            {
                std::filesystem::create_directories(path);
                return Value::rien();
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.creer_dossiers", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "lister",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.lister", call_site);
            try
            {
                std::vector<std::string> entries;
                for (const auto &entry : std::filesystem::directory_iterator(path))
                {
                    entries.push_back(entry.path().string());
                }
                std::sort(entries.begin(), entries.end());

                auto values = std::make_shared<ListeData>();
                for (const auto &entry : entries)
                {
                    values->elements.push_back(Value::texte(entry));
                }
                Value result = Value::liste(std::move(values));
                runtime.annotate_value(result, "Liste[Texte]", call_site);
                return result;
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.lister", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "lister_recursif",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.lister_recursif", call_site);
            try
            {
                std::vector<std::string> entries;
                for (const auto &entry : std::filesystem::recursive_directory_iterator(path))
                {
                    entries.push_back(entry.path().string());
                }
                std::sort(entries.begin(), entries.end());

                auto values = std::make_shared<ListeData>();
                for (const auto &entry : entries)
                {
                    values->elements.push_back(Value::texte(entry));
                }
                Value result = Value::liste(std::move(values));
                runtime.annotate_value(result, "Liste[Texte]", call_site);
                return result;
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.lister_recursif", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "copier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto [source, destination] = stdlib_expect_two_text_args(runtime, args, "Fichier.copier", "source", "destination", call_site);
            try
            {
                std::filesystem::copy_file(
                    std::filesystem::path(source),
                    std::filesystem::path(destination),
                    std::filesystem::copy_options::overwrite_existing);
                return Value::rien();
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.copier", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "deplacer",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto [source, destination] = stdlib_expect_two_text_args(runtime, args, "Fichier.deplacer", "source", "destination", call_site);
            try
            {
                std::filesystem::rename(std::filesystem::path(source), std::filesystem::path(destination));
                return Value::rien();
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.deplacer", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "supprimer",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.supprimer", call_site);
            try
            {
                const bool removed = std::filesystem::remove(path);
                if (!removed)
                {
                    runtime.raise_runtime_error(call_site, "Fichier.supprimer a echoue: chemin introuvable ou non supprimable");
                }
                return Value::rien();
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.supprimer", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "supprimer_dossier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.supprimer_dossier", call_site);
            try
            {
                if (!std::filesystem::is_directory(path))
                {
                    runtime.raise_runtime_error(call_site, "Fichier.supprimer_dossier a echoue: le chemin cible n'est pas un dossier");
                }
                const bool removed = std::filesystem::remove(path);
                if (!removed)
                {
                    runtime.raise_runtime_error(call_site, "Fichier.supprimer_dossier a echoue: dossier introuvable ou non vide");
                }
                return Value::rien();
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.supprimer_dossier", error.what());
            }
            return Value::rien();
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "supprimer_arbre",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.supprimer_arbre", call_site);
            try
            {
                if (!std::filesystem::exists(path))
                {
                    runtime.raise_runtime_error(call_site, "Fichier.supprimer_arbre a echoue: chemin introuvable");
                }
                const auto removed = std::filesystem::remove_all(path);
                if (removed == 0)
                {
                    runtime.raise_runtime_error(call_site, "Fichier.supprimer_arbre a echoue: suppression recursive impossible");
                }
                return Value::rien();
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                stdlib_throw_filesystem_failure(runtime, call_site, "Fichier.supprimer_arbre", error.what());
            }
            return Value::rien();
        });
}

} // namespace lumiere
