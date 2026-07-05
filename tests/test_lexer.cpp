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

} // namespace
