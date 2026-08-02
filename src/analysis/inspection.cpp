#include "lumiere/analysis/inspection.hpp"

#include "lumiere/analysis/analysis.hpp"
#include "lumiere/analysis/semantic_analysis.hpp"
#include "lumiere/lexer/lexer.hpp"
#include "lumiere/parser/ast.hpp"

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

/// Human-readable documentation for common global builtins. The compiler knows
/// these functions without a source declaration; this registry lets hover show
/// complete usage information even though they are never defined in the file.
struct BuiltinDocumentation
{
    std::string_view name;
    std::string_view kind;
    std::string_view signature;
    std::string_view return_type;
    std::string_view documentation;
};

const std::vector<BuiltinDocumentation> &builtin_documentation()
{
    static const std::vector<BuiltinDocumentation> registry = {
        {"afficher",
         "fonction",
         "afficher(texte : Texte) -> Rien",
         "Rien",
         "Écrit la représentation en texte d'une valeur sur la sortie standard, puis passe à la ligne suivante."},
        {"lire",
         "fonction",
         "lire() -> Résultat[Texte, ErreurEntrée]",
         "Résultat[Texte, ErreurEntrée]",
         "Lit une ligne entière depuis l'entrée standard et la retourne. Échoue avec une ErreurEntrée en fin de flux."},
        {"lire_entier",
         "fonction",
         "lire_entier() -> Résultat[Entier, ErreurEntrée]",
         "Résultat[Entier, ErreurEntrée]",
         "Lit une ligne depuis l'entrée standard et la convertit en Entier. Échoue avec une ErreurEntrée si la valeur n'est pas un entier valide."},
        {"lire_décimal",
         "fonction",
         "lire_décimal() -> Résultat[Décimal, ErreurEntrée]",
         "Résultat[Décimal, ErreurEntrée]",
         "Lit une ligne depuis l'entrée standard et la convertit en Décimal. Échoue avec une ErreurEntrée si la valeur n'est pas un entier décimal valide."},
        {"lire_decimal",
         "fonction",
         "lire_decimal() -> Résultat[Décimal, ErreurEntrée]",
         "Résultat[Décimal, ErreurEntrée]",
         "Variante sans accent de lire_décimal() : lit une ligne et la convertit en Décimal."},
        {"lire_logique",
         "fonction",
         "lire_logique() -> Résultat[Logique, ErreurEntrée]",
         "Résultat[Logique, ErreurEntrée]",
         "Lit une ligne depuis l'entrée standard ('vrai' ou 'faux') et la convertit en Logique. Échoue en cas de valeur invalide."},
        {"Succès",
         "constructeur",
         "Succès(payload : T) -> Résultat[T, E]",
         "Résultat[T, E]",
         "Construit un Résultat réussi contenant la valeur donnée. Charge le type d'erreur du contexte."},
        {"Échec",
         "constructeur",
         "Échec(erreur : E) -> Résultat[T, E]",
         "Résultat[T, E]",
         "Construit un Résultat en échec portant une valeur d'erreur réalisant Erreur. Charge le type attendu du contexte."},
        {"ignorer",
         "opérateur",
         "ignorer <résultat>",
         "",
         "Ignore explicitement le résultat d'une opération et arrête de le propager, en exigeant que la valeur soit bien un Résultat."},
        {"propager",
         "mot-clé",
         "expression ou propager",
         "",
         "Propage l'échec d'une expression Résultat : si elle échoue, la fonction courante se termine en retournant l'erreur ; sinon, unwra le valeur russie."},
    };
    return registry;
}

