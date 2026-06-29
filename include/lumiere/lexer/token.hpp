#pragma once
#include <string>



namespace lumiere
{
//https://www.univ-orleans.fr/lifo/Members/Mirian.Halfeld/Cours/TLComp/l3-0708-LexA.pdf
//This document was helpful for brushing up on these concepts

    enum class TokenType
    {
        // Literals
        ENTIER_LIT,
        DECIMAL_LIT,
        TEXTE_LIT,
        SYMBOLE_LIT,
        VRAI,
        FAUX,
        RIEN,

        // Keywords — declarations
        SOIT,
        FIXE,
        FONCTION,
        RETOURNE,
        CLASSE,
        INTERFACE,
        REALISE,
        REMPLACE,
        PUBLIC,
        PRIVE,

        // Keywords — control flow
        SI,
        SINON,
        POUR,
        CHAQUE,
        DANS,
        TANT_QUE,
        AGIR_SELON,
        ARRETER,
        CONTINUER,

        // Keywords — error handling
        ESSAYER,
        ATTRAPER,
        FINALEMENT,
        LANCER,

        // Keywords — other
        ICI,
        PARENT,
        EN,
        IMPORTER,
        COMME,
        EST,

        // Operators — arithmetic
        PLUS,
        MOINS,
        ETOILE,
        SLASH,
        MODULO,

        // Operators — comparison
        EGAL_EGAL,
        BANG_EGAL,
        INFERIEUR,
        INFERIEUR_EGAL,
        SUPERIEUR,
        SUPERIEUR_EGAL,

        // Operators — assignment
        EGAL,

        // Operators — logical
        ET,
        OU,
        NON,

        // Operators — set
        PIPE,
        AMPERSAND,

        // Operators — misc
        FLECHE,
        POINT,
        VIRGULE,
        DEUX_POINTS,

        // Delimiters
        PAREN_OUV,
        PAREN_FERM,
        ACCOLADE_OUV,
        ACCOLADE_FERM,
        CROCHET_OUV,
        CROCHET_FERM,

        // Identifiers
        IDENT,

        // Special
        FIN_FICHIER,
        ERREUR,
    };

    struct Token
    {
        TokenType type;
        std::string lexeme;
        uint32_t line;
        uint32_t column;

        Token(TokenType type, std::string lexeme, u_int32_t line, uint32_t column)
            : type(type), lexeme(std::move(lexeme)), line(line), column(column) {}
        // A std::string internally owns a heap-allocated buffer.
        //  I choose to move it to avoid allocating a new buffer and copying every character into it.

        std::string to_string() const;
    };
}
