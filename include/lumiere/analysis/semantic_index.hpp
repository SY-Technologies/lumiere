#pragma once

#include "lumiere/analysis/semantic_analysis.hpp"
#include "lumiere/analysis/semantic_type.hpp"
#include "lumiere/analysis/source_id.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lumiere
{

/**
 * @brief An opaque, snapshot-local handle for one recorded declaration.
 *
 * One `SymbolId` per `SOIT`/`FIXE` binding (module-level or local), every
 * parameter, every `fonction`/`classe`/`interface`/`type` declaration, and
 * every name an `importer` statement binds. Stable for the lifetime of the
 * `SemanticIndex` that issued it; meaningless compared against a different
 * index, and never serialized -- the same discipline `SourceId` documents
 * for itself in source_id.hpp.
 */
enum class SymbolId : std::uint32_t
{
};

/** An opaque, snapshot-local handle for one lexical scope. */
enum class ScopeId : std::uint32_t
{
};

/** The module scope: every top-level declaration's `Symbol::scope`. */
inline constexpr ScopeId kModuleScopeId{0};

/** One recorded declaration: what it is, where it lives, what it means. */
struct Symbol
{
    SymbolId id{0};
    SemanticSymbolKind kind = SemanticSymbolKind::VARIABLE;
    std::string name;
    /** The declaring token's own span, e.g. just `x` in `soit x = 0`. */
    SourceSpan declaration_span;
    /** The whole declaration's extent, e.g. all of `soit x = 0`. */
    SourceSpan enclosing_span;
    /** Null until type inference reaches this declaration, same as
     *  `SemanticSymbol::type` does today. */
    SemanticTypeRef type;
    /** The doc comment attached to this declaration, if any. */
    std::string documentation;
    /** The scope this name is visible in -- not the scope it introduces:
     *  a function's own `Symbol` lives in its enclosing scope, while its
     *  body is a separate, nested `Scope`. */
    ScopeId scope = kModuleScopeId;
};

/** One lexical scope: a name lookup boundary with a parent and an extent. */
struct Scope
{
    ScopeId id{0};
    /** `kModuleScopeId` has no parent; `parent == id` marks that. */
    ScopeId parent = kModuleScopeId;
    SourceSpan span;
    /** Declared in this scope, in declaration order. */
    std::vector<SymbolId> symbols;
};

/** One resolved use of a symbol: where, which one, read or write. */
struct Occurrence
{
    SourceSpan span;
    SymbolId symbol{0};
    bool is_write = false;
};

/**
 * @brief The semantic tooling index: every declaration and every resolved
 * occurrence recorded during one analysis, queryable by position or by
 * symbol.
 *
 * Purely additive to `SemanticModel`: nothing here replaces
 * `find_value`/`find_type`/`type_of`, which keep working exactly as they do
 * today for the interpreter, the VM compiler, and every existing analysis
 * consumer. This index exists so a caller that only has a byte offset (a
 * hover request, eventually definition/references/rename) can answer
 * "what declaration is this, and where else is it used" without re-walking
 * the AST and re-deriving an answer the analyzer already computed once.
 *
 * `declare`/`record_occurrence`/`push_scope` are the write side, called by
 * `SemanticAnalyzer` as it walks the tree (see stage1-semantic-index-design
 * for exactly which call sites). Everything else is read-only and safe to
 * call on a partial/error-recovered analysis: a `Symbol` only ever exists
 * for a declaration that survived parsing, and an occurrence that never
 * resolved is simply never recorded -- `occurrence_at` returning nullptr is
 * an ordinary answer, not a distinct error case.
 */
class SemanticIndex final
{
public:
    /** Interns `path` and returns its `SourceId` (see `SourceRegistry`). */
    [[nodiscard]] SourceId source(const std::string &path)
    {
        return m_sources.intern(path);
    }

    [[nodiscard]] const std::string &path_of(const SourceId id) const
    {
        return m_sources.path_of(id);
    }

    /**
     * @brief Opens a new scope nested in `parent`, returning its id.
     *
     * The index only ever grows a scope tree; it does not itself track
     * "the current scope" the way `SemanticAnalyzer::m_scopes` does for
     * name resolution during the walk -- that stack stays exactly as it is
     * today, transient and popped per block. This just keeps a permanent
     * record of the scopes that stack passed through.
     */
    [[nodiscard]] ScopeId push_scope(const ScopeId parent, const SourceSpan span)
    {
        const auto id = static_cast<ScopeId>(m_scopes.size());
        m_scopes.push_back(Scope{id, parent, span, {}});
        return id;
    }

    /** Records one declaration, appending it to `scope`'s symbol list. */
    [[nodiscard]] SymbolId declare(const SemanticSymbolKind kind,
                                   std::string name,
                                   const SourceSpan declaration_span,
                                   const SourceSpan enclosing_span,
                                   const ScopeId scope,
                                   SemanticTypeRef type = nullptr,
                                   std::string documentation = {})
    {
        const auto id = static_cast<SymbolId>(m_symbols.size());
        m_symbols.push_back(Symbol{id, kind, std::move(name), declaration_span,
                                   enclosing_span, std::move(type),
                                   std::move(documentation), scope});
        m_scopes.at(static_cast<std::size_t>(scope)).symbols.push_back(id);
        return id;
    }

    /** Records one resolved use of `symbol` at `span`. */
    void record_occurrence(const SourceSpan span, const SymbolId symbol, const bool is_write)
    {
        m_occurrences.push_back(Occurrence{span, symbol, is_write});
    }

    [[nodiscard]] const Symbol *symbol(const SymbolId id) const
    {
        const auto index = static_cast<std::size_t>(id);
        return index < m_symbols.size() ? &m_symbols[index] : nullptr;
    }

    [[nodiscard]] const Scope *scope(const ScopeId id) const
    {
        const auto index = static_cast<std::size_t>(id);
        return index < m_scopes.size() ? &m_scopes[index] : nullptr;
    }

    /** What, if anything, resolved at this exact byte in this document. */
    [[nodiscard]] const Occurrence *occurrence_at(const SourceId document,
                                                  const std::size_t byte_offset) const
    {
        for (const Occurrence &occurrence : m_occurrences)
        {
            if (occurrence.span.source == document &&
                byte_offset >= occurrence.span.start &&
                byte_offset < occurrence.span.end)
            {
                return &occurrence;
            }
        }
        return nullptr;
    }

    /** Every recorded occurrence of `symbol`, in recording order. */
    [[nodiscard]] std::vector<const Occurrence *> occurrences_of(const SymbolId target) const
    {
        std::vector<const Occurrence *> found;
        for (const Occurrence &occurrence : m_occurrences)
        {
            if (occurrence.symbol == target)
            {
                found.push_back(&occurrence);
            }
        }
        return found;
    }

    /**
     * @brief What `name` means standing in `scope`, walking outward to
     * `kModuleScopeId` if it isn't declared there.
     *
     * Mirrors `SemanticAnalyzer`'s own resolution order (innermost binding
     * wins; a name declared later in the same scope than the position being
     * asked about is not modeled here -- this answers "what does this name
     * mean in this scope overall", the same granularity `Scope::symbols`
     * already offers, not "as of this exact statement").
     */
    [[nodiscard]] const Symbol *lookup(ScopeId scope, const std::string_view name) const
    {
        while (true)
        {
            const Scope *current = this->scope(scope);
            if (current == nullptr)
            {
                return nullptr;
            }
            for (auto found = current->symbols.rbegin(); found != current->symbols.rend(); ++found)
            {
                if (const Symbol *candidate = symbol(*found); candidate != nullptr && candidate->name == name)
                {
                    return candidate;
                }
            }
            if (current->parent == scope)
            {
                return nullptr;
            }
            scope = current->parent;
        }
    }

private:
    SourceRegistry m_sources;
    std::vector<Symbol> m_symbols;
    std::vector<Scope> m_scopes{Scope{kModuleScopeId, kModuleScopeId, {}, {}}};
    std::vector<Occurrence> m_occurrences;
};

} // namespace lumiere