const std::vector<BuiltinDocumentation> &module_documentation()
{
    static const std::vector<BuiltinDocumentation> registry = {
        // Maths
        {"pi", "constante", "pi", "Décimal", "La constante mathématique π (environ 3,14159)."},
        {"e", "constante", "e", "Décimal", "La constante d'Euler, base des logarithmes naturels (environ 2,71828)."},
        {"infini", "constante", "infini", "Décimal", "Représente l'infini positif en arithmétique décimale."},
        {"non_nombre", "constante", "non_nombre", "Décimal", "La valeur spéciale NaN (not a number), résultat d'un calcul indéterminé."},
        {"absolu", "fonction", "absolu(valeur : Décimal) -> Entier | Décimal", "Entier | Décimal", "Retourne la valeur absolue (distance à zéro) d'un nombre."},
        {"abs", "fonction", "abs(valeur : Décimal) -> Entier | Décimal", "Entier | Décimal", "Alias sans accent de absolu : valeur absolue d'un nombre."},
        {"min", "fonction", "min(gauche : Décimal, droite : Décimal) -> Entier | Décimal", "Entier | Décimal", "Retourne l'élément minimal de deux valeurs."},
        {"max", "fonction", "max(gauche : Décimal, droite : Décimal) -> Entier | Décimal", "Entier | Décimal", "Retourne l'élément maximal de deux valeurs."},
        {"arrondir", "fonction", "arrondir(valeur : Décimal) -> Entier", "Entier", "Arrondit une décimale à l'entier le plus proche."},
        {"arrondi", "fonction", "arrondi(valeur : Décimal) -> Entier", "Entier", "Alias sans accent de arrondir."},
        {"plancher", "fonction", "plancher(valeur : Décimal) -> Entier", "Entier", "Retourne le plus grand entier inférieur ou égal à la valeur."},
        {"plafond", "fonction", "plafond(valeur : Décimal) -> Entier", "Entier", "Retourne le plus petit entier supérieur ou égal à la valeur."},
        {"tronquer", "fonction", "tronquer(valeur : Décimal) -> Entier", "Entier", "Coupe la partie décimale sans arrondir."},
        {"racine", "fonction", "racine(valeur : Décimal) -> Décimal", "Décimal", "Retourne la racine carrée de la valeur."},
        {"racine_n", "fonction", "racine_n(base : Décimal, n : Décimal) -> Décimal", "Décimal", "Retourne la racine n-ième de la valeur."},
        {"puissance", "fonction", "puissance(base : Décimal, exposant : Décimal) -> Décimal", "Décimal", "Élève une base à un exposant."},
        {"log", "fonction", "log(valeur : Décimal) -> Décimal", "Décimal", "Logarithme naturel (base e) de la valeur."},
        {"log10", "fonction", "log10(valeur : Décimal) -> Décimal", "Décimal", "Logarithme en base 10 de la valeur."},
        {"log2", "fonction", "log2(valeur : Décimal) -> Décimal", "Décimal", "Logarithme en base 2 de la valeur."},
        {"sin", "fonction", "sin(valeur : Décimal) -> Décimal", "Décimal", "Sinus d'un angle exprimé en radians."},
        {"sinus", "fonction", "sinus(valeur : Décimal) -> Décimal", "Décimal", "Alias sans accent de sin."},
        {"cos", "fonction", "cos(valeur : Décimal) -> Décimal", "Décimal", "Cosinus d'un angle exprimé en radians."},
        {"cosinus", "fonction", "cosinus(valeur : Décimal) -> Décimal", "Décimal", "Alias sans accent de cos."},
        {"tan", "fonction", "tan(valeur : Décimal) -> Décimal", "Décimal", "Tangente d'un angle exprimé en radians."},
        {"tangente", "fonction", "tangente(valeur : Décimal) -> Décimal", "Décimal", "Alias sans accent de tan."},
        {"arctan", "fonction", "arctan(valeur : Décimal) -> Décimal", "Décimal", "Arc tangente d'une valeur."},
        {"acos", "fonction", "acos(valeur : Décimal) -> Décimal", "Décimal", "Cosinus inverse (arccosinus) d'une valeur."},
        {"asin", "fonction", "asin(valeur : Décimal) -> Décimal", "Décimal", "Sinus inverse (arcsinus) d'une valeur."},
        {"atan", "fonction", "atan(valeur : Décimal) -> Décimal", "Décimal", "Tangente inverse (arctangente) d'une valeur."},
        {"atan2", "fonction", "atan2(y : Décimal, x : Décimal) -> Décimal", "Décimal", "Arc tangente de y/x en tenant compte du quadrant."},
        {"degres_vers_radians", "fonction", "degres_vers_radians(valeur : Décimal) -> Décimal", "Décimal", "Convertit des degrés en radians."},
        {"radians_vers_degres", "fonction", "radians_vers_degres(valeur : Décimal) -> Décimal", "Décimal", "Convertit des radians en degrés."},
        {"est_non_nombre", "fonction", "est_non_nombre(valeur : Décimal) -> Logique", "Logique", "Retourne vrai si la valeur est NaN."},
        {"est_infini", "fonction", "est_infini(valeur : Décimal) -> Logique", "Logique", "Retourne vrai si la valeur est infinie."},
        {"est_pair", "fonction", "est_pair(valeur : Entier) -> Logique", "Logique", "Retourne vrai si l'entier est pair."},
        {"est_impair", "fonction", "est_impair(valeur : Entier) -> Logique", "Logique", "Retourne vrai si l'entier est impair."},

        // Texte (free functions)
        {"joindre", "fonction", "joindre(valeurs : Liste[Texte], séparateur : Texte) -> Texte", "Texte", "Assemble une liste de textes en un seul texte, en les séparant par séparateur."},
        {"convertir_entier", "fonction", "convertir_entier(valeur : Entier) -> Texte", "Texte", "Convertit un entier en sa représentation textuelle décimale."},
        {"convertir_decimal", "fonction", "convertir_decimal(valeur : Décimal) -> Texte", "Texte", "Convertit une décimale en sa représentation textuelle."},
        {"convertir_logique", "fonction", "convertir_logique(valeur : Logique) -> Texte", "Texte", "Convertit un booléen en 'vrai' ou 'faux'."},

        // Chemin
        {"separateur", "constante", "separateur", "Texte", "Le séparateur de dossier du système courant ('/' sur Unix, '\\\\' sur Windows)."},
        {"dossier_courant", "fonction", "dossier_courant() -> Texte", "Texte", "Retourne le chemin complet du dossier de travail courant."},
        {"absolu", "fonction", "absolu(chemin : Texte) -> Texte", "Texte", "Convertit un chemin relatif en chemin absolu."},
        {"nom", "fonction", "nom(chemin : Texte) -> Texte", "Texte", "Retourne le nom final du fichier ou dossier."},
        {"nom_sans_extension", "fonction", "nom_sans_extension(chemin : Texte) -> Texte", "Texte", "Retourne le nom du fichier sans son extension."},
        {"extension", "fonction", "extension(chemin : Texte) -> Texte", "Texte", "Retourne l'extension (avec le point) d'un fichier."},
        {"dossier", "fonction", "dossier(chemin : Texte) -> Texte", "Texte", "Retourne le chemin du dossier contenant un chemin."},
        {"normaliser", "fonction", "normaliser(chemin : Texte) -> Texte", "Texte", "Normalise un chemin (résout les points et les dossiers redondants)."},
        {"parties", "fonction", "parties(chemin : Texte) -> Liste de Texte", "Liste de Texte", "Découpe un chemin en la liste de ses segments."},
        {"est_absolu", "fonction", "est_absolu(chemin : Texte) -> Logique", "Logique", "Retourne vrai si le chemin est absolu."},
        {"est_relatif", "fonction", "est_relatif(chemin : Texte) -> Logique", "Logique", "Retourne vrai si le chemin est relatif."},

        // Aléatoire
        {"graine", "fonction", "graine(graine : Entier) -> Rien", "Rien", "Fixe le germe du générateur pseudo-aléatoire pour obtenir des séquences reproductibles."},
        {"entier", "fonction", "entier(minimum : Entier, maximum : Entier) -> Entier", "Entier", "Retourne un entier pseudo-aléatoire entre minimum et maximum inclus."},
        {"décimal", "fonction", "décimal() -> Décimal", "Décimal", "Retourne un nombre décimal pseudo-aléatoire entre 0 (inclut) et 1 (exclu)."},
        {"décimal_entre", "fonction", "décimal_entre(minimum : Décimal, maximum : Décimal) -> Décimal", "Décimal", "Retourne un nombre décimal pseudo-aléatoire entre deux bornes."},
        {"choisir", "fonction", "choisir(valeurs) -> Universel", "Universel", "Retourne un élément aléatoire d'une séquence."},
        {"mélanger", "fonction", "mélanger(valeurs) -> Liste", "Liste", "Retourne une nouvelle copie d'une liste dont les éléments ont été mélangés."},
        {"échantillon", "fonction", "échantillon(valeurs, nombre : Entier) -> Liste", "Liste", "Retourne un nombre donné d'éléments aléatoires distincts d'une séquence."},

        // LumiTest
        {"test", "fonction", "test(description : Texte, corps) -> Rien", "Rien", "Déclare un test avec une description, dont le corps doit réussir sans lancer."},
        {"groupe", "fonction", "groupe(nom : Texte, corps) -> Rien", "Rien", "Regroupe plusieurs tests sous un nom commun pour l'affichage."},
        {"avant_tout", "fonction", "avant_tout(corps) -> Rien", "Rien", "Exécute le corps une fois avant tous les tests du groupe."},
        {"avant_chaque", "fonction", "avant_chaque(corps) -> Rien", "Rien", "Exécute le corps avant chaque test du groupe."},
        {"après_chaque", "fonction", "après_chaque(corps) -> Rien", "Rien", "Exécute le corps après chaque test du groupe."},
        {"après_tout", "fonction", "après_tout(corps) -> Rien", "Rien", "Exécute le corps après tous les tests du groupe."},
        {"vérifier", "fonction", "vérifier(condition : Logique) -> Rien", "Rien", "Fait échouer le test si la condition est fausse."},
        {"vérifier_égal", "fonction", "vérifier_égal(attendu, obtenu) -> Rien", "Rien", "Échoue le test si attendu et obtenu diffèrent."},
        {"vérifier_différent", "fonction", "vérifier_différent(a, b) -> Rien", "Rien", "Échoue le test si a et b sont égaux."},
        {"vérifier_lance", "fonction", "vérifier_lance() -> Rien", "Rien", "Échoue le test si le corps suivant ne lance pas une erreur."},
        {"vérifier_contient", "fonction", "vérifier_contient(collection, élément) -> Rien", "Rien", "Échoue le test si la collection ne contient pas l'élément."},
        {"vérifier_approx", "fonction", "vérifier_approx(attendu : Décimal, obtenu : Décimal) -> Rien", "Rien", "Échoue le test si deux décimales ne sont pas approchées à une petite tolérance."},

        {"indisponible", "constante", "indisponible", "Dans une autre dimension", "Valeur de repli quand une fonctionnalité (ex. LumiNet) n'est pas disponible sur la plateforme."},
    };
    return registry;
}

