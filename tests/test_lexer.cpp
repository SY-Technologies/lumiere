#include <gtest/gtest.h>

#include <vector>

#include "lumiere/lexer/lexer.hpp"
#include "lumiere/lexer/token.hpp"

namespace
{

using lumiere::Lexer;
using lumiere::Token;
using lumiere::TokenType;

std::vector<Token> lex(const std::string &source)
{
    Lexer lexer(source);
    return lexer.tokenise();
}

TEST(LexerComments, SupportsJavaAndJavaScriptComments)
{
    const std::vector<Token> tokens = lex("// line comment\n/* block comment */+\n");

    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::PLUS);
    EXPECT_EQ(tokens[1].type, TokenType::FIN_FICHIER);
}

TEST(LexerComments, DoesNotTreatTripleDashAsComment)
{
    const std::vector<Token> tokens = lex("---");

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::MOINS);
    EXPECT_EQ(tokens[1].type, TokenType::MOINS);
    EXPECT_EQ(tokens[2].type, TokenType::MOINS);
    EXPECT_EQ(tokens[3].type, TokenType::FIN_FICHIER);
}

TEST(LexerComments, IgnoresUnterminatedBlockCommentUntilEndOfFile)
{
    const std::vector<Token> tokens = lex("/* commentaire jamais ferme");

    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::FIN_FICHIER);
}

TEST(LexerComments, RecognisesJavadocStyleDocumentation)
{
    const std::vector<Token> tokens = lex(
        "/**\n"
        " * Calcule une valeur.\n"
        " * Retourne le résultat.\n"
        " */\n"
        "fonction calculer() {}\n");

    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].type, TokenType::DOCUMENTATION);
    EXPECT_EQ(tokens[0].lexeme,
              "Calcule une valeur.\nRetourne le résultat.");
    EXPECT_EQ(tokens[1].type, TokenType::FONCTION);
}

TEST(LexerComments, TreatsTripleSlashAsAnOrdinaryLineComment)
{
    const std::vector<Token> tokens = lex(
        "/// Ancienne documentation.\n"
        "fonction calculer() {}\n");

    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0].type, TokenType::FONCTION);
}

TEST(LexerKeywords, RecognisesAgirSelonAsSingleToken)
{
    const std::vector<Token> tokens = lex("agir selon valeur");

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::AGIR_SELON);
    EXPECT_EQ(tokens[1].type, TokenType::IDENT);
    EXPECT_EQ(tokens[2].type, TokenType::FIN_FICHIER);
}

TEST(LexerKeywords, RecognisesTantQueAsSingleToken)
{
    const std::vector<Token> tokens = lex("tant que (vrai)");

    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].type, TokenType::TANT_QUE);
    EXPECT_EQ(tokens[1].type, TokenType::PAREN_OUV);
    EXPECT_EQ(tokens[2].type, TokenType::VRAI);
    EXPECT_EQ(tokens[3].type, TokenType::PAREN_FERM);
    EXPECT_EQ(tokens[4].type, TokenType::FIN_FICHIER);
}

TEST(LexerKeywords, RecognisesParentKeyword)
{
    const std::vector<Token> tokens = lex("parent.nom");

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::PARENT);
    EXPECT_EQ(tokens[1].type, TokenType::POINT);
    EXPECT_EQ(tokens[2].type, TokenType::IDENT);
    EXPECT_EQ(tokens[3].type, TokenType::FIN_FICHIER);
}

TEST(LexerKeywords, RecognisesTypeAliasKeyword)
{
    Lexer lexer("type Lecture = Résultat[Texte, Erreur]\n");
    const auto tokens = lexer.tokenise();

    ASSERT_TRUE(lexer.diagnostics().empty());
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::TYPE);
    EXPECT_EQ(tokens[1].type, TokenType::IDENT);
    EXPECT_EQ(tokens[1].lexeme, "Lecture");
}

TEST(LexerKeywords, RecognisesResultControlKeywords)
{
    const std::vector<Token> tokens =
        lex("opération() ou propager\nignorer autre()");

    ASSERT_GE(tokens.size(), 9u);
    EXPECT_EQ(tokens[3].type, TokenType::OU);
    EXPECT_EQ(tokens[4].type, TokenType::PROPAGER);
    EXPECT_EQ(tokens[5].type, TokenType::IGNORER);
}

