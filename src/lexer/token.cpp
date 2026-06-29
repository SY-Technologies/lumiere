#include "lumiere/lexer/token.hpp"
#include <unordered_map>

namespace lumiere
{

    std::string Token::to_string() const
    {
        // making this static to avoid reallocation multiple times within the same context
        static const std::unordered_map<TokenType, std::string> names = {
            // Literals
            {TokenType::ENTIER_LIT, "ENTIER_LIT"},
            {TokenType::DECIMAL_LIT, "DECIMAL_LIT"},
            {TokenType::TEXTE_LIT, "TEXTE_LIT"},
            {TokenType::SYMBOLE_LIT, "SYMBOLE_LIT"},
            {TokenType::VRAI, "VRAI"},
            {TokenType::FAUX, "FAUX"},
            {TokenType::RIEN, "RIEN"},

            // Keywords — declarations
            {TokenType::SOIT, "SOIT"},
            {TokenType::FIXE, "FIXE"},
            {TokenType::FONCTION, "FONCTION"},
            {TokenType::RETOURNE, "RETOURNE"},
            {TokenType::CLASSE, "CLASSE"},
            {TokenType::INTERFACE, "INTERFACE"},
            {TokenType::REALISE, "REALISE"},
            {TokenType::REMPLACE, "REMPLACE"},
            {TokenType::PUBLIC, "PUBLIC"},
            {TokenType::PRIVE, "PRIVE"},

            // Keywords — control flow
            {TokenType::SI, "SI"},
            {TokenType::SINON, "SINON"},
            {TokenType::POUR, "POUR"},
            {TokenType::CHAQUE, "CHAQUE"},
            {TokenType::DANS, "DANS"},
            {TokenType::TANT_QUE, "TANT_QUE"},
            {TokenType::AGIR_SELON, "AGIR_SELON"},
            {TokenType::ARRETER, "ARRETER"},
            {TokenType::CONTINUER, "CONTINUER"},

            // Keywords — error handling
            {TokenType::ESSAYER, "ESSAYER"},
            {TokenType::ATTRAPER, "ATTRAPER"},
            {TokenType::FINALEMENT, "FINALEMENT"},
            {TokenType::LANCER, "LANCER"},

            // Keywords — other
            {TokenType::ICI, "ICI"},
            {TokenType::PARENT, "PARENT"},
            {TokenType::EN, "EN"},
            {TokenType::IMPORTER, "IMPORTER"},
            {TokenType::COMME, "COMME"},
            {TokenType::EST, "EST"},

            // Operators — arithmetic
            {TokenType::PLUS, "PLUS"},
            {TokenType::MOINS, "MOINS"},
            {TokenType::ETOILE, "ETOILE"},
            {TokenType::SLASH, "SLASH"},
            {TokenType::MODULO, "MODULO"},

            // Operators — comparison
            {TokenType::EGAL_EGAL, "EGAL_EGAL"},
            {TokenType::BANG_EGAL, "BANG_EGAL"},
            {TokenType::INFERIEUR, "INFERIEUR"},
            {TokenType::INFERIEUR_EGAL, "INFERIEUR_EGAL"},
            {TokenType::SUPERIEUR, "SUPERIEUR"},
            {TokenType::SUPERIEUR_EGAL, "SUPERIEUR_EGAL"},

            // Operators — assignment
            {TokenType::EGAL, "EGAL"},

            // Operators — logical
            {TokenType::ET, "ET"},
            {TokenType::OU, "OU"},
            {TokenType::NON, "NON"},

            // Operators — set
            {TokenType::PIPE, "PIPE"},
            {TokenType::AMPERSAND, "AMPERSAND"},

            // Operators — misc
            {TokenType::FLECHE, "FLECHE"},
            {TokenType::POINT, "POINT"},
            {TokenType::VIRGULE, "VIRGULE"},
            {TokenType::DEUX_POINTS, "DEUX_POINTS"},

            // Delimiters
            {TokenType::PAREN_OUV, "PAREN_OUV"},
            {TokenType::PAREN_FERM, "PAREN_FERM"},
            {TokenType::ACCOLADE_OUV, "ACCOLADE_OUV"},
            {TokenType::ACCOLADE_FERM, "ACCOLADE_FERM"},
            {TokenType::CROCHET_OUV, "CROCHET_OUV"},
            {TokenType::CROCHET_FERM, "CROCHET_FERM"},

            // Identifiers
            {TokenType::IDENT, "IDENT"},

            // Special
            {TokenType::FIN_FICHIER, "FIN_FICHIER"},
            {TokenType::ERREUR, "ERREUR"},
        };

        auto it = names.find(type);
        std::string name = (it != names.end()) ? it->second : "INCONNU";// it->first is the key, it->second is the value

        switch (type)
        {
        case TokenType::IDENT:
        case TokenType::ENTIER_LIT:
        case TokenType::DECIMAL_LIT:
        case TokenType::TEXTE_LIT:
        case TokenType::SYMBOLE_LIT:
        case TokenType::ERREUR:
        //including the lexeme for the above as their names are not self explanatory
            return name + "(" + lexeme + ")";
        default:
            return name;
        }
    }

} // namespace lumiere