/// Documentation entries for methods called with the member syntax `objet.méthode(...)`.
/// Keyed by `\"Type.méthode\"`.
const std::vector<BuiltinDocumentation> &member_documentation()
{
    static const std::vector<BuiltinDocumentation> registry = {
        // Texte.méthodes
        {"Texte.taille", "méthode", "taille() -> Entier", "Entier", "Retourne le nombre de caractères du texte."},
        {"Texte.est_vide", "méthode", "est_vide() -> Logique", "Logique", "Retourne vrai si le texte ne contient aucun caractère."},
        {"Texte.contient", "méthode", "contient(recherche : Texte) -> Logique", "Logique", "Retourne vrai si le texte contient le sous-texte recherché."},
        {"Texte.index_de", "méthode", "index_de(recherche : Texte) -> Entier", "Entier", "Retourne la position de la première occurrence de recherche dans le texte."},
        {"Texte.commence_par", "méthode", "commence_par(prefixe : Texte) -> Logique", "Logique", "Retourne vrai si le texte commence par la préfixe donnée."},
        {"Texte.finit_par", "méthode", "finit_par(suffixe : Texte) -> Logique", "Logique", "Retourne vrai si le texte se termine par le suffixe donnée."},
        {"Texte.separer", "méthode", "separer(séparateur : Texte) -> Liste de Texte", "Liste de Texte", "Découpe le texte en segments selon un séparateur."},
        {"Texte.separer_lignes", "méthode", "separer_lignes() -> Liste de Texte", "Liste de Texte", "Découpe le texte en lignes à chaque saut de ligne."},
        {"Texte.remplacer", "méthode", "remplacer(recherche : Texte, remplacement : Texte) -> Texte", "Texte", "Remplace la première occurrence de recherche par remplacement."},
        {"Texte.remplacer_tout", "méthode", "remplacer_tout(recherche : Texte, remplacement : Texte) -> Texte", "Texte", "Remplace toutes les occurrences de recherche par remplacement."},
        {"Texte.elaguer", "méthode", "elaguer() -> Texte", "Texte", "Retire les espaces et tabulations aux deux extrémités du texte."},
        {"Texte.elaguer_gauche", "méthode", "elaguer_gauche() -> Texte", "Texte", "Retire les espaces et tabulations au début du texte."},
        {"Texte.elaguer_droite", "méthode", "elaguer_droite() -> Texte", "Texte", "Retire les espaces et tabulations en fin du texte."},
        {"Texte.minuscules", "méthode", "minuscules() -> Texte", "Texte", "Convertit tous les caractères en minuscules."},
        {"Texte.majuscules", "méthode", "majuscules() -> Texte", "Texte", "Convertit tous les caractères en majuscules."},
        {"Texte.inverser", "méthode", "inverser() -> Texte", "Texte", "Inverse l'ordre des caractères du texte."},
        {"Texte.repeter", "méthode", "repeter(nombre : Entier) -> Texte", "Texte", "Repète le texte un nombre donné de fois, bout à bout."},
        {"Texte.inserer", "méthode", "inserer(position : Entier, ajout : Texte) -> Texte", "Texte", "Insère un texte à la position donnée (zéro-indexé)."},
        {"Texte.supprimer", "méthode", "supprimer(position : Entier, longueur : Entier) -> Texte", "Texte", "Retire longueur caractères à partir de la position donnée."},
        {"Texte.sous_texte", "méthode", "sous_texte(début : Entier, longueur : Entier) -> Texte", "Texte", "Extrait une sous-chaîne à partir d'une position et d'une longueur."},
        {"Texte.en_entier", "méthode", "en_entier() -> Résultat[Entier, ErreurConversion]", "Résultat", "Tente de convertir le texte en Entier, retourne un Résultat."},
        {"Texte.en_decimal", "méthode", "en_decimal() -> Résultat[Décimal, ErreurConversion]", "Résultat", "Tente de convertir le texte en Décimal, retourne un Résultat."},
        {"Texte.en_logique", "méthode", "en_logique() -> Résultat[Logique, ErreurConversion]", "Résultat", "Tente de convertir le texte en Logique ('vrai'/'faux'), retourne un Résultat."},

        // Liste.méthodes (présentes sur toute séquence)
        {"Liste.taille", "méthode", "taille() -> Entier", "Entier", "Retourne le nombre d'éléments de la liste."},
        {"Liste.est_vide", "méthode", "est_vide() -> Logique", "Logique", "Retourne vrai si la liste ne contient aucun élément."},
        {"Liste.ajouter", "méthode", "ajouter(élément) -> Aucun", "Aucun", "Ajoute un élément à la fin de la liste."},
        {"Liste.inserer", "méthode", "insérer(position : Entier, valeur) -> Aucun", "Aucun", "Insère une valeur à l'indice donné."},
        {"Liste.supprimer", "méthode", "supprimer(position : Entier) -> Aucun", "Aucun", "Retire l'élément à l'indice donné."},
        {"Liste.premier", "méthode", "premier() -> Aucun", "Aucun", "Retourne le premier élément de la liste."},
        {"Liste.dernier", "méthode", "dernier() -> Aucun", "Aucun", "Retourne le dernier élément de la liste."},
        {"Liste.contient", "méthode", "contient(valeur) -> Logique", "Logique", "Retourne vrai si la liste contient la valeur."},
        {"Liste.index_de", "méthode", "indice(valeur) -> Entier", "Entier", "Retourne la position de la valeur dans la liste."},
        {"Liste.trier", "méthode", "trier() -> Aucun", "Aucun", "Trie les éléments de la liste par ordre croissant."},
        {"Liste.inverser", "méthode", "inverser() -> Aucun", "Aucun", "Inverse l'ordre des éléments sur place."},
    };
    return registry;
}