TEST(LexerIdentifiers, FormerExceptionKeywordsAreOrdinaryIdentifiers)
{
    const std::vector<Token> tokens = lex("essayer attraper finalement lancer");

    ASSERT_EQ(tokens.size(), 5u);
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(tokens[i].type, TokenType::IDENT) << "token index " << i;
    }
    EXPECT_EQ(tokens[0].lexeme, "essayer");
    EXPECT_EQ(tokens[1].lexeme, "attraper");
    EXPECT_EQ(tokens[2].lexeme, "finalement");
    EXPECT_EQ(tokens[3].lexeme, "lancer");
    EXPECT_EQ(tokens[4].type, TokenType::FIN_FICHIER);
}

TEST(LexerIdentifiers, FormerExceptionKeywordsRemainWholeInsideLongerIdentifiers)
{
    const std::vector<Token> tokens = lex(
        "essayerEncore attraper_erreur finalement2 relancer lancer");

    ASSERT_EQ(tokens.size(), 6u);
    for (std::size_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ(tokens[i].type, TokenType::IDENT) << "token index " << i;
    }
    EXPECT_EQ(tokens[0].lexeme, "essayerEncore");
    EXPECT_EQ(tokens[1].lexeme, "attraper_erreur");
    EXPECT_EQ(tokens[2].lexeme, "finalement2");
    EXPECT_EQ(tokens[3].lexeme, "relancer");
    EXPECT_EQ(tokens[4].lexeme, "lancer");
}

TEST(LexerKeywords, RecognisesSelectiveImportPunctuation)
{
    const std::vector<Token> tokens = lex("importer outils.maths.calcul.{tripler, base comme origine}");

    ASSERT_GE(tokens.size(), 11u);
    EXPECT_EQ(tokens[0].type, TokenType::IMPORTER);
    EXPECT_EQ(tokens[1].type, TokenType::IDENT);
    EXPECT_EQ(tokens[2].type, TokenType::POINT);
    EXPECT_EQ(tokens[3].type, TokenType::IDENT);
    EXPECT_EQ(tokens[4].type, TokenType::POINT);
    EXPECT_EQ(tokens[5].type, TokenType::IDENT);
    EXPECT_EQ(tokens[6].type, TokenType::POINT);
    EXPECT_EQ(tokens[7].type, TokenType::ACCOLADE_OUV);
    EXPECT_EQ(tokens[8].type, TokenType::IDENT);
    EXPECT_EQ(tokens[9].type, TokenType::VIRGULE);
}

TEST(LexerKeywords, RejectsBareAgirWithoutSelon)
{
    const std::vector<Token> tokens = lex("agir vite");

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::ERREUR);
    EXPECT_NE(tokens[0].lexeme.find("agir"), std::string::npos);
    EXPECT_EQ(tokens[1].type, TokenType::IDENT);
    EXPECT_EQ(tokens[1].lexeme, "vite");
}

TEST(LexerKeywords, RejectsBareTantWithoutQue)
{
    const std::vector<Token> tokens = lex("tant vite");

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::ERREUR);
    EXPECT_NE(tokens[0].lexeme.find("tant"), std::string::npos);
    EXPECT_EQ(tokens[1].type, TokenType::IDENT);
    EXPECT_EQ(tokens[1].lexeme, "vite");
}

