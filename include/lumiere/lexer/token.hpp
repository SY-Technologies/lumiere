#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace lumiere
{
// https://www.univ-orleans.fr/lifo/Members/Mirian.Halfeld/Cours/TLComp/l3-0708-LexA.pdf
// This document was helpful for brushing up on these concepts.

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
        TYPE,
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

        // Keywords — other
        ICI,
        PARENT,
        EN,
        IMPORTER,
        COMME,
        EST,
        PROPAGER,
        IGNORER,

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
        DOCUMENTATION,
    };

    struct Token
    {
        TokenType type;
        std::string lexeme;
        // Where the token begins. A token used to carry both this and the
        // position just past its last character, and everything that draws a
        // caret reached for the wrong one: an error about `valeur` pointed at
        // the space after it. The span is start_offset..end_offset; there is
        // one line and column, and it is the first one.
        uint32_t line;
        uint32_t column;
        std::size_t start_offset;
        std::size_t end_offset;

        Token(TokenType type,
              std::string lexeme,
              uint32_t line,
              uint32_t column,
              std::size_t start_offset = 0,
              std::size_t end_offset = 0)
            : type(type),
              lexeme(std::move(lexeme)),
              line(line),
              column(column),
              start_offset(start_offset),
              end_offset(end_offset) {}
        // A std::string internally owns a heap-allocated buffer.
        //  I choose to move it to avoid allocating a new buffer and copying every character into it.

        std::string to_string() const;
    };
}
