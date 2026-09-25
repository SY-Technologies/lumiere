#include <gtest/gtest.h>

#include "lumiere/analysis/semantic_index.hpp"

namespace
{

using lumiere::kModuleScopeId;
using lumiere::ScopeId;
using lumiere::SemanticIndex;
using lumiere::SemanticSymbolKind;
using lumiere::SourceSpan;
using lumiere::SymbolId;

TEST(SemanticIndex, StartsWithAnEmptyModuleScopeAndNoSymbols)
{
    const SemanticIndex index;

    const lumiere::Scope *module_scope = index.scope(kModuleScopeId);
    ASSERT_NE(module_scope, nullptr);
    EXPECT_EQ(module_scope->parent, kModuleScopeId);
    EXPECT_TRUE(module_scope->symbols.empty());

    EXPECT_EQ(index.symbol(SymbolId{0}), nullptr);
    EXPECT_EQ(index.scope(ScopeId{1}), nullptr);
    EXPECT_EQ(index.occurrence_at(lumiere::SourceId{0}, 0), nullptr);
    EXPECT_TRUE(index.occurrences_of(SymbolId{0}).empty());
    EXPECT_EQ(index.lookup(kModuleScopeId, "inconnu"), nullptr);
}

TEST(SemanticIndex, InternsEachPathOnceAndReturnsItByPath)
{
    SemanticIndex index;

    const auto main_id = index.source("main.lum");
    const auto again = index.source("main.lum");
    const auto other_id = index.source("Formes.lum");

    EXPECT_EQ(main_id, again);
    EXPECT_NE(main_id, other_id);
    EXPECT_EQ(index.path_of(main_id), "main.lum");
    EXPECT_EQ(index.path_of(other_id), "Formes.lum");
}

TEST(SemanticIndex, DeclareRecordsTheSymbolAndAppendsItToItsScope)
{
    SemanticIndex index;
    const auto main = index.source("main.lum");
    const SourceSpan name_span{main, 5, 6};
    const SourceSpan whole_span{main, 0, 10};

    const SymbolId id = index.declare(
        SemanticSymbolKind::VARIABLE, "x", name_span, whole_span, kModuleScopeId);

    const lumiere::Symbol *symbol = index.symbol(id);
    ASSERT_NE(symbol, nullptr);
    EXPECT_EQ(symbol->kind, SemanticSymbolKind::VARIABLE);
    EXPECT_EQ(symbol->name, "x");
    EXPECT_EQ(symbol->declaration_span, name_span);
    EXPECT_EQ(symbol->enclosing_span, whole_span);
    EXPECT_EQ(symbol->scope, kModuleScopeId);
    EXPECT_EQ(symbol->type, nullptr);
    EXPECT_TRUE(symbol->documentation.empty());

    const lumiere::Scope *scope = index.scope(kModuleScopeId);
    ASSERT_NE(scope, nullptr);
    ASSERT_EQ(scope->symbols.size(), 1u);
    EXPECT_EQ(scope->symbols[0], id);
}

TEST(SemanticIndex, PushScopeNestsUnderItsParentAndKeepsThemDistinct)
{
    SemanticIndex index;
    const auto main = index.source("main.lum");

    const ScopeId body = index.push_scope(kModuleScopeId, SourceSpan{main, 0, 20});
    const ScopeId nested = index.push_scope(body, SourceSpan{main, 5, 15});

    EXPECT_NE(body, kModuleScopeId);
    EXPECT_NE(nested, body);
    ASSERT_NE(index.scope(body), nullptr);
    ASSERT_NE(index.scope(nested), nullptr);
    EXPECT_EQ(index.scope(body)->parent, kModuleScopeId);
    EXPECT_EQ(index.scope(nested)->parent, body);
}

TEST(SemanticIndex, OccurrenceAtFindsTheOccurrenceWhoseSpanContainsTheOffset)
{
    SemanticIndex index;
    const auto main = index.source("main.lum");
    const auto other = index.source("Autre.lum");

    const SymbolId x = index.declare(
        SemanticSymbolKind::VARIABLE, "x", {main, 5, 6}, {main, 0, 10}, kModuleScopeId);
    index.record_occurrence({main, 20, 21}, x, /*is_write=*/false);

    // Inside the span.
    EXPECT_EQ(index.occurrence_at(main, 20)->symbol, x);
    // Exclusive end: the byte just past the span is not a match.
    EXPECT_EQ(index.occurrence_at(main, 21), nullptr);
    // One before start is not a match either.
    EXPECT_EQ(index.occurrence_at(main, 19), nullptr);
    // Same offset, wrong document: no match.
    EXPECT_EQ(index.occurrence_at(other, 20), nullptr);
}

TEST(SemanticIndex, OccurrencesOfReturnsOnlyThatSymbolsUsesInRecordingOrder)
{
    SemanticIndex index;
    const auto main = index.source("main.lum");

    const SymbolId x = index.declare(
        SemanticSymbolKind::VARIABLE, "x", {main, 5, 6}, {main, 0, 10}, kModuleScopeId);
    const SymbolId y = index.declare(
        SemanticSymbolKind::VARIABLE, "y", {main, 15, 16}, {main, 10, 20}, kModuleScopeId);

    index.record_occurrence({main, 30, 31}, x, /*is_write=*/false);
    index.record_occurrence({main, 40, 41}, y, /*is_write=*/false);
    index.record_occurrence({main, 50, 51}, x, /*is_write=*/true);

    const std::vector<const lumiere::Occurrence *> uses_of_x = index.occurrences_of(x);
    ASSERT_EQ(uses_of_x.size(), 2u);
    EXPECT_EQ(uses_of_x[0]->span.start, 30u);
    EXPECT_FALSE(uses_of_x[0]->is_write);
    EXPECT_EQ(uses_of_x[1]->span.start, 50u);
    EXPECT_TRUE(uses_of_x[1]->is_write);

    EXPECT_EQ(index.occurrences_of(y).size(), 1u);
}

TEST(SemanticIndex, LookupPrefersTheInnermostScopeAndFallsBackToTheParent)
{
    SemanticIndex index;
    const auto main = index.source("main.lum");

    const SymbolId outer_x = index.declare(
        SemanticSymbolKind::VARIABLE, "x", {main, 0, 1}, {main, 0, 1}, kModuleScopeId);
    const SymbolId only_in_module = index.declare(
        SemanticSymbolKind::FUNCTION, "f", {main, 2, 3}, {main, 2, 3}, kModuleScopeId);

    const ScopeId body = index.push_scope(kModuleScopeId, {main, 4, 20});
    const SymbolId inner_x = index.declare(
        SemanticSymbolKind::VARIABLE, "x", {main, 6, 7}, {main, 6, 7}, body);

    // Shadowed: the inner scope's own `x` wins over the module's.
    EXPECT_EQ(index.lookup(body, "x"), index.symbol(inner_x));
    EXPECT_NE(index.lookup(body, "x"), index.symbol(outer_x));

    // Not declared in the inner scope: falls back to the module scope.
    EXPECT_EQ(index.lookup(body, "f"), index.symbol(only_in_module));

    // Declared nowhere: nullopt-like nullptr, not a crash.
    EXPECT_EQ(index.lookup(body, "inconnu"), nullptr);

    // From the module scope directly, the inner `x` is invisible.
    EXPECT_EQ(index.lookup(kModuleScopeId, "x"), index.symbol(outer_x));
}

TEST(SemanticIndex, LookupOnAnUnknownScopeReturnsNullRatherThanLooping)
{
    const SemanticIndex index;
    EXPECT_EQ(index.lookup(ScopeId{99}, "x"), nullptr);
}

} // namespace
