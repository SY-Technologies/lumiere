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

Value file_failure(
    const std::string &operation,
    const std::filesystem::path &path,
    std::string cause,
    const RuntimeSite &origin)
{
    return stdlib_failure(
        stdlib_error_value(
            "Fichier.ErreurFichier",
            operation,
            std::move(cause),
            path.string()),
        origin);
}

std::filesystem::path sanitize_path(IRuntime &runtime,
                                    const std::filesystem::path &path,
                                    const std::string &signature,
                                    const RuntimeSite &call_site)
{
    const std::string path_str = path.generic_string();
    if (path_str.find("..") != std::string::npos)
    {
        runtime.raise_runtime_error(call_site, signature + " rejette les chemins contenant '..'");
    }
    return path;
}

} // namespace

void register_fichier_module(Module &module)
{
    const auto &make_native_function = native_function_factory();
    auto error_class = make_ref<LumiereClass>();
    error_class->name = "Fichier.ErreurFichier";
    stdlib_bind_public_value(
        module,
        "ErreurFichier",
        Value::classe(std::move(error_class)));

    stdlib_bind_public_function(
        module,
        make_native_function,
        "existe",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.existe", call_site);
            sanitize_path(runtime, path, "Fichier.existe", call_site);
            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            return error
                       ? file_failure(
                             "existe",
                             path,
                             error.message(),
                             call_site)
                       : stdlib_success(Value::logique(exists));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "lire_texte",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.lire_texte", call_site);
            sanitize_path(runtime, path, "Fichier.lire_texte", call_site);
            std::ifstream file(path);
            if (!file.is_open())
            {
                return file_failure(
                    "lire_texte",
                    path,
                    "impossible d'ouvrir le fichier",
                    call_site);
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            if (file.bad())
            {
                return file_failure(
                    "lire_texte",
                    path,
                    "échec pendant la lecture",
                    call_site);
            }
            return stdlib_success(Value::texte(buffer.str()));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_fichier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(
                runtime, args, "Fichier.est_fichier", call_site);
            sanitize_path(runtime, path, "Fichier.est_fichier", call_site);
            std::error_code error;
            const bool is_file =
                std::filesystem::is_regular_file(path, error);
            if (error ==
                std::errc::no_such_file_or_directory)
            {
                error.clear();
            }
            return error
                       ? file_failure(
                             "est_fichier",
                             path,
                             error.message(),
                             call_site)
                       : stdlib_success(Value::logique(is_file));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "est_dossier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(
                runtime, args, "Fichier.est_dossier", call_site);
            sanitize_path(runtime, path, "Fichier.est_dossier", call_site);
            std::error_code error;
            const bool is_directory =
                std::filesystem::is_directory(path, error);
            if (error ==
                std::errc::no_such_file_or_directory)
            {
                error.clear();
            }
            return error
                       ? file_failure(
                             "est_dossier",
                             path,
                             error.message(),
                             call_site)
                       : stdlib_success(Value::logique(is_directory));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "taille",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.taille", call_site);
            sanitize_path(runtime, path, "Fichier.taille", call_site);
            std::error_code error;
            const std::uintmax_t size =
                std::filesystem::file_size(path, error);
            return error
                       ? file_failure(
                             "taille",
                             path,
                             error.message(),
                             call_site)
                       : stdlib_success(Value::entier(
                             static_cast<int64_t>(size)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "modifie_le",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.modifie_le", call_site);
            sanitize_path(runtime, path, "Fichier.modifie_le", call_site);
            std::error_code error;
            const auto modified =
                std::filesystem::last_write_time(path, error);
            return error
                       ? file_failure(
                             "modifie_le",
                             path,
                             error.message(),
                             call_site)
                       : stdlib_success(Value::texte(
                             format_file_time_utc(modified)));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "lire_lignes",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.lire_lignes", call_site);
            sanitize_path(runtime, path, "Fichier.lire_lignes", call_site);
            std::ifstream file(path);
            if (!file.is_open())
            {
                return file_failure(
                    "lire_lignes",
                    path,
                    "impossible d'ouvrir le fichier",
                    call_site);
            }

            auto lines = make_ref<ListeData>();
            std::string line;
            while (std::getline(file, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                lines->elements.push_back(Value::texte(line));
            }
            if (file.bad())
            {
                return file_failure(
                    "lire_lignes",
                    path,
                    "échec pendant la lecture",
                    call_site);
            }
            Value result = Value::liste(std::move(lines));
            runtime.annotate_value(
                result,
                "Liste[Texte]",
                call_site);
            return stdlib_success(std::move(result));
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "ecrire_texte",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto [path_text, content] = stdlib_expect_two_text_args(runtime, args, "Fichier.ecrire_texte", "chemin", "contenu", call_site);
            sanitize_path(runtime, std::filesystem::path(path_text), "Fichier.ecrire_texte", call_site);
            std::ofstream file(path_text, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                return file_failure(
                    "ecrire_texte",
                    path_text,
                    "impossible d'ouvrir le fichier",
                    call_site);
            }
            file << content;
            if (!file)
            {
                return file_failure(
                    "ecrire_texte",
                    path_text,
                    "échec pendant l'écriture",
                    call_site);
            }
            return stdlib_success(Value::rien());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "ajouter_texte",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto [path_text, content] = stdlib_expect_two_text_args(runtime, args, "Fichier.ajouter_texte", "chemin", "contenu", call_site);
            sanitize_path(runtime, std::filesystem::path(path_text), "Fichier.ajouter_texte", call_site);
            std::ofstream file(path_text, std::ios::binary | std::ios::app);
            if (!file.is_open())
            {
                return file_failure(
                    "ajouter_texte",
                    path_text,
                    "impossible d'ouvrir le fichier",
                    call_site);
            }
            file << content;
            if (!file)
            {
                return file_failure(
                    "ajouter_texte",
                    path_text,
                    "échec pendant l'écriture",
                    call_site);
            }
            return stdlib_success(Value::rien());
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
            const std::string path_text = args[0].value.is_texte() ? args[0].value.as_texte() : "";
            if (!args[0].value.is_texte())
            {
                runtime.raise_runtime_error(call_site, "Fichier.ecrire_lignes attend un chemin de type Texte");
            }
            sanitize_path(runtime, std::filesystem::path(path_text), "Fichier.ecrire_lignes", call_site);
            if (!args[1].value.is_liste())
            {
                runtime.raise_runtime_error(call_site, "Fichier.ecrire_lignes attend une liste de lignes");
            }

            const auto list = args[1].value.as_liste();
            for (std::size_t i = 0; i < list->elements.size(); ++i)
            {
                if (!list->elements[i].is_texte())
                {
                    runtime.raise_runtime_error(call_site, "Fichier.ecrire_lignes attend une liste contenant uniquement des valeurs de type Texte");
                }
            }

            std::ofstream file(path_text, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                return file_failure(
                    "ecrire_lignes",
                    path_text,
                    "impossible d'ouvrir le fichier",
                    call_site);
            }

            for (std::size_t i = 0; i < list->elements.size(); ++i)
            {
                if (i > 0)
                {
                    file << "\n";
                }
                file << list->elements[i].as_texte();
            }
            if (!file)
            {
                return file_failure(
                    "ecrire_lignes",
                    args[0].value.as_texte(),
                    "échec pendant l'écriture",
                    call_site);
            }
            return stdlib_success(Value::rien());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "creer_dossiers",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.creer_dossiers", call_site);
            sanitize_path(runtime, path, "Fichier.creer_dossiers", call_site);
            std::error_code error;
            std::filesystem::create_directories(path, error);
            return error
                       ? file_failure(
                             "creer_dossiers",
                             path,
                             error.message(),
                             call_site)
                       : stdlib_success(Value::rien());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "lister",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.lister", call_site);
            sanitize_path(runtime, path, "Fichier.lister", call_site);
            try
            {
                std::vector<std::string> entries;
                for (const auto &entry : std::filesystem::directory_iterator(path))
                {
                    entries.push_back(entry.path().string());
                }
                std::sort(entries.begin(), entries.end());

                auto values = make_ref<ListeData>();
                for (const auto &entry : entries)
                {
                    values->elements.push_back(Value::texte(entry));
                }
                Value result = Value::liste(std::move(values));
                runtime.annotate_value(result, "Liste[Texte]", call_site);
                return stdlib_success(std::move(result));
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                return file_failure(
                    "lister",
                    path,
                    error.what(),
                    call_site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "lister_recursif",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.lister_recursif", call_site);
            sanitize_path(runtime, path, "Fichier.lister_recursif", call_site);
            try
            {
                std::vector<std::string> entries;
                for (const auto &entry : std::filesystem::recursive_directory_iterator(path))
                {
                    entries.push_back(entry.path().string());
                }
                std::sort(entries.begin(), entries.end());

                auto values = make_ref<ListeData>();
                for (const auto &entry : entries)
                {
                    values->elements.push_back(Value::texte(entry));
                }
                Value result = Value::liste(std::move(values));
                runtime.annotate_value(result, "Liste[Texte]", call_site);
                return stdlib_success(std::move(result));
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                return file_failure(
                    "lister_recursif",
                    path,
                    error.what(),
                    call_site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "copier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto [source, destination] = stdlib_expect_two_text_args(runtime, args, "Fichier.copier", "source", "destination", call_site);
            sanitize_path(runtime, std::filesystem::path(source), "Fichier.copier", call_site);
            sanitize_path(runtime, std::filesystem::path(destination), "Fichier.copier", call_site);
            try
            {
                if (std::filesystem::path(source) == std::filesystem::path(destination))
                {
                    runtime.raise_runtime_error(call_site, "Fichier.copier: la source et la destination sont identiques");
                }
                std::filesystem::copy_file(
                    std::filesystem::path(source),
                    std::filesystem::path(destination),
                    std::filesystem::copy_options::overwrite_existing);
                return stdlib_success(Value::rien());
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                return file_failure(
                    "copier",
                    source,
                    error.what(),
                    call_site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "deplacer",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto [source, destination] = stdlib_expect_two_text_args(runtime, args, "Fichier.deplacer", "source", "destination", call_site);
            sanitize_path(runtime, std::filesystem::path(source), "Fichier.deplacer", call_site);
            sanitize_path(runtime, std::filesystem::path(destination), "Fichier.deplacer", call_site);
            try
            {
                std::filesystem::rename(std::filesystem::path(source), std::filesystem::path(destination));
                return stdlib_success(Value::rien());
            }
            catch (const std::filesystem::filesystem_error &error)
            {
                return file_failure(
                    "deplacer",
                    source,
                    error.what(),
                    call_site);
            }
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "supprimer",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.supprimer", call_site);
            sanitize_path(runtime, path, "Fichier.supprimer", call_site);
            std::error_code error;
            const bool removed =
                std::filesystem::remove(path, error);
            if (error)
            {
                return file_failure(
                    "supprimer",
                    path,
                    error.message(),
                    call_site);
            }
            if (!removed)
            {
                return file_failure(
                    "supprimer",
                    path,
                    "chemin introuvable",
                    call_site);
            }
            return stdlib_success(Value::rien());
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "supprimer_dossier",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.supprimer_dossier", call_site);
            sanitize_path(runtime, path, "Fichier.supprimer_dossier", call_site);
            std::error_code error;
            const bool is_directory =
                std::filesystem::is_directory(path, error);
            if (error)
            {
                return file_failure(
                    "supprimer_dossier",
                    path,
                    error.message(),
                    call_site);
            }
            if (!is_directory)
            {
                return file_failure(
                    "supprimer_dossier",
                    path,
                    "le chemin n'est pas un dossier",
                    call_site);
            }
            const bool removed =
                std::filesystem::remove(path, error);
            if (error)
            {
                return file_failure(
                    "supprimer_dossier",
                    path,
                    error.message(),
                    call_site);
            }
            return removed
                       ? stdlib_success(Value::rien())
                       : file_failure(
                             "supprimer_dossier",
                             path,
                             "dossier introuvable ou non vide",
                             call_site);
        });

    stdlib_bind_public_function(
        module,
        make_native_function,
        "supprimer_arbre",
        [](IRuntime &runtime, const NativeArgs &native_args) -> Value {
            const auto &args = *native_args.arguments;
            const auto &call_site = native_args.site;
            const auto path = stdlib_expect_path_arg(runtime, args, "Fichier.supprimer_arbre", call_site);
            sanitize_path(runtime, path, "Fichier.supprimer_arbre", call_site);
            std::error_code error;
            const bool exists =
                std::filesystem::exists(path, error);
            if (error)
            {
                return file_failure(
                    "supprimer_arbre",
                    path,
                    error.message(),
                    call_site);
            }
            if (!exists)
            {
                return file_failure(
                    "supprimer_arbre",
                    path,
                    "chemin introuvable",
                    call_site);
            }
            const auto removed =
                std::filesystem::remove_all(path, error);
            if (error)
            {
                return file_failure(
                    "supprimer_arbre",
                    path,
                    error.message(),
                    call_site);
            }
            return removed > 0
                       ? stdlib_success(Value::rien())
                       : file_failure(
                             "supprimer_arbre",
                             path,
                             "suppression récursive impossible",
                             call_site);
        });
}

} // namespace lumiere