TEST(LexerKeywords, DoesNotSplitLongerIdentifiersAroundCompositeKeywords)
{
    const std::vector<Token> tokens = lex("agirselon tantque superieur");

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::IDENT);
    EXPECT_EQ(tokens[0].lexeme, "agirselon");
    EXPECT_EQ(tokens[1].type, TokenType::IDENT);
    EXPECT_EQ(tokens[1].lexeme, "tantque");
    EXPECT_EQ(tokens[2].type, TokenType::IDENT);
    EXPECT_EQ(tokens[2].lexeme, "superieur");
    EXPECT_EQ(tokens[3].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, RecognisesIntegerDecimalTextAndSymbolLiterals)
{
    const std::vector<Token> tokens = lex("42 3.14 \"salut\" 'x'");

    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].type, TokenType::ENTIER_LIT);
    EXPECT_EQ(tokens[0].lexeme, "42");
    EXPECT_EQ(tokens[1].type, TokenType::DECIMAL_LIT);
    EXPECT_EQ(tokens[1].lexeme, "3.14");
    EXPECT_EQ(tokens[2].type, TokenType::TEXTE_LIT);
    EXPECT_EQ(tokens[2].lexeme, "\"salut\"");
    EXPECT_EQ(tokens[3].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[3].lexeme, "'x'");
    EXPECT_EQ(tokens[4].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, RecognisesAccentedSymbolLiterals)
{
    const std::vector<Token> tokens = lex("'é' 'à' 'ç' 'ù' 'œ'");

    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[0].lexeme, "'é'");
    EXPECT_EQ(tokens[1].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[1].lexeme, "'à'");
    EXPECT_EQ(tokens[2].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[2].lexeme, "'ç'");
    EXPECT_EQ(tokens[3].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[3].lexeme, "'ù'");
    EXPECT_EQ(tokens[4].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[4].lexeme, "'œ'");
    EXPECT_EQ(tokens[5].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, RejectsSymbolLiteralsWithMultipleUnicodeCodePoints)
{
    const std::vector<Token> tokens = lex("'été'");

    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::ERREUR);
    EXPECT_NE(tokens[0].lexeme.find("un seul caractère Unicode"), std::string::npos);
    EXPECT_EQ(tokens.back().type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, SplitsIntegerFollowedByDotWithoutFractionalDigits)
{
    const std::vector<Token> tokens = lex("12.");

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::ENTIER_LIT);
    EXPECT_EQ(tokens[0].lexeme, "12");
    EXPECT_EQ(tokens[1].type, TokenType::POINT);
    EXPECT_EQ(tokens[2].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, ReportsUnterminatedTextLiteral)
{
    const std::vector<Token> tokens = lex("\"bonjour");

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::ERREUR);
    EXPECT_NE(tokens[0].lexeme.find("texte non terminé"), std::string::npos);
    EXPECT_EQ(tokens[1].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, ReportsUnterminatedSymbolLiteral)
{
    const std::vector<Token> tokens = lex("'a");

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::ERREUR);
    EXPECT_NE(tokens[0].lexeme.find("symbole non terminé"), std::string::npos);
    EXPECT_EQ(tokens[1].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, DecodesStandardEscapeSequencesInTextLiterals)
{
    const std::vector<Token> tokens = lex(R"("a\nb\tc\rd\\e\"f\'g\0h")");

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::TEXTE_LIT);
    // lexeme stays the raw, still-escaped source slice...
    EXPECT_EQ(tokens[0].lexeme, R"("a\nb\tc\rd\\e\"f\'g\0h")");
    // ...decoded is what a runtime Texte is built from.
    EXPECT_EQ(tokens[0].decoded, std::string("a\nb\tc\rd\\e\"f'g\0h", 15));
    EXPECT_EQ(tokens[1].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, DecodesUnicodeEscapesInTextLiterals)
{
    const std::vector<Token> tokens = lex(R"("caf\u{e9} \u{1F600}")");

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::TEXTE_LIT);
    EXPECT_EQ(tokens[0].decoded, "café 😀");
    EXPECT_EQ(tokens[1].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, DecodesEscapeSequencesInSymbolLiterals)
{
    const std::vector<Token> tokens = lex(R"('\n' '\'' '\u{e9}')");

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[0].decoded, "\n");
    EXPECT_EQ(tokens[1].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[1].decoded, "'");
    EXPECT_EQ(tokens[2].type, TokenType::SYMBOLE_LIT);
    EXPECT_EQ(tokens[2].decoded, "é");
    EXPECT_EQ(tokens[3].type, TokenType::FIN_FICHIER);
}

TEST(LexerLiterals, RejectsUnknownEscapeSequenceInTextLiterals)
{
    const std::vector<Token> tokens = lex(R"("a\zb")");

    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::ERREUR);
    EXPECT_NE(tokens[0].lexeme.find("échappement invalide"), std::string::npos);
}

TEST(LexerLiterals, RejectsUnicodeEscapeMissingBraces)
{
    const std::vector<Token> tokens = lex(R"("\u00e9")");

    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::ERREUR);
    EXPECT_NE(tokens[0].lexeme.find("échappement invalide"), std::string::npos);
}