/// Returns documentation for a well-known global builtin, or nothing.
std::optional<Inspection> builtin_inspection(const std::string &name,
                                             const std::size_t start_offset,
                                             const std::size_t end_offset)
{
    const auto &registry = builtin_documentation();
    const auto found = std::find_if(registry.begin(), registry.end(),
                                    [&](const BuiltinDocumentation &entry)
                                    { return entry.name == name; });
    if (found == registry.end())
    {
        const auto &module_registry = module_documentation();
        const auto module_found = std::find_if(module_registry.begin(), module_registry.end(),
                                               [&](const BuiltinDocumentation &entry)
                                               { return entry.name == name; });
        if (module_found == module_registry.end())
        {
            return std::nullopt;
        }
        Inspection inspection;
        inspection.label = name;
        inspection.kind = std::string(module_found->kind);
        inspection.signature = std::string(module_found->signature);
        inspection.return_type = std::string(module_found->return_type);
        inspection.documentation = std::string(module_found->documentation);
        inspection.start_offset = start_offset;
        inspection.end_offset = end_offset;
        return inspection;
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
    const auto &registry = member_documentation();
    const auto found = std::find_if(registry.begin(), registry.end(),
                                    [&](const BuiltinDocumentation &entry)
                                    { return entry.name == qualified; });
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
    if (analysis.has_errors())
    {
        return builtin;
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