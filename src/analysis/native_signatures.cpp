#include "lumiere/analysis/native_signatures.hpp"

#include <initializer_list>
#include <utility>

namespace lumiere
{
namespace
{

TypeExpr named(std::string name)
{
    return TypeExpr::named(
        Token(TokenType::IDENT, std::move(name), 0, 0));
}

TypeExpr generic(std::string name,
                 std::initializer_list<TypeExpr> arguments)
{
    const std::string source_name = name;
    return TypeExpr::generic(
        Token(TokenType::IDENT, source_name, 0, 0),
        std::move(name),
        std::vector<TypeExpr>(arguments));
}

TypeExpr union_type(std::initializer_list<TypeExpr> alternatives)
{
    return TypeExpr::union_of(
        Token(TokenType::PIPE, "|", 0, 0),
        std::vector<TypeExpr>(alternatives));
}

NativeParameterSpec parameter(std::string name,
                              std::string type,
                              const bool optional = false)
{
    return {
        std::move(name),
        named(std::move(type)),
        optional,
    };
}

SemanticModuleExports::Callable callable(
    std::initializer_list<NativeParameterSpec> parameters,
    TypeExpr return_type)
{
    SemanticModuleExports::Callable result;
    result.has_explicit_return_type = true;
    result.return_type = std::move(return_type);
    for (const NativeParameterSpec &item : parameters)
    {
        result.parameter_names.push_back(item.name);
        result.parameter_types.push_back(item.type);
        result.optional_parameters.push_back(item.optional);
    }
    return result;
}

// Like callable(), but additionally accepts any number of trailing positional
// arguments beyond `parameters`, each checked against `variadic_type`.
SemanticModuleExports::Callable variadic_callable(
    std::initializer_list<NativeParameterSpec> parameters,
    TypeExpr variadic_type,
    TypeExpr return_type)
{
    SemanticModuleExports::Callable result = callable(parameters, std::move(return_type));
    result.variadic = true;
    result.variadic_type = std::move(variadic_type);
    return result;
}

void export_callable(
    SemanticModuleExports &exports,
    std::string name,
    SemanticModuleExports::Callable signature)
{
    exports.values.emplace(name, SemanticSymbolKind::FUNCTION);
    exports.callables.emplace(std::move(name), std::move(signature));
}

void export_value(
    SemanticModuleExports &exports,
    std::string name,
    TypeExpr type)
{
    exports.values.emplace(name, SemanticSymbolKind::VARIABLE);
    exports.value_types.emplace(std::move(name), std::move(type));
}

} // namespace

const std::vector<NativeCallableSpec> &core_native_signatures()
{
    static const std::vector<NativeCallableSpec> signatures = {
        {
            "afficher",
            {},
            named("Rien"),
            true,
            named("Universel"),
        },
        {
            "lire",
            {parameter("invite", "Texte", true)},
            generic(
                "Résultat",
                {named("Texte"), named("ErreurEntrée")}),
            false,
            {},
        },
        {
            "lire_entier",
            {parameter("invite", "Texte", true)},
            generic(
                "Résultat",
                {named("Entier"), named("ErreurEntrée")}),
            false,
            {},
        },
        {
            "lire_décimal",
            {},
            generic(
                "Résultat",
                {named("Décimal"), named("ErreurEntrée")}),
            false,
            {},
        },
        {
            "lire_decimal",
            {},
            generic(
                "Résultat",
                {named("Décimal"), named("ErreurEntrée")}),
            false,
            {},
        },
        {
            "lire_logique",
            {},
            generic(
                "Résultat",
                {named("Logique"), named("ErreurEntrée")}),
            false,
            {},
        },
        {
            "type_de",
            {parameter("valeur", "Universel")},
            named("Texte"),
            false,
            {},
        },
    };
    return signatures;
}

std::optional<SemanticModuleExports>
native_module_exports(const std::string_view module_name)
{
    SemanticModuleExports exports;

    if (module_name == "Fichier")
    {
        exports.types.emplace(
            "ErreurFichier",
            SemanticTypeKind::CLASS);
        exports.values.emplace(
            "ErreurFichier",
            SemanticSymbolKind::CLASS);
        exports.error_types.insert("ErreurFichier");

        const TypeExpr error = named("ErreurFichier");
        const auto result = [&](TypeExpr success) {
            return generic(
                "Résultat",
                {std::move(success), error});
        };
        export_callable(
            exports,
            "lire_texte",
            callable(
                {parameter("chemin", "Texte")},
                result(named("Texte"))));
        export_callable(
            exports,
            "lire_lignes",
            callable(
                {parameter("chemin", "Texte")},
                result(generic("Liste", {named("Texte")}))));
        export_callable(
            exports,
            "taille",
            callable(
                {parameter("chemin", "Texte")},
                result(named("Entier"))));
        export_callable(
            exports,
            "modifie_le",
            callable(
                {parameter("chemin", "Texte")},
                result(named("Texte"))));
        for (const char *name :
             {"existe", "est_fichier", "est_dossier"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {parameter("chemin", "Texte")},
                    result(named("Logique"))));
        }
        for (const char *name :
             {"lister", "lister_recursif"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {parameter("chemin", "Texte")},
                    result(generic(
                        "Liste",
                        {named("Texte")}))));
        }
        for (const char *name :
             {"creer_dossiers", "supprimer",
              "supprimer_dossier", "supprimer_arbre"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {parameter("chemin", "Texte")},
                    result(named("Rien"))));
        }
        for (const char *name :
             {"ecrire_texte", "ajouter_texte"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("chemin", "Texte"),
                        parameter("contenu", "Texte"),
                    },
                    result(named("Rien"))));
        }
        export_callable(
            exports,
            "ecrire_lignes",
            callable(
                {
                    parameter("chemin", "Texte"),
                    NativeParameterSpec{
                        "lignes",
                        generic("Liste", {named("Texte")}),
                        false,
                    },
                },
                result(named("Rien"))));
        for (const char *name : {"copier", "deplacer"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("source", "Texte"),
                        parameter("destination", "Texte"),
                    },
                    result(named("Rien"))));
        }
        return exports;
    }

    if (module_name == "Texte")
    {
        exports.types.emplace(
            "ErreurConversion",
            SemanticTypeKind::CLASS);
        exports.values.emplace(
            "ErreurConversion",
            SemanticSymbolKind::CLASS);
        exports.error_types.insert("ErreurConversion");
        const TypeExpr error = named("ErreurConversion");
        for (const auto &[name, success] :
             std::initializer_list<std::pair<const char *, const char *>>{
                 {"en_entier", "Entier"},
                 {"en_decimal", "Décimal"},
                 {"en_logique", "Logique"}})
        {
            export_callable(
                exports,
                name,
                callable(
                    {parameter("texte", "Texte")},
                    generic(
                        "Résultat",
                        {named(success), error})));
        }
        export_callable(
            exports,
            "convertir_entier",
            callable(
                {parameter("valeur", "Entier")},
                named("Texte")));
        export_callable(
            exports,
            "convertir_decimal",
            callable(
                {parameter("valeur", "Décimal")},
                named("Texte")));
        export_callable(
            exports,
            "convertir_logique",
            callable(
                {parameter("valeur", "Logique")},
                named("Texte")));
        export_callable(
            exports,
            "joindre",
            callable(
                {
                    NativeParameterSpec{
                        "valeurs",
                        generic("Liste", {named("Texte")}),
                        false,
                    },
                    parameter("séparateur", "Texte"),
                },
                named("Texte")));
        export_callable(
            exports,
            "taille",
            callable({parameter("texte", "Texte")}, named("Entier")));
        export_callable(
            exports,
            "est_vide",
            callable({parameter("texte", "Texte")}, named("Logique")));
        for (const char *name :
             {"contient", "commence_par", "finit_par"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("texte", "Texte"),
                        parameter("recherche", "Texte"),
                    },
                    named("Logique")));
        }
        export_callable(
            exports,
            "index_de",
            callable(
                {
                    parameter("texte", "Texte"),
                    parameter("recherche", "Texte"),
                },
                named("Entier")));
        for (const char *name : {"separer", "separer_lignes"})
        {
            if (name == std::string_view("separer"))
            {
                export_callable(
                    exports,
                    name,
                    callable(
                        {
                            parameter("texte", "Texte"),
                            parameter("séparateur", "Texte"),
                        },
                        generic("Liste", {named("Texte")})));
            }
            else
            {
                export_callable(
                    exports,
                    name,
                    callable(
                        {parameter("texte", "Texte")},
                        generic("Liste", {named("Texte")})));
            }
        }
        for (const char *name :
             {"elaguer", "elaguer_gauche", "elaguer_droite",
              "minuscules", "majuscules", "inverser"})
        {
            export_callable(
                exports,
                name,
                callable({parameter("texte", "Texte")}, named("Texte")));
        }
        for (const char *name : {"remplacer", "remplacer_tout"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("texte", "Texte"),
                        parameter("recherche", "Texte"),
                        parameter("remplacement", "Texte"),
                    },
                    named("Texte")));
        }
        export_callable(
            exports,
            "repeter",
            callable(
                {
                    parameter("texte", "Texte"),
                    parameter("nombre", "Entier"),
                },
                named("Texte")));
        for (const char *name : {"inserer", "supprimer"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("texte", "Texte"),
                        parameter("position", "Entier"),
                        name == std::string_view("inserer")
                            ? parameter("ajout", "Texte")
                            : parameter("longueur", "Entier"),
                    },
                    named("Texte")));
        }
        export_callable(
            exports,
            "sous_texte",
            callable(
                {
                    parameter("texte", "Texte"),
                    parameter("début", "Entier"),
                    parameter("longueur", "Entier", true),
                },
                named("Texte")));
        return exports;
    }

    if (module_name == "Maths")
    {
        for (const char *name : {"pi", "e", "infini", "non_nombre"})
        {
            export_value(exports, name, named("Décimal"));
        }
        for (const char *name : {"absolu", "abs"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {parameter("valeur", "Décimal")},
                    union_type({named("Entier"), named("Décimal")})));
        }
        for (const char *name : {"min", "max"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("gauche", "Décimal"),
                        parameter("droite", "Décimal"),
                    },
                    union_type({named("Entier"), named("Décimal")})));
        }
        for (const char *name :
             {"arrondir", "arrondi", "plancher", "plafond", "tronquer"})
        {
            export_callable(
                exports,
                name,
                callable({parameter("valeur", "Décimal")}, named("Entier")));
        }
        for (const char *name :
             {"racine", "log", "log10", "log2", "sin", "sinus",
              "cos", "cosinus", "tan", "tangente", "asin", "acos",
              "atan", "degres_vers_radians", "radians_vers_degres"})
        {
            export_callable(
                exports,
                name,
                callable({parameter("valeur", "Décimal")}, named("Décimal")));
        }
        for (const char *name : {"racine_n", "puissance", "atan2"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("gauche", "Décimal"),
                        parameter("droite", "Décimal"),
                    },
                    named("Décimal")));
        }
        for (const char *name : {"est_non_nombre", "est_infini"})
        {
            export_callable(
                exports,
                name,
                callable({parameter("valeur", "Décimal")}, named("Logique")));
        }
        for (const char *name : {"est_pair", "est_impair"})
        {
            export_callable(
                exports,
                name,
                callable({parameter("valeur", "Entier")}, named("Logique")));
        }
        return exports;
    }

    if (module_name == "Temps")
    {
        exports.types.emplace(
            "Instant",
            SemanticTypeKind::CLASS);
        exports.types.emplace(
            "Durée",
            SemanticTypeKind::CLASS);
        exports.types.emplace(
            "ErreurTemps",
            SemanticTypeKind::CLASS);
        exports.values.emplace(
            "ErreurTemps",
            SemanticSymbolKind::CLASS);
        exports.error_types.insert("ErreurTemps");
        exports.types.emplace(
            "Fuseau",
            SemanticTypeKind::CLASS);
        exports.types.emplace(
            "DateHeure",
            SemanticTypeKind::CLASS);
        exports.types.emplace(
            "RepèreMonotone",
            SemanticTypeKind::CLASS);
        export_callable(
            exports,
            "analyser",
            callable(
                {
                    parameter("texte", "Texte"),
                    parameter("format", "Texte"),
                },
                generic(
                    "Résultat",
                    {
                        named("Instant"),
                        named("ErreurTemps"),
                    })));
        export_callable(
            exports,
            "horodatage",
            callable({}, named("Entier")));
        export_callable(
            exports,
            "maintenant",
            callable({}, named("Instant")));
        export_callable(
            exports,
            "depuis_horodatage",
            callable(
                {parameter("millisecondes", "Entier")},
                named("Instant")));
        export_callable(
            exports,
            "entre",
            callable(
                {
                    parameter("début", "Instant"),
                    parameter("fin", "Instant"),
                },
                named("Durée")));
        export_callable(
            exports,
            "attendre",
            callable({parameter("durée", "Durée")}, named("Rien")));
        for (const char *name :
             {"millisecondes", "secondes", "minutes", "heures", "jours"})
        {
            export_callable(
                exports,
                name,
                callable({parameter("nombre", "Entier")}, named("Durée")));
        }
        export_callable(
            exports,
            "analyser_iso8601",
            callable(
                {parameter("texte", "Texte")},
                generic(
                    "Résultat",
                    {
                        named("Instant"),
                        named("ErreurTemps"),
                    })));
        export_callable(
            exports,
            "formater_iso8601",
            callable({parameter("instant", "Instant")}, named("Texte")));
        export_callable(
            exports,
            "fuseau",
            callable(
                {parameter("nom", "Texte")},
                generic(
                    "Résultat",
                    {
                        named("Fuseau"),
                        named("ErreurTemps"),
                    })));
        export_callable(
            exports,
            "fuseau_local",
            callable(
                {},
                generic(
                    "Résultat",
                    {
                        named("Fuseau"),
                        named("ErreurTemps"),
                    })));
        export_callable(
            exports,
            "dans_fuseau",
            callable(
                {
                    parameter("instant", "Instant"),
                    parameter("fuseau", "Fuseau"),
                },
                named("DateHeure")));
        export_callable(
            exports,
            "repère",
            callable({}, named("RepèreMonotone")));
        export_callable(
            exports,
            "écoulé",
            callable({parameter("depuis", "RepèreMonotone")}, named("Durée")));
        return exports;
    }

    if (module_name == "Chemin")
    {
        export_value(exports, "separateur", named("Texte"));
        export_callable(
            exports,
            "dossier_courant",
            callable({}, named("Texte")));
        for (const char *name :
             {"absolu", "nom", "nom_sans_extension", "extension",
              "dossier", "normaliser"})
        {
            export_callable(
                exports,
                name,
                callable({parameter("chemin", "Texte")}, named("Texte")));
        }
        export_callable(
            exports,
            "parties",
            callable(
                {parameter("chemin", "Texte")},
                generic("Liste", {named("Texte")})));
        for (const char *name : {"est_absolu", "est_relatif"})
        {
            export_callable(
                exports,
                name,
                callable({parameter("chemin", "Texte")}, named("Logique")));
        }
        export_callable(
            exports,
            "joindre",
            variadic_callable(
                {parameter("premier_segment", "Texte")},
                named("Texte"),
                named("Texte")));
        return exports;
    }

    if (module_name == "Aléatoire" || module_name == "Aleatoire")
    {
        export_callable(
            exports,
            "graine",
            callable({parameter("graine", "Entier")}, named("Rien")));
        export_callable(
            exports,
            "entier",
            callable(
                {
                    parameter("minimum", "Entier"),
                    parameter("maximum", "Entier"),
                },
                named("Entier")));
        export_callable(
            exports,
            "décimal",
            callable({}, named("Décimal")));
        export_callable(
            exports,
            "décimal_entre",
            callable(
                {
                    parameter("minimum", "Décimal"),
                    parameter("maximum", "Décimal"),
                },
                named("Décimal")));
        export_callable(
            exports,
            "choisir",
            callable(
                {parameter("valeurs", "Universel")},
                named("Universel")));
        for (const char *name : {"mélanger", "échantillon"})
        {
            export_callable(
                exports,
                name,
                name == std::string_view("mélanger")
                    ? callable(
                          {parameter("valeurs", "Universel")},
                          generic("Liste", {named("Universel")}))
                    : callable(
                          {
                              parameter("valeurs", "Universel"),
                              parameter("nombre", "Entier"),
                          },
                          generic("Liste", {named("Universel")})));
        }
        return exports;
    }

    if (module_name == "LumiNet")
    {
        for (const char *name :
             {"HTTP", "Canal", "TCP", "UDP", "DNS", "Adresse",
              "AdresseRéseau", "ConnexionTCP", "ServeurTCP",
              "SocketUDP", "PaquetUDP", "RéponseHTTP",
              "RequêteHTTP", "RéponseServeurHTTP", "ServeurHTTP",
              "CanalClient", "ServeurCanal",
              "ErreurAdresse", "ErreurDNS", "ErreurConnexion",
              "ErreurDélai", "ErreurIO", "ErreurProtocole",
              "ErreurHTTP"})
        {
            exports.types.emplace(name, SemanticTypeKind::CLASS);
        }
        for (const char *name :
             {"HTTP", "Canal", "TCP", "UDP", "DNS", "Adresse"})
        {
            export_value(exports, name, named(name));
        }
        for (const char *name :
             {"ErreurAdresse", "ErreurDNS", "ErreurConnexion",
              "ErreurDélai", "ErreurIO", "ErreurProtocole",
              "ErreurHTTP"})
        {
            exports.values.emplace(name, SemanticSymbolKind::CLASS);
            exports.error_types.insert(name);
        }

        const auto result = [](TypeExpr success, const char *error) {
            return generic(
                "Résultat",
                {std::move(success), named(error)});
        };
        export_callable(
            exports,
            "Adresse.analyser",
            callable(
                {parameter("texte", "Texte")},
                result(named("AdresseRéseau"), "ErreurAdresse")));
        export_callable(
            exports,
            "Adresse.locale",
            callable(
                {},
                result(named("AdresseRéseau"), "ErreurAdresse")));
        for (const char *name :
             {"est_valide", "est_ipv4", "est_ipv6", "est_locale"})
        {
            export_callable(
                exports,
                "Adresse." + std::string(name),
                callable({parameter("texte", "Texte")}, named("Logique")));
        }
        export_callable(
            exports,
            "DNS.résoudre",
            callable(
                {parameter("hôte", "Texte")},
                result(named("Texte"), "ErreurDNS")));
        export_callable(
            exports,
            "DNS.résoudre_tous",
            callable(
                {parameter("hôte", "Texte")},
                result(
                    generic("Liste", {named("Texte")}),
                    "ErreurDNS")));
        export_callable(
            exports,
            "DNS.résoudre_inverse",
            callable(
                {parameter("adresse", "Texte")},
                result(named("Texte"), "ErreurDNS")));
        export_callable(
            exports,
            "TCP.connecter",
            callable(
                {
                    parameter("hôte", "Texte"),
                    parameter("port", "Entier"),
                    parameter("délai", "Universel", true),
                },
                result(named("ConnexionTCP"), "ErreurConnexion")));
        export_callable(
            exports,
            "TCP.Serveur",
            callable({}, named("ServeurTCP")));
        export_callable(
            exports,
            "UDP.ouvrir",
            callable(
                {parameter("port", "Entier", true)},
                result(named("SocketUDP"), "ErreurIO")));
        for (const char *name :
             {"obtenir", "créer", "modifier", "supprimer"})
        {
            export_callable(
                exports,
                "HTTP." + std::string(name),
                callable(
                    {
                        parameter("url", "Texte"),
                        parameter("entêtes", "Universel", true),
                        parameter("corps", "Texte", true),
                        parameter("type", "Texte", true),
                        parameter("délai", "Universel", true),
                    },
                    result(named("RéponseHTTP"), "ErreurHTTP")));
        }
        export_callable(
            exports,
            "HTTP.requête",
            callable(
                {
                    parameter("méthode", "Texte"),
                    parameter("url", "Texte"),
                    parameter("entêtes", "Universel", true),
                    parameter("corps", "Texte", true),
                    parameter("type", "Texte", true),
                    parameter("délai", "Universel", true),
                },
                result(named("RéponseHTTP"), "ErreurHTTP")));
        export_callable(
            exports,
            "HTTP.Serveur",
            callable({}, named("ServeurHTTP")));
        export_callable(
            exports,
            "Canal.connecter",
            callable(
                {parameter("url", "Texte")},
                result(named("CanalClient"), "ErreurConnexion")));
        export_callable(
            exports,
            "Canal.Serveur",
            callable({}, named("ServeurCanal")));

        for (const char *name : {"écrire", "écrire_octets"})
        {
            export_callable(
                exports,
                "ConnexionTCP." + std::string(name),
                callable(
                    {parameter("valeur", "Universel")},
                    result(named("Rien"), "ErreurIO")));
        }
        for (const auto &[name, success] :
             std::initializer_list<std::pair<const char *, TypeExpr>>{
                 {"lire", named("Texte")},
                 {"lire_ligne", named("Texte")},
                 {"lire_octets", generic("Liste", {named("Entier")})}})
        {
            export_callable(
                exports,
                "ConnexionTCP." + std::string(name),
                callable({}, result(success, "ErreurIO")));
        }
        export_callable(
            exports,
            "ConnexionTCP.définir_délai",
            callable(
                {parameter("délai", "Universel")},
                result(named("Rien"), "ErreurDélai")));
        export_callable(
            exports,
            "ServeurTCP.écouter",
            callable(
                {
                    parameter("hôte", "Texte"),
                    parameter("port", "Entier"),
                },
                result(named("Rien"), "ErreurIO")));
        export_callable(
            exports,
            "SocketUDP.définir_délai",
            callable(
                {parameter("délai", "Universel")},
                result(named("Rien"), "ErreurDélai")));
        for (const char *name : {"recevoir", "recevoir_octets"})
        {
            export_callable(
                exports,
                "SocketUDP." + std::string(name),
                callable(
                    {},
                    result(named("PaquetUDP"), "ErreurIO")));
        }
        export_callable(
            exports,
            "SocketUDP.envoyer",
            callable(
                {
                    parameter("texte", "Texte"),
                    parameter("hôte", "Texte"),
                    parameter("port", "Entier"),
                },
                result(named("Rien"), "ErreurIO")));
        export_callable(
            exports,
            "SocketUDP.envoyer_octets",
            callable(
                {
                    parameter("octets", "Universel"),
                    parameter("hôte", "Texte"),
                    parameter("port", "Entier"),
                },
                result(named("Rien"), "ErreurIO")));
        export_callable(
            exports,
            "SocketUDP.diffuser",
            callable(
                {
                    parameter("texte", "Texte"),
                    parameter("port", "Entier"),
                },
                result(named("Rien"), "ErreurIO")));
        export_callable(
            exports,
            "ServeurHTTP.écouter",
            callable(
                {
                    parameter("hôte", "Texte"),
                    parameter("port", "Entier"),
                },
                result(named("Rien"), "ErreurHTTP")));
        for (const char *name :
             {"envoyer", "envoyer_json", "envoyer_fichier"})
        {
            export_callable(
                exports,
                "RéponseServeurHTTP." + std::string(name),
                callable(
                    {
                        parameter("statut", "Entier"),
                        parameter("contenu", "Texte"),
                    },
                    result(named("Rien"), "ErreurHTTP")));
        }
        export_callable(
            exports,
            "RéponseServeurHTTP.rediriger",
            callable(
                {
                    parameter("url", "Texte"),
                    parameter("statut", "Entier", true),
                },
                result(named("Rien"), "ErreurHTTP")));
        export_callable(
            exports,
            "CanalClient.attendre",
            callable({}, result(named("Rien"), "ErreurIO")));
        export_callable(
            exports,
            "CanalClient.envoyer",
            callable(
                {parameter("message", "Texte")},
                result(named("Rien"), "ErreurIO")));
        export_callable(
            exports,
            "CanalClient.envoyer_octets",
            callable(
                {parameter("octets", "Universel")},
                result(named("Rien"), "ErreurIO")));
        export_callable(
            exports,
            "CanalClient.fermer",
            callable(
                {
                    parameter("code", "Entier", true),
                    parameter("raison", "Texte", true),
                },
                result(named("Rien"), "ErreurIO")));
        export_callable(
            exports,
            "ServeurCanal.écouter",
            callable(
                {
                    parameter("hôte", "Texte"),
                    parameter("port", "Entier"),
                },
                result(named("Rien"), "ErreurIO")));
        return exports;
    }

    if (module_name == "Collections")
    {
        const TypeExpr universel_list = generic("Liste", {named("Universel")});
        export_callable(
            exports,
            "étendue",
            callable(
                {
                    parameter("début", "Entier"),
                    parameter("fin", "Entier"),
                    parameter("pas", "Entier"),
                },
                generic("Liste", {named("Entier")})));
        export_callable(
            exports,
            "transformer",
            callable(
                {
                    parameter("valeurs", "Universel"),
                    parameter("transformation", "Universel"),
                },
                universel_list));
        export_callable(
            exports,
            "filtrer",
            callable(
                {
                    parameter("valeurs", "Universel"),
                    parameter("prédicat", "Universel"),
                },
                universel_list));
        export_callable(
            exports,
            "réduire",
            callable(
                {
                    parameter("valeurs", "Universel"),
                    parameter("initial", "Universel"),
                    parameter("réduction", "Universel"),
                },
                named("Universel")));
        export_callable(
            exports,
            "trouver",
            callable(
                {
                    parameter("valeurs", "Universel"),
                    parameter("prédicat", "Universel"),
                },
                union_type({named("Universel"), named("Rien")})));
        export_callable(
            exports,
            "position",
            callable(
                {
                    parameter("valeurs", "Universel"),
                    parameter("prédicat", "Universel"),
                },
                union_type({named("Entier"), named("Rien")})));
        for (const char *name : {"tout", "au_moins_un"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("valeurs", "Universel"),
                        parameter("prédicat", "Universel"),
                    },
                    named("Logique")));
        }
        export_callable(
            exports,
            "trier",
            callable({parameter("valeurs", "Universel")}, universel_list));
        export_callable(
            exports,
            "trier_par",
            callable(
                {
                    parameter("valeurs", "Universel"),
                    parameter("clé", "Universel"),
                },
                universel_list));
        export_callable(
            exports,
            "inverser",
            callable({parameter("valeurs", "Universel")}, universel_list));
        return exports;
    }

    if (module_name == "JSON")
    {
        exports.types.emplace("ErreurJSON", SemanticTypeKind::CLASS);
        exports.values.emplace("ErreurJSON", SemanticSymbolKind::CLASS);
        exports.error_types.insert("ErreurJSON");

        const TypeExpr error = named("ErreurJSON");
        const auto result = [&](TypeExpr success) {
            return generic("Résultat", {std::move(success), error});
        };
        export_callable(
            exports,
            "analyser",
            callable({parameter("texte", "Texte")}, result(named("Universel"))));
        export_callable(
            exports,
            "encoder",
            callable({parameter("valeur", "Universel")}, result(named("Texte"))));
        export_callable(
            exports,
            "encoder_indenté",
            callable(
                {
                    parameter("valeur", "Universel"),
                    parameter("espaces", "Entier"),
                },
                result(named("Texte"))));
        return exports;
    }

    if (module_name == "Regex")
    {
        exports.types.emplace("Motif", SemanticTypeKind::CLASS);
        exports.types.emplace("Correspondance", SemanticTypeKind::CLASS);
        exports.types.emplace("ErreurRegex", SemanticTypeKind::CLASS);
        exports.values.emplace("ErreurRegex", SemanticSymbolKind::CLASS);
        exports.error_types.insert("ErreurRegex");

        const TypeExpr motif = named("Motif");
        const TypeExpr correspondance = named("Correspondance");
        const TypeExpr error = named("ErreurRegex");
        export_callable(
            exports,
            "analyser",
            callable(
                {parameter("source", "Texte")},
                generic("Résultat", {motif, error})));
        export_callable(
            exports,
            "correspond",
            callable(
                {
                    parameter("motif", "Motif"),
                    parameter("texte", "Texte"),
                },
                named("Logique")));
        export_callable(
            exports,
            "chercher",
            callable(
                {
                    parameter("motif", "Motif"),
                    parameter("texte", "Texte"),
                },
                union_type({correspondance, named("Rien")})));
        export_callable(
            exports,
            "trouver_tous",
            callable(
                {
                    parameter("motif", "Motif"),
                    parameter("texte", "Texte"),
                },
                generic("Liste", {correspondance})));
        for (const char *name : {"remplacer", "remplacer_tout"})
        {
            export_callable(
                exports,
                name,
                callable(
                    {
                        parameter("motif", "Motif"),
                        parameter("texte", "Texte"),
                        parameter("remplacement", "Texte"),
                    },
                    named("Texte")));
        }
        return exports;
    }

    if (module_name == "LumiDessin")
    {
        exports.types.emplace("Canevas", SemanticTypeKind::CLASS);
        exports.types.emplace("Crayon", SemanticTypeKind::CLASS);
        exports.types.emplace("Couleur", SemanticTypeKind::CLASS);
        exports.types.emplace("Image", SemanticTypeKind::CLASS);
        exports.types.emplace("Point", SemanticTypeKind::CLASS);
        exports.types.emplace("Dimensions", SemanticTypeKind::CLASS);
        exports.types.emplace("ErreurImage", SemanticTypeKind::CLASS);
        exports.values.emplace("ErreurImage", SemanticSymbolKind::CLASS);
        exports.error_types.insert("ErreurImage");
        exports.types.emplace("ErreurCouleur", SemanticTypeKind::CLASS);
        exports.values.emplace("ErreurCouleur", SemanticSymbolKind::CLASS);
        exports.error_types.insert("ErreurCouleur");

        // Object and Crayon method calls (canevas.largeur(), point.x, ...)
        // are resolved dynamically, like every other native object's
        // methods in this codebase (compare Temps.DateHeure/Fuseau, whose
        // methods also have no exports here) -- only module-level members
        // need a static signature.

        export_callable(
            exports,
            "canevas",
            callable(
                {
                    parameter("largeur", "Entier"),
                    parameter("hauteur", "Entier"),
                },
                named("Canevas")));
        export_callable(
            exports,
            "fenêtre",
            callable(
                {
                    parameter("largeur", "Entier"),
                    parameter("hauteur", "Entier"),
                    parameter("titre", "Texte"),
                },
                named("Canevas")));
        export_callable(
            exports,
            "point",
            callable(
                {
                    parameter("x", "Décimal"),
                    parameter("y", "Décimal"),
                },
                named("Point")));
        export_callable(
            exports,
            "couleur",
            callable(
                {
                    parameter("rouge", "Entier"),
                    parameter("vert", "Entier"),
                    parameter("bleu", "Entier"),
                    parameter("alpha", "Entier", true),
                },
                named("Couleur")));
        export_callable(
            exports,
            "couleur_hex",
            callable(
                {parameter("valeur", "Texte")},
                generic("Résultat", {named("Couleur"), named("ErreurCouleur")})));
        export_value(exports, "Couleurs", named("Universel"));
        export_callable(
            exports,
            "charger_image",
            callable(
                {parameter("chemin", "Texte")},
                generic("Résultat", {named("Image"), named("ErreurImage")})));

        // Canevas.enregistrer_png is the one Canevas *method* (as opposed to
        // module-level function) that needs a static signature: every other
        // method call resolves dynamically (see the comment above), but a
        // method's return type must be statically known as Résultat[...]
        // for 'agir selon' to accept Succès/Échec patterns against it
        // (semantic_analysis.cpp's resolve_match). The dotted key mirrors
        // how semantic_analysis.cpp's callable_signature() looks up a
        // method: object_type->name() + "." + member_name, which for an
        // imported module becomes "<alias>." + this export's own key --
        // i.e. "LumiDessin." + "Canevas.enregistrer_png" ==
        // "LumiDessin.Canevas.enregistrer_png", exactly what a Canevas
        // receiver's type name resolves to.
        exports.callables.emplace(
            "Canevas.enregistrer_png",
            callable(
                {parameter("chemin", "Texte")},
                generic("Résultat", {named("Rien"), named("ErreurImage")})));

        return exports;
    }

    if (module_name == "LumiTest")
    {
        return exports;
    }

    return std::nullopt;
}

} // namespace lumiere