TEST(LexerLiterals, RejectsUnicodeEscapeOutsideValidCodePointRange)
{
    const std::vector<Token> too_large = lex(R"("\u{110000}")");
    ASSERT_GE(too_large.size(), 2u);
    EXPECT_EQ(too_large[0].type, TokenType::ERREUR);
    EXPECT_NE(too_large[0].lexeme.find("échappement invalide"), std::string::npos);

    const std::vector<Token> surrogate = lex(R"("\u{d800}")");
    ASSERT_GE(surrogate.size(), 2u);
    EXPECT_EQ(surrogate[0].type, TokenType::ERREUR);
    EXPECT_NE(surrogate[0].lexeme.find("échappement invalide"), std::string::npos);
}

TEST(LexerLiterals, RejectsEscapeAtEndOfUnterminatedTextLiteral)
{
    const std::vector<Token> tokens = lex("\"a\\");

    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::ERREUR);
    EXPECT_NE(tokens[0].lexeme.find("échappement invalide"), std::string::npos);
}

TEST(LexerIdentifiers, SupportsAccentedIdentifiersAndKeywords)
{
    const std::vector<Token> tokens = lex("réalise café");

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::REALISE);
    EXPECT_EQ(tokens[1].type, TokenType::IDENT);
    EXPECT_EQ(tokens[1].lexeme, "café");
    EXPECT_EQ(tokens[2].type, TokenType::FIN_FICHIER);
}

TEST(LexerIdentifiers, AcceptsAccentlessAliasesForAccentedKeywords)
{
    const std::vector<Token> tokens = lex("realise prive arreter");

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::REALISE);
    EXPECT_EQ(tokens[1].type, TokenType::PRIVE);
    EXPECT_EQ(tokens[2].type, TokenType::ARRETER);
    EXPECT_EQ(tokens[3].type, TokenType::FIN_FICHIER);
}

TEST(LexerOperators, RecognisesArrowEqualityAndComparisonOperators)
{
    const std::vector<Token> tokens = lex("-> == != <= >=");

    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0].type, TokenType::FLECHE);
    EXPECT_EQ(tokens[1].type, TokenType::EGAL_EGAL);
    EXPECT_EQ(tokens[2].type, TokenType::BANG_EGAL);
    EXPECT_EQ(tokens[3].type, TokenType::INFERIEUR_EGAL);
    EXPECT_EQ(tokens[4].type, TokenType::SUPERIEUR_EGAL);
    EXPECT_EQ(tokens[5].type, TokenType::FIN_FICHIER);
}

TEST(LexerNumbers, ReadsExponentsAndDigitSeparators)
{
    const auto tokens = lex("1e3 1.5E-3 2e+2 1_000 1_000.25 1.5e1_0 42");
    ASSERT_EQ(tokens.size(), 8u);
    // An exponent makes the literal a decimal even without a fractional part.
    EXPECT_EQ(tokens[0].type, TokenType::DECIMAL_LIT);
    EXPECT_EQ(tokens[1].type, TokenType::DECIMAL_LIT);
    EXPECT_EQ(tokens[2].type, TokenType::DECIMAL_LIT);
    EXPECT_EQ(tokens[3].type, TokenType::ENTIER_LIT);
    EXPECT_EQ(tokens[3].lexeme, "1_000");
    EXPECT_EQ(tokens[4].type, TokenType::DECIMAL_LIT);
    EXPECT_EQ(tokens[5].type, TokenType::DECIMAL_LIT);
    EXPECT_EQ(tokens[6].type, TokenType::ENTIER_LIT);
    EXPECT_EQ(tokens[7].type, TokenType::FIN_FICHIER);
}

TEST(LexerNumbers, RejectsTrailingLettersAndOutOfRangeIntegers)
{
    // "1.0e308" used to split into 1.0 and an identifier e308, which only failed
    // later as a missing variable.
    for (const auto *source : {"12abc", "1_", "1e", "1.5e", "3x"})
    {
        const auto tokens = lex(source);
        ASSERT_FALSE(tokens.empty()) << source;
        EXPECT_EQ(tokens[0].type, TokenType::ERREUR) << source;
    }

    const auto overflow = lex("9223372036854775808");
    ASSERT_FALSE(overflow.empty());
    EXPECT_EQ(overflow[0].type, TokenType::ERREUR);
    EXPECT_NE(overflow[0].lexeme.find("hors limites"), std::string::npos);

    const auto largest = lex("9223372036854775807");
    ASSERT_FALSE(largest.empty());
    EXPECT_EQ(largest[0].type, TokenType::ENTIER_LIT);
}

} // namespace
